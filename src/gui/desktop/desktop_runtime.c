#include "gui/desktop_runtime.h"
#include "gui/desktop.h"
#include "arch/x86_64/framebuffer_console.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/timer/pit.h"
#include "drivers/acpi/acpi.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#include "auth/session.h"
#include "arch/x86_64/kernel_shell_dispatch.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "net/stack.h"
#include <stddef.h>

static void desktop_net_yield(void) {
  struct mouse_state ms;
  mouse_get_state(&ms);
  compositor_render_cursor(ms.x, ms.y);
}

static struct desktop_session g_desktop;
static int g_desktop_active = 0;
static struct shell_context *g_desktop_shell_ctx = NULL;
static int g_reboot_requested = 0;
static int g_shutdown_requested = 0;

int desktop_is_active(void) { return g_desktop_active; }

int kernel_desktop_dispatch_shell_command(char *line) {
  struct session_context *previous_session = NULL;
  struct session_context *desktop_session = NULL;
  int handled = 0;
  if (!g_desktop_shell_ctx || !line) return 0;
  previous_session = session_active();
  desktop_session = shell_context_session(g_desktop_shell_ctx);
  if (desktop_session) session_set_active(desktop_session);
  handled = x64_kernel_try_shell_command(g_desktop_shell_ctx, 1, line);
  session_set_active(previous_session);
  return handled;
}

void desktop_stop(void) {
  if (g_desktop_active) {
    g_desktop_active = 0;
  }
}

static void sync_and_flush_desktop(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev)
    buffer_cache_sync(root->bdev);
}

void kernel_request_reboot(void) {
  g_reboot_requested = 1;
}

void kernel_request_shutdown(void) {
  g_shutdown_requested = 1;
}

static inline void desktop_frame_delay(void) {
  uint64_t start = pit_ticks();
  uint32_t spins = 0;
  while (pit_ticks() == start && spins++ < 200000u) {
    if (mouse_pending()) break;
    __asm__ volatile("pause");
  }
}

int desktop_runtime_start(struct shell_context *ctx) {
  struct session_context *previous_session = session_active();
  if (g_desktop_active) { fbcon_print("Desktop already running.\n"); return 0; }
  uint32_t *fb = kernel_desktop_get_fb();
  uint32_t w = kernel_desktop_get_width();
  uint32_t h = kernel_desktop_get_height();
  uint32_t pitch = kernel_desktop_get_pitch();
  if (!fb || w == 0 || h == 0) { fbcon_print("Error: no framebuffer.\n"); return -1; }

  mouse_ps2_init();
  g_desktop_shell_ctx = ctx;
  if (ctx && shell_context_session(ctx)) {
    session_set_active(shell_context_session(ctx));
  }
  desktop_init(&g_desktop, fb, w, h, pitch, ctx ? ctx->settings : NULL);
  desktop_open_terminal(&g_desktop);
  net_stack_set_yield_hook(desktop_net_yield);
  sync_and_flush_desktop();
  fbcon_print("[desktop] session started\n");
  sync_and_flush_desktop();
  g_desktop_active = 1;

  /* Small state machine to distinguish a bare ESC press (exit desktop)
   * from a VT100 arrow-key escape sequence (ESC [ A/B/C/D).
   * escape_state: 0 = idle, 1 = saw ESC, 2 = saw ESC+[ */
  int escape_state = 0;

  while (g_desktop_active) {
    char ch = 0;
    int had_activity = 0;

    x64_kernel_runtime_poll_background();

    while (kernel_input_trygetc(&ch)) {
      had_activity = 1;
      if (escape_state == 2) {
        escape_state = 0;
        switch (ch) {
          case 'A': desktop_handle_input(&g_desktop, KEY_UP, 0); break;
          case 'B': desktop_handle_input(&g_desktop, KEY_DOWN, 0); break;
          case 'C': desktop_handle_input(&g_desktop, KEY_RIGHT, 0); break;
          case 'D': desktop_handle_input(&g_desktop, KEY_LEFT, 0); break;
          default: break;
        }
        continue;
      }
      if (escape_state == 1) {
        escape_state = 0;
        if (ch == '[') { escape_state = 2; continue; }
        /* Non-sequence char after ESC: dispatch as normal input */
        desktop_handle_input(&g_desktop, (uint32_t)(uint8_t)ch, ch);
        continue;
      }
      if (ch == 0x1B) { escape_state = 1; continue; }
      desktop_handle_input(&g_desktop, (uint32_t)(uint8_t)ch, ch);
    }
    if (escape_state == 1) {
      /* Give arrow-key escape sequences a brief window to arrive.
       * PS/2 scan codes and serial VT100 sequences may split the
       * ESC [ <letter> triplet across poll cycles. */
      int esc_resolved = 0;
      for (int retry = 0; retry < 64 && !esc_resolved; retry++) {
        if (kernel_input_trygetc(&ch)) {
          if (ch == '[') { escape_state = 2; esc_resolved = 1; }
          else { escape_state = 0; esc_resolved = 1; }
        }
        if (!esc_resolved) __asm__ volatile("pause");
      }
      if (!esc_resolved) {
        /* Bare ESC with no follow-up: ignore it (user can close
         * the desktop via the terminal 'exit' command). */
        escape_state = 0;
      }
    }
    if (!g_desktop_active) break;
    if (desktop_run_frame(&g_desktop)) had_activity = 1;
    if (!had_activity && !mouse_pending()) desktop_frame_delay();
  }

  desktop_shutdown(&g_desktop);
  net_stack_set_yield_hook((void *)0);
  fbcon_print("[desktop] session stopped\n");
  g_desktop_active = 0;
  g_desktop_shell_ctx = NULL;
  session_set_active(previous_session);

  fbcon_clear_view();

  if (g_reboot_requested) {
    g_reboot_requested = 0;
    sync_and_flush_desktop();
    fbcon_print("Rebooting...\n");
    acpi_reboot();
  }
  if (g_shutdown_requested) {
    g_shutdown_requested = 0;
    sync_and_flush_desktop();
    fbcon_print("Shutting down...\n");
    acpi_shutdown();
  }

  return 0;
}
