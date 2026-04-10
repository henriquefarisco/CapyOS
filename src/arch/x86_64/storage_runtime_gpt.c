#include "storage_runtime_gpt.h"

#include <stdint.h>

#define GPT_HEADER_LBA_K 1ULL
#define GPT_SIG_K 0x5452415020494645ULL

static const uint8_t k_gpt_linux_fs_guid[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

struct gpt_header_k {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t backup_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint8_t disk_guid[16];
  uint64_t part_entry_lba;
  uint32_t num_part_entries;
  uint32_t part_entry_size;
  uint32_t part_entries_crc32;
} __attribute__((packed));

struct gpt_entry_k {
  uint8_t part_type_guid[16];
  uint8_t uniq_guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attrs;
  uint16_t name[36];
} __attribute__((packed));

static void io_print(const struct x64_storage_runtime_io *io,
                     const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
}

static int guid_eq_local(const uint8_t *a, const uint8_t *b) {
  if (!a || !b) {
    return 0;
  }
  for (uint32_t i = 0; i < 16u; ++i) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static int gpt_name_equals_ascii(const uint16_t *name, const char *ascii) {
  if (!name || !ascii) {
    return 0;
  }
  for (uint32_t i = 0; i < 36u; ++i) {
    char ch = ascii[i];
    if (ch == '\0') {
      return name[i] == 0;
    }
    if ((uint16_t)(uint8_t)ch != name[i]) {
      return 0;
    }
  }
  return 0;
}

int x64_storage_runtime_compute_effective_data_count(
    uint64_t data_start, uint64_t data_count, uint64_t disk_last_lba,
    uint64_t *out_effective_count) {
  if (!out_effective_count) {
    return -1;
  }
  *out_effective_count = 0;
  if (data_start > 0xFFFFFFFFULL || data_count > 0xFFFFFFFFULL) {
    return -1;
  }
  if (data_start + data_count < data_start) {
    return -1;
  }
  if (data_start > disk_last_lba) {
    return -1;
  }

  {
    uint64_t max_data_count = (disk_last_lba - data_start) + 1ULL;
    uint64_t effective_data_count = data_count;
    if (effective_data_count > max_data_count) {
      effective_data_count = max_data_count;
    }
    if (effective_data_count == 0 || disk_last_lba > 0xFFFFFFFFULL) {
      return -1;
    }
    *out_effective_count = effective_data_count;
  }
  return 0;
}

int x64_storage_runtime_find_data_partition_native(
    struct block_device *raw, void *probe_buf, uint32_t probe_buf_size,
    uint32_t *out_data_start, uint32_t *out_data_count,
    const struct x64_storage_runtime_io *io) {
  struct gpt_header_k *hdr = (struct gpt_header_k *)probe_buf;
  uint32_t part_entry_size = 0;
  uint32_t num_part_entries = 0;
  uint64_t part_entry_lba = 0;
  uint32_t ents_per_block = 0;
  uint32_t read_entries = 0;
  uint64_t cur_lba = 0;
  uint32_t data_start = 0;
  uint32_t data_count = 0;
  uint32_t named_data_start = 0;
  uint32_t named_data_count = 0;

  if (!raw || !probe_buf || !out_data_start || !out_data_count ||
      raw->block_size == 0 || raw->block_size > probe_buf_size) {
    io_print(io, "[storage] GPT nativo invalido: parametros de probe.\n");
    return -1;
  }
  *out_data_start = 0;
  *out_data_count = 0;

  if (block_device_read(raw, (uint32_t)GPT_HEADER_LBA_K, probe_buf) != 0) {
    io_print(io, "[storage] GPT nativo invalido: leitura do header falhou.\n");
    return -1;
  }
  if (hdr->signature != GPT_SIG_K || hdr->part_entry_size == 0 ||
      hdr->num_part_entries == 0 || hdr->part_entry_size > raw->block_size) {
    io_print(io, "[storage] GPT nativo invalido: header ou entries invalidos.\n");
    return -1;
  }

  part_entry_size = hdr->part_entry_size;
  num_part_entries = hdr->num_part_entries;
  part_entry_lba = hdr->part_entry_lba;

  ents_per_block = raw->block_size / part_entry_size;
  if (ents_per_block == 0) {
    return -1;
  }

  cur_lba = part_entry_lba;
  while (read_entries < num_part_entries) {
    uint32_t max_in_block = num_part_entries - read_entries;
    if (max_in_block > ents_per_block) {
      max_in_block = ents_per_block;
    }
    if (cur_lba > 0xFFFFFFFFULL ||
        block_device_read(raw, (uint32_t)cur_lba, probe_buf) != 0) {
      io_print(io, "[storage] GPT nativo invalido: leitura das entries falhou.\n");
      return -1;
    }

    for (uint32_t i = 0; i < max_in_block; ++i) {
      uint8_t *ptr = (uint8_t *)probe_buf + (i * part_entry_size);
      struct gpt_entry_k *entry = (struct gpt_entry_k *)ptr;

      if (entry->first_lba == 0 || entry->last_lba < entry->first_lba ||
          entry->first_lba > 0xFFFFFFFFULL ||
          (entry->last_lba - entry->first_lba) > 0xFFFFFFFFULL) {
        continue;
      }

      if (guid_eq_local(entry->part_type_guid, k_gpt_linux_fs_guid)) {
        uint32_t part_start = (uint32_t)entry->first_lba;
        uint32_t part_count = (uint32_t)((entry->last_lba - entry->first_lba) +
                                         1ULL);
        uint16_t part_name[36];
        for (uint32_t name_i = 0; name_i < 36u; ++name_i) {
          part_name[name_i] = entry->name[name_i];
        }
        if (gpt_name_equals_ascii(part_name, "DATA")) {
          named_data_start = part_start;
          named_data_count = part_count;
        } else if (data_start == 0) {
          data_start = part_start;
          data_count = part_count;
        }
      }
    }

    read_entries += max_in_block;
    ++cur_lba;
  }

  if (named_data_start != 0) {
    *out_data_start = named_data_start;
    *out_data_count = named_data_count;
    return 0;
  }
  if (data_start != 0 && data_count != 0) {
    *out_data_start = data_start;
    *out_data_count = data_count;
    return 0;
  }

  io_print(io, "[storage] GPT nativo invalido: particao DATA nao encontrada.\n");
  return -1;
}
