#include "drivers/audio/ac97_core.h"

#define AC97_DMA_LIMIT 0x100000000ull
#define AC97_IO_PORT_LIMIT 0x10000u

int ac97_io_base_ok(uint32_t raw_bar, uint32_t span, uint16_t *base) {
    uint32_t io = raw_bar & ~0x3u;
    if (!base || span == 0 || span > AC97_IO_PORT_LIMIT) return 0;
    if (!(raw_bar & 1u) || io == 0) return 0;
    if (io > AC97_IO_PORT_LIMIT - span) return 0;
    *base = (uint16_t)io;
    return 1;
}

int ac97_dma_address_ok(uint64_t address, uint32_t bytes) {
    if (address == 0 || bytes == 0) return 0;
    if (address >= AC97_DMA_LIMIT) return 0;
    return address <= AC97_DMA_LIMIT - bytes;
}

int ac97_bdl_build(struct ac97_bdl_entry *entries,
                   size_t entry_capacity,
                   uint64_t buffer_address,
                   uint32_t buffer_bytes,
                   size_t *entry_count) {
    size_t i;
    if (!entries || !entry_count) return -1;
    if (entry_capacity < AC97_BDL_ENTRIES) return -1;
    if (buffer_bytes != AC97_RING_BYTES) return -1;
    if ((buffer_address & 0x3u) != 0) return -1;
    if (!ac97_dma_address_ok(buffer_address, buffer_bytes)) return -1;
    for (i = 0; i < AC97_BDL_ENTRIES; ++i) {
        uint32_t fragment = (uint32_t)(i % AC97_RING_FRAGMENTS);
        entries[i].address =
            (uint32_t)(buffer_address + fragment * AC97_BDL_FRAGMENT_BYTES);
        entries[i].samples = (uint16_t)AC97_BDL_FRAGMENT_SAMPLES;
        entries[i].flags = (uint16_t)AC97_BDL_FLAG_IOC;
    }
    *entry_count = AC97_BDL_ENTRIES;
    return 0;
}

int ac97_ring_position(uint8_t civ,
                       uint16_t picb_samples,
                       uint32_t buffer_bytes,
                       uint32_t *position_bytes) {
    uint32_t fragment;
    uint32_t consumed;
    if (!position_bytes || buffer_bytes != AC97_RING_BYTES) return -1;
    if (civ >= AC97_BDL_ENTRIES || picb_samples > AC97_BDL_FRAGMENT_SAMPLES)
        return -1;
    fragment = (uint32_t)civ % AC97_RING_FRAGMENTS;
    /* PICB counts samples still to be played in the current buffer. */
    consumed = AC97_BDL_FRAGMENT_BYTES - (uint32_t)picb_samples * 2u;
    *position_bytes = fragment * AC97_BDL_FRAGMENT_BYTES + consumed;
    return 0;
}

int ac97_next_lvi(uint8_t civ, uint8_t *lvi) {
    if (!lvi || civ >= AC97_BDL_ENTRIES) return -1;
    *lvi = (uint8_t)((civ + AC97_BDL_ENTRIES - 1u) % AC97_BDL_ENTRIES);
    return 0;
}

int ac97_ring_fragment_writable(uint32_t fragment, uint32_t position,
                                uint32_t bytes) {
    uint32_t start, distance;
    if (bytes != AC97_RING_BYTES || position > bytes ||
        fragment >= AC97_RING_FRAGMENTS) return 0;
    if (position == bytes) position = 0;
    start = fragment * AC97_BDL_FRAGMENT_BYTES;
    if (position / AC97_BDL_FRAGMENT_BYTES == fragment) return 0;
    distance = start >= position ? start - position : bytes - position + start;
    /* Do not overwrite the current descriptor or its near-future neighbours. */
    return distance >= 2u * AC97_BDL_FRAGMENT_BYTES;
}

int ac97_mixer_volume_word(uint8_t attenuation, int mute, uint16_t *word) {
    if (!word || attenuation > AC97_MIXER_ATTENUATION_MAX) return -1;
    *word = (uint16_t)(((uint16_t)attenuation << 8) | attenuation);
    if (mute) *word |= (uint16_t)AC97_MIXER_VOLUME_MUTE;
    return 0;
}
