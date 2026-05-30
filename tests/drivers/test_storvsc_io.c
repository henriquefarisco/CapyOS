#include <stdio.h>

#include "drivers/storage/storvsc_io.h"

static void write_u16_le(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u32_le(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *src) {
  return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *src) {
  return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *src) {
  uint64_t value = 0u;
  for (uint8_t i = 0u; i < 8u; ++i) {
    value |= ((uint64_t)src[i]) << (i * 8u);
  }
  return value;
}

static void zero_bytes(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  for (size_t i = 0u; i < len; ++i) {
    bytes[i] = 0u;
  }
}

int run_storvsc_io_tests(void) {
  int fails = 0;
  uint8_t packet[STORVSP_PACKET_SIZE];
  struct storvsc_io_request req;
  struct storvsc_io_completion completion;
  size_t len = 0u;

  zero_bytes(&req, sizeof(req));
  req.path_id = 2u;
  req.target_id = 3u;
  req.lun = 1u;
  req.data_in = STORVSC_IO_DATA_IN;
  req.data_transfer_length = 32u;
  req.cdb_length =
      (uint8_t)storvsc_scsi_build_read_capacity_16(req.cdb, sizeof(req.cdb),
                                                   32u);
  len = storvsc_io_build_execute_srb(packet, sizeof(packet), &req);
  if (len != STORVSP_PACKET_SIZE ||
      read_u32_le(packet) != STORVSP_OPERATION_EXECUTE_SRB ||
      read_u32_le(packet + 4) != STORVSP_FLAG_REQUEST_COMPLETION ||
      read_u32_le(packet + 8) != STORVSP_STATUS_SUCCESS ||
      read_u16_le(packet + 12) != STORVSC_IO_REQUEST_SIZE ||
      packet[17] != 2u || packet[18] != 3u || packet[19] != 1u ||
      packet[20] != STORVSC_SCSI_CDB_MAX ||
      packet[21] != STORVSC_IO_SENSE_BYTES ||
      packet[22] != STORVSC_IO_DATA_IN ||
      read_u32_le(packet + 24) != 32u ||
      packet[28] != STORVSC_SCSI_OP_READ_CAPACITY_16) {
    printf("[storvsc_io] execute SRB READ CAPACITY packet failed\n");
    fails++;
  }

  zero_bytes(&req, sizeof(req));
  req.data_in = STORVSC_IO_DATA_OUT;
  req.data_transfer_length = 4096u;
  req.cdb_length = (uint8_t)storvsc_scsi_build_rw16(
      req.cdb, sizeof(req.cdb), 1, 0x0102030405060708ULL, 8u);
  len = storvsc_io_build_execute_srb(packet, sizeof(packet), &req);
  if (len != STORVSP_PACKET_SIZE ||
      packet[22] != STORVSC_IO_DATA_OUT ||
      read_u32_le(packet + 24) != 4096u ||
      packet[28] != STORVSC_SCSI_OP_WRITE_16) {
    printf("[storvsc_io] execute SRB WRITE packet failed\n");
    fails++;
  }

  packet[0] = 0xAAu;
  req.cdb_length = 0u;
  if (storvsc_io_build_execute_srb(packet, sizeof(packet), &req) != 0u ||
      packet[0] != 0u ||
      storvsc_io_build_execute_srb(packet, 8u, &req) != 0u) {
    printf("[storvsc_io] invalid execute SRB inputs were accepted\n");
    fails++;
  }

  zero_bytes(packet, sizeof(packet));
  write_u32_le(packet, STORVSP_OPERATION_COMPLETE_IO);
  write_u32_le(packet + 4, STORVSP_FLAG_REQUEST_COMPLETION);
  write_u32_le(packet + 8, STORVSP_STATUS_SUCCESS);
  write_u16_le(packet + 12, STORVSC_IO_REQUEST_SIZE);
  packet[14] = 0x01u;
  packet[15] = 0x00u;
  packet[21] = STORVSC_IO_SENSE_BYTES;
  packet[22] = STORVSC_IO_DATA_IN;
  write_u32_le(packet + 24, 512u);
  if (storvsc_io_parse_completion(packet, sizeof(packet), &completion, 0,
                                  1) != 0 ||
      completion.operation != STORVSP_OPERATION_COMPLETE_IO ||
      completion.error_class != BLOCK_IO_OK ||
      completion.data_transfer_length != 512u) {
    printf("[storvsc_io] successful completion parse failed\n");
    fails++;
  }

  write_u32_le(packet + 8, 5u);
  if (storvsc_io_parse_completion(packet, sizeof(packet), &completion, 0,
                                  1) != 0 ||
      completion.error_class != BLOCK_IO_ERR_PERMANENT) {
    printf("[storvsc_io] non-success VSP status was not classified\n");
    fails++;
  }
  write_u32_le(packet + 8, STORVSP_STATUS_SUCCESS);

  packet[15] = 0x02u;
  packet[44] = 0x70u;
  if (storvsc_io_parse_completion(packet, sizeof(packet), &completion, 0,
                                  1) != 0 ||
      completion.error_class != BLOCK_IO_ERR_PERMANENT ||
      completion.sense_data[0] != 0x70u) {
    printf("[storvsc_io] CHECK CONDITION completion parse failed\n");
    fails++;
  }

  packet[15] = 0x00u;
  packet[14] = 0x09u;
  if (storvsc_io_parse_completion(packet, sizeof(packet), &completion, 0,
                                  1) != 0 ||
      completion.error_class != BLOCK_IO_ERR_TIMEOUT) {
    printf("[storvsc_io] SRB timeout classification failed\n");
    fails++;
  }

  write_u32_le(packet, STORVSP_OPERATION_BEGIN_INITIALIZATION);
  if (storvsc_io_parse_completion(packet, sizeof(packet), &completion, 0,
                                  1) >= 0 ||
      completion.operation != STORVSP_OPERATION_BEGIN_INITIALIZATION) {
    printf("[storvsc_io] invalid completion operation was accepted\n");
    fails++;
  }

  zero_bytes(packet, sizeof(packet));
  write_u32_le(packet, STORVSP_OPERATION_COMPLETE_IO);
  write_u16_le(packet + 12, 8u);
  if (storvsc_io_parse_completion(packet, sizeof(packet), &completion, 0,
                                  1) >= 0) {
    printf("[storvsc_io] invalid completion request length was accepted\n");
    fails++;
  }

  zero_bytes(&req, sizeof(req));
  req.data_in = STORVSC_IO_DATA_IN;
  req.data_transfer_length = 512u;
  req.cdb_length = (uint8_t)storvsc_scsi_build_rw10(
      req.cdb, sizeof(req.cdb), 0, 0x1000u, 1u);
  len = storvsc_io_build_execute_srb(packet, sizeof(packet), &req);
  {
    uint8_t envelope[128];
    struct storvsc_io_gpa_direct_request gpa_req;
    struct storvsc_io_gpa_direct_info info;
    zero_bytes(&gpa_req, sizeof(gpa_req));
    gpa_req.payload = packet;
    gpa_req.payload_length = (uint32_t)len;
    gpa_req.data_gpa = 0x12345000ULL;
    gpa_req.data_length = 4096u;
    gpa_req.transaction_id = 0x1122334455667788ULL;
    gpa_req.flags = STORVSC_IO_VMBUS_FLAG_COMPLETION_REQUESTED;
    len = storvsc_io_build_gpa_direct_packet(envelope, sizeof(envelope),
                                             &gpa_req);
    if (len != 104u ||
        read_u16_le(envelope) != STORVSC_IO_VMBUS_PACKET_GPA_DIRECT ||
        read_u16_le(envelope + 2u) != 5u ||
        read_u16_le(envelope + 4u) != 13u ||
        read_u16_le(envelope + 6u) !=
            STORVSC_IO_VMBUS_FLAG_COMPLETION_REQUESTED ||
        read_u64_le(envelope + 8u) != 0x1122334455667788ULL ||
        read_u32_le(envelope + 20u) != 1u ||
        read_u32_le(envelope + 24u) != 4096u ||
        read_u32_le(envelope + 28u) != 0u ||
        read_u64_le(envelope + 32u) != 0x12345ULL ||
        read_u32_le(envelope + 40u) != STORVSP_OPERATION_EXECUTE_SRB ||
        storvsc_io_parse_gpa_direct_packet(envelope, len, &info) != 0 ||
        info.pfn_count != 1u || info.payload_offset != 40u ||
        info.payload_length != 64u) {
      printf("[storvsc_io] GPA-direct packet envelope failed\n");
      fails++;
    }
  }

  {
    uint8_t envelope[160];
    struct storvsc_io_gpa_direct_request gpa_req;
    struct storvsc_io_gpa_direct_info info;
    zero_bytes(&gpa_req, sizeof(gpa_req));
    gpa_req.payload = packet;
    gpa_req.payload_length = STORVSP_PACKET_SIZE;
    gpa_req.data_gpa = 0x20000FF0ULL;
    gpa_req.data_length = 32u;
    gpa_req.transaction_id = 0x42u;
    len = storvsc_io_build_gpa_direct_packet(envelope, sizeof(envelope),
                                             &gpa_req);
    if (len != 112u ||
        storvsc_io_parse_gpa_direct_packet(envelope, len, &info) != 0 ||
        info.pfn_count != 2u || info.byte_offset != 0xFF0u ||
        info.pfns[0] != 0x20000ULL || info.pfns[1] != 0x20001ULL ||
        info.payload_offset != 48u) {
      printf("[storvsc_io] unaligned GPA-direct range failed\n");
      fails++;
    }

    write_u32_le(envelope + 24u, 8192u);
    if (storvsc_io_parse_gpa_direct_packet(envelope, len, &info) >= 0) {
      printf("[storvsc_io] inconsistent GPA-direct range was accepted\n");
      fails++;
    }
    write_u32_le(envelope + 24u, 32u);

    gpa_req.data_gpa = 0x30000000ULL;
    gpa_req.data_length =
        (STORVSC_IO_GPA_DIRECT_MAX_PFNS + 1u) * STORVSC_IO_PAGE_BYTES;
    if (storvsc_io_build_gpa_direct_packet(envelope, sizeof(envelope),
                                           &gpa_req) != 0u) {
      printf("[storvsc_io] oversized GPA-direct range was accepted\n");
      fails++;
    }

    envelope[0] = 0x06u;
    if (storvsc_io_parse_gpa_direct_packet(envelope, len, &info) >= 0) {
      printf("[storvsc_io] invalid GPA-direct packet type was accepted\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] storvsc_io OK\n");
  }
  return fails;
}
