#include "fs/storage/partition.h"

#include <stddef.h>

// Entradas do MBR ocupam 16 bytes na tabela iniciando no offset 446.
// layout: status(1), chs-first(3), type(1), chs-last(3), lba_start(4),
// sectors(4)
static int parse_entry(const uint8_t *entry, struct mbr_partition *out) {
  if (!entry || !out) {
    return -1;
  }
  out->bootable = entry[0];
  out->type = entry[4];
  out->lba_start = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                   ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
  out->sector_count = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                      ((uint32_t)entry[14] << 16) | ((uint32_t)entry[15] << 24);
  if (out->type == 0 || out->sector_count == 0) {
    return -1;
  }
  return 0;
}

int mbr_read_partition(struct block_device *raw, int index,
                       struct mbr_partition *out) {
  if (!raw || !out || index < 0 || index > 3) {
    return -1;
  }
  if (raw->block_size != 512 || raw->block_count == 0) {
    return -1;
  }
  uint8_t mbr[512];
  if (block_device_read(raw, 0, mbr) != 0) {
    return -1;
  }
  if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
    return -1;
  }
  const uint8_t *entry = &mbr[446 + index * 16];
  if (parse_entry(entry, out) != 0) {
    return -1;
  }
  if (out->lba_start >= raw->block_count) {
    return -1;
  }
  if (out->sector_count > raw->block_count - out->lba_start) {
    return -1;
  }
  return 0;
}
