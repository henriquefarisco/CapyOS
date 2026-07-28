#ifndef BOOT_GPT_IDENTITY_H
#define BOOT_GPT_IDENTITY_H

#include <stdint.h>

#define CAPYOS_GPT_IDENTITY_SECTOR_SIZE 512u
#define CAPYOS_GPT_IDENTITY_ERR (-1)

typedef int (*capyos_gpt_read_fn)(
    void *ctx, uint32_t lba,
    uint8_t sector[CAPYOS_GPT_IDENTITY_SECTOR_SIZE]);

struct capyos_gpt_partition_identity {
  uint32_t lba;
  uint32_t sectors;
  uint8_t guid[16];
};

struct capyos_gpt_identity {
  uint32_t disk_sectors;
  uint8_t disk_guid[16];
  struct capyos_gpt_partition_identity esp;
  struct capyos_gpt_partition_identity boot;
  struct capyos_gpt_partition_identity data;
};

int capyos_gpt_identity_read(capyos_gpt_read_fn reader, void *ctx,
                             uint32_t block_size, uint32_t block_count,
                             struct capyos_gpt_identity *out);
int capyos_gpt_identity_read_legacy(capyos_gpt_read_fn reader, void *ctx,
                                    uint32_t block_size, uint32_t block_count,
                                    struct capyos_gpt_identity *out);

#endif /* BOOT_GPT_IDENTITY_H */
