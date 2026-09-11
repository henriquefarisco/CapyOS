#include "drivers/audio/hda_core.h"

#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("[hda-core] FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

int run_hda_core_tests(void) {
    struct hda_bdl_entry entries[HDA_BDL_MAX_ENTRIES];
    struct hda_node_range range;
    uint16_t value = 0;
    size_t count = 0;
    int failures = 0;
    uint32_t advance;
    CHECK(hda_ring_advance(61440, 65536, 65536, &advance) == 0 && advance == 4096);
    CHECK(hda_ring_advance(65536, 0, 65536, &advance) == 0 && advance == 0);
    CHECK(hda_ring_advance(61440, 4096, 65536, &advance) == 0 && advance == 8192);
    CHECK(hda_ring_advance(0, 65537, 65536, &advance) != 0);
    CHECK(!hda_ring_fragment_writable(0, 0, 65536));
    CHECK(!hda_ring_fragment_writable(0, 65536, 65536));
    CHECK(!hda_ring_fragment_writable(1, 0, 65536));
    CHECK(hda_ring_fragment_writable(0, 4096, 65536));
    CHECK(!hda_ring_fragment_writable(16, 4096, 65536));

    CHECK(hda_stream_format(48000, 16, 2, &value) == 0);
    CHECK(value == 0x0011u);
    CHECK(hda_stream_format(48000, 24, 1, &value) == 0);
    CHECK(value == 0x0030u);
    CHECK(hda_stream_format(44100, 16, 2, &value) != 0);
    CHECK(hda_stream_format(48000, 12, 2, &value) != 0);
    CHECK(hda_stream_format(48000, 16, 0, &value) != 0);

    CHECK(hda_output_stream_offset(0x1200u, &value) == 0);
    CHECK(value == 0xc0u);
    CHECK(hda_output_stream_offset(0x0200u, &value) != 0);
    CHECK(hda_node_range_decode(0x00020005u, &range) == 0);
    CHECK(range.first == 2 && range.count == 5);
    CHECK(hda_node_range_decode(0x00010000u, &range) != 0);
    CHECK(hda_node_range_decode(0x007f0001u, &range) == 0);
    CHECK(hda_node_range_decode(0x007f0002u, &range) != 0);
    CHECK(hda_node_range_decode(0x00ff0002u, &range) != 0);
    CHECK(hda_widget_type(4u << 20) == HDA_WIDGET_PIN_COMPLEX);

    CHECK(hda_bdl_build(entries, HDA_BDL_MAX_ENTRIES, 0x100000u,
                        12288u, &count) == 0);
    CHECK(count == 3);
    CHECK(entries[0].address == 0x100000u && entries[0].length == 4096u);
    CHECK(entries[2].address == 0x102000u && entries[2].length == 4096u);
    CHECK(entries[0].flags == 1u && entries[2].flags == 1u);
    CHECK(hda_bdl_build(entries, 1, 0x100000u, 8192u, &count) != 0);
    CHECK(hda_bdl_build(entries, 2, 0x100001u, 4096u, &count) != 0);
    CHECK(hda_bdl_build(entries, 2, 0x100000u, 4097u, &count) != 0);
    CHECK(hda_bdl_build(entries, HDA_BDL_MAX_ENTRIES, 0x100000u,
                        0xffffff80u, &count) != 0);
    CHECK(hda_bdl_build(entries, 2, UINT64_MAX - 127u,
                        256u, &count) != 0);

    if (failures == 0) printf("[hda-core] ok\n");
    return failures;
}

#ifdef HDA_CORE_STANDALONE_TEST
int main(void) { return run_hda_core_tests(); }
#endif
