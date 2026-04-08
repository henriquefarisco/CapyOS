#ifndef X64_STORAGE_RUNTIME_GPT_H
#define X64_STORAGE_RUNTIME_GPT_H

#include <stdint.h>

#include "arch/x86_64/storage_runtime.h"

int x64_storage_runtime_compute_effective_data_count(
    uint64_t data_start, uint64_t data_count, uint64_t disk_last_lba,
    uint64_t *out_effective_count);
int x64_storage_runtime_find_data_partition_native(
    struct block_device *raw, void *probe_buf, uint32_t probe_buf_size,
    uint32_t *out_data_start, uint32_t *out_data_count,
    const struct x64_storage_runtime_io *io);

#endif /* X64_STORAGE_RUNTIME_GPT_H */
