#ifndef ARCH_X86_64_NATIVE_RUNTIME_GATE_H
#define ARCH_X86_64_NATIVE_RUNTIME_GATE_H

#include <stdint.h>

#include "arch/x86_64/input_runtime.h"
#include "boot/handoff.h"
#include "core/system_init.h"

struct x64_native_runtime_gate_status {
  uint8_t gate;
  uint64_t last_status;
};

void x64_native_runtime_gate_eval(const struct boot_handoff *handoff,
                                  const struct x64_input_runtime *runtime,
                                  int exit_attempted, int exit_done,
                                  uint64_t exit_status,
                                  struct x64_native_runtime_gate_status *out);

int x64_native_runtime_gate_is_ready(
    const struct x64_native_runtime_gate_status *status);

#endif /* ARCH_X86_64_NATIVE_RUNTIME_GATE_H */
