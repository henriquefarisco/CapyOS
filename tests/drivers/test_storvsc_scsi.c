#include <stdio.h>

#include "drivers/storage/storvsc_scsi.h"

static uint32_t be32_at(const uint8_t *src) {
  return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

int run_storvsc_scsi_tests(void) {
  int fails = 0;
  uint8_t cdb[STORVSC_SCSI_CDB_MAX];
  uint8_t capacity[32];
  uint64_t last_lba = 0u;
  uint32_t block_size = 0u;
  size_t len = 0u;

  len = storvsc_scsi_build_test_unit_ready(cdb, sizeof(cdb));
  if (len != 6u || cdb[0] != STORVSC_SCSI_OP_TEST_UNIT_READY ||
      cdb[5] != 0u) {
    printf("[storvsc_scsi] TEST UNIT READY encoding failed\n");
    fails++;
  }

  len = storvsc_scsi_build_inquiry(cdb, sizeof(cdb), 1u, 0x83u, 96u);
  if (len != 6u || cdb[0] != STORVSC_SCSI_OP_INQUIRY || cdb[1] != 1u ||
      cdb[2] != 0x83u || cdb[3] != 0u || cdb[4] != 96u) {
    printf("[storvsc_scsi] INQUIRY encoding failed\n");
    fails++;
  }

  len = storvsc_scsi_build_read_capacity_16(cdb, sizeof(cdb), 32u);
  if (len != 16u || cdb[0] != STORVSC_SCSI_OP_READ_CAPACITY_16 ||
      cdb[1] != STORVSC_SCSI_SERVICE_ACTION_READ_CAPACITY_16 ||
      be32_at(&cdb[10]) != 32u) {
    printf("[storvsc_scsi] READ CAPACITY(16) encoding failed\n");
    fails++;
  }

  len = storvsc_scsi_build_rw10(cdb, sizeof(cdb), 0, 0x00123456u, 8u);
  if (len != 10u || cdb[0] != STORVSC_SCSI_OP_READ_10 ||
      be32_at(&cdb[2]) != 0x00123456u || cdb[7] != 0u || cdb[8] != 8u) {
    printf("[storvsc_scsi] READ(10) encoding failed\n");
    fails++;
  }

  len = storvsc_scsi_build_rw16(cdb, sizeof(cdb), 1, 0x0102030405060708ULL,
                                0x00100020u);
  if (len != 16u || cdb[0] != STORVSC_SCSI_OP_WRITE_16 ||
      be32_at(&cdb[2]) != 0x01020304u ||
      be32_at(&cdb[6]) != 0x05060708u ||
      be32_at(&cdb[10]) != 0x00100020u) {
    printf("[storvsc_scsi] WRITE(16) encoding failed\n");
    fails++;
  }

  if (storvsc_scsi_build_inquiry(cdb, sizeof(cdb), 0u, 0u, 256u) != 0u ||
      storvsc_scsi_build_rw10(cdb, sizeof(cdb), 0, 0u, 0u) != 0u ||
      storvsc_scsi_build_rw16(cdb, 8u, 0, 0u, 1u) != 0u ||
      storvsc_scsi_build_read_capacity_16(cdb, sizeof(cdb), 0u) != 0u) {
    printf("[storvsc_scsi] invalid builder inputs were accepted\n");
    fails++;
  }

  for (size_t i = 0u; i < sizeof(capacity); ++i) {
    capacity[i] = 0u;
  }
  capacity[0] = 0x00u;
  capacity[1] = 0x00u;
  capacity[2] = 0x00u;
  capacity[3] = 0x00u;
  capacity[4] = 0x00u;
  capacity[5] = 0x00u;
  capacity[6] = 0x1Fu;
  capacity[7] = 0xFFu;
  capacity[8] = 0x00u;
  capacity[9] = 0x00u;
  capacity[10] = 0x02u;
  capacity[11] = 0x00u;
  if (storvsc_scsi_parse_read_capacity_16(capacity, sizeof(capacity),
                                          &last_lba, &block_size) != 0 ||
      last_lba != 0x1FFFu || block_size != 512u) {
    printf("[storvsc_scsi] READ CAPACITY(16) parse failed\n");
    fails++;
  }

  capacity[11] = 0u;
  capacity[10] = 0u;
  last_lba = 0xFFFFu;
  block_size = 0xFFFFu;
  if (storvsc_scsi_parse_read_capacity_16(capacity, sizeof(capacity),
                                          &last_lba, &block_size) >= 0 ||
      last_lba != 0u || block_size != 0u) {
    printf("[storvsc_scsi] zero block size was accepted\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] storvsc_scsi OK\n");
  }
  return fails;
}
