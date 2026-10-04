#include "drivers/audio/ac97.h"

#include "arch/x86_64/timebase.h"
#include "drivers/audio/ac97_core.h"
#include "drivers/io.h"
#include "drivers/pcie.h"
#include "kernel/log/klog.h"
#include "util/kstring.h"

#define AC97_TIMEOUT_SPINS 2000000u
/* Codec ready after a cold reset may take up to a second on real parts. */
#define AC97_CODEC_READY_SPINS (8u * AC97_TIMEOUT_SPINS)
#define AC97_POSITION_READ_ATTEMPTS 4u

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

struct ac97_runtime {
    uint16_t nam;  /* Native Audio Mixer I/O base (BAR0). */
    uint16_t nabm; /* Native Audio Bus Master I/O base (BAR1). */
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t codec_vendor;
    uint64_t tsc_hz; /* Cached in task context; IRQ paths never probe it. */
    uint8_t *dma;
    struct ac97_bdl_entry *bdl;
    uint32_t buffer_bytes;
    enum ac97_runtime_state state;
    int last_error;
};

static struct ac97_runtime g_ac97;

static uint8_t nabm_read8(uint16_t reg) { return inb((uint16_t)(g_ac97.nabm + reg)); }

/* Mixer registers travel over the AC-link: the controller serializes them
 * behind the codec access semaphore. Task-context only (init path). */
static void nam_access_wait(void) {
    uint32_t spin;
    for (spin = 0; spin < AC97_TIMEOUT_SPINS; ++spin) {
        if ((nabm_read8(AC97_NABM_CAS) & AC97_CAS_BUSY) == 0) return;
        __asm__ volatile("pause");
    }
}
static uint16_t nam_read16(uint16_t reg) {
    nam_access_wait();
    return inw((uint16_t)(g_ac97.nam + reg));
}
static void nam_write16(uint16_t reg, uint16_t value) {
    nam_access_wait();
    outw((uint16_t)(g_ac97.nam + reg), value);
}
static uint16_t nabm_read16(uint16_t reg) { return inw((uint16_t)(g_ac97.nabm + reg)); }
static uint32_t nabm_read32(uint16_t reg) { return inl((uint16_t)(g_ac97.nabm + reg)); }
static void nabm_write8(uint16_t reg, uint8_t value) {
    outb((uint16_t)(g_ac97.nabm + reg), value);
}
static void nabm_write16(uint16_t reg, uint16_t value) {
    outw((uint16_t)(g_ac97.nabm + reg), value);
}
static void nabm_write32(uint16_t reg, uint32_t value) {
    outl((uint16_t)(g_ac97.nabm + reg), value);
}

static int wait_nabm32(uint16_t reg, uint32_t mask, uint32_t expected,
                       uint32_t spins) {
    uint32_t spin;
    for (spin = 0; spin < spins; ++spin) {
        if ((nabm_read32(reg) & mask) == expected) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int wait_nabm8(uint16_t reg, uint8_t mask, uint8_t expected) {
    uint32_t spin;
    for (spin = 0; spin < AC97_TIMEOUT_SPINS; ++spin) {
        if ((nabm_read8(reg) & mask) == expected) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int wait_nabm16(uint16_t reg, uint16_t mask, uint16_t expected) {
    uint32_t spin;
    for (spin = 0; spin < AC97_TIMEOUT_SPINS; ++spin) {
        if ((nabm_read16(reg) & mask) == expected) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

/* 24 MHz-equivalent clock from the TSC so the service applies the same
 * stall/gap thresholds it uses for the HDA WALLCLK. Split the division so
 * the remainder product cannot overflow 64 bits for any realistic TSC rate. */
static uint32_t ac97_wallclock(void) {
    uint32_t lo, hi;
    uint64_t tsc, seconds, remainder;
    if (!g_ac97.tsc_hz) return 0;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    tsc = ((uint64_t)hi << 32) | lo;
    seconds = tsc / g_ac97.tsc_hz;
    remainder = tsc % g_ac97.tsc_hz;
    return (uint32_t)(seconds * (uint64_t)AC97_WALLCLOCK_HZ +
                      (remainder * (uint64_t)AC97_WALLCLOCK_HZ) / g_ac97.tsc_hz);
}

/* CIV and PICB are separate registers: retry until CIV is stable around the
 * PICB read so a descriptor boundary cannot pair the old index with the new
 * remainder. Bounded; IRQ-safe (PIO only). */
static int ac97_read_position(uint8_t *civ_out, uint32_t *position) {
    uint32_t attempt;
    for (attempt = 0; attempt < AC97_POSITION_READ_ATTEMPTS; ++attempt) {
        uint8_t civ = nabm_read8(AC97_NABM_PO_CIV);
        uint16_t picb = nabm_read16(AC97_NABM_PO_PICB);
        if (nabm_read8(AC97_NABM_PO_CIV) != civ) continue;
        if (ac97_ring_position(civ, picb, g_ac97.buffer_bytes, position) != 0)
            return -1;
        if (civ_out) *civ_out = civ;
        return 0;
    }
    return -1;
}

static void ac97_keep_running(uint8_t civ) {
    uint8_t lvi;
    if (ac97_next_lvi(civ, &lvi) == 0) nabm_write8(AC97_NABM_PO_LVI, lvi);
}

static int ac97_reset_channel(void) {
    uint8_t cr = nabm_read8(AC97_NABM_PO_CR);
    nabm_write8(AC97_NABM_PO_CR, (uint8_t)(cr & ~AC97_PO_CR_RPBM));
    if (wait_nabm16(AC97_NABM_PO_SR, AC97_PO_SR_DCH, AC97_PO_SR_DCH) != 0)
        return -1;
    nabm_write8(AC97_NABM_PO_CR, AC97_PO_CR_RR);
    return wait_nabm8(AC97_NABM_PO_CR, AC97_PO_CR_RR, 0u);
}

static void ac97_fail(int error, const char *message) {
    /* Retain DMA storage on failure: the controller may still own it if a
     * stop times out. A failed controller is not retried in this session. */
    if (g_ac97.nabm) {
        nabm_write8(AC97_NABM_PO_CR,
                    (uint8_t)(nabm_read8(AC97_NABM_PO_CR) & ~AC97_PO_CR_RPBM));
        (void)wait_nabm16(AC97_NABM_PO_SR, AC97_PO_SR_DCH, AC97_PO_SR_DCH);
    }
    g_ac97.last_error = error;
    g_ac97.state = AC97_STATE_FAILED;
    klog(KLOG_WARN, message);
}

static int ac97_read_io_bar(const struct pci_device *dev, uint8_t bar_offset,
                            uint32_t span, uint16_t *base) {
    uint32_t raw = pci_config_read32(dev->bus, dev->device, dev->function,
                                     bar_offset);
    return ac97_io_base_ok(raw, span, base) ? 0 : -1;
}

static int ac97_locate(struct pci_device *pci_dev, uint16_t *nam, uint16_t *nabm) {
    pci_init();
    if (pci_find_device(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AUDIO, pci_dev) != 0)
        return -1;
    if (ac97_read_io_bar(pci_dev, PCI_BAR0, AC97_NAM_IO_SPAN, nam) != 0 ||
        ac97_read_io_bar(pci_dev, PCI_BAR1, AC97_NABM_IO_SPAN, nabm) != 0)
        return -2;
    return 0;
}

static void ac97_release_dma(void) {
    if (g_ac97.dma) kfree_aligned(g_ac97.dma);
    if (g_ac97.bdl) kfree_aligned(g_ac97.bdl);
    g_ac97.dma = 0;
    g_ac97.bdl = 0;
}

static int ac97_reset_link(void) {
    uint32_t cnt = nabm_read32(AC97_NABM_GLOB_CNT) & ~AC97_GLOB_CNT_GIE;
    if (cnt & AC97_GLOB_CNT_COLD_RESET) {
        /* Link already out of cold reset: issue a self-clearing warm reset. */
        nabm_write32(AC97_NABM_GLOB_CNT, cnt | AC97_GLOB_CNT_WARM_RESET);
        if (wait_nabm32(AC97_NABM_GLOB_CNT, AC97_GLOB_CNT_WARM_RESET, 0u,
                        AC97_TIMEOUT_SPINS) != 0)
            return -1;
    } else {
        /* Bit clear means the cold reset is asserted; setting it releases it. */
        nabm_write32(AC97_NABM_GLOB_CNT, cnt | AC97_GLOB_CNT_COLD_RESET);
    }
    return wait_nabm32(AC97_NABM_GLOB_STA, AC97_GLOB_STA_PCR,
                       AC97_GLOB_STA_PCR, AC97_CODEC_READY_SPINS);
}

static int ac97_configure_mixer(void) {
    uint16_t master;
    uint16_t ext_id;
    nam_write16(AC97_NAM_RESET, 0u);
    g_ac97.codec_vendor = ((uint32_t)nam_read16(AC97_NAM_VENDOR_ID1) << 16) |
                          nam_read16(AC97_NAM_VENDOR_ID2);
    if (ac97_mixer_volume_word(0u, 0, &master) != 0) return -1;
    nam_write16(AC97_NAM_MASTER_VOLUME, master);
    nam_write16(AC97_NAM_PCM_OUT_VOLUME, AC97_PCM_OUT_VOLUME_0DB);
    /* Read-back proves the codec answers on the link; the service applies
     * gain in software, so hardware volume stays at 0 dB. */
    if (nam_read16(AC97_NAM_MASTER_VOLUME) != master) return -1;
    /* Variable rate audio off: the DAC then runs at the fixed 48 kHz rate the
     * audio service produces. */
    ext_id = nam_read16(AC97_NAM_EXT_AUDIO_ID);
    if (ext_id & AC97_EXT_AUDIO_VRA) {
        uint16_t ctrl = nam_read16(AC97_NAM_EXT_AUDIO_CTRL);
        nam_write16(AC97_NAM_EXT_AUDIO_CTRL,
                    (uint16_t)(ctrl & ~AC97_EXT_AUDIO_VRA));
    }
    return 0;
}

int ac97_init(void) {
    struct pci_device pci_dev;
    uint16_t command;
    if (g_ac97.state == AC97_STATE_READY || g_ac97.state == AC97_STATE_PLAYING)
        return 0;
    if (g_ac97.state == AC97_STATE_FAILED) return -1;
    g_ac97.state = AC97_STATE_UNAVAILABLE;
    g_ac97.last_error = 0;
    switch (ac97_locate(&pci_dev, &g_ac97.nam, &g_ac97.nabm)) {
        case 0:
            break;
        case -1:
            klog(KLOG_INFO, "[ac97] no AC'97 audio controller found");
            g_ac97.last_error = -1;
            return -1;
        default:
            g_ac97.nam = 0;
            g_ac97.nabm = 0;
            g_ac97.vendor_id = pci_dev.vendor_id;
            g_ac97.device_id = pci_dev.device_id;
            g_ac97.last_error = -2;
            g_ac97.state = AC97_STATE_FAILED;
            klog(KLOG_WARN, "[ac97] invalid NAM/NABM I/O BARs");
            return -1;
    }
    g_ac97.vendor_id = pci_dev.vendor_id;
    g_ac97.device_id = pci_dev.device_id;
    /* Polling-only driver: keep INTx masked at the PCI level as well. */
    command = pci_config_read16(pci_dev.bus, pci_dev.device,
                                pci_dev.function, PCI_COMMAND);
    command |= PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER | PCI_CMD_INT_DISABLE;
    pci_config_write16(pci_dev.bus, pci_dev.device, pci_dev.function,
                       PCI_COMMAND, command);
    if (ac97_reset_link() != 0) {
        ac97_fail(-3, "[ac97] codec not ready after AC-link reset");
        return -1;
    }
    if (ac97_configure_mixer() != 0) {
        ac97_fail(-4, "[ac97] mixer did not acknowledge configuration");
        return -1;
    }
    g_ac97.dma = (uint8_t *)kmalloc_aligned(AC97_RING_BYTES, 4096u);
    g_ac97.bdl = (struct ac97_bdl_entry *)kmalloc_aligned(
        sizeof(struct ac97_bdl_entry) * AC97_BDL_ENTRIES, 128u);
    if (!g_ac97.dma || !g_ac97.bdl) {
        ac97_release_dma();
        ac97_fail(-5, "[ac97] DMA allocation failed");
        return -1;
    }
    /* The bus master is 32-bit: both the ring and its descriptors must sit
     * below 4 GiB. The channel has never run here, so the storage can be
     * released; only post-start failures retain it. */
    if (!ac97_dma_address_ok((uint64_t)(uintptr_t)g_ac97.dma, AC97_RING_BYTES) ||
        !ac97_dma_address_ok((uint64_t)(uintptr_t)g_ac97.bdl,
                             (uint32_t)(sizeof(struct ac97_bdl_entry) *
                                        AC97_BDL_ENTRIES))) {
        ac97_release_dma();
        ac97_fail(-8, "[ac97] DMA memory exceeds 32-bit controller address width");
        return -1;
    }
    g_ac97.tsc_hz = x64_timebase_hz();
    if (!g_ac97.tsc_hz) {
        ac97_release_dma();
        ac97_fail(-9, "[ac97] no monotonic timebase for stall detection");
        return -1;
    }
    g_ac97.state = AC97_STATE_READY;
    klog(KLOG_INFO, "[ac97] controller and codec ready");
    return 0;
}

int ac97_play_stereo_s16(const int16_t *samples, size_t frame_count) {
    uint32_t bytes;
    uint32_t offset;
    size_t entries;
    if (!samples || frame_count == 0 || frame_count > AC97_RING_BYTES / 4u)
        return -1;
    if (g_ac97.state != AC97_STATE_READY && g_ac97.state != AC97_STATE_PLAYING &&
        ac97_init() != 0) return -1;
    bytes = (uint32_t)frame_count * 4u;
    /* Stop and acknowledge ownership transfer BEFORE overwriting either the
     * descriptors or sample storage from an earlier playback. */
    if (ac97_reset_channel() != 0) {
        ac97_fail(-7, "[ac97] channel reset failed before DMA reuse");
        return -1;
    }
    if (ac97_bdl_build(g_ac97.bdl, AC97_BDL_ENTRIES,
                       (uint64_t)(uintptr_t)g_ac97.dma, AC97_RING_BYTES,
                       &entries) != 0) {
        ac97_fail(-6, "[ac97] rejected PCM DMA layout");
        return -1;
    }
    /* The descriptor list always spans the full ring, so a source shorter
     * than 64 KiB is repeated to fill it: the source loops at its own length
     * and is re-phased once per ring wrap, instead of playing silence. */
    for (offset = 0; offset < AC97_RING_BYTES; offset += bytes) {
        uint32_t chunk = AC97_RING_BYTES - offset < bytes
                             ? AC97_RING_BYTES - offset : bytes;
        kmemcpy(g_ac97.dma + offset, samples, chunk);
    }
    nabm_write32(AC97_NABM_PO_BDBAR, (uint32_t)(uintptr_t)g_ac97.bdl);
    nabm_write8(AC97_NABM_PO_LVI, (uint8_t)(entries - 1u));
    nabm_write16(AC97_NABM_PO_SR, AC97_PO_SR_RW1C_MASK);
    __asm__ volatile("mfence" ::: "memory");
    nabm_write8(AC97_NABM_PO_CR, AC97_PO_CR_RPBM);
    g_ac97.buffer_bytes = AC97_RING_BYTES;
    g_ac97.state = AC97_STATE_PLAYING;
    return 0;
}

int ac97_refill_fragment(uint32_t fragment, const int16_t *stereo_samples) {
    uint32_t position;
    uint8_t civ;
    if (!stereo_samples || g_ac97.state != AC97_STATE_PLAYING ||
        g_ac97.buffer_bytes != AC97_RING_BYTES) return -1;
    if (ac97_read_position(&civ, &position) != 0) return -1;
    if (!ac97_ring_fragment_writable(fragment, position, g_ac97.buffer_bytes))
        return -1;
    kmemcpy(g_ac97.dma + fragment * AC97_BDL_FRAGMENT_BYTES, stereo_samples,
            AC97_BDL_FRAGMENT_BYTES);
    __asm__ volatile("mfence" ::: "memory");
    ac97_keep_running(civ);
    return 0;
}

void ac97_request_stop(void) {
    if (g_ac97.nabm && g_ac97.state == AC97_STATE_PLAYING) {
        nabm_write8(AC97_NABM_PO_CR,
                    (uint8_t)(nabm_read8(AC97_NABM_PO_CR) & ~AC97_PO_CR_RPBM));
    }
}

void ac97_stop(void) {
    if (g_ac97.nabm && g_ac97.state == AC97_STATE_PLAYING) {
        nabm_write8(AC97_NABM_PO_CR,
                    (uint8_t)(nabm_read8(AC97_NABM_PO_CR) & ~AC97_PO_CR_RPBM));
        if (wait_nabm16(AC97_NABM_PO_SR, AC97_PO_SR_DCH, AC97_PO_SR_DCH) != 0) {
            ac97_fail(-9, "[ac97] stream stop timed out");
            return;
        }
        g_ac97.state = AC97_STATE_READY;
    }
}

int ac97_get_status(struct ac97_runtime_status *status) {
    if (!status) return -1;
    status->state = g_ac97.state;
    status->vendor_id = g_ac97.vendor_id;
    status->device_id = g_ac97.device_id;
    status->codec_vendor = g_ac97.codec_vendor;
    status->buffer_bytes = g_ac97.buffer_bytes;
    status->position_bytes = 0;
    status->stream_status = 0;
    if (g_ac97.nabm && g_ac97.state == AC97_STATE_PLAYING) {
        uint8_t civ;
        uint32_t position;
        uint16_t sr = nabm_read16(AC97_NABM_PO_SR);
        if (ac97_read_position(&civ, &position) == 0) {
            status->position_bytes = position;
            ac97_keep_running(civ);
        } else {
            /* Unstable or malformed CIV/PICB pair: report the ring size so a
             * caller using the CBL-style advance math sees no progress and
             * its stall detection stays authoritative. */
            status->position_bytes = g_ac97.buffer_bytes;
        }
        status->stream_status = sr;
        /* Polling owns completion acknowledgement while GIE is clear. BCIS
         * and LVBCI are RW1C: clear only observed completions and retain
         * FIFOE for failure diagnostics. */
        if (sr & (AC97_PO_SR_BCIS | AC97_PO_SR_LVBCI))
            nabm_write16(AC97_NABM_PO_SR,
                         (uint16_t)(sr & (AC97_PO_SR_BCIS | AC97_PO_SR_LVBCI)));
    } else if (g_ac97.nabm) {
        status->stream_status = nabm_read16(AC97_NABM_PO_SR);
    }
    status->wallclock_ticks = ac97_wallclock();
    status->last_error = g_ac97.last_error;
    return 0;
}

const char *ac97_state_name(enum ac97_runtime_state state) {
    switch (state) {
        case AC97_STATE_UNINITIALIZED: return "uninitialized";
        case AC97_STATE_UNAVAILABLE: return "unavailable";
        case AC97_STATE_READY: return "ready";
        case AC97_STATE_PLAYING: return "playing";
        case AC97_STATE_FAILED: return "failed";
        default: return "unknown";
    }
}
