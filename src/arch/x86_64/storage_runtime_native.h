#ifndef X64_STORAGE_RUNTIME_NATIVE_H
#define X64_STORAGE_RUNTIME_NATIVE_H

#include <stdint.h>

#include "arch/x86_64/storage_runtime.h"

struct x64_storage_native_candidate_state {
  enum x64_storage_backend backend;
  const char *data_path;
  int ready;
  struct block_device *device;
};

void x64_storage_runtime_native_reset(
    struct x64_storage_native_candidate_state *state);
void x64_storage_runtime_native_probe(
    struct x64_storage_native_candidate_state *state,
    const struct boot_handoff *handoff, const struct x64_storage_runtime_io *io,
    void *probe_buf, uint32_t probe_buf_size);
struct block_device *x64_storage_runtime_native_promote(
    struct x64_storage_native_candidate_state *state,
    enum x64_storage_backend *active_backend, const char **active_data_path,
    int *has_device, const struct x64_storage_runtime_io *io,
    const char *reason);

#endif /* X64_STORAGE_RUNTIME_NATIVE_H */
