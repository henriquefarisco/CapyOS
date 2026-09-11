#include "drivers/audio/hda_core.h"

#define HDA_BDL_FLAG_IOC 0x1u

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
