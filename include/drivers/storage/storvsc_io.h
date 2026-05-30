#ifndef DRIVERS_STORAGE_STORVSC_IO_H
#define DRIVERS_STORAGE_STORVSC_IO_H

#include "drivers/storage/block_error.h"
#include "drivers/storage/storvsc_scsi.h"
#include "drivers/storage/storvsp.h"

#include <stddef.h>
#include <stdint.h>

#define STORVSC_IO_REQUEST_SIZE 52u
#define STORVSC_IO_SENSE_BYTES 18u

#define STORVSC_IO_DATA_OUT 0u
#define STORVSC_IO_DATA_IN 1u
#define STORVSC_IO_DATA_UNSPECIFIED 2u

#define STORVSC_IO_PAGE_BYTES 4096u
#define STORVSC_IO_GPA_DIRECT_MAX_PFNS 16u
#define STORVSC_IO_VMBUS_PACKET_GPA_DIRECT 0x9u
#define STORVSC_IO_VMBUS_FLAG_COMPLETION_REQUESTED 1u

struct storvsc_io_request {
  uint8_t port_number;
  uint8_t path_id;
  uint8_t target_id;
  uint8_t lun;
  uint8_t cdb_length;
  uint8_t data_in;
  uint16_t reserved;
  uint32_t data_transfer_length;
  uint8_t cdb[STORVSC_SCSI_CDB_MAX];
};

struct storvsc_io_completion {
  uint32_t operation;
  uint32_t flags;
  uint32_t status;
  uint16_t request_length;
  uint8_t srb_status;
  uint8_t scsi_status;
  uint8_t sense_info_length;
  uint8_t data_in;
  uint32_t data_transfer_length;
  uint8_t sense_data[STORVSC_IO_SENSE_BYTES];
  enum block_io_error_class error_class;
};

struct storvsc_io_gpa_direct_request {
  const uint8_t *payload;
  uint32_t payload_length;
  uint64_t data_gpa;
  uint32_t data_length;
  uint64_t transaction_id;
  uint16_t flags;
};

struct storvsc_io_gpa_direct_info {
  uint16_t type;
  uint16_t offset8;
  uint16_t len8;
  uint16_t flags;
  uint64_t transaction_id;
  uint32_t range_count;
  uint32_t byte_count;
  uint32_t byte_offset;
  uint32_t pfn_count;
  uint64_t pfns[STORVSC_IO_GPA_DIRECT_MAX_PFNS];
  uint32_t payload_offset;
  uint32_t payload_length;
};

size_t storvsc_io_build_execute_srb(uint8_t *out, size_t cap,
                                    const struct storvsc_io_request *req);
int storvsc_io_parse_completion(const uint8_t *buf, size_t len,
                                struct storvsc_io_completion *out,
                                int timed_out, int device_present);
size_t storvsc_io_build_gpa_direct_packet(
    uint8_t *out, size_t cap, const struct storvsc_io_gpa_direct_request *req);
int storvsc_io_parse_gpa_direct_packet(
    const uint8_t *buf, size_t len, struct storvsc_io_gpa_direct_info *out);

#endif /* DRIVERS_STORAGE_STORVSC_IO_H */
