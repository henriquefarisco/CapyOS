#include "arch/x86_64/input_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/efi/efi_console.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/timer/pit.h"
#include "drivers/usb/xhci.h"

enum {
  HYPERV_PROMOTION_RETRY_DELAY_TICKS = 100u,
  HYPERV_PROMOTION_MAX_ATTEMPTS = 3u,
  HYPERV_STABLE_PARK_PS2_EVENTS = 8u,
};

static int g_has_com1 = -1;
static int g_has_ps2_active = -1;
static int g_has_ps2_passive = -1;

static inline void outb_local(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb_local(uint16_t port) {
  uint8_t value = 0;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline void cpu_relax_local(void) {
  __asm__ volatile("pause" ::: "memory");
}

static void runtime_print(void (*print)(const char *), const char *message) {
  if (print && message) {
    print(message);
  }
}

static int com1_detect(void) {
  if (g_has_com1 >= 0) {
    return g_has_com1;
  }

  outb_local(0x3F8 + 4, 0x1Eu);
  outb_local(0x3F8, 0xAEu);

  for (int i = 0; i < 10000; ++i) {
    if (inb_local(0x3F8 + 5) & 0x01u) {
      uint8_t received = inb_local(0x3F8);
      outb_local(0x3F8 + 4, 0x0Bu);
      if (received == 0xAEu) {
        g_has_com1 = 1;
        return 1;
      }
      break;
    }
    cpu_relax_local();
  }

  outb_local(0x3F8 + 4, 0x0Bu);
  g_has_com1 = 0;
  return 0;
}

static int ps2_poll_scancode(uint8_t *out_scancode) {
  if (!out_scancode) {
    return 0;
  }
  if (inb_local(0x64) & 0x01u) {
    *out_scancode = inb_local(0x60);
    return 1;
  }
  return 0;
}

static int com1_poll_char(char *out_char) {
  if (!out_char) {
    return 0;
  }
  if (inb_local(0x3F8 + 5) & 0x01u) {
    *out_char = (char)inb_local(0x3F8);
    return 1;
  }
  return 0;
}

static int layout_name_equal(const char *a, const char *b) {
  if (!a || !b) {
    return 0;
  }
  while (*a && *b) {
    if (*a++ != *b++) {
      return 0;
    }
  }
  return *a == *b;
}

static const struct keyboard_layout *active_layout_map(void) {
  const char *name = keyboard_current_layout();
  if (name && layout_name_equal(name, "br-abnt2")) {
    return &g_keyboard_layout_br_abnt2;
  }
  return &g_keyboard_layout_us;
}

static int accent_is_dead(char ch) {
  return ch == '\'' || ch == '`' || ch == '^' || ch == '~' || ch == '"';
}

static char compose_dead_key(char accent, char base) {
  if (accent == '\'') {
    if (base == 'a')
      return (char)0xA0;
    if (base == 'e')
      return (char)0x82;
    if (base == 'i')
      return (char)0xA1;
    if (base == 'o')
      return (char)0xA2;
    if (base == 'u')
      return (char)0xA3;
    if (base == 'A')
      return (char)0xB5;
    if (base == 'E')
      return (char)0x90;
    if (base == 'I')
      return (char)0xD6;
    if (base == 'O')
      return (char)0xE0;
    if (base == 'U')
      return (char)0xE9;
  } else if (accent == '^') {
    if (base == 'a')
      return (char)0x83;
    if (base == 'e')
      return (char)0x88;
    if (base == 'i')
      return (char)0x8C;
    if (base == 'o')
      return (char)0x93;
    if (base == 'u')
      return (char)0x96;
  } else if (accent == '~') {
    if (base == 'a')
      return (char)0xC6;
    if (base == 'o')
      return (char)0xE5;
    if (base == 'A')
      return (char)0xC7;
    if (base == 'O')
      return (char)0xE4;
  } else if (accent == '`') {
    if (base == 'a')
      return (char)0x85;
    if (base == 'A')
      return (char)0xB7;
  } else if (accent == '"') {
    if (base == 'u')
      return (char)0x81;
    if (base == 'U')
      return (char)0x9A;
  }
  return 0;
}

static int find_us_scancode_for_char(char ch, uint8_t *out_sc, int *out_shift) {
  if (!out_sc || !out_shift) {
    return 0;
  }
  for (uint8_t sc = 0; sc < 128; ++sc) {
    if (g_keyboard_layout_us.base[sc] == ch && ch != 0) {
      *out_sc = sc;
      *out_shift = 0;
      return 1;
    }
    if (g_keyboard_layout_us.shift[sc] == ch && ch != 0) {
      *out_sc = sc;
      *out_shift = 1;
      return 1;
    }
  }
  return 0;
}

static char scancode_to_ascii(struct x64_input_runtime *runtime, uint8_t sc,
                              int shift) {
  const struct keyboard_layout *layout = active_layout_map();
  if (!runtime || sc >= 128 || !layout) {
    return 0;
  }

  const char *mapping = shift ? layout->shift : layout->base;
  char ch = mapping[sc];
  if (!ch) {
    return 0;
  }

  uint8_t dead_flags = layout->dead[sc];
  int is_dead = shift ? ((dead_flags & 0x2) != 0) : ((dead_flags & 0x1) != 0);
  if (is_dead && accent_is_dead(ch)) {
    runtime->dead_accent = ch;
    return 0;
  }

  if (runtime->dead_accent) {
    char accent = runtime->dead_accent;
    runtime->dead_accent = 0;
    char composed = compose_dead_key(accent, ch);
    if (composed) {
      return composed;
    }
    runtime->pending_char = ch;
    return accent;
  }

  return ch;
}

static int translate_efi_char_to_active_layout(struct x64_input_runtime *runtime,
                                               char in, char *out) {
  const struct keyboard_layout *layout = active_layout_map();
  uint8_t sc = 0;
  int shift = 0;
  char mapped = 0;
  uint8_t dead_flags = 0;
  int is_dead = 0;

  if (!runtime || !out) {
    return 0;
  }
  if (!layout || layout == &g_keyboard_layout_us) {
    *out = in;
    return 1;
  }

  if (!find_us_scancode_for_char(in, &sc, &shift)) {
    *out = in;
    return 1;
  }

  mapped = (shift ? layout->shift : layout->base)[sc];
  if (!mapped) {
    *out = in;
    return 1;
  }

  dead_flags = layout->dead[sc];
  is_dead = shift ? ((dead_flags & 0x2) != 0) : ((dead_flags & 0x1) != 0);
  if (is_dead && accent_is_dead(mapped)) {
    runtime->dead_accent = mapped;
    return 0;
  }

  if (runtime->dead_accent) {
    char accent = runtime->dead_accent;
    runtime->dead_accent = 0;
    char composed = compose_dead_key(accent, mapped);
    if (composed) {
      *out = composed;
      return 1;
    }
    runtime->pending_char = mapped;
    *out = accent;
    return 1;
  }

  *out = mapped;
  return 1;
}

static void ps2_flush(void) {
  uint8_t discarded = 0;
  for (uint32_t i = 0; i < 1024u; ++i) {
    if (!ps2_poll_scancode(&discarded)) {
      break;
    }
  }
}

static int ps2_wait_input_empty(uint32_t spins) {
  for (uint32_t i = 0; i < spins; ++i) {
    if ((inb_local(0x64) & 0x02u) == 0u) {
      return 1;
    }
    cpu_relax_local();
  }
  return 0;
}

static int ps2_wait_output_ready(uint32_t spins) {
  for (uint32_t i = 0; i < spins; ++i) {
    if (inb_local(0x64) & 0x01u) {
      return 1;
    }
    cpu_relax_local();
  }
  return 0;
}

static int ps2_read_config_byte(uint8_t *out_cfg) {
  if (!out_cfg) {
    return 0;
  }

  if (inb_local(0x64) == 0xFFu) {
    return 0;
  }

  if (!ps2_wait_input_empty(10000u)) {
    return 0;
  }

  outb_local(0x64, 0x20u);
  if (!ps2_wait_output_ready(10000u)) {
    return 0;
  }

  *out_cfg = inb_local(0x60);
  return 1;
}

static int ps2_controller_detect_passive(void) {
  uint8_t cfg = 0;

  if (g_has_ps2_passive >= 0) {
    return g_has_ps2_passive;
  }

  if (ps2_read_config_byte(&cfg) && cfg != 0xFFu) {
    g_has_ps2_passive = 1;
    return 1;
  }

  g_has_ps2_passive = 0;
  return 0;
}

static int ps2_controller_detect_active(void) {
  if (g_has_ps2_active >= 0) {
    return g_has_ps2_active;
  }

  if (ps2_controller_detect_passive()) {
    g_has_ps2_active = 1;
    return 1;
  }

  ps2_flush();
  if (!ps2_wait_input_empty(10000u)) {
    g_has_ps2_active = 0;
    return 0;
  }

  outb_local(0x64, 0xAAu);
  if (ps2_wait_output_ready(100000u) && inb_local(0x60) == 0x55u) {
    g_has_ps2_active = 1;
    return 1;
  }

  g_has_ps2_active = 0;
  return 0;
}

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

static void append_backend(struct x64_input_runtime *runtime,
                           enum x64_input_backend backend) {
  if (!runtime || runtime->order_count >= 4u ||
      backend == X64_INPUT_BACKEND_NONE) {
    return;
  }
  runtime->order[runtime->order_count++] = backend;
}

static void prepend_backend(struct x64_input_runtime *runtime,
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

static void refresh_primary_backend(struct x64_input_runtime *runtime) {
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

static void retire_firmware_backend(struct x64_input_runtime *runtime) {
  if (!runtime || !runtime->has_efi) {
    return;
  }

  runtime->has_efi = 0;
  runtime->firmware_retired = 1;
  remove_backend(runtime, X64_INPUT_BACKEND_EFI);
}

static void park_ps2_fallback(struct x64_input_runtime *runtime) {
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

static void defer_hyperv_backend(struct x64_input_runtime *runtime) {
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
  runtime->ps2_fallback_parked = 0;
  runtime->hyperv_deferred = config->hyperv_deferred ? 1 : 0;
  runtime->hyperv_promotion_attempted = config->has_hyperv ? 1 : 0;
  runtime->hyperv_promotion_attempts = 0u;
  runtime->hyperv_event_count = 0u;
  runtime->hyperv_degrade_count = 0u;
  runtime->hyperv_retry_tick = 0u;
  runtime->has_com1 = config->has_com1 ? 1 : 0;
  runtime->efi_system_table = config->efi_system_table;
  runtime->native_confirmed = 0;
  runtime->firmware_retired = 0;
  runtime->shift_active = 0;
  runtime->dead_accent = 0;
  runtime->pending_char = 0;

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

int x64_input_poll_char(struct x64_input_runtime *runtime, char *out_char) {
  char c = 0;
  uint8_t sc = 0;

  if (!runtime || !out_char) {
    return 0;
  }

  if (runtime->pending_char) {
    *out_char = runtime->pending_char;
    runtime->pending_char = 0;
    return 1;
  }

  for (uint32_t i = 0; i < runtime->order_count; ++i) {
    enum x64_input_backend backend = runtime->order[i];
    switch (backend) {
    case X64_INPUT_BACKEND_EFI:
      if (runtime->has_efi &&
          efi_poll_char(runtime->efi_system_table, &c)) {
        if (c == '\n' || c == '\b' || c == '\t') {
          *out_char = c;
          x64_input_note_backend(runtime, backend);
          return 1;
        }
        if (translate_efi_char_to_active_layout(runtime, c, out_char)) {
          x64_input_note_backend(runtime, backend);
          return 1;
        }
      }
      break;
    case X64_INPUT_BACKEND_PS2:
      if (runtime->has_ps2 && ps2_poll_scancode(&sc)) {
        if (sc == 0x2A || sc == 0x36) {
          runtime->shift_active = 1;
          break;
        }
        if (sc == 0xAA || sc == 0xB6) {
          runtime->shift_active = 0;
          break;
        }
        if (sc & 0x80u) {
          break;
        }
        if (sc == 0x1C) {
          *out_char = '\n';
          x64_input_note_backend(runtime, backend);
          return 1;
        }
        if (sc == 0x0E) {
          *out_char = '\b';
          x64_input_note_backend(runtime, backend);
          return 1;
        }
        c = scancode_to_ascii(runtime, sc, runtime->shift_active);
        if (c) {
          *out_char = c;
          x64_input_note_backend(runtime, backend);
          return 1;
        }
      }
      break;
    case X64_INPUT_BACKEND_HYPERV:
      if (runtime->has_hyperv) {
        struct vmbus_keyboard *hvkbd = vmbus_get_keyboard();
        if (!hvkbd) {
          defer_hyperv_backend(runtime);
          return 0;
        }
        {
          uint8_t hv_sc = 0;
          int hv_break = 0;
          int hv_ret = vmbus_keyboard_poll(hvkbd, &hv_sc, &hv_break);
          if (hv_ret < 0) {
            defer_hyperv_backend(runtime);
            return 0;
          }
          if (hv_ret > 0) {
            if (hv_sc == 0x2A || hv_sc == 0x36) {
              runtime->shift_active = hv_break ? 0 : 1;
              break;
            }
            if (hv_sc == 0xAA || hv_sc == 0xB6) {
              runtime->shift_active = 0;
              break;
            }
            if (hv_break) {
              break;
            }
            if (hv_sc == 0x1C) {
              *out_char = '\n';
              x64_input_note_backend(runtime, backend);
              return 1;
            }
            if (hv_sc == 0x0E) {
              *out_char = '\b';
              x64_input_note_backend(runtime, backend);
              return 1;
            }
            c = scancode_to_ascii(runtime, hv_sc, runtime->shift_active);
            if (c) {
              *out_char = c;
              x64_input_note_backend(runtime, backend);
              return 1;
            }
          }
        }
      }
      break;
    case X64_INPUT_BACKEND_COM1:
      if (runtime->has_com1 && com1_poll_char(&c)) {
        if (c == '\r') {
          c = '\n';
        }
        *out_char = c;
        x64_input_note_backend(runtime, backend);
        return 1;
      }
      break;
    default:
      break;
    }
  }

  return 0;
}

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

int x64_input_try_enable_hyperv_native(struct x64_input_runtime *runtime,
                                       int boot_services_active,
                                       void (*print)(const char *)) {
  uint64_t now = 0;

  if (!runtime || !runtime->hyperv_deferred || runtime->has_hyperv ||
      boot_services_active) {
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
