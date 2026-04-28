#include "internal/input_runtime_internal.h"

static int emit_arrow_escape(struct x64_input_runtime *runtime, uint8_t sc,
                             char *out_char) {
  char dir = 0;

  switch (sc) {
  case 0x48:
    dir = 'A';
    break; /* Up */
  case 0x50:
    dir = 'B';
    break; /* Down */
  case 0x4D:
    dir = 'C';
    break; /* Right */
  case 0x4B:
    dir = 'D';
    break; /* Left */
  default:
    return 0;
  }

  runtime->escape_seq[0] = '[';
  runtime->escape_seq[1] = dir;
  runtime->escape_seq_len = 2;
  runtime->escape_seq_pos = 0;
  *out_char = 0x1B; /* ESC — first byte emitted immediately */
  return 1;
}

int x64_input_poll_char(struct x64_input_runtime *runtime, char *out_char) {
  char c = 0;
  uint8_t sc = 0;
  int key_break = 0;

  if (!runtime || !out_char) {
    return 0;
  }

  /* Drain any pending arrow-key escape sequence first. */
  if (runtime->escape_seq_pos < runtime->escape_seq_len) {
    *out_char = runtime->escape_seq[runtime->escape_seq_pos++];
    if (runtime->escape_seq_pos >= runtime->escape_seq_len) {
      runtime->escape_seq_len = 0;
      runtime->escape_seq_pos = 0;
    }
    return 1;
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
        int was_extended = 0;
        if (!decode_prefixed_scancode(&runtime->ps2_extended_prefix, sc, &sc,
                                      &key_break)) {
          break;
        }
        was_extended = runtime->ps2_extended_prefix;
        runtime->ps2_extended_prefix = 0;
        if (sc == 0x2A || sc == 0x36) {
          runtime->shift_active = key_break ? 0 : 1;
          break;
        }
        if (key_break) {
          break;
        }
        /* Arrow keys on extended scancodes → VT100 escape sequence. */
        if (was_extended && emit_arrow_escape(runtime, sc, out_char)) {
          x64_input_note_backend(runtime, backend);
          return 1;
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
          int hv_extended = 0;
          int hv_ret =
              vmbus_keyboard_poll(hvkbd, &hv_sc, &hv_break, &hv_extended);
          if (hv_ret < 0) {
            defer_hyperv_backend(runtime);
            return 0;
          }
          if (hv_ret > 0) {
            if (hv_extended) {
              hv_sc = normalize_extended_scancode(hv_sc);
            }
            if (hv_sc == 0x2A || hv_sc == 0x36) {
              runtime->shift_active = hv_break ? 0 : 1;
              break;
            }
            if (hv_break) {
              break;
            }
            /* Arrow keys on extended scancodes → VT100 escape sequence. */
            if (hv_extended && emit_arrow_escape(runtime, hv_sc, out_char)) {
              x64_input_note_backend(runtime, backend);
              return 1;
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

