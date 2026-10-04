#include "drivers/audio/hda_core.h"

#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("[hda-core] FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

struct route_fixture {
    unsigned mode, calls, writes, mixer_gain, input_on, input_off, pin_on, dac_gain, selected;
};
static int route_command(void *ctx, uint8_t node, uint32_t verb, uint32_t *response) {
    struct route_fixture *f = ctx;
    ++f->calls; *response = 0;
    if (verb == 0xf0009u) {
        if (node == 20) *response = 0x40018fu;
        else if (node == 12) *response = f->mode == 8 ? 0x30010fu : 0x20010fu;
        else if (node == 2) *response = f->mode == 1 ? 0xdu : 0x11u;
        else *response = 0xf00000u;
    } else if (verb == 0xf000eu) {
        *response = node == 20 ? 1u : 2u;
        if (f->mode == 2) *response = 0x81u;
        if (f->mode == 3) *response = 17u;
    } else if (verb == 0xf0200u) {
        *response = node == 20 ? (f->mode == 1 ? 2u : 12u) : 0x0b02u;
        if (f->mode == 4) *response = 0x82u;
        if (f->mode == 5) *response = 0x0c0cu;
        if (f->mode == 8 && node == 12) *response = 0x020bu;
    } else if (verb == 0xf0012u) {
        *response = node == 12 ? 0x34040u : node == 2 ? 0x80034a4au : 0x80000000u;
        if (f->mode == 6) *response = 0x34041u;
    } else if (verb == 0xf000du) *response = 0x80000000u;
    else if (verb == 0xf000cu) *response = 0x10u;
    else {
        ++f->writes;
        if (node == 12 && verb == 0x3b040u) ++f->mixer_gain;
        if (node == 12 && verb == 0x37000u) ++f->input_on;
        if (node == 12 && verb == 0x37180u) ++f->input_off;
        if (node == 12 && verb == 0x70101u) ++f->selected;
        if (node == 20 && verb == 0x70740u) ++f->pin_on;
        if (node == 2 && (verb & 0xf0000u) == 0x30000u) ++f->dac_gain;
        if (f->mode == 7) return -1;
    }
    return 0;
}

int run_hda_core_tests(void) {
    struct hda_bdl_entry entries[HDA_BDL_MAX_ENTRIES];
    struct hda_node_range range;
    uint16_t value = 0;
    size_t count = 0;
    int failures = 0;
    uint32_t advance;
    struct route_fixture route = {0};
    CHECK(hda_codec_route_setup(1, 2, 20, 0x11, route_command, &route) == 0);
    CHECK(route.mixer_gain == 1 && route.input_on == 1 && route.input_off == 1);
    CHECK(route.pin_on == 1 && !route.dac_gain && route.calls < 80);
    route = (struct route_fixture){.mode = 1};
    CHECK(hda_codec_route_setup(1, 2, 20, 0x11, route_command, &route) == 0);
    CHECK(!route.mixer_gain && route.dac_gain == 1 && route.pin_on == 1);
    route = (struct route_fixture){.mode = 8};
    CHECK(hda_codec_route_setup(1, 2, 20, 0x11, route_command, &route) == 0);
    CHECK(route.selected == 1 && route.input_on == 1 && !route.input_off);
    for (unsigned mode = 2; mode <= 7; ++mode) {
        route = (struct route_fixture){.mode = mode};
        CHECK(hda_codec_route_setup(1, 2, 20, 0x11, route_command, &route) != 0);
        CHECK(route.calls <= 256 && !route.pin_on);
        if (mode <= 5) CHECK(!route.writes);
    }
    CHECK(hda_codec_route_setup(1, 2, 20, 0x11, 0, &route) != 0);
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
