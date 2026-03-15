#ifndef ARCH_X86_64_INPUT_RUNTIME_H
#define ARCH_X86_64_INPUT_RUNTIME_H

#include <stdint.h>

enum x64_input_backend {
  X64_INPUT_BACKEND_NONE = 0,
  X64_INPUT_BACKEND_EFI,
  X64_INPUT_BACKEND_PS2,
  X64_INPUT_BACKEND_HYPERV,
  X64_INPUT_BACKEND_COM1,
};

struct x64_input_config {
  int prefer_native;
  int has_efi;
  int has_ps2;
  int has_hyperv;
  int hyperv_deferred;
  int has_com1;
  uint64_t efi_system_table;
};

struct x64_input_runtime {
  enum x64_input_backend order[4];
  uint32_t order_count;
  enum x64_input_backend primary_backend;
  enum x64_input_backend last_backend;
  int prefer_native;
  int has_efi;
  int has_ps2;
  int has_hyperv;
  int hyperv_confirmed;
  int hyperv_preferred;
  int ps2_fallback_parked;
  int hyperv_deferred;
  int hyperv_promotion_attempted;
  uint32_t hyperv_promotion_attempts;
  uint32_t hyperv_event_count;
  uint32_t hyperv_degrade_count;
  uint64_t hyperv_retry_tick;
  int has_com1;
  uint64_t efi_system_table;
  int native_confirmed;
  int firmware_retired;
  int shift_active;
  char dead_accent;
  char pending_char;
};

struct x64_input_probe_result {
  int has_efi;
  int has_ps2;
  int has_hyperv;
  int has_hyperv_ready;
  int has_hyperv_deferred;
  int has_com1;
  int has_usb;
  uint64_t efi_system_table;
};

void x64_input_probe_backends(struct x64_input_probe_result *result,
                              int firmware_input_allowed,
                              int boot_services_active,
                              uint64_t efi_system_table,
                              int is_hyperv,
                              void (*print)(const char *));
void x64_input_runtime_init(struct x64_input_runtime *runtime,
                            const struct x64_input_config *config);
int x64_input_poll_char(struct x64_input_runtime *runtime, char *out_char);
int x64_input_has_any(const struct x64_input_runtime *runtime);
uint32_t x64_input_backend_count(const struct x64_input_runtime *runtime);
enum x64_input_backend
x64_input_backend_at(const struct x64_input_runtime *runtime, uint32_t index);
void x64_input_note_backend(struct x64_input_runtime *runtime,
                            enum x64_input_backend backend);
const char *x64_input_backend_name(enum x64_input_backend backend);
const char *x64_input_primary_backend_name(const struct x64_input_runtime *runtime);
const char *x64_input_last_backend_name(const struct x64_input_runtime *runtime);
const char *x64_input_priority_mode(const struct x64_input_runtime *runtime);
const char *x64_input_firmware_state(const struct x64_input_runtime *runtime);
const char *x64_input_hyperv_state(const struct x64_input_runtime *runtime);
int x64_input_has_firmware_backend(const struct x64_input_runtime *runtime);
int x64_input_has_native_backend(const struct x64_input_runtime *runtime);
void x64_input_retire_firmware_backend(struct x64_input_runtime *runtime);
int x64_input_try_enable_hyperv_native(struct x64_input_runtime *runtime,
                                       int boot_services_active,
                                       void (*print)(const char *));

#endif /* ARCH_X86_64_INPUT_RUNTIME_H */
