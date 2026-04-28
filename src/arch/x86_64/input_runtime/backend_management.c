#include "internal/input_runtime_internal.h"

void x64_input_probe_backends(struct x64_input_probe_result *result,
                              int firmware_input_allowed,
                              int boot_services_active,
                              uint64_t efi_system_table,
                              int is_hyperv,
                              void (*print)(const char *)) {
  struct xhci_controller xhci = {0};

  if (!result) {
    return;
  }

  result->has_efi = 0;
  result->has_ps2 = 0;
  result->has_hyperv = 0;
  result->has_hyperv_ready = 0;
  result->has_hyperv_deferred = 0;
  result->has_com1 = 0;
  result->has_usb = 0;
  result->efi_system_table = 0;

  if (firmware_input_allowed) {
    EFI_SYSTEM_TABLE_K *st = (EFI_SYSTEM_TABLE_K *)(uintptr_t)efi_system_table;
    result->efi_system_table = efi_system_table;
    if (st && st->ConIn && st->ConIn->ReadKeyStroke) {
      result->has_efi = 1;
    }
    runtime_print(print, result->has_efi ? "[info] EFI ConIn: disponivel.\n"
                                         : "[info] EFI ConIn: indisponivel.\n");
  } else {
    runtime_print(print, "[info] EFI ConIn: indisponivel.\n");
  }

  if (result->has_efi) {
    result->has_ps2 = ps2_controller_detect_passive();
    runtime_print(print, result->has_ps2
                             ? "[info] PS/2 detectado (probe passivo; backend nativo elegivel).\n"
                             : "[info] PS/2 nao detectado no probe passivo.\n");
  } else {
    result->has_ps2 = ps2_controller_detect_active();
    runtime_print(print, result->has_ps2 ? "[info] PS/2 detectado.\n"
                                         : "[info] PS/2 nao detectado.\n");
  }

  result->has_com1 = com1_detect();
  runtime_print(print, result->has_com1
                           ? (result->has_efi
                                  ? "[info] COM1 detectado (canal auxiliar de automacao).\n"
                                  : "[info] COM1 detectado.\n")
                           : "[info] COM1 nao detectado.\n");

  if (is_hyperv) {
    runtime_print(print, "[hyperv] Hyper-V detectado.\n");
    if (boot_services_active) {
      result->has_hyperv_deferred = 1;
      runtime_print(
          print,
          "[hyperv] VMBus keyboard adiado em runtime hibrido; mantendo EFI/PS/2 ate o caminho nativo ficar estavel.\n");
    } else {
      runtime_print(print, "[hyperv] Tentando teclado VMBus...\n");
      if (hyperv_keyboard_init() == 0) {
        result->has_hyperv = 1;
        result->has_hyperv_ready = 1;
        if (result->has_efi || result->has_ps2) {
          runtime_print(
              print,
              "[hyperv] Teclado VMBus ativo; backend nativo pronto com fallback de firmware.\n");
        } else {
          runtime_print(print, "[hyperv] Teclado VMBus ativo.\n");
        }
      } else {
        if (!result->has_efi && !result->has_ps2) {
          runtime_print(print,
                        "[hyperv] Falha no VMBus keyboard; usando fallback.\n");
        } else {
          runtime_print(
              print,
              "[hyperv] Falha no VMBus keyboard; mantendo EFI/PS/2 como backend primario.\n");
        }
      }
    }
  }

  if (!result->has_efi && !result->has_ps2 && !result->has_hyperv_ready &&
      !result->has_com1) {
    result->has_com1 = 1;
    runtime_print(print, "[info] COM1 habilitado em modo de emergencia.\n");
  }

  if (!result->has_ps2 && !result->has_efi && !result->has_hyperv_ready &&
      !is_hyperv) {
    runtime_print(print, "[usb] Buscando controlador XHCI...\n");
    if (xhci_find(&xhci) == 0) {
      runtime_print(print, "[usb] XHCI encontrado.\n");
      if (xhci_init(&xhci) == 0) {
        runtime_print(print, "[usb] XHCI inicializado.\n");
        if (xhci_start(&xhci) == 0) {
          result->has_usb = 1;
          runtime_print(print, "[usb] XHCI rodando.\n");
          runtime_print(print, "[usb] Enumeracao HID pendente para teclado.\n");
        } else {
          runtime_print(print, "[usb] Falha ao iniciar XHCI.\n");
        }
      } else {
        runtime_print(print, "[usb] Falha ao inicializar XHCI.\n");
      }
    } else {
      runtime_print(print, "[usb] XHCI nao encontrado via PCIe.\n");
    }
  }
}

void append_backend(struct x64_input_runtime *runtime,
                           enum x64_input_backend backend) {
  if (!runtime || runtime->order_count >= 4u ||
      backend == X64_INPUT_BACKEND_NONE) {
    return;
  }
  runtime->order[runtime->order_count++] = backend;
}

void prepend_backend(struct x64_input_runtime *runtime,
                            enum x64_input_backend backend) {
  if (!runtime || runtime->order_count >= 4u ||
      backend == X64_INPUT_BACKEND_NONE) {
    return;
  }

  for (uint32_t i = 0; i < runtime->order_count; ++i) {
    if (runtime->order[i] == backend) {
      return;
    }
  }

  for (uint32_t i = runtime->order_count; i > 0u; --i) {
    runtime->order[i] = runtime->order[i - 1u];
  }
  runtime->order[0] = backend;
  runtime->order_count++;
}

void refresh_primary_backend(struct x64_input_runtime *runtime) {
  if (!runtime) {
    return;
  }
  runtime->primary_backend =
      (runtime->order_count != 0u) ? runtime->order[0] : X64_INPUT_BACKEND_NONE;
}

static void remove_backend(struct x64_input_runtime *runtime,
                           enum x64_input_backend backend) {
  uint32_t write_index = 0;

  if (!runtime || backend == X64_INPUT_BACKEND_NONE) {
    return;
  }

  for (uint32_t i = 0; i < runtime->order_count; ++i) {
    if (runtime->order[i] != backend) {
      runtime->order[write_index++] = runtime->order[i];
    }
  }
  for (uint32_t i = write_index; i < runtime->order_count; ++i) {
    runtime->order[i] = X64_INPUT_BACKEND_NONE;
  }
  runtime->order_count = write_index;
  refresh_primary_backend(runtime);
}

void retire_firmware_backend(struct x64_input_runtime *runtime) {
  if (!runtime || !runtime->has_efi) {
    return;
  }

  runtime->has_efi = 0;
  runtime->firmware_retired = 1;
  remove_backend(runtime, X64_INPUT_BACKEND_EFI);
}

void park_ps2_fallback(struct x64_input_runtime *runtime) {
  if (!runtime || !runtime->has_ps2 || runtime->ps2_fallback_parked) {
    return;
  }
  remove_backend(runtime, X64_INPUT_BACKEND_PS2);
  runtime->ps2_fallback_parked = 1;
}

static void restore_ps2_fallback(struct x64_input_runtime *runtime) {
  if (!runtime || !runtime->has_ps2 || !runtime->ps2_fallback_parked) {
    return;
  }
  prepend_backend(runtime, X64_INPUT_BACKEND_PS2);
  refresh_primary_backend(runtime);
  runtime->ps2_fallback_parked = 0;
}

void defer_hyperv_backend(struct x64_input_runtime *runtime) {
  if (!runtime || !runtime->has_hyperv) {
    return;
  }

  runtime->has_hyperv = 0;
  runtime->hyperv_confirmed = 0;
  runtime->hyperv_deferred = 1;
  runtime->hyperv_promotion_attempted = 1;
  runtime->hyperv_promotion_attempts = 0u;
  runtime->hyperv_degrade_count++;
  runtime->hyperv_retry_tick = pit_ticks() + HYPERV_PROMOTION_RETRY_DELAY_TICKS;
  if (!runtime->has_ps2) {
    runtime->native_confirmed = 0;
  }
  remove_backend(runtime, X64_INPUT_BACKEND_HYPERV);
  restore_ps2_fallback(runtime);
}

void x64_input_runtime_init(struct x64_input_runtime *runtime,
                            const struct x64_input_config *config) {
  if (!runtime || !config) {
    return;
  }

  runtime->order_count = 0;
  runtime->primary_backend = X64_INPUT_BACKEND_NONE;
  runtime->last_backend = X64_INPUT_BACKEND_NONE;
  runtime->prefer_native = (config->prefer_native || config->has_ps2 ||
                            config->has_hyperv)
                               ? 1
                               : 0;
  runtime->has_efi = config->has_efi ? 1 : 0;
  runtime->has_ps2 = config->has_ps2 ? 1 : 0;
  runtime->has_hyperv = config->has_hyperv ? 1 : 0;
  runtime->hyperv_confirmed = 0;
  runtime->hyperv_preferred = 0;
  runtime->hyperv_transport_prepared = 0;
  runtime->ps2_fallback_parked = 0;
  runtime->hyperv_deferred = config->hyperv_deferred ? 1 : 0;
  runtime->hyperv_promotion_attempted = config->has_hyperv ? 1 : 0;
  runtime->hyperv_promotion_attempts = 0u;
  runtime->hyperv_prepare_attempts = 0u;
  runtime->hyperv_event_count = 0u;
  runtime->hyperv_degrade_count = 0u;
  runtime->hyperv_prepare_last_result = 0;
  runtime->hyperv_retry_tick = 0u;
  runtime->has_com1 = config->has_com1 ? 1 : 0;
  runtime->efi_system_table = config->efi_system_table;
  runtime->native_confirmed = 0;
  runtime->firmware_retired = 0;
  runtime->shift_active = 0;
  runtime->ps2_extended_prefix = 0;
  runtime->hyperv_extended_prefix = 0;
  runtime->dead_accent = 0;
  runtime->pending_char = 0;
  runtime->escape_seq[0] = 0;
  runtime->escape_seq[1] = 0;
  runtime->escape_seq[2] = 0;
  runtime->escape_seq[3] = 0;
  runtime->escape_seq_len = 0;
  runtime->escape_seq_pos = 0;

  if (runtime->prefer_native) {
    if (config->has_ps2) {
      append_backend(runtime, X64_INPUT_BACKEND_PS2);
    }
    if (config->has_hyperv) {
      append_backend(runtime, X64_INPUT_BACKEND_HYPERV);
    }
    if (config->has_efi) {
      append_backend(runtime, X64_INPUT_BACKEND_EFI);
    }
  } else {
    if (config->has_efi) {
      append_backend(runtime, X64_INPUT_BACKEND_EFI);
    }
    if (config->has_ps2) {
      append_backend(runtime, X64_INPUT_BACKEND_PS2);
    }
    if (config->has_hyperv) {
      append_backend(runtime, X64_INPUT_BACKEND_HYPERV);
    }
  }

  if (config->has_com1) {
    append_backend(runtime, X64_INPUT_BACKEND_COM1);
  }

  refresh_primary_backend(runtime);
}

/* Translate extended scancodes for arrow keys into VT100 escape sequences.
 * Returns 1 if the scancode was an arrow key (caller should emit the first
 * byte and return), 0 if it wasn't. */
