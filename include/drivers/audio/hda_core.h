#ifndef CAPYOS_DRIVERS_AUDIO_HDA_CORE_H
#define CAPYOS_DRIVERS_AUDIO_HDA_CORE_H

#include <stddef.h>
#include <stdint.h>

#define HDA_BDL_MAX_ENTRIES 32u
#define HDA_BDL_FRAGMENT_BYTES 4096u

struct hda_bdl_entry {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct hda_node_range {
    uint8_t first;
    uint8_t count;
};

enum hda_widget_type {
    HDA_WIDGET_AUDIO_OUTPUT = 0,
    HDA_WIDGET_AUDIO_INPUT = 1,
    HDA_WIDGET_MIXER = 2,
    HDA_WIDGET_SELECTOR = 3,
    HDA_WIDGET_PIN_COMPLEX = 4
};

int hda_stream_format(uint32_t sample_rate,
                      uint8_t bits_per_sample,
                      uint8_t channels,
                      uint16_t *format);
int hda_output_stream_offset(uint16_t gcap, uint16_t *offset);
int hda_node_range_decode(uint32_t response, struct hda_node_range *range);
uint8_t hda_widget_type(uint32_t widget_caps);
/* Positions include CBL itself immediately before wrap. */
int hda_ring_advance(uint32_t previous, uint32_t position, uint32_t bytes,
                     uint32_t *advance);
int hda_ring_fragment_writable(uint32_t fragment, uint32_t position,
                               uint32_t bytes);
int hda_bdl_build(struct hda_bdl_entry *entries,
                  size_t entry_capacity,
                  uint64_t buffer_address,
                  uint32_t buffer_bytes,
                  size_t *entry_count);

#endif
