#ifndef CAPYOS_DRIVERS_AUDIO_AC97_CORE_H
#define CAPYOS_DRIVERS_AUDIO_AC97_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Pure, host-testable helpers for the Intel ICH-style AC'97 controller
 * (82801AA class 0x04/0x01; QEMU `-device AC97`). No I/O happens here: the
 * PIO backend in ac97.c feeds register values in and writes results out.
 * Register offsets and bit names follow the Intel I/O Controller Hub AC'97
 * programmer's reference (NAM = mixer, NABM = bus master). */

/* Native Audio Mixer (BAR0): 16-bit registers. */
#define AC97_NAM_RESET              0x00u
#define AC97_NAM_MASTER_VOLUME      0x02u
#define AC97_NAM_PCM_OUT_VOLUME     0x18u
#define AC97_NAM_EXT_AUDIO_ID       0x28u
#define AC97_NAM_EXT_AUDIO_CTRL     0x2au
#define AC97_NAM_PCM_FRONT_DAC_RATE 0x2cu
#define AC97_NAM_VENDOR_ID1         0x7cu
#define AC97_NAM_VENDOR_ID2         0x7eu
#define AC97_EXT_AUDIO_VRA          0x0001u
#define AC97_PCM_OUT_VOLUME_0DB     0x0808u
#define AC97_MIXER_VOLUME_MUTE      0x8000u
#define AC97_MIXER_ATTENUATION_MAX  63u

/* I/O window sizes of the two BARs; a base must keep the whole window inside
 * the 16-bit port space so register arithmetic can never wrap. */
#define AC97_NAM_IO_SPAN  0x100u
#define AC97_NABM_IO_SPAN 0x40u

/* Native Audio Bus Master (BAR1): PCM OUT channel and global registers. */
#define AC97_NABM_PO_BDBAR 0x10u /* u32: buffer descriptor list base */
#define AC97_NABM_PO_CIV   0x14u /* u8: current index value (0..31) */
#define AC97_NABM_PO_LVI   0x15u /* u8: last valid index (0..31) */
#define AC97_NABM_PO_SR    0x16u /* u16: status */
#define AC97_NABM_PO_PICB  0x18u /* u16: samples left in the current buffer */
#define AC97_NABM_PO_PIV   0x1au /* u8: prefetched index value */
#define AC97_NABM_PO_CR    0x1bu /* u8: control */
#define AC97_NABM_GLOB_CNT 0x2cu /* u32 */
#define AC97_NABM_GLOB_STA 0x30u /* u32 */
#define AC97_NABM_CAS      0x34u /* u8: codec access semaphore */
#define AC97_CAS_BUSY      0x01u /* set while a codec register access is pending */

#define AC97_PO_CR_RPBM  0x01u /* run/pause bus master */
#define AC97_PO_CR_RR    0x02u /* reset registers; self-clearing */
#define AC97_PO_CR_LVBIE 0x04u
#define AC97_PO_CR_FEIE  0x08u
#define AC97_PO_CR_IOCE  0x10u
#define AC97_PO_SR_DCH   0x01u /* DMA controller halted */
#define AC97_PO_SR_CELV  0x02u /* current index equals last valid index */
#define AC97_PO_SR_LVBCI 0x04u /* last valid buffer completion; RW1C */
#define AC97_PO_SR_BCIS  0x08u /* buffer completion interrupt status; RW1C */
#define AC97_PO_SR_FIFOE 0x10u /* FIFO error; RW1C */
#define AC97_PO_SR_RW1C_MASK \
    (AC97_PO_SR_LVBCI | AC97_PO_SR_BCIS | AC97_PO_SR_FIFOE)
#define AC97_GLOB_CNT_GIE        0x01u
#define AC97_GLOB_CNT_COLD_RESET 0x02u /* 0 asserts the AC-link cold reset */
#define AC97_GLOB_CNT_WARM_RESET 0x04u /* self-clearing */
#define AC97_GLOB_STA_PCR        0x0100u /* primary codec ready */

/* Output ring contract shared with the audio service: a 64 KiB cyclic ring of
 * sixteen 4096-byte fragments. The controller walks a 32-entry descriptor
 * list, so each fragment is described twice (entry i -> fragment i % 16) and
 * the backend keeps LVI one entry behind CIV to keep the engine running. */
#define AC97_BDL_ENTRIES          32u
#define AC97_BDL_FRAGMENT_BYTES   4096u
#define AC97_BDL_FRAGMENT_SAMPLES (AC97_BDL_FRAGMENT_BYTES / 2u)
#define AC97_RING_FRAGMENTS       16u
#define AC97_RING_BYTES           (AC97_RING_FRAGMENTS * AC97_BDL_FRAGMENT_BYTES)
#define AC97_BDL_FLAG_IOC 0x8000u
#define AC97_BDL_FLAG_BUP 0x4000u
/* The controller has no wall clock; the backend reports a TSC-derived counter
 * in this domain (the HDA WALLCLK rate) so the service keeps one threshold set. */
#define AC97_WALLCLOCK_HZ 24000000u

struct ac97_bdl_entry {
    uint32_t address; /* physical, below 4 GiB, DWORD aligned */
    uint16_t samples; /* 16-bit samples in this buffer */
    uint16_t flags;
} __attribute__((packed));

/* Decode a raw PCI BAR dword as an I/O window of `span` bytes: bit 0 must
 * flag I/O space, the base must be non-zero and the window must end inside
 * the 16-bit port space. Fails closed on memory BARs and on wraparound. */
int ac97_io_base_ok(uint32_t raw_bar, uint32_t span, uint16_t *base);
/* The bus master addresses 32 bits: [address, address + bytes) must lie
 * entirely below 4 GiB with no wraparound. */
int ac97_dma_address_ok(uint64_t address, uint32_t bytes);
int ac97_bdl_build(struct ac97_bdl_entry *entries,
                   size_t entry_capacity,
                   uint64_t buffer_address,
                   uint32_t buffer_bytes,
                   size_t *entry_count);
/* Ring byte position from CIV/PICB. Positions include the ring size itself
 * when the last fragment is fully consumed before CIV advances, matching the
 * HDA CBL contract consumed by the audio service. */
int ac97_ring_position(uint8_t civ,
                       uint16_t picb_samples,
                       uint32_t buffer_bytes,
                       uint32_t *position_bytes);
/* LVI one entry behind CIV: every other entry stays valid, so the engine never
 * reaches its last valid buffer while polling continues. */
int ac97_next_lvi(uint8_t civ, uint8_t *lvi);
/* Same distance rule as hda_ring_fragment_writable, kept local so the two
 * drivers stay independent. */
int ac97_ring_fragment_writable(uint32_t fragment, uint32_t position,
                                uint32_t bytes);
/* Master/aux volume word: 6-bit attenuation on both channels, mute bit 15. */
int ac97_mixer_volume_word(uint8_t attenuation, int mute, uint16_t *word);

#endif
