#include "drivers/storage/storvsc_scsi.h"

static void storvsc_scsi_zero(uint8_t *dst, size_t len) {
  if (!dst) {
    return;
  }
  for (size_t i = 0u; i < len; ++i) {
    dst[i] = 0u;
  }
}

static void storvsc_scsi_put_be16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)((value >> 8) & 0xFFu);
  dst[1] = (uint8_t)(value & 0xFFu);
}

static void storvsc_scsi_put_be32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)((value >> 24) & 0xFFu);
  dst[1] = (uint8_t)((value >> 16) & 0xFFu);
  dst[2] = (uint8_t)((value >> 8) & 0xFFu);
  dst[3] = (uint8_t)(value & 0xFFu);
}

static void storvsc_scsi_put_be64(uint8_t *dst, uint64_t value) {
  storvsc_scsi_put_be32(dst, (uint32_t)((value >> 32) & 0xFFFFFFFFu));
  storvsc_scsi_put_be32(dst + 4, (uint32_t)(value & 0xFFFFFFFFu));
}

static uint32_t storvsc_scsi_get_be32(const uint8_t *src) {
  return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static uint64_t storvsc_scsi_get_be64(const uint8_t *src) {
  return ((uint64_t)storvsc_scsi_get_be32(src) << 32) |
         (uint64_t)storvsc_scsi_get_be32(src + 4);
}

static size_t storvsc_scsi_begin(uint8_t *out, size_t cap, size_t cdb_len) {
  if (!out || cap < cdb_len || cdb_len == 0u ||
      cdb_len > STORVSC_SCSI_CDB_MAX) {
    return 0u;
  }
  storvsc_scsi_zero(out, cdb_len);
  return cdb_len;
}

size_t storvsc_scsi_build_test_unit_ready(uint8_t *out, size_t cap) {
  size_t len = storvsc_scsi_begin(out, cap, 6u);
  if (len == 0u) {
    return 0u;
  }
  out[0] = STORVSC_SCSI_OP_TEST_UNIT_READY;
  return len;
}

size_t storvsc_scsi_build_inquiry(uint8_t *out, size_t cap, uint8_t evpd,
                                  uint8_t page_code, uint16_t alloc_len) {
  size_t len = storvsc_scsi_begin(out, cap, 6u);
  if (len == 0u || alloc_len > 0xFFu) {
    return 0u;
  }
  out[0] = STORVSC_SCSI_OP_INQUIRY;
  out[1] = evpd ? 1u : 0u;
  out[2] = page_code;
  out[4] = (uint8_t)alloc_len;
  return len;
}

size_t storvsc_scsi_build_read_capacity_16(uint8_t *out, size_t cap,
                                           uint32_t alloc_len) {
  size_t len = storvsc_scsi_begin(out, cap, 16u);
  if (len == 0u || alloc_len == 0u) {
    return 0u;
  }
  out[0] = STORVSC_SCSI_OP_READ_CAPACITY_16;
  out[1] = STORVSC_SCSI_SERVICE_ACTION_READ_CAPACITY_16;
  storvsc_scsi_put_be32(out + 10, alloc_len);
  return len;
}

size_t storvsc_scsi_build_rw10(uint8_t *out, size_t cap, int write,
                               uint32_t lba, uint16_t block_count) {
  size_t len = storvsc_scsi_begin(out, cap, 10u);
  if (len == 0u || block_count == 0u) {
    return 0u;
  }
  out[0] = write ? STORVSC_SCSI_OP_WRITE_10 : STORVSC_SCSI_OP_READ_10;
  storvsc_scsi_put_be32(out + 2, lba);
  storvsc_scsi_put_be16(out + 7, block_count);
  return len;
}

size_t storvsc_scsi_build_rw16(uint8_t *out, size_t cap, int write,
                               uint64_t lba, uint32_t block_count) {
  size_t len = storvsc_scsi_begin(out, cap, 16u);
  if (len == 0u || block_count == 0u) {
    return 0u;
  }
  out[0] = write ? STORVSC_SCSI_OP_WRITE_16 : STORVSC_SCSI_OP_READ_16;
  storvsc_scsi_put_be64(out + 2, lba);
  storvsc_scsi_put_be32(out + 10, block_count);
  return len;
}

int storvsc_scsi_parse_read_capacity_16(const uint8_t *buf, size_t len,
                                        uint64_t *out_last_lba,
                                        uint32_t *out_block_size) {
  if (!buf || !out_last_lba || !out_block_size) {
    return -1;
  }
  *out_last_lba = 0u;
  *out_block_size = 0u;
  if (len < 12u) {
    return -1;
  }
  *out_last_lba = storvsc_scsi_get_be64(buf);
  *out_block_size = storvsc_scsi_get_be32(buf + 8);
  if (*out_block_size == 0u) {
    *out_last_lba = 0u;
    return -2;
  }
  return 0;
}
