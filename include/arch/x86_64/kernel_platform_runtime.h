#ifndef ARCH_X86_64_KERNEL_PLATFORM_RUNTIME_H
#define ARCH_X86_64_KERNEL_PLATFORM_RUNTIME_H

#include <stdint.h>

#include "arch/x86_64/input_runtime.h"
#include "boot/handoff.h"

struct x64_platform_diag_io {
  void (*print)(const char *message);
  void (*print_hex64)(uint64_t value);
  void (*print_dec_u32)(uint32_t value);
  void (*putc)(char ch);
};

uint32_t x64_kernel_handoff_runtime_flags(const struct boot_handoff *handoff);
int x64_kernel_handoff_has_firmware_input(const struct boot_handoff *handoff);
int x64_kernel_handoff_has_firmware_block_io(const struct boot_handoff *handoff);
int x64_kernel_handoff_boot_services_active(const struct boot_handoff *handoff);
int x64_kernel_handoff_has_exit_boot_services_contract(
    const struct boot_handoff *handoff);
const char *x64_kernel_input_ps2_fallback_state(
    const struct x64_input_runtime *runtime);
void x64_kernel_print_platform_runtime_mode(
    const struct boot_handoff *handoff, const struct x64_platform_diag_io *io);
void x64_kernel_print_platform_tables_status(
    const struct x64_platform_diag_io *io);
void x64_kernel_print_platform_timer_status(
    const struct x64_platform_diag_io *io);
void x64_kernel_print_input_runtime_status(
    const struct x64_input_runtime *runtime,
    const struct x64_platform_diag_io *io);
void x64_kernel_print_storage_runtime_status(
    const struct boot_handoff *handoff, const struct x64_platform_diag_io *io);
void x64_kernel_print_native_runtime_gate_status(
    const struct boot_handoff *handoff,
    const struct x64_input_runtime *runtime, int exit_attempted, int exit_done,
    uint64_t exit_status, const struct x64_platform_diag_io *io);
void x64_kernel_print_cmd_info(const struct boot_handoff *handoff, int rsdp_valid,
                               const struct x64_input_runtime *runtime,
                               const struct x64_platform_diag_io *io);
void x64_kernel_print_active_efi_runtime_trace(
    const struct x64_platform_diag_io *io);
void x64_kernel_update_system_runtime_platform_status(
    const struct boot_handoff *handoff,
    const struct x64_input_runtime *runtime, int exit_attempted,
    int exit_done, uint64_t exit_status);
void x64_kernel_print_timebase_status(const struct x64_platform_diag_io *io);

#endif /* ARCH_X86_64_KERNEL_PLATFORM_RUNTIME_H */
