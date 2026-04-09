#ifndef ARCH_X86_64_HYPERV_INPUT_GATE_H
#define ARCH_X86_64_HYPERV_INPUT_GATE_H

#include <stdint.h>

#include "arch/x86_64/input_runtime.h"
#include "core/system_init.h"

uint8_t x64_hyperv_input_gate_state(const struct x64_input_runtime *runtime,
                                    int boot_services_active);

#endif /* ARCH_X86_64_HYPERV_INPUT_GATE_H */
