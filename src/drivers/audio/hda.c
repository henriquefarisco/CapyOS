#include "drivers/audio/hda.h"

#include "drivers/audio/hda_core.h"
#include "drivers/pcie.h"
#include "kernel/log/klog.h"
#include "util/kstring.h"

#define HDA_TIMEOUT_SPINS 2000000u

#define HDA_REG_GCAP 0x00u
#define HDA_REG_GCTL 0x08u
#define HDA_REG_STATESTS 0x0eu
#define HDA_REG_INTCTL 0x20u
#define HDA_REG_ICOI 0x60u
#define HDA_REG_ICII 0x64u
#define HDA_REG_ICIS 0x68u

#define HDA_GCTL_CRST 0x1u
#define HDA_ICIS_BUSY 0x1u
#define HDA_ICIS_VALID 0x2u
#define HDA_SD_CTL_RUN 0x2u
#define HDA_SD_CTL_IOCE 0x4u
#define HDA_SD_STS_BCIS 0x4u

#define HDA_PARAM_NODE_COUNT 0x04u
#define HDA_PARAM_FUNCTION_TYPE 0x05u
#define HDA_PARAM_WIDGET_CAPS 0x09u
#define HDA_PARAM_PIN_CAPS 0x0cu
#define HDA_FUNCTION_AUDIO 0x01u
#define HDA_PIN_CAP_OUTPUT (1u << 4)

extern void *kmalloc_aligned(uint64_t size, uint64_t alignment);
extern void kfree_aligned(void *ptr);

struct hda_runtime {
    volatile uint8_t *mmio;
    uint16_t stream_offset;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t codec;
    uint8_t afg;
    uint8_t output_node;
    uint8_t pin_node;
    uint8_t *dma;
    struct hda_bdl_entry *bdl;
    uint32_t buffer_bytes;
    enum hda_runtime_state state;
    int last_error;
};

static struct hda_runtime g_hda;

static uint8_t mmio_read8(uint32_t offset) {
    return *(volatile uint8_t *)(g_hda.mmio + offset);
}

static uint16_t mmio_read16(uint32_t offset) {
    return *(volatile uint16_t *)(void *)(g_hda.mmio + offset);
}

static uint32_t mmio_read32(uint32_t offset) {
    return *(volatile uint32_t *)(void *)(g_hda.mmio + offset);
}

static void mmio_write8(uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(g_hda.mmio + offset) = value;
}

static void mmio_write16(uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(void *)(g_hda.mmio + offset) = value;
}

static void mmio_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(void *)(g_hda.mmio + offset) = value;
}

static int wait32(uint32_t offset, uint32_t mask, uint32_t expected) {
    uint32_t spin;
    for (spin = 0; spin < HDA_TIMEOUT_SPINS; ++spin) {
        if ((mmio_read32(offset) & mask) == expected) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int wait16(uint32_t offset, uint16_t mask, uint16_t expected) {
    uint32_t spin;
    for (spin = 0; spin < HDA_TIMEOUT_SPINS; ++spin) {
        if ((mmio_read16(offset) & mask) == expected) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int hda_command(uint8_t node, uint32_t payload, uint32_t *response) {
    uint32_t command;
    if (wait16(HDA_REG_ICIS, HDA_ICIS_BUSY, 0) != 0) return -1;
    mmio_write16(HDA_REG_ICIS, HDA_ICIS_VALID);
    command = ((uint32_t)g_hda.codec << 28) | ((uint32_t)node << 20) |
              (payload & 0x000fffffu);
    mmio_write32(HDA_REG_ICOI, command);
    mmio_write16(HDA_REG_ICIS, HDA_ICIS_BUSY);
    if (wait16(HDA_REG_ICIS, HDA_ICIS_BUSY, 0) != 0) return -1;
    if ((mmio_read16(HDA_REG_ICIS) & HDA_ICIS_VALID) == 0) return -1;
    if (response) *response = mmio_read32(HDA_REG_ICII);
    mmio_write16(HDA_REG_ICIS, HDA_ICIS_VALID);
    return 0;
}

static int get_parameter(uint8_t node, uint8_t parameter, uint32_t *response) {
    return hda_command(node, 0x000f0000u | parameter, response);
}

static int hda_reset_controller(void) {
    uint32_t gctl = mmio_read32(HDA_REG_GCTL);
    mmio_write32(HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST);
    if (wait32(HDA_REG_GCTL, HDA_GCTL_CRST, 0) != 0) return -1;
    mmio_write32(HDA_REG_GCTL, gctl | HDA_GCTL_CRST);
    return wait32(HDA_REG_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST);
}

static int hda_discover_codec(void) {
    struct hda_node_range range;
    uint16_t codecs = mmio_read16(HDA_REG_STATESTS);
    uint32_t response;
    uint16_t codec;
    uint8_t i;
    for (codec = 0; codec < 15u; ++codec) {
        if (codecs & (1u << codec)) {
            g_hda.codec = (uint8_t)codec;
            break;
        }
    }
    if (codec == 15u || get_parameter(0, HDA_PARAM_NODE_COUNT, &response) != 0 ||
        hda_node_range_decode(response, &range) != 0) return -1;
    for (i = 0; i < range.count; ++i) {
        uint8_t node = (uint8_t)(range.first + i);
        if (get_parameter(node, HDA_PARAM_FUNCTION_TYPE, &response) == 0 &&
            (response & 0xffu) == HDA_FUNCTION_AUDIO) {
            g_hda.afg = node;
            break;
        }
    }
    if (!g_hda.afg || get_parameter(g_hda.afg, HDA_PARAM_NODE_COUNT,
                                    &response) != 0 ||
        hda_node_range_decode(response, &range) != 0) return -1;
    for (i = 0; i < range.count; ++i) {
        uint8_t node = (uint8_t)(range.first + i);
        uint32_t caps;
        if (get_parameter(node, HDA_PARAM_WIDGET_CAPS, &caps) != 0) continue;
        if (!g_hda.output_node &&
            hda_widget_type(caps) == HDA_WIDGET_AUDIO_OUTPUT)
            g_hda.output_node = node;
        if (!g_hda.pin_node && hda_widget_type(caps) == HDA_WIDGET_PIN_COMPLEX &&
            get_parameter(node, HDA_PARAM_PIN_CAPS, &caps) == 0 &&
            (caps & HDA_PIN_CAP_OUTPUT))
            g_hda.pin_node = node;
    }
    return g_hda.output_node && g_hda.pin_node ? 0 : -1;
}

static int hda_route_command(void *ctx, uint8_t node, uint32_t verb, uint32_t *response) {
    (void)ctx;
    return hda_command(node, verb, response);
}

static int hda_configure_codec(uint16_t format) {
    return hda_codec_route_setup(g_hda.afg, g_hda.output_node, g_hda.pin_node,
                                 format, hda_route_command, 0);
}

static int hda_reset_stream(void) {
    uint32_t base = g_hda.stream_offset;
    uint8_t ctl = mmio_read8(base);
    mmio_write8(base, (uint8_t)(ctl & ~HDA_SD_CTL_RUN));
    if (wait32(base, HDA_SD_CTL_RUN, 0u) != 0) return -1;
    mmio_write8(base, (uint8_t)((ctl & ~HDA_SD_CTL_RUN) | 1u));
    if (wait32(base, 1u, 1u) != 0) return -1;
    mmio_write8(base, (uint8_t)(ctl & ~(HDA_SD_CTL_RUN | 1u)));
    return wait32(base, 1u, 0u);
}

static void hda_fail(int error, const char *message) {
    /* Retain DMA storage on failure: the controller may still own it if a
     * stop times out. A failed controller is not retried in this session. */
    if (g_hda.mmio && g_hda.stream_offset) {
        mmio_write8(g_hda.stream_offset,
                    (uint8_t)(mmio_read8(g_hda.stream_offset) & ~HDA_SD_CTL_RUN));
        (void)wait32(g_hda.stream_offset, HDA_SD_CTL_RUN, 0u);
    }
    g_hda.last_error = error;
    g_hda.state = HDA_STATE_FAILED;
    klog(KLOG_WARN, message);
}

int hda_init(void) {
    struct pci_device pci_dev;
    uint16_t command;
    uint16_t gcap;
    uint64_t bar;
    if (g_hda.state == HDA_STATE_READY || g_hda.state == HDA_STATE_PLAYING)
        return 0;
    if (g_hda.state == HDA_STATE_FAILED) return -1;
    g_hda.state = HDA_STATE_UNAVAILABLE;
    g_hda.last_error = 0;
    pci_init();
    if (pci_find_device(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HDA, &pci_dev) != 0) {
        klog(KLOG_INFO, "[hda] no High Definition Audio controller found");
        g_hda.last_error = -1;
        return -1;
    }
    bar = pci_read_bar64(pci_dev.bus, pci_dev.device, pci_dev.function, 0);
    if (!bar || (pci_config_read32(pci_dev.bus, pci_dev.device,
                                   pci_dev.function, PCI_BAR0) & 1u)) {
        hda_fail(-2, "[hda] invalid MMIO BAR");
        return -1;
    }
    command = pci_config_read16(pci_dev.bus, pci_dev.device,
                                pci_dev.function, PCI_COMMAND);
    command |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
    pci_config_write16(pci_dev.bus, pci_dev.device, pci_dev.function,
                       PCI_COMMAND, command);
    g_hda.mmio = (volatile uint8_t *)(uintptr_t)bar;
    g_hda.vendor_id = pci_dev.vendor_id;
    g_hda.device_id = pci_dev.device_id;
    if (hda_reset_controller() != 0) {
        hda_fail(-3, "[hda] controller reset timed out");
        return -1;
    }
    gcap = mmio_read16(HDA_REG_GCAP);
    if (hda_output_stream_offset(gcap, &g_hda.stream_offset) != 0 ||
        hda_discover_codec() != 0) {
        hda_fail(-4, "[hda] no usable codec output route");
        return -1;
    }
    g_hda.dma = (uint8_t *)kmalloc_aligned(HDA_DMA_BYTES, 4096u);
    g_hda.bdl = (struct hda_bdl_entry *)kmalloc_aligned(
        sizeof(struct hda_bdl_entry) * HDA_BDL_MAX_ENTRIES, 128u);
    if (!g_hda.dma || !g_hda.bdl) {
        if (g_hda.dma) kfree_aligned(g_hda.dma);
        if (g_hda.bdl) kfree_aligned(g_hda.bdl);
        g_hda.dma = 0;
        g_hda.bdl = 0;
        hda_fail(-5, "[hda] DMA allocation failed");
        return -1;
    }
    if (!(gcap & 1u) &&
        ((uint64_t)(uintptr_t)g_hda.dma > UINT32_MAX - HDA_DMA_BYTES ||
         (uint64_t)(uintptr_t)g_hda.bdl > UINT32_MAX -
             sizeof(struct hda_bdl_entry) * HDA_BDL_MAX_ENTRIES)) {
        hda_fail(-8, "[hda] DMA memory exceeds controller address width");
        return -1;
    }
    mmio_write32(HDA_REG_INTCTL, 0);
    g_hda.state = HDA_STATE_READY;
    klog(KLOG_INFO, "[hda] controller and codec ready");
    return 0;
}

int hda_play_stereo_s16(const int16_t *samples, size_t frame_count) {
    uint16_t format;
    uint32_t bytes;
    uint32_t padded;
    size_t entries;
    uint32_t base;
    uint64_t bdl_address;
    if (!samples || frame_count == 0 || frame_count > HDA_DMA_BYTES / 4u)
        return -1;
    if (g_hda.state != HDA_STATE_READY && g_hda.state != HDA_STATE_PLAYING &&
        hda_init() != 0) return -1;
    bytes = (uint32_t)frame_count * 4u;
    padded = (bytes + 127u) & ~127u;
    /* Stop and acknowledge ownership transfer BEFORE overwriting either the
     * descriptors or sample storage from an earlier playback. */
    if (hda_reset_stream() != 0) {
        hda_fail(-7, "[hda] stream reset failed before DMA reuse");
        return -1;
    }
    if (padded > HDA_DMA_BYTES ||
        hda_stream_format(48000, 16, 2, &format) != 0 ||
        hda_bdl_build(g_hda.bdl, HDA_BDL_MAX_ENTRIES,
                      (uint64_t)(uintptr_t)g_hda.dma, padded, &entries) != 0) {
        hda_fail(-6, "[hda] rejected PCM DMA layout");
        return -1;
    }
    kmemzero(g_hda.dma, padded);
    kmemcpy(g_hda.dma, samples, bytes);
    if (hda_configure_codec(format) != 0) {
        hda_fail(-7, "[hda] stream or codec configuration failed");
        return -1;
    }
    base = g_hda.stream_offset;
    bdl_address = (uint64_t)(uintptr_t)g_hda.bdl;
    mmio_write32(base + 0x08u, padded);
    mmio_write16(base + 0x0cu, (uint16_t)(entries - 1u));
    mmio_write16(base + 0x12u, format);
    mmio_write32(base + 0x18u, (uint32_t)bdl_address);
    mmio_write32(base + 0x1cu, (uint32_t)(bdl_address >> 32));
    __asm__ volatile("mfence" ::: "memory");
    mmio_write8(base + 2u, 0x10u); /* stream tag 1 */
    mmio_write8(base, (uint8_t)(HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE));
    g_hda.buffer_bytes = padded;
    g_hda.state = HDA_STATE_PLAYING;
    return 0;
}

int hda_refill_fragment(uint32_t fragment, const int16_t *stereo_samples) {
    uint32_t position;
    if (!stereo_samples || g_hda.state != HDA_STATE_PLAYING ||
        g_hda.buffer_bytes != HDA_DMA_BYTES) return -1;
    position = mmio_read32(g_hda.stream_offset + 0x04u);
    if (!hda_ring_fragment_writable(fragment, position, g_hda.buffer_bytes))
        return -1;
    kmemcpy(g_hda.dma + fragment * HDA_BDL_FRAGMENT_BYTES, stereo_samples,
            HDA_BDL_FRAGMENT_BYTES);
    __asm__ volatile("mfence" ::: "memory");
    return 0;
}

void hda_request_stop(void) {
    if (g_hda.mmio && g_hda.state == HDA_STATE_PLAYING) {
        uint32_t base = g_hda.stream_offset;
        mmio_write8(base, (uint8_t)(mmio_read8(base) & ~HDA_SD_CTL_RUN));
    }
}

void hda_stop(void) {
    if (g_hda.mmio && g_hda.state == HDA_STATE_PLAYING) {
        uint32_t base = g_hda.stream_offset;
        mmio_write8(base, (uint8_t)(mmio_read8(base) & ~HDA_SD_CTL_RUN));
        if (wait32(base, HDA_SD_CTL_RUN, 0u) != 0) {
            hda_fail(-9, "[hda] stream stop timed out");
            return;
        }
        g_hda.state = HDA_STATE_READY;
    }
}

int hda_get_status(struct hda_runtime_status *status) {
    if (!status) return -1;
    status->state = g_hda.state;
    status->vendor_id = g_hda.vendor_id;
    status->device_id = g_hda.device_id;
    status->codec_address = g_hda.codec;
    status->output_node = g_hda.output_node;
    status->pin_node = g_hda.pin_node;
    status->buffer_bytes = g_hda.buffer_bytes;
    status->position_bytes = g_hda.mmio && g_hda.stream_offset
                                 ? mmio_read32(g_hda.stream_offset + 0x04u) : 0;
    status->wallclock_ticks = g_hda.mmio ? mmio_read32(0x30u) : 0;
    status->stream_status = g_hda.mmio && g_hda.stream_offset
                               ? mmio_read8(g_hda.stream_offset + 3u) : 0;
    /* Polling owns completion acknowledgement while INTCTL is disabled.
     * SDSTS.BCIS is RW1C (HDA 1.0a): clear only the observed
     * completion, retaining FIFO/descriptor errors for failure diagnostics. */
    if (g_hda.state == HDA_STATE_PLAYING &&
        (status->stream_status & HDA_SD_STS_BCIS))
        mmio_write8(g_hda.stream_offset + 3u, HDA_SD_STS_BCIS);
    status->last_error = g_hda.last_error;
    return 0;
}

const char *hda_state_name(enum hda_runtime_state state) {
    switch (state) {
        case HDA_STATE_UNINITIALIZED: return "uninitialized";
        case HDA_STATE_UNAVAILABLE: return "unavailable";
        case HDA_STATE_READY: return "ready";
        case HDA_STATE_PLAYING: return "playing";
        case HDA_STATE_FAILED: return "failed";
        default: return "unknown";
    }
}
