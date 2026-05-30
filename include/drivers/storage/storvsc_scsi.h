#ifndef DRIVERS_STORAGE_STORVSC_SCSI_H
#define DRIVERS_STORAGE_STORVSC_SCSI_H

#include <stddef.h>
#include <stdint.h>

#define STORVSC_SCSI_CDB_MAX 16u

#define STORVSC_SCSI_OP_TEST_UNIT_READY 0x00u
#define STORVSC_SCSI_OP_INQUIRY 0x12u
#define STORVSC_SCSI_OP_READ_10 0x28u
#define STORVSC_SCSI_OP_WRITE_10 0x2Au
#define STORVSC_SCSI_OP_READ_CAPACITY_16 0x9Eu
#define STORVSC_SCSI_OP_READ_16 0x88u
#define STORVSC_SCSI_OP_WRITE_16 0x8Au

#define STORVSC_SCSI_SERVICE_ACTION_READ_CAPACITY_16 0x10u

size_t storvsc_scsi_build_test_unit_ready(uint8_t *out, size_t cap);
size_t storvsc_scsi_build_inquiry(uint8_t *out, size_t cap, uint8_t evpd,
                                  uint8_t page_code, uint16_t alloc_len);
size_t storvsc_scsi_build_read_capacity_16(uint8_t *out, size_t cap,
                                           uint32_t alloc_len);
size_t storvsc_scsi_build_rw10(uint8_t *out, size_t cap, int write,
                               uint32_t lba, uint16_t block_count);
size_t storvsc_scsi_build_rw16(uint8_t *out, size_t cap, int write,
                               uint64_t lba, uint32_t block_count);
int storvsc_scsi_parse_read_capacity_16(const uint8_t *buf, size_t len,
                                        uint64_t *out_last_lba,
                                        uint32_t *out_block_size);

#endif /* DRIVERS_STORAGE_STORVSC_SCSI_H */
