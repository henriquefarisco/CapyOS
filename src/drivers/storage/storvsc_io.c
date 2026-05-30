#include "drivers/storage/storvsc_io.h"

#define STORVSC_IO_HEADER_OPERATION 0u
#define STORVSC_IO_HEADER_FLAGS 4u
#define STORVSC_IO_HEADER_STATUS 8u
#define STORVSC_IO_PAYLOAD 12u
#define STORVSC_IO_REQ_LENGTH 0u
#define STORVSC_IO_REQ_SRB_STATUS 2u
#define STORVSC_IO_REQ_SCSI_STATUS 3u
#define STORVSC_IO_REQ_PORT 4u
#define STORVSC_IO_REQ_PATH 5u
#define STORVSC_IO_REQ_TARGET 6u
#define STORVSC_IO_REQ_LUN 7u
#define STORVSC_IO_REQ_CDB_LENGTH 8u
#define STORVSC_IO_REQ_SENSE_LENGTH 9u
#define STORVSC_IO_REQ_DATA_IN 10u
#define STORVSC_IO_REQ_RESERVED 11u
#define STORVSC_IO_REQ_TRANSFER_LENGTH 12u
#define STORVSC_IO_REQ_CDB 16u
#define STORVSC_IO_REQ_SENSE 32u
#define STORVSC_IO_GPA_RESERVED 16u
#define STORVSC_IO_GPA_RANGE_COUNT 20u
#define STORVSC_IO_GPA_BYTE_COUNT 24u
#define STORVSC_IO_GPA_BYTE_OFFSET 28u
#define STORVSC_IO_GPA_PFNS 32u

static void storvsc_io_zero(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  if (!bytes) {
    return;
  }
  for (size_t i = 0u; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static void storvsc_io_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (size_t i = 0u; i < len; ++i) {
    d[i] = s[i];
  }
}

static void storvsc_io_put_le16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void storvsc_io_put_le32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void storvsc_io_put_le64(uint8_t *dst, uint64_t value) {
  for (uint8_t i = 0u; i < 8u; ++i) {
    dst[i] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
  }
}

static uint16_t storvsc_io_get_le16(const uint8_t *src) {
  return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t storvsc_io_get_le32(const uint8_t *src) {
  return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t storvsc_io_get_le64(const uint8_t *src) {
  uint64_t value = 0u;
  for (uint8_t i = 0u; i < 8u; ++i) {
    value |= ((uint64_t)src[i]) << (i * 8u);
  }
  return value;
}

static int storvsc_io_data_direction_valid(uint8_t data_in) {
  return data_in == STORVSC_IO_DATA_OUT || data_in == STORVSC_IO_DATA_IN ||
         data_in == STORVSC_IO_DATA_UNSPECIFIED;
}

size_t storvsc_io_build_execute_srb(uint8_t *out, size_t cap,
                                    const struct storvsc_io_request *req) {
  uint8_t *payload;

  if (out && cap >= STORVSP_PACKET_SIZE) {
    storvsc_io_zero(out, STORVSP_PACKET_SIZE);
  }
  if (!out || !req || cap < STORVSP_PACKET_SIZE ||
      req->cdb_length == 0u ||
      req->cdb_length > STORVSC_SCSI_CDB_MAX ||
      !storvsc_io_data_direction_valid(req->data_in)) {
    return 0u;
  }

  storvsc_io_put_le32(out + STORVSC_IO_HEADER_OPERATION,
                      STORVSP_OPERATION_EXECUTE_SRB);
  storvsc_io_put_le32(out + STORVSC_IO_HEADER_FLAGS,
                      STORVSP_FLAG_REQUEST_COMPLETION);
  storvsc_io_put_le32(out + STORVSC_IO_HEADER_STATUS, STORVSP_STATUS_SUCCESS);

  payload = out + STORVSC_IO_PAYLOAD;
  storvsc_io_put_le16(payload + STORVSC_IO_REQ_LENGTH,
                      (uint16_t)STORVSC_IO_REQUEST_SIZE);
  payload[STORVSC_IO_REQ_SRB_STATUS] = 0u;
  payload[STORVSC_IO_REQ_SCSI_STATUS] = 0u;
  payload[STORVSC_IO_REQ_PORT] = req->port_number;
  payload[STORVSC_IO_REQ_PATH] = req->path_id;
  payload[STORVSC_IO_REQ_TARGET] = req->target_id;
  payload[STORVSC_IO_REQ_LUN] = req->lun;
  payload[STORVSC_IO_REQ_CDB_LENGTH] = req->cdb_length;
  payload[STORVSC_IO_REQ_SENSE_LENGTH] = STORVSC_IO_SENSE_BYTES;
  payload[STORVSC_IO_REQ_DATA_IN] = req->data_in;
  payload[STORVSC_IO_REQ_RESERVED] = 0u;
  storvsc_io_put_le32(payload + STORVSC_IO_REQ_TRANSFER_LENGTH,
                      req->data_transfer_length);
  for (uint8_t i = 0u; i < req->cdb_length; ++i) {
    payload[STORVSC_IO_REQ_CDB + i] = req->cdb[i];
  }
  return STORVSP_PACKET_SIZE;
}

int storvsc_io_parse_completion(const uint8_t *buf, size_t len,
                                struct storvsc_io_completion *out,
                                int timed_out, int device_present) {
  const uint8_t *payload;

  if (out) {
    storvsc_io_zero(out, sizeof(*out));
    out->error_class = block_io_classify_scsi(0u, 0u, timed_out,
                                              device_present);
  }
  if (!buf || !out || len < STORVSP_PACKET_SIZE) {
    return -1;
  }

  out->operation = storvsc_io_get_le32(buf + STORVSC_IO_HEADER_OPERATION);
  out->flags = storvsc_io_get_le32(buf + STORVSC_IO_HEADER_FLAGS);
  out->status = storvsc_io_get_le32(buf + STORVSC_IO_HEADER_STATUS);
  if (out->operation != STORVSP_OPERATION_COMPLETE_IO &&
      out->operation != STORVSP_OPERATION_EXECUTE_SRB) {
    return -2;
  }

  payload = buf + STORVSC_IO_PAYLOAD;
  out->request_length = storvsc_io_get_le16(payload + STORVSC_IO_REQ_LENGTH);
  out->srb_status = payload[STORVSC_IO_REQ_SRB_STATUS];
  out->scsi_status = payload[STORVSC_IO_REQ_SCSI_STATUS];
  out->sense_info_length = payload[STORVSC_IO_REQ_SENSE_LENGTH];
  out->data_in = payload[STORVSC_IO_REQ_DATA_IN];
  if (out->request_length != STORVSC_IO_REQUEST_SIZE) {
    return -3;
  }
  if (out->sense_info_length > STORVSC_IO_SENSE_BYTES) {
    return -4;
  }
  if (!storvsc_io_data_direction_valid(out->data_in)) {
    return -5;
  }
  out->data_transfer_length =
      storvsc_io_get_le32(payload + STORVSC_IO_REQ_TRANSFER_LENGTH);
  for (uint8_t i = 0u; i < STORVSC_IO_SENSE_BYTES; ++i) {
    out->sense_data[i] = payload[STORVSC_IO_REQ_SENSE + i];
  }
  out->error_class = block_io_classify_scsi(out->scsi_status, out->srb_status,
                                            timed_out, device_present);
  if (out->status != STORVSP_STATUS_SUCCESS &&
      out->error_class == BLOCK_IO_OK) {
    out->error_class = BLOCK_IO_ERR_PERMANENT;
  }
  return 0;
}

size_t storvsc_io_build_gpa_direct_packet(
    uint8_t *out, size_t cap, const struct storvsc_io_gpa_direct_request *req) {
  uint32_t byte_offset;
  uint32_t pfn_count;
  uint32_t payload_offset;
  uint32_t packet_len;
  uint32_t aligned_len;
  uint64_t first_pfn;
  uint64_t covered_bytes;
  uint64_t pfn_count64;

  if (!out || !req || !req->payload || req->payload_length == 0u ||
      req->data_length == 0u) {
    return 0u;
  }

  byte_offset = (uint32_t)(req->data_gpa & (STORVSC_IO_PAGE_BYTES - 1u));
  covered_bytes = (uint64_t)byte_offset + (uint64_t)req->data_length;
  pfn_count64 = (covered_bytes + STORVSC_IO_PAGE_BYTES - 1u) /
                STORVSC_IO_PAGE_BYTES;
  if (pfn_count64 == 0u ||
      pfn_count64 > STORVSC_IO_GPA_DIRECT_MAX_PFNS) {
    return 0u;
  }
  pfn_count = (uint32_t)pfn_count64;

  payload_offset = STORVSC_IO_GPA_PFNS + pfn_count * 8u;
  if (req->payload_length > 0xFFFFFFFFu - payload_offset) {
    return 0u;
  }
  packet_len = payload_offset + req->payload_length;
  aligned_len = (packet_len + 7u) & ~7u;
  if (aligned_len < packet_len || (aligned_len >> 3) > 0xFFFFu ||
      cap < aligned_len) {
    return 0u;
  }

  storvsc_io_zero(out, aligned_len);
  storvsc_io_put_le16(out, STORVSC_IO_VMBUS_PACKET_GPA_DIRECT);
  storvsc_io_put_le16(out + 2u, (uint16_t)(payload_offset >> 3));
  storvsc_io_put_le16(out + 4u, (uint16_t)(aligned_len >> 3));
  storvsc_io_put_le16(out + 6u, req->flags);
  storvsc_io_put_le64(out + 8u, req->transaction_id);
  storvsc_io_put_le32(out + STORVSC_IO_GPA_RESERVED, 0u);
  storvsc_io_put_le32(out + STORVSC_IO_GPA_RANGE_COUNT, 1u);
  storvsc_io_put_le32(out + STORVSC_IO_GPA_BYTE_COUNT, req->data_length);
  storvsc_io_put_le32(out + STORVSC_IO_GPA_BYTE_OFFSET, byte_offset);

  first_pfn = req->data_gpa >> 12;
  for (uint32_t i = 0u; i < pfn_count; ++i) {
    storvsc_io_put_le64(out + STORVSC_IO_GPA_PFNS + i * 8u,
                        first_pfn + i);
  }
  storvsc_io_copy(out + payload_offset, req->payload, req->payload_length);
  return aligned_len;
}

int storvsc_io_parse_gpa_direct_packet(
    const uint8_t *buf, size_t len, struct storvsc_io_gpa_direct_info *out) {
  uint32_t declared_len;
  uint32_t payload_offset;
  uint32_t pfn_bytes;
  uint64_t covered_bytes;
  uint64_t expected_pfns;

  if (!buf || !out || len < STORVSC_IO_GPA_PFNS + 8u) {
    return -1;
  }
  storvsc_io_zero(out, sizeof(*out));

  out->type = storvsc_io_get_le16(buf);
  out->offset8 = storvsc_io_get_le16(buf + 2u);
  out->len8 = storvsc_io_get_le16(buf + 4u);
  out->flags = storvsc_io_get_le16(buf + 6u);
  out->transaction_id = storvsc_io_get_le64(buf + 8u);
  if (out->type != STORVSC_IO_VMBUS_PACKET_GPA_DIRECT) {
    return -2;
  }

  declared_len = ((uint32_t)out->len8) << 3;
  payload_offset = ((uint32_t)out->offset8) << 3;
  if (declared_len > len || payload_offset < STORVSC_IO_GPA_PFNS + 8u ||
      payload_offset >= declared_len || (payload_offset & 7u) != 0u) {
    return -3;
  }

  out->range_count = storvsc_io_get_le32(buf + STORVSC_IO_GPA_RANGE_COUNT);
  if (out->range_count != 1u) {
    return -4;
  }
  pfn_bytes = payload_offset - STORVSC_IO_GPA_PFNS;
  if ((pfn_bytes & 7u) != 0u ||
      pfn_bytes / 8u > STORVSC_IO_GPA_DIRECT_MAX_PFNS) {
    return -5;
  }

  out->byte_count = storvsc_io_get_le32(buf + STORVSC_IO_GPA_BYTE_COUNT);
  out->byte_offset = storvsc_io_get_le32(buf + STORVSC_IO_GPA_BYTE_OFFSET);
  if (out->byte_offset >= STORVSC_IO_PAGE_BYTES) {
    return -6;
  }
  out->pfn_count = pfn_bytes / 8u;
  if (out->byte_count == 0u) {
    return -7;
  }
  covered_bytes = (uint64_t)out->byte_offset + (uint64_t)out->byte_count;
  expected_pfns = (covered_bytes + STORVSC_IO_PAGE_BYTES - 1u) /
                  STORVSC_IO_PAGE_BYTES;
  if (expected_pfns != out->pfn_count) {
    return -8;
  }
  for (uint32_t i = 0u; i < out->pfn_count; ++i) {
    out->pfns[i] =
        storvsc_io_get_le64(buf + STORVSC_IO_GPA_PFNS + i * 8u);
  }
  out->payload_offset = payload_offset;
  out->payload_length = declared_len - payload_offset;
  return 0;
}
