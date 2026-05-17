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
#include "kernel/scheduler.h"
#include "kernel/task.h"
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
static struct task *g_desktop_task = (struct task *)0;
static int g_desktop_gui_session_smoke_announced = 0;
static int g_desktop_mouse_events_smoke_announced = 0;
/* Blocked-state diagnostic emitted at most once per gate, only after the
 * boot has stabilized (DESKTOP_SMOKE_DIAG_DELAY_ITERATIONS main-loop
 * iterations). Gives the external smoke operator a serial breadcrumb
 * pointing at the first persistent blocker without spamming the log. */
#define DESKTOP_SMOKE_DIAG_DELAY_ITERATIONS 1000u
static int g_desktop_gui_session_smoke_diag_emitted = 0;
static int g_desktop_mouse_events_smoke_diag_emitted = 0;
static uint32_t g_desktop_smoke_diag_iter_count = 0;

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

struct session_context *kernel_desktop_shell_session(void) {
  if (!g_desktop_shell_ctx) return NULL;
  return shell_context_session(g_desktop_shell_ctx);
}

int kernel_desktop_shell_should_logout(void) {
  if (!g_desktop_shell_ctx) return 0;
  return shell_context_should_logout(g_desktop_shell_ctx);
}

int kernel_desktop_shell_should_stop(void) {
  if (!g_desktop_shell_ctx) return 0;
  return !shell_context_running(g_desktop_shell_ctx) ||
         shell_context_should_logout(g_desktop_shell_ctx);
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

static inline int desktop_scheduler_tick_observed(void) {
  struct scheduler_stats stats;
  scheduler_stats_get(&stats);
  return stats.total_ticks != 0;
}

static inline void desktop_idle_wait_cpu(void) {
#if defined(__x86_64__) && !defined(UNIT_TEST)
  if (desktop_scheduler_tick_observed()) {
    __asm__ volatile("sti; hlt" ::: "memory");
    return;
  }
#endif
  uint64_t start = pit_ticks();
  uint32_t spins = 0;
  while (pit_ticks() == start && spins++ < 1024u) {
    if (mouse_pending()) break;
    __asm__ volatile("pause");
  }
}

static inline void desktop_frame_delay(void) {
  if (scheduler_can_sleep_current()) {
    task_sleep(1);
    return;
  }
  if (!mouse_pending()) desktop_idle_wait_cpu();
}

static void desktop_scheduler_entry(void *arg) {
  (void)arg;
  for (;;) task_yield();
}

static int desktop_scheduler_adopt_current(void) {
  if (task_current()) {
    scheduler_set_running(1);
    return 0;
  }
  if (!g_desktop_task) {
    g_desktop_task = task_create_kernel("desktop-main",
                                        desktop_scheduler_entry,
                                        (void *)0);
    if (!g_desktop_task) return -1;
    scheduler_add(g_desktop_task);
  }
  g_desktop_task->state = TASK_STATE_RUNNING;
  task_set_current(g_desktop_task);
  scheduler_set_running(1);
  return 0;
}

static void desktop_gui_session_smoke_emit_once(void) {
  struct desktop_session_smoke_readiness readiness;
  struct desktop_gui_session_smoke_gate gate;
  if (g_desktop_gui_session_smoke_announced) return;
  if (desktop_session_smoke_readiness_snapshot(&g_desktop, &readiness) != 1) {
    return;
  }
  if (desktop_gui_session_smoke_gate_from_readiness(&readiness, &gate) != 1) {
    return;
  }
  if (!gate.smoke_ready) return;
  fbcon_print(desktop_gui_session_smoke_marker());
  fbcon_print("\n");
  g_desktop_gui_session_smoke_announced = 1;
}

static void desktop_mouse_events_smoke_emit_once(void) {
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  if (g_desktop_mouse_events_smoke_announced) return;
  if (desktop_session_smoke_readiness_snapshot(&g_desktop, &readiness) != 1) {
    return;
  }
  if (desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) != 1) {
    return;
  }
  if (!gate.smoke_ready) return;
  fbcon_print(desktop_mouse_events_smoke_marker());
  fbcon_print("\n");
  g_desktop_mouse_events_smoke_announced = 1;
}

/*
 * Blocked-state diagnostics for external smoke operators.
 *
 * Each gate has a matching "blocked diagnostic" emitter that runs on the
 * main loop alongside the ready marker emitter. When the boot has
 * passed DESKTOP_SMOKE_DIAG_DELAY_ITERATIONS iterations and the gate
 * is still not ready, we emit exactly ONE line of the form:
 *
 *   [smoke-diag] gui-session blocked: <first_blocker_name>
 *   [smoke-diag] mouse-events blocked: <first_blocker_name>
 *
 * The `[smoke-diag]` prefix is deliberately distinct from `[smoke]` so
 * the official smoke parser's substring matching on the mandatory
 * markers cannot collide. This is observability only and does not
 * change any gate logic or change which markers are mandatory.
 *
 * Bounded total messages: at most 2 (one per gate) over the entire
 * desktop runtime lifetime, so the serial log is never flooded.
 */
static void desktop_gui_session_smoke_emit_blocked_diagnostic_once(void) {
  struct desktop_session_smoke_readiness readiness;
  struct desktop_gui_session_smoke_gate gate;
  if (g_desktop_gui_session_smoke_announced) return;
  if (g_desktop_gui_session_smoke_diag_emitted) return;
  if (g_desktop_smoke_diag_iter_count < DESKTOP_SMOKE_DIAG_DELAY_ITERATIONS) return;
  if (desktop_session_smoke_readiness_snapshot(&g_desktop, &readiness) != 1) return;
  if (desktop_gui_session_smoke_gate_from_readiness(&readiness, &gate) != 1) return;
  if (gate.smoke_ready) return;
  fbcon_print("[smoke-diag] gui-session blocked: ");
  fbcon_print(gate.first_blocker_name ? gate.first_blocker_name : "unknown");
  fbcon_print("\n");
  g_desktop_gui_session_smoke_diag_emitted = 1;
}

static void desktop_mouse_events_smoke_emit_blocked_diagnostic_once(void) {
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  if (g_desktop_mouse_events_smoke_announced) return;
  if (g_desktop_mouse_events_smoke_diag_emitted) return;
  if (g_desktop_smoke_diag_iter_count < DESKTOP_SMOKE_DIAG_DELAY_ITERATIONS) return;
  if (desktop_session_smoke_readiness_snapshot(&g_desktop, &readiness) != 1) return;
  if (desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) != 1) return;
  if (gate.smoke_ready) return;
  fbcon_print("[smoke-diag] mouse-events blocked: ");
  fbcon_print(gate.first_blocker_name ? gate.first_blocker_name : "unknown");
  fbcon_print("\n");
  g_desktop_mouse_events_smoke_diag_emitted = 1;
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
  g_desktop_gui_session_smoke_announced = 0;
  g_desktop_mouse_events_smoke_announced = 0;
  g_desktop_gui_session_smoke_diag_emitted = 0;
  g_desktop_mouse_events_smoke_diag_emitted = 0;
  g_desktop_smoke_diag_iter_count = 0;
  g_desktop_shell_ctx = ctx;
  if (ctx && shell_context_session(ctx)) {
    session_set_active(shell_context_session(ctx));
  }
  desktop_init(&g_desktop, fb, w, h, pitch, ctx ? ctx->settings : NULL);
  desktop_open_terminal(&g_desktop);
  if (desktop_scheduler_adopt_current() != 0) {
    fbcon_print("Error: desktop scheduler unavailable.\n");
    return -1;
  }
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
      if ((uint8_t)ch == KEY_SUPER) {
        desktop_handle_input(&g_desktop, KEY_SUPER, 0);
        continue;
      }
      if ((uint8_t)ch == KEY_TTY_FALLBACK) {
        fbcon_print("[desktop] CTRL+ALT+F1 fallback requested\n");
        desktop_stop();
        break;
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
    if (g_desktop_smoke_diag_iter_count < DESKTOP_SMOKE_DIAG_DELAY_ITERATIONS) {
      g_desktop_smoke_diag_iter_count++;
    }
    desktop_gui_session_smoke_emit_once();
    desktop_mouse_events_smoke_emit_once();
    desktop_gui_session_smoke_emit_blocked_diagnostic_once();
    desktop_mouse_events_smoke_emit_blocked_diagnostic_once();
    task_yield();
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
