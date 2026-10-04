#include "drivers/audio/hda_core.h"

#define HDA_BDL_FLAG_IOC 0x1u

struct codec_route {
    hda_codec_command_fn command;
    void *ctx;
    unsigned budget, length;
    uint8_t nodes[8], selected[8], count[8];
    uint32_t caps[8];
};

static int route_query(struct codec_route *r, uint8_t node, uint32_t verb, uint32_t *value) {
    if (!r->budget) return -1;
    --r->budget;
    return r->command(r->ctx, node, verb, value);
}

static int route_find(struct codec_route *r, uint8_t node, uint8_t target, unsigned depth) {
    uint32_t caps, length, packed = 0;
    if (!node || node > 127 || depth >= 8) return -1;
    for (unsigned i = 0; i < depth; ++i) if (r->nodes[i] == node) return -1;
    if (route_query(r, node, 0xf0009u, &caps) != 0 || (caps & (1u << 9))) return -1;
    r->nodes[depth] = node; r->caps[depth] = caps;
    if (node == target) {
        if (hda_widget_type(caps) != HDA_WIDGET_AUDIO_OUTPUT) return -1;
        r->length = depth + 1u;
        return 0;
    }
    unsigned type = hda_widget_type(caps);
    if ((type != HDA_WIDGET_PIN_COMPLEX && type != HDA_WIDGET_MIXER &&
         type != HDA_WIDGET_SELECTOR) || !(caps & (1u << 8)) ||
        route_query(r, node, 0xf000eu, &length) != 0 ||
        !length || length > 16u) return -1;
    r->count[depth] = (uint8_t)length;
    uint8_t connections[16];
    for (unsigned i = 0; i < length; ++i) {
        if (!(i % 4u) && route_query(r, node, 0xf0200u | i, &packed) != 0) return -1;
        connections[i] = (uint8_t)(packed >> ((i % 4u) * 8u));
        if (!connections[i] || (connections[i] & 0x80u)) return -1;
    }
    for (unsigned i = 0; i < length; ++i) {
        if (route_find(r, connections[i], target, depth + 1u) == 0) {
            r->selected[depth] = (uint8_t)i;
            return 0;
        }
    }
    return -1;
}

int hda_codec_route_setup(uint8_t afg, uint8_t output, uint8_t pin, uint16_t format,
                          hda_codec_command_fn command, void *ctx) {
    if (!command || !afg || afg > 127 || !output || output == pin) return -1;
    struct codec_route r = {.command = command, .ctx = ctx, .budget = 256};
    uint32_t value;
    if (route_find(&r, pin, output, 0) != 0 ||
        command(ctx, afg, 0x70500u, &value) != 0) return -1;
    for (unsigned n = r.length; n-- > 0;) {
        uint8_t node = r.nodes[n];
        uint32_t caps = r.caps[n];
        if ((caps & (1u << 10)) && command(ctx, node, 0x70500u, &value) != 0) return -1;
        if (n + 1u < r.length && hda_widget_type(caps) != HDA_WIDGET_MIXER &&
            command(ctx, node, 0x70100u | r.selected[n], &value) != 0) return -1;
        for (unsigned input = 0; input < 2; ++input) {
            if (input && hda_widget_type(caps) == HDA_WIDGET_PIN_COMPLEX) continue;
            if (!(caps & (input ? 2u : 4u))) continue;
            uint32_t amp;
            if (command(ctx, (caps & 8u) ? node : afg,
                        input ? 0xf000du : 0xf0012u, &amp) != 0) return -1;
            uint32_t gain = amp & 127u, steps = (amp >> 8) & 127u;
            if (gain > steps) return -1;
            int per_input = input && hda_widget_type(caps) == HDA_WIDGET_MIXER;
            unsigned count = per_input ? r.count[n] : 1u;
            for (unsigned i = 0; i < count; ++i) {
                uint32_t setting = gain;
                if (per_input && i != r.selected[n]) {
                    if (!(amp & (1u << 31))) return -1;
                    setting = 0x80u; /* Never enable unrelated microphone/loopback inputs. */
                }
                uint32_t verb = 0x30000u | (input ? 0x7000u : 0xb000u) |
                                (i << 8) | setting;
                if (command(ctx, node, verb, &value) != 0) return -1;
            }
        }
    }
    if (command(ctx, output, 0x70610u, &value) != 0 ||
        command(ctx, output, 0x20000u | format, &value) != 0 ||
        command(ctx, pin, 0x70740u, &value) != 0) return -1;
    /* EAPD exists only on pins advertising the corresponding capability. */
    if (command(ctx, pin, 0xf000cu, &value) != 0) return -1;
    if ((value & (1u << 16)) && command(ctx, pin, 0x70c02u, &value) != 0) return -1;
    return 0;
}

int hda_stream_format(uint32_t sample_rate,
                      uint8_t bits_per_sample,
                      uint8_t channels,
                      uint16_t *format) {
    uint16_t bits;
    if (!format || channels == 0 || channels > 16) return -1;
    if (sample_rate != 48000u) return -1;
    switch (bits_per_sample) {
        case 8: bits = 0u; break;
        case 16: bits = 1u; break;
        case 20: bits = 2u; break;
        case 24: bits = 3u; break;
        case 32: bits = 4u; break;
        default: return -1;
    }
    /* Base 48 kHz, multiplier/divisor 1, encoded width, channels minus one. */
    *format = (uint16_t)((bits << 4) | (uint16_t)(channels - 1u));
    return 0;
}

int hda_output_stream_offset(uint16_t gcap, uint16_t *offset) {
    uint8_t input_streams = (uint8_t)((gcap >> 8) & 0x0fu);
    uint8_t output_streams = (uint8_t)((gcap >> 12) & 0x0fu);
    if (!offset || output_streams == 0) return -1;
    *offset = (uint16_t)(0x80u + (uint16_t)input_streams * 0x20u);
    return 0;
}

int hda_node_range_decode(uint32_t response, struct hda_node_range *range) {
    if (!range) return -1;
    range->first = (uint8_t)((response >> 16) & 0xffu);
    range->count = (uint8_t)(response & 0xffu);
    /* Codec node IDs occupy seven bits in a command. Reject wraparound and
     * ranges crossing the protocol boundary before enumeration. */
    return range->count && range->first < 128u &&
           (uint16_t)range->first + range->count <= 128u ? 0 : -1;
}

uint8_t hda_widget_type(uint32_t widget_caps) {
    return (uint8_t)((widget_caps >> 20) & 0x0fu);
}

int hda_ring_advance(uint32_t previous, uint32_t position, uint32_t bytes,
                     uint32_t *advance) {
    if (!advance || !bytes || previous > bytes || position > bytes) return -1;
    *advance = position >= previous ? position - previous : bytes - previous + position;
    return 0;
}

int hda_ring_fragment_writable(uint32_t fragment, uint32_t position,
                               uint32_t bytes) {
    uint32_t start, distance;
    if (bytes < 4u * HDA_BDL_FRAGMENT_BYTES ||
        bytes % HDA_BDL_FRAGMENT_BYTES || position > bytes ||
        fragment >= bytes / HDA_BDL_FRAGMENT_BYTES) return 0;
    if (position == bytes) position = 0;
    start = fragment * HDA_BDL_FRAGMENT_BYTES;
    if (position / HDA_BDL_FRAGMENT_BYTES == fragment) return 0;
    distance = start >= position ? start - position : bytes - position + start;
    /* Do not overwrite the current descriptor or its near-future neighbours. */
    return distance >= 2u * HDA_BDL_FRAGMENT_BYTES;
}

int hda_bdl_build(struct hda_bdl_entry *entries,
                  size_t entry_capacity,
                  uint64_t buffer_address,
                  uint32_t buffer_bytes,
                  size_t *entry_count) {
    size_t count;
    size_t i;
    if (!entries || !entry_count || buffer_address == 0 || buffer_bytes == 0)
        return -1;
    if ((buffer_address & 0x7fu) != 0 || (buffer_bytes & 0x7fu) != 0)
        return -1;
    if (buffer_address > UINT64_MAX - buffer_bytes) return -1;
    count = 1u + (buffer_bytes - 1u) / HDA_BDL_FRAGMENT_BYTES;
    if (count == 0 || count > entry_capacity || count > HDA_BDL_MAX_ENTRIES)
        return -1;
    for (i = 0; i < count; ++i) {
        uint32_t offset = (uint32_t)i * HDA_BDL_FRAGMENT_BYTES;
        uint32_t remaining = buffer_bytes - offset;
        entries[i].address = buffer_address + offset;
        entries[i].length = remaining > HDA_BDL_FRAGMENT_BYTES
                                ? HDA_BDL_FRAGMENT_BYTES : remaining;
        entries[i].flags = HDA_BDL_FLAG_IOC;
    }
    *entry_count = count;
    return 0;
}
