#include "internal/storage_runtime_gpt.h"

#include <stdint.h>

#include "boot/gpt_identity.h"

static void io_print(const struct x64_storage_runtime_io *io,
                     const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
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

static int native_gpt_read(
    void *ctx, uint32_t lba,
    uint8_t sector[CAPYOS_GPT_IDENTITY_SECTOR_SIZE]) {
  struct block_device *raw = ctx;
  return raw ? block_device_read(raw, lba, sector) : -1;
}

int x64_storage_runtime_find_data_partition_native(
    struct block_device *raw, void *probe_buf, uint32_t probe_buf_size,
    uint32_t *out_data_start, uint32_t *out_data_count,
    int allow_legacy_identity, int *out_identity_strict,
    struct capyos_gpt_identity *out_identity,
    const struct x64_storage_runtime_io *io) {
  struct capyos_gpt_identity strict_identity;

  if (!raw || !probe_buf || !out_data_start || !out_data_count ||
      !out_identity_strict || !out_identity || raw->block_size == 0 ||
      raw->block_size > probe_buf_size) {
    io_print(io, "[storage] GPT nativo invalido: parametros de probe.\n");
    return -1;
  }
  *out_data_start = 0;
  *out_data_count = 0;
  *out_identity_strict = 0;
  *out_identity = (struct capyos_gpt_identity){0};
  if (capyos_gpt_identity_read(native_gpt_read, raw, raw->block_size,
                               raw->block_count, &strict_identity) != 0) {
    if (!allow_legacy_identity ||
        capyos_gpt_identity_read_legacy(native_gpt_read, raw, raw->block_size,
                                        raw->block_count,
                                        &strict_identity) != 0) {
      io_print(io, "[storage] GPT nativo invalido: identidade estrita falhou.\n");
      return -1;
    }
  } else {
    *out_identity_strict = 1;
  }
  *out_data_start = strict_identity.data.lba;
  *out_data_count = strict_identity.data.sectors;
  *out_identity = strict_identity;
  return 0;
}
