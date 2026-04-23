#ifndef DRIVERS_STORAGE_EFI_BLOCK_H
#define DRIVERS_STORAGE_EFI_BLOCK_H

#include <stdint.h>

#include "fs/block.h"

#define EFI_STATUS_ERROR_BIT_K (1ULL << 63)

typedef uint64_t EFI_STATUS_K;

typedef struct efi_block_io_media_k {
  uint32_t media_id;
  uint8_t removable_media;
  uint8_t media_present;
  uint8_t logical_partition;
  uint8_t read_only;
  uint8_t write_caching;
  uint8_t _pad0[3];
  uint32_t block_size;
  uint32_t io_align;
  uint64_t last_block;
  uint64_t lowest_aligned_lba;
  uint32_t logical_blocks_per_physical_block;
  uint32_t optimal_transfer_length_granularity;
} efi_block_io_media_k_t;

struct efi_block_io_protocol_k;

typedef EFI_STATUS_K(__attribute__((ms_abi)) *efi_bio_reset_fn)(
    struct efi_block_io_protocol_k *This, uint8_t ExtendedVerification);
typedef EFI_STATUS_K(__attribute__((ms_abi)) *efi_bio_read_fn)(
    struct efi_block_io_protocol_k *This, uint32_t MediaId, uint64_t Lba,
    uint64_t BufferSize, void *Buffer);
typedef EFI_STATUS_K(__attribute__((ms_abi)) *efi_bio_write_fn)(
    struct efi_block_io_protocol_k *This, uint32_t MediaId, uint64_t Lba,
    uint64_t BufferSize, void *Buffer);
typedef EFI_STATUS_K(__attribute__((ms_abi)) *efi_bio_flush_fn)(
    struct efi_block_io_protocol_k *This);

typedef struct efi_block_io_protocol_k {
  uint64_t revision;
  efi_block_io_media_k_t *media;
  efi_bio_reset_fn reset;
  efi_bio_read_fn read_blocks;
  efi_bio_write_fn write_blocks;
  efi_bio_flush_fn flush_blocks;
} efi_block_io_protocol_k_t;

struct efi_block_ctx {
  efi_block_io_protocol_k_t *bio;
  uint32_t media_id;
  uint32_t handoff_media_id;
  uint32_t block_size;
  uint32_t io_align;
  void *bounce_raw;
  void *bounce_aligned;
  uint8_t bounce_inline[8192];
  EFI_STATUS_K last_status;
  uint32_t last_block_no;
  uint32_t last_media_id;
  EFI_STATUS_K last_error_status;
  uint32_t last_error_block_no;
  uint32_t last_error_media_id;
  uint32_t error_count;
};

struct efi_block_device {
  struct block_device dev;
  struct efi_block_ctx ctx;
};

int efi_block_device_init(struct efi_block_device *out, uint64_t bio_ptr,
                          uint32_t media_id, uint32_t block_size,
                          uint64_t last_lba);
int efi_block_device_flush(struct efi_block_device *dev);

#endif /* DRIVERS_STORAGE_EFI_BLOCK_H */
