/*
 * tests/kernel/test_syscall_gfx.c — host tests for the Etapa 7 / Slice 7.2
 * graphical surface syscall handlers (SYS_WINDOW_CREATE..SYS_WINDOW_DESTROY,
 * src/kernel/syscall_gfx.c).
 *
 * Drives the handlers directly with synthetic `struct syscall_frame`s and a
 * faked `process_current()` (process_set_current), mirroring
 * tests/net/test_syscall_net.c. A recording fake backend with a small in-RAM
 * surface lets the tests assert the full policy layer without the compositor:
 *
 *   1. Ownership: a handle created by process A is rejected for process B on
 *      fill / blit / present / poll / destroy (the core isolation guarantee).
 *   2. Bounds / overflow: oversized dims, NULL src, oversized blit area and a
 *      context-less caller all fail-closed with -1.
 *   3. Pixel landing: fill and blit copy the right pixels (with clipping) into
 *      the backend surface -- the "compose in app buffer -> blit" data path.
 *   4. Lifecycle: destroy frees the handle; syscall_gfx_release_owner destroys
 *      every window a (dying) pid owns; the handle table is bounded.
 *   5. No backend installed -> the whole family returns -1.
 */

#include "kernel/syscall.h"
#include "kernel/syscall_gfx.h"
#include "kernel/process.h"
#include "kernel/task.h"
#include "kernel/pipe.h"
#include "kernel/stdin_buf.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void task_set_current(struct task *t);
extern void process_set_current(struct process *p);

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, name)                                                      \
  do {                                                                         \
    tests_run++;                                                               \
    if (cond) {                                                                \
      tests_passed++;                                                          \
    } else {                                                                   \
      printf("  FAIL: %s\n", (name));                                          \
    }                                                                          \
  } while (0)

/* === recording fake backend with an in-RAM surface ======================= */

#define FAKE_W 16u
#define FAKE_H 8u
#define FAKE_BACKEND_ID_BASE 1000

static uint32_t g_surface[FAKE_W * FAKE_H];

static struct fake_state {
  int create_calls, destroy_calls, present_calls, blit_calls, fill_calls,
      poll_calls, focus_calls;
  int32_t last_created, last_destroyed, last_focused;
  int create_should_fail;
  int poll_has_event;
  int poll_should_fail; /* make fk_poll_event return -1 (backend error) */
  struct capy_gfx_event poll_event;
  char last_title[64]; /* records the (sanitized) title the handler passed */
} g_fake;

static void fake_reset(void) {
  memset(&g_fake, 0, sizeof(g_fake));
  memset(g_surface, 0, sizeof(g_surface));
}

static int32_t fk_win_create(const char *title, uint32_t w, uint32_t h,
                             uint32_t pid) {
  size_t i;
  (void)w;
  (void)h;
  (void)pid;
  g_fake.create_calls++;
  /* Record exactly what the handler forwarded so a test can assert the
   * title sanitization the handler performed (gfx_copy_title). */
  g_fake.last_title[0] = '\0';
  if (title) {
    for (i = 0u; i + 1u < sizeof(g_fake.last_title) && title[i]; ++i)
      g_fake.last_title[i] = title[i];
    g_fake.last_title[i] = '\0';
  }
  if (g_fake.create_should_fail) return -1;
  g_fake.last_created = FAKE_BACKEND_ID_BASE + g_fake.create_calls;
  return g_fake.last_created;
}

static void fk_win_destroy(int32_t id) {
  g_fake.destroy_calls++;
  g_fake.last_destroyed = id;
}

static int fk_surface_info(int32_t id, uint32_t *w, uint32_t *h) {
  (void)id;
  if (w) *w = FAKE_W;
  if (h) *h = FAKE_H;
  return 0;
}

static int fk_surface_fill(int32_t id, uint32_t x, uint32_t y, uint32_t w,
                           uint32_t h, uint32_t argb) {
  uint32_t row, col, x1, y1;
  (void)id;
  g_fake.fill_calls++;
  if (x >= FAKE_W || y >= FAKE_H) return 0;
  x1 = (x + w < FAKE_W) ? (x + w) : FAKE_W;
  y1 = (y + h < FAKE_H) ? (y + h) : FAKE_H;
  for (row = y; row < y1; ++row)
    for (col = x; col < x1; ++col) g_surface[row * FAKE_W + col] = argb;
  return 0;
}

static int fk_surface_blit(int32_t id, const uint32_t *src, uint32_t sw,
                           uint32_t sh, uint32_t dx, uint32_t dy) {
  uint32_t row, col, cols, rows;
  (void)id;
  g_fake.blit_calls++;
  if (!src) return -1;
  if (dx >= FAKE_W || dy >= FAKE_H) return 0;
  cols = (dx + sw < FAKE_W) ? sw : (FAKE_W - dx);
  rows = (dy + sh < FAKE_H) ? sh : (FAKE_H - dy);
  for (row = 0; row < rows; ++row)
    for (col = 0; col < cols; ++col)
      g_surface[(dy + row) * FAKE_W + (dx + col)] = src[row * sw + col];
  return 0;
}

static void fk_win_present(int32_t id) {
  (void)id;
  g_fake.present_calls++;
}

static void fk_win_focus(int32_t id) {
  g_fake.focus_calls++;
  g_fake.last_focused = id;
}

static int fk_poll_event(int32_t id, struct capy_gfx_event *out) {
  (void)id;
  g_fake.poll_calls++;
  if (g_fake.poll_should_fail) return -1; /* backend error path */
  if (g_fake.poll_has_event) {
    *out = g_fake.poll_event;
    return 1;
  }
  return 0;
}

static const struct syscall_gfx_ops g_fake_ops = {
    .win_create = fk_win_create,
    .win_destroy = fk_win_destroy,
    .surface_info = fk_surface_info,
    .surface_fill = fk_surface_fill,
    .surface_blit = fk_surface_blit,
    .win_present = fk_win_present,
    .poll_event = fk_poll_event,
    .win_focus = fk_win_focus,
};

/* === harness ============================================================== */

static void reset_world(void) {
  pipe_system_init();
  process_system_init();
  task_system_init();
  stdin_buf_init();
  task_set_current((struct task *)0);
  process_set_current((struct process *)0);
  fake_reset();
  syscall_gfx_reset(); /* drop any handles leaked by a previous test case */
  syscall_gfx_install_ops(&g_fake_ops);
}

static struct syscall_frame frame6(uint64_t rdi, uint64_t rsi, uint64_t rdx,
                                   uint64_t r10, uint64_t r8, uint64_t r9) {
  struct syscall_frame f;
  memset(&f, 0, sizeof(f));
  f.rdi = rdi;
  f.rsi = rsi;
  f.rdx = rdx;
  f.r10 = r10;
  f.r8 = r8;
  f.r9 = r9;
  return f;
}

/* Create a window for the current process; returns the handle (or -1). */
static int64_t make_window(uint32_t w, uint32_t h) {
  struct syscall_frame f = frame6(0 /*title NULL*/, w, h, 0, 0, 0);
  return sys_window_create(&f);
}

/* === tests ================================================================ */

static void test_create_requires_process(void) {
  reset_world();
  /* no current process */
  CHECK(make_window(64, 32) == -1, "create without process -> -1");
}

static void test_create_and_handles_distinct(void) {
  struct process *p;
  int64_t h1, h2;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h1 = make_window(64, 32);
  h2 = make_window(64, 32);
  CHECK(h1 > 0 && h2 > 0 && h1 != h2, "two creates -> distinct handles");
  CHECK(g_fake.create_calls == 2, "backend win_create called per create");
}

static void test_create_rejects_bad_dims(void) {
  struct process *p;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  CHECK(make_window(0, 32) == -1, "zero width -> -1");
  CHECK(make_window(64, 0) == -1, "zero height -> -1");
  CHECK(make_window(CAPY_GFX_MAX_DIM + 1u, 32) == -1, "oversized width -> -1");
  CHECK(g_fake.create_calls == 0, "bad dims never reach backend");
}

static void test_create_backend_failure(void) {
  struct process *p;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  g_fake.create_should_fail = 1;
  CHECK(make_window(64, 32) == -1, "backend refusal -> -1");
  /* slot not consumed: a subsequent success should still get handle 1 */
  g_fake.create_should_fail = 0;
  CHECK(make_window(64, 32) == 1, "failed create did not consume a handle");
}

static void test_no_backend_family_fails(void) {
  struct process *p;
  struct syscall_frame f;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  syscall_gfx_install_ops(NULL); /* drop backend (also resets handles) */
  CHECK(make_window(64, 32) == -1, "create with no backend -> -1");
  f = frame6(1, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == -1, "present with no backend -> -1");
  CHECK(sys_window_destroy(&f) == -1, "destroy with no backend -> -1");
}

static void test_ownership_enforced(void) {
  struct process *a, *b;
  int64_t ha;
  struct syscall_frame f;
  reset_world();
  a = process_create("gfx-a", 0, 0);
  b = process_create("gfx-b", 0, 0);
  process_set_current(a);
  ha = make_window(64, 32);
  CHECK(ha > 0, "A creates window");

  /* B must not touch A's window. */
  process_set_current(b);
  f = frame6((uint64_t)ha, 0, 0, 4, 4, 0xFF00FF00u);
  CHECK(sys_surface_fill(&f) == -1, "B fill of A window -> -1");
  f = frame6((uint64_t)ha, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == -1, "B present of A window -> -1");
  CHECK(sys_window_destroy(&f) == -1, "B destroy of A window -> -1");
  CHECK(g_fake.fill_calls == 0 && g_fake.destroy_calls == 0,
        "unowned ops never reach backend");

  /* A still owns it. */
  process_set_current(a);
  f = frame6((uint64_t)ha, 0, 0, 4, 4, 0xFF00FF00u);
  CHECK(sys_surface_fill(&f) == 0, "A fill of A window -> 0");
}

static void test_fill_lands_pixels(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);
  /* fill a 2x2 at (1,1) with 0xAABBCCDD */
  f = frame6((uint64_t)h, 1, 1, 2, 2, 0xAABBCCDDu);
  CHECK(sys_surface_fill(&f) == 0, "fill returns 0");
  CHECK(g_surface[1 * FAKE_W + 1] == 0xAABBCCDDu &&
            g_surface[2 * FAKE_W + 2] == 0xAABBCCDDu,
        "fill set the right pixels");
  CHECK(g_surface[0] == 0u, "fill left other pixels untouched");
}

static void test_fill_rejects_bad_dims(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);
  f = frame6((uint64_t)h, 0, 0, CAPY_GFX_MAX_DIM + 1u, 2, 0xFFu);
  CHECK(sys_surface_fill(&f) == -1, "oversized fill width -> -1");
  CHECK(g_fake.fill_calls == 0, "oversized fill never reaches backend");
}

static void test_blit_lands_pixels(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  uint32_t src[4] = {0x11u, 0x22u, 0x33u, 0x44u}; /* 2x2 */
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)src, 2, 2, 3, 4); /* dst (3,4) */
  CHECK(sys_surface_blit(&f) == 0, "blit returns 0");
  CHECK(g_surface[4 * FAKE_W + 3] == 0x11u &&
            g_surface[4 * FAKE_W + 4] == 0x22u &&
            g_surface[5 * FAKE_W + 3] == 0x33u &&
            g_surface[5 * FAKE_W + 4] == 0x44u,
        "blit copied the 2x2 pattern to (3,4)");
}

static void test_blit_rejects_null_and_oversize(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  uint32_t one = 0u;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);
  f = frame6((uint64_t)h, 0 /*NULL src*/, 2, 2, 0, 0);
  CHECK(sys_surface_blit(&f) == -1, "NULL src blit -> -1");
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&one, CAPY_GFX_MAX_DIM + 1u, 2, 0,
             0);
  CHECK(sys_surface_blit(&f) == -1, "oversized blit width -> -1");
}

static void test_poll_event(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  struct capy_gfx_event ev;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);

  memset(&ev, 0, sizeof(ev));
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&ev, 0, 0, 0, 0);
  CHECK(sys_window_poll_event(&f) == 0, "no event -> 0");

  g_fake.poll_has_event = 1;
  g_fake.poll_event.kind = CAPY_GFX_EV_KEY_DOWN;
  g_fake.poll_event.code = 65u;
  CHECK(sys_window_poll_event(&f) == 1, "event -> 1");
  CHECK(ev.kind == CAPY_GFX_EV_KEY_DOWN && ev.code == 65u,
        "event copied to user struct");
}

static void test_destroy_frees_handle(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);
  f = frame6((uint64_t)h, 0, 0, 0, 0, 0);
  CHECK(sys_window_destroy(&f) == 0, "destroy returns 0");
  CHECK(g_fake.destroy_calls == 1, "backend win_destroy called");
  /* the handle is now free: a fill on it must fail */
  f = frame6((uint64_t)h, 0, 0, 2, 2, 0xFFu);
  CHECK(sys_surface_fill(&f) == -1, "fill on destroyed handle -> -1");
}

static void test_release_owner(void) {
  struct process *a, *b;
  int64_t ha1, ha2, hb;
  struct syscall_frame f;
  reset_world();
  a = process_create("gfx-a", 0, 0);
  b = process_create("gfx-b", 0, 0);
  process_set_current(a);
  ha1 = make_window(FAKE_W, FAKE_H);
  ha2 = make_window(FAKE_W, FAKE_H);
  process_set_current(b);
  hb = make_window(FAKE_W, FAKE_H);
  CHECK(ha1 > 0 && ha2 > 0 && hb > 0, "A owns 2, B owns 1");

  syscall_gfx_release_owner(a->pid);
  CHECK(g_fake.destroy_calls == 2, "release_owner destroyed exactly A's 2");

  /* A's handles are gone; B's survives. */
  process_set_current(a);
  f = frame6((uint64_t)ha1, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == -1, "A handle gone after release");
  process_set_current(b);
  f = frame6((uint64_t)hb, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == 0, "B handle survives A release");
}

static void test_invalid_handles(void) {
  struct process *p;
  struct syscall_frame f;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  (void)make_window(FAKE_W, FAKE_H); /* handle 1 exists */
  f = frame6(0, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == -1, "handle 0 -> -1");
  f = frame6(999, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == -1, "out-of-range handle -> -1");
}

static void test_handle_table_full(void) {
  struct process *p;
  int i;
  int64_t h;
  int got_all = 1;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  for (i = 0; i < 32; ++i) {
    h = make_window(FAKE_W, FAKE_H);
    if (h <= 0) got_all = 0;
  }
  CHECK(got_all, "32 windows created");
  CHECK(make_window(FAKE_W, FAKE_H) == -1, "33rd window -> -1 (table full)");
}

/* === alpha.311 / hardening: additional handler-layer coverage ============ */

/* Ownership is enforced for poll + blit too (test_ownership_enforced only
 * covered fill / present / destroy). */
static void test_ownership_enforced_poll_blit(void) {
  struct process *a, *b;
  int64_t ha;
  struct syscall_frame f;
  struct capy_gfx_event ev;
  uint32_t src = 0u;
  reset_world();
  a = process_create("gfx-a", 0, 0);
  b = process_create("gfx-b", 0, 0);
  process_set_current(a);
  ha = make_window(FAKE_W, FAKE_H);
  CHECK(ha > 0, "A creates window");

  process_set_current(b);
  f = frame6((uint64_t)ha, (uint64_t)(uintptr_t)&ev, 0, 0, 0, 0);
  CHECK(sys_window_poll_event(&f) == -1, "B poll of A window -> -1");
  f = frame6((uint64_t)ha, (uint64_t)(uintptr_t)&src, 1, 1, 0, 0);
  CHECK(sys_surface_blit(&f) == -1, "B blit of A window -> -1");
  CHECK(g_fake.poll_calls == 0 && g_fake.blit_calls == 0,
        "unowned poll/blit never reach the backend");
}

/* poll_event fail-closed: no process, NULL out, and a backend error all -> -1. */
static void test_poll_fail_closed(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  struct capy_gfx_event ev;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);

  process_set_current((struct process *)0); /* no process context */
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&ev, 0, 0, 0, 0);
  CHECK(sys_window_poll_event(&f) == -1, "poll without process -> -1");

  process_set_current(p);
  f = frame6((uint64_t)h, 0 /*NULL out*/, 0, 0, 0, 0);
  CHECK(sys_window_poll_event(&f) == -1, "poll with NULL out -> -1");

  g_fake.poll_should_fail = 1; /* backend returns -1 */
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&ev, 0, 0, 0, 0);
  CHECK(sys_window_poll_event(&f) == -1, "backend poll error -> -1");
}

/* blit boundary branches: zero dims are a no-op success; dx/dy overflow and a
 * pixel-product over the 16 Mpx cap fail-closed WITHOUT reaching the backend. */
static void test_blit_boundaries(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  uint32_t one = 0u;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);

  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&one, 0, 4, 0, 0);
  CHECK(sys_surface_blit(&f) == 0, "blit sw==0 -> 0 (nothing to copy)");
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&one, 4, 0, 0, 0);
  CHECK(sys_surface_blit(&f) == 0, "blit sh==0 -> 0 (nothing to copy)");
  CHECK(g_fake.blit_calls == 0, "zero-dim blit never reaches the backend");

  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&one, 1, 1, CAPY_GFX_MAX_DIM + 1u,
             0);
  CHECK(sys_surface_blit(&f) == -1, "blit dx overflow -> -1");
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&one, 1, 1, 0,
             CAPY_GFX_MAX_DIM + 1u);
  CHECK(sys_surface_blit(&f) == -1, "blit dy overflow -> -1");

  /* 4097*4097 = 16,785,409 > 16,777,216 (16 Mpx) even though each dim is
   * <= CAPY_GFX_MAX_DIM: proves the u64 product guard, not just per-dim. */
  f = frame6((uint64_t)h, (uint64_t)(uintptr_t)&one, 4097, 4097, 0, 0);
  CHECK(sys_surface_blit(&f) == -1, "blit pixel-product over 16Mpx -> -1");
  CHECK(g_fake.blit_calls == 0, "overflowing blit never reaches the backend");
}

/* fill bounds each of x/y/w/h; the original suite only checked width. */
static void test_fill_rejects_bad_y_and_h(void) {
  struct process *p;
  int64_t h;
  struct syscall_frame f;
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  h = make_window(FAKE_W, FAKE_H);
  f = frame6((uint64_t)h, 0, CAPY_GFX_MAX_DIM + 1u, 2, 2, 0xFFu);
  CHECK(sys_surface_fill(&f) == -1, "oversized fill y -> -1");
  f = frame6((uint64_t)h, 0, 0, 2, CAPY_GFX_MAX_DIM + 1u, 0xFFu);
  CHECK(sys_surface_fill(&f) == -1, "oversized fill height -> -1");
  CHECK(g_fake.fill_calls == 0, "oversized fill never reaches the backend");
}

/* The title is sanitized (control bytes -> '?') and NUL-terminated before it
 * reaches the backend, so a hostile title cannot inject terminal escapes. */
static void test_title_sanitized(void) {
  struct process *p;
  struct syscall_frame f;
  char evil[6];
  reset_world();
  p = process_create("gfx", 0, 0);
  process_set_current(p);
  evil[0] = 'H';
  evil[1] = (char)0x1b; /* ESC -> '?' */
  evil[2] = 'i';
  evil[3] = (char)0x07; /* BEL -> '?' */
  evil[4] = (char)0x7f; /* DEL -> '?' */
  evil[5] = '\0';
  f = frame6((uint64_t)(uintptr_t)evil, 64, 32, 0, 0, 0);
  CHECK(sys_window_create(&f) > 0, "create with a hostile title succeeds");
  CHECK(g_fake.last_title[0] == 'H' && g_fake.last_title[1] == '?' &&
            g_fake.last_title[2] == 'i' && g_fake.last_title[3] == '?' &&
            g_fake.last_title[4] == '?' && g_fake.last_title[5] == '\0',
        "control bytes replaced with '?' and NUL-terminated");
}

/* release_owner(0) is a no-op: pid 0 never owns a window. */
static void test_release_owner_zero_is_noop(void) {
  struct process *a;
  int64_t ha;
  struct syscall_frame f;
  reset_world();
  a = process_create("gfx-a", 0, 0);
  process_set_current(a);
  ha = make_window(FAKE_W, FAKE_H);
  CHECK(ha > 0, "A owns a window");
  syscall_gfx_release_owner(0u);
  CHECK(g_fake.destroy_calls == 0, "release_owner(0) destroys nothing");
  f = frame6((uint64_t)ha, 0, 0, 0, 0, 0);
  CHECK(sys_window_present(&f) == 0, "A's window survives release_owner(0)");
}

static void test_focus_owner_isolated_and_lifecycle_safe(void) {
  struct process *a;
  struct process *b;
  int64_t ha;
  reset_world();
  a = process_create("gfx-a", 0, 0);
  b = process_create("gfx-b", 0, 0);
  process_set_current(a);
  ha = make_window(FAKE_W, FAKE_H);
  CHECK(ha > 0, "focus_owner fixture creates A window");
  CHECK(syscall_gfx_focus_owner(a->pid) == 1 && g_fake.focus_calls == 1 &&
            g_fake.last_focused == g_fake.last_created,
        "focus_owner raises the matching owner's backend window");
  CHECK(syscall_gfx_focus_owner(b->pid) == 0 && g_fake.focus_calls == 1,
        "focus_owner does not cross process ownership");
  syscall_gfx_release_owner(a->pid);
  CHECK(syscall_gfx_focus_owner(a->pid) == 0 && g_fake.focus_calls == 1,
        "focus_owner forgets handles after owner teardown");
  CHECK(syscall_gfx_focus_owner(0u) == 0,
        "focus_owner rejects the kernel pseudo-owner");
}

int test_syscall_gfx_run(void) {
  printf("[test_syscall_gfx]\n");
  tests_run = 0;
  tests_passed = 0;

  test_create_requires_process();
  test_create_and_handles_distinct();
  test_create_rejects_bad_dims();
  test_create_backend_failure();
  test_no_backend_family_fails();
  test_ownership_enforced();
  test_fill_lands_pixels();
  test_fill_rejects_bad_dims();
  test_blit_lands_pixels();
  test_blit_rejects_null_and_oversize();
  test_poll_event();
  test_destroy_frees_handle();
  test_release_owner();
  test_invalid_handles();
  test_handle_table_full();
  test_ownership_enforced_poll_blit();
  test_poll_fail_closed();
  test_blit_boundaries();
  test_fill_rejects_bad_y_and_h();
  test_title_sanitized();
  test_release_owner_zero_is_noop();
  test_focus_owner_isolated_and_lifecycle_safe();

  syscall_gfx_install_ops(NULL); /* leave a clean state for the next suite */
  printf("  %d/%d checks passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
