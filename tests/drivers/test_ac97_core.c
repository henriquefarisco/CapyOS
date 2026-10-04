#include "drivers/audio/ac97_core.h"

#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("[ac97-core] FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

int run_ac97_core_tests(void) {
    struct ac97_bdl_entry entries[AC97_BDL_ENTRIES];
    size_t count = 0;
    uint32_t position = 0xffffffffu;
    uint16_t word = 0xffffu;
    uint16_t base = 0xffffu;
    uint8_t lvi = 0xffu;
    int failures = 0;

    /* Register map and descriptor layout follow the ICH AC'97 reference. */
    CHECK(sizeof(struct ac97_bdl_entry) == 8u);
    CHECK(AC97_NABM_PO_CR == 0x1bu && AC97_NABM_PO_SR == 0x16u);
    CHECK(AC97_NABM_GLOB_CNT == 0x2cu && AC97_NABM_GLOB_STA == 0x30u);
    CHECK(AC97_NABM_CAS == 0x34u && AC97_CAS_BUSY == 0x01u);
    CHECK(AC97_RING_BYTES == 65536u && AC97_BDL_FRAGMENT_SAMPLES == 2048u);

    /* I/O BAR decoding: I/O flag, non-zero base, window inside 16-bit ports. */
    CHECK(ac97_io_base_ok(0xc401u, AC97_NAM_IO_SPAN, &base) && base == 0xc400u);
    CHECK(ac97_io_base_ok(0xffc1u, AC97_NABM_IO_SPAN, &base) && base == 0xffc0u);
    CHECK(ac97_io_base_ok(0xff01u, AC97_NAM_IO_SPAN, &base) && base == 0xff00u);
    CHECK(!ac97_io_base_ok(0xffc5u, AC97_NABM_IO_SPAN, &base));
    CHECK(!ac97_io_base_ok(0xff05u, AC97_NAM_IO_SPAN, &base));
    CHECK(!ac97_io_base_ok(0xc400u, AC97_NAM_IO_SPAN, &base)); /* memory BAR */
    CHECK(!ac97_io_base_ok(0x1u, AC97_NAM_IO_SPAN, &base));    /* zero base */
    CHECK(!ac97_io_base_ok(0xfffffff1u, AC97_NAM_IO_SPAN, &base));
    CHECK(!ac97_io_base_ok(0xc401u, 0u, &base));
    CHECK(!ac97_io_base_ok(0xc401u, AC97_NAM_IO_SPAN, 0));

    /* 32-bit DMA window: the whole ring must sit below 4 GiB. */
    CHECK(ac97_dma_address_ok(0x100000u, 65536u));
    CHECK(ac97_dma_address_ok(0xffff0000u, 65536u));
    CHECK(!ac97_dma_address_ok(0xffff0001u, 65536u));
    CHECK(!ac97_dma_address_ok(0x100000000ull, 4096u));
    CHECK(!ac97_dma_address_ok(0u, 65536u));
    CHECK(!ac97_dma_address_ok(0x100000u, 0u));

    /* Descriptor list: 32 entries aliasing the 16 ring fragments twice. */
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0x100000u, 65536u,
                         &count) == 0);
    CHECK(count == 32u);
    CHECK(entries[0].address == 0x100000u && entries[0].samples == 2048u);
    CHECK(entries[15].address == 0x10f000u);
    CHECK(entries[16].address == 0x100000u);
    CHECK(entries[31].address == 0x10f000u && entries[31].samples == 2048u);
    CHECK(entries[0].flags == AC97_BDL_FLAG_IOC &&
          entries[31].flags == AC97_BDL_FLAG_IOC);
    CHECK(ac97_bdl_build(entries, 31u, 0x100000u, 65536u, &count) != 0);
    CHECK(ac97_bdl_build(0, AC97_BDL_ENTRIES, 0x100000u, 65536u, &count) != 0);
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0x100000u, 65536u, 0) != 0);
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0u, 65536u, &count) != 0);
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0x100002u, 65536u,
                         &count) != 0);
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0x100000u, 4096u,
                         &count) != 0);
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0xffff0004u, 65536u,
                         &count) != 0);
    CHECK(ac97_bdl_build(entries, AC97_BDL_ENTRIES, 0x100000000ull, 65536u,
                         &count) != 0);

    /* Ring position from CIV/PICB (PICB = samples still to play). */
    CHECK(ac97_ring_position(0, 2048u, 65536u, &position) == 0 && position == 0u);
    CHECK(ac97_ring_position(0, 0u, 65536u, &position) == 0 && position == 4096u);
    CHECK(ac97_ring_position(3, 1u, 65536u, &position) == 0 &&
          position == 3u * 4096u + 4094u);
    CHECK(ac97_ring_position(15, 0u, 65536u, &position) == 0 &&
          position == 65536u);
    CHECK(ac97_ring_position(16, 2048u, 65536u, &position) == 0 && position == 0u);
    CHECK(ac97_ring_position(31, 1024u, 65536u, &position) == 0 &&
          position == 15u * 4096u + 2048u);
    CHECK(ac97_ring_position(32, 2048u, 65536u, &position) != 0);
    CHECK(ac97_ring_position(0, 2049u, 65536u, &position) != 0);
    CHECK(ac97_ring_position(0, 2048u, 32768u, &position) != 0);
    CHECK(ac97_ring_position(0, 2048u, 65536u, 0) != 0);

    /* LVI trails CIV by one so the engine never halts on its last buffer. */
    CHECK(ac97_next_lvi(0, &lvi) == 0 && lvi == 31u);
    CHECK(ac97_next_lvi(5, &lvi) == 0 && lvi == 4u);
    CHECK(ac97_next_lvi(31, &lvi) == 0 && lvi == 30u);
    CHECK(ac97_next_lvi(32, &lvi) != 0);
    CHECK(ac97_next_lvi(0, 0) != 0);

    /* Fragment refill window mirrors the HDA rule. */
    CHECK(!ac97_ring_fragment_writable(0, 0, 65536u));
    CHECK(!ac97_ring_fragment_writable(0, 65536u, 65536u));
    CHECK(!ac97_ring_fragment_writable(1, 0, 65536u));
    CHECK(ac97_ring_fragment_writable(0, 4096u, 65536u));
    CHECK(ac97_ring_fragment_writable(2, 65536u, 65536u));
    CHECK(!ac97_ring_fragment_writable(16, 4096u, 65536u));
    CHECK(!ac97_ring_fragment_writable(0, 4096u, 32768u));
    CHECK(!ac97_ring_fragment_writable(0, 65537u, 65536u));

    /* Mixer volume word: 6-bit attenuation per channel, mute in bit 15. */
    CHECK(ac97_mixer_volume_word(0, 0, &word) == 0 && word == 0x0000u);
    CHECK(ac97_mixer_volume_word(63, 0, &word) == 0 && word == 0x3f3fu);
    CHECK(ac97_mixer_volume_word(0, 1, &word) == 0 && word == 0x8000u);
    CHECK(ac97_mixer_volume_word(64, 0, &word) != 0);
    CHECK(ac97_mixer_volume_word(0, 0, 0) != 0);

    if (failures == 0) printf("[ac97-core] ok\n");
    return failures;
}

#ifdef AC97_CORE_STANDALONE_TEST
int main(void) { return run_ac97_core_tests(); }
#endif
