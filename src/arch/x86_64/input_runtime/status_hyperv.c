#include "internal/input_runtime_internal.h"

int x64_input_has_any(const struct x64_input_runtime *runtime) {
  return runtime && runtime->order_count != 0u;
}

uint32_t x64_input_backend_count(const struct x64_input_runtime *runtime) {
  return runtime ? runtime->order_count : 0u;
}

enum x64_input_backend
x64_input_backend_at(const struct x64_input_runtime *runtime, uint32_t index) {
  if (!runtime || index >= runtime->order_count) {
    return X64_INPUT_BACKEND_NONE;
  }
  return runtime->order[index];
}

void x64_input_note_backend(struct x64_input_runtime *runtime,
                            enum x64_input_backend backend) {
  if (!runtime || backend == X64_INPUT_BACKEND_NONE) {
    return;
  }
  runtime->last_backend = backend;
  if ((backend == X64_INPUT_BACKEND_PS2 || backend == X64_INPUT_BACKEND_HYPERV) &&
      runtime->prefer_native) {
    runtime->native_confirmed = 1;
    if (runtime->has_efi) {
      retire_firmware_backend(runtime);
    }
  }
  if (backend == X64_INPUT_BACKEND_HYPERV) {
    runtime->hyperv_confirmed = 1;
    runtime->hyperv_preferred = 1;
    runtime->hyperv_event_count++;
    prepend_backend(runtime, X64_INPUT_BACKEND_HYPERV);
    refresh_primary_backend(runtime);
    if (runtime->has_ps2 &&
        runtime->hyperv_event_count >= HYPERV_STABLE_PARK_PS2_EVENTS) {
      park_ps2_fallback(runtime);
    }
  }
}

const char *x64_input_backend_name(enum x64_input_backend backend) {
  switch (backend) {
  case X64_INPUT_BACKEND_EFI:
    return "efi";
  case X64_INPUT_BACKEND_PS2:
    return "ps2";
  case X64_INPUT_BACKEND_HYPERV:
    return "hyperv";
  case X64_INPUT_BACKEND_COM1:
    return "com1";
  default:
    return "none";
  }
}

const char *x64_input_primary_backend_name(
    const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return "none";
  }
  return x64_input_backend_name(runtime->primary_backend);
}

const char *x64_input_last_backend_name(const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return "none";
  }
  return x64_input_backend_name(runtime->last_backend);
}

const char *x64_input_priority_mode(const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return "unknown";
  }
  if (runtime->prefer_native) {
    return runtime->has_efi ? "native-first" : "native-only";
  }
  return runtime->has_efi ? "firmware-first" : "native-only";
}

const char *x64_input_firmware_state(const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return "unknown";
  }
  if (runtime->has_efi) {
    return runtime->prefer_native && x64_input_has_native_backend(runtime)
               ? "fallback"
               : "primary";
  }
  return runtime->firmware_retired ? "retired" : "off";
}

const char *x64_input_hyperv_state(const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return "unknown";
  }
  if (runtime->has_hyperv) {
    return runtime->hyperv_confirmed ? "active" : "ready";
  }
  if (runtime->hyperv_deferred) {
    if (runtime->hyperv_promotion_attempts >= HYPERV_PROMOTION_MAX_ATTEMPTS) {
      return "deferred-failed";
    }
    return runtime->hyperv_promotion_attempted ? "deferred-retrying"
                                               : "deferred";
  }
  return "off";
}

int x64_input_has_firmware_backend(const struct x64_input_runtime *runtime) {
  return runtime && runtime->has_efi;
}

int x64_input_has_native_backend(const struct x64_input_runtime *runtime) {
  if (!runtime) {
    return 0;
  }
  return runtime->has_ps2 || runtime->has_hyperv;
}

void x64_input_retire_firmware_backend(struct x64_input_runtime *runtime) {
  retire_firmware_backend(runtime);
}

static int x64_input_enable_hyperv_native_internal(
    struct x64_input_runtime *runtime, int boot_services_active,
    void (*print)(const char *), int force) {
  uint64_t now = 0;

  if (!runtime || !runtime->hyperv_deferred || runtime->has_hyperv ||
      boot_services_active) {
    return 0;
  }
  if (!force && !g_hyperv_keyboard_auto_promotion_enabled) {
    if (!g_hyperv_keyboard_auto_promotion_logged) {
      runtime_print(
          print,
          "[hyperv] Promocao automatica do VMBus keyboard suspensa; seguindo com fallback atual para liberar storage/rede.\n");
      g_hyperv_keyboard_auto_promotion_logged = 1;
    }
    return 0;
  }
  if (runtime->hyperv_promotion_attempts >= HYPERV_PROMOTION_MAX_ATTEMPTS) {
    return -1;
  }
  now = pit_ticks();
  if (runtime->hyperv_promotion_attempted && now < runtime->hyperv_retry_tick) {
    return 0;
  }

  runtime->hyperv_promotion_attempted = 1;
  runtime->hyperv_promotion_attempts++;
  if (runtime->hyperv_promotion_attempts == 1u) {
    runtime_print(
        print,
        "[hyperv] Runtime nativo detectado; tentando promocao controlada do VMBus keyboard.\n");
  } else {
    runtime_print(print,
                  "[hyperv] Retentando promocao controlada do VMBus keyboard.\n");
  }

  if (hyperv_keyboard_init() != 0) {
    if (runtime->hyperv_promotion_attempts >= HYPERV_PROMOTION_MAX_ATTEMPTS) {
      runtime_print(
          print,
          "[hyperv] Promocao do VMBus keyboard falhou no limite de tentativas; mantendo backend atual.\n");
    } else {
      runtime->hyperv_retry_tick = now + HYPERV_PROMOTION_RETRY_DELAY_TICKS;
      runtime_print(
          print,
          "[hyperv] Promocao do VMBus keyboard falhou; reagendando tentativa em runtime nativo.\n");
    }
    return -1;
  }

  runtime->has_hyperv = 1;
  runtime->hyperv_transport_prepared = 1;
  runtime->hyperv_confirmed = 0;
  runtime->hyperv_deferred = 0;
  runtime->hyperv_retry_tick = 0u;
  runtime->prefer_native = 1;
  if (!runtime->has_ps2 || runtime->hyperv_preferred) {
    prepend_backend(runtime, X64_INPUT_BACKEND_HYPERV);
  } else {
    append_backend(runtime, X64_INPUT_BACKEND_HYPERV);
  }
  refresh_primary_backend(runtime);
  runtime_print(print, "[hyperv] Teclado VMBus promovido em runtime nativo.\n");
  return 1;
}

int x64_input_try_enable_hyperv_native(struct x64_input_runtime *runtime,
                                       int boot_services_active,
                                       void (*print)(const char *)) {
  return x64_input_enable_hyperv_native_internal(runtime, boot_services_active,
                                                 print, 0);
}

int x64_input_force_enable_hyperv_native(struct x64_input_runtime *runtime,
                                         int boot_services_active,
                                         void (*print)(const char *)) {
  return x64_input_enable_hyperv_native_internal(runtime, boot_services_active,
                                                 print, 1);
}

int x64_input_try_prepare_hyperv_runtime(struct x64_input_runtime *runtime,
                                         int boot_services_active,
                                         void (*print)(const char *)) {
  int rc = 0;

  if (!runtime || !runtime->hyperv_deferred || runtime->has_hyperv ||
      !boot_services_active) {
    return 0;
  }

  runtime->hyperv_prepare_attempts++;
  if (runtime->hyperv_transport_prepared || vmbus_runtime_hypercall_prepared()) {
    runtime->hyperv_transport_prepared = 1;
    runtime->hyperv_prepare_last_result = 0;
    return 0;
  }

  runtime_print(
      print,
      "[hyperv] Preparando base VMBus do input em modo manual/controlado.\n");
  rc = vmbus_runtime_prepare_hypercall();
  runtime->hyperv_prepare_last_result = rc;
  if (rc != 0) {
    runtime_print(
        print,
        "[hyperv] Falha ao preparar base VMBus do input; mantendo promocao adiada.\n");
    return -1;
  }

  runtime->hyperv_transport_prepared = 1;
  runtime_print(
      print,
      "[hyperv] Base VMBus do input preparada; promocao do teclado segue adiada ate passo explicito futuro.\n");
  return 1;
}
