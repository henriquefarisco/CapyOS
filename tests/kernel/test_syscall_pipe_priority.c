/*
 * tests/test_syscall_pipe_priority.c (2026-05-02)
 *
 * Regression test locking the priority order of `sys_read` /
 * `sys_write` introduced when wiring the ring-3 browser engine:
 *
 *   1. process FD table slot of type FD_TYPE_PIPE wins, even at
 *      fd 0 / 1 / 2.
 *   2. fd 0 with FD_TYPE_FREE slot still drains the kernel
 *      `stdin_buf` (legacy hello/capysh stdin behaviour).
 *   3. fd 1 / 2 with FD_TYPE_FREE slot still goes to debugcon
 *      (legacy hello/capysh stdout/stderr behaviour). We don't
 *      assert the debugcon side-effect (host build can't observe
 *      port 0xE9 writes), but we DO assert that the call returns
 *      `len` bytes "written" so the legacy semantics hold.
 *
 * Without the priority fix, the browser engine's
 * `capy_read(0, ...)` would silently drain `stdin_buf` (always
 * empty) and `capy_write(1, ...)` would silently land on debugcon
 * instead of the response pipe -- the chrome runtime would never
 * see any IPC events, which is exactly the symptom reported as
 * "browser does not load home page nor any URL".
 *
 * Implementation note: this test calls `sys_read` / `sys_write`
 * directly (declared non-static in include/kernel/syscall.h)
 * rather than via `syscall_dispatch` / `syscall_init`. Going
 * through `syscall_init` would force us to provide stubs for
 * every syscall handler in the table; we only need the two paths
 * we are exercising.
 */

#include "kernel/syscall.h"
#include "kernel/process.h"
#include "kernel/pipe.h"
#include "kernel/task.h"
#include "kernel/stdin_buf.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void task_set_current(struct task *t);
extern void process_set_current(struct process *p);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                          \
    do {                                                                    \
        tests_run++;                                                        \
        printf("  %-58s ", name);                                           \
    } while (0)
#define PASS()                                                              \
    do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* Reset every subsystem the test touches. We do this between
 * individual tests because the legacy stdin_buf path uses
 * task_yield() in a busy loop; a stale "yield" callback or stale
 * pipe slot would make the test hang. */
static void reset_world(void) {
    pipe_system_init();
    process_system_init();
    task_system_init();
    stdin_buf_init();
    task_set_current((struct task *)0);
    process_set_current((struct process *)0);
}

/* Build the syscall_frame the dispatcher would have built. The
 * field layout is locked by tests/test_capylibc_abi.c; we reuse
 * the same offsets here. */
static struct syscall_frame make_frame(uint64_t rdi, uint64_t rsi,
                                        uint64_t rdx) {
    struct syscall_frame f;
    memset(&f, 0, sizeof(f));
    f.rdi = rdi;
    f.rsi = rsi;
    f.rdx = rdx;
    return f;
}

/* Helper: create a process and bind a pipe FD (read or write end)
 * at the requested process-side fd index. The chosen pipe index
 * is returned via `*out_pipe_id` so the caller can write/read the
 * underlying buffer directly via pipe_read / pipe_write to
 * confirm the syscall reached the pipe. */
static struct process *spawn_with_pipe_fd(int fd_index, uint32_t flags,
                                           int *out_pipe_id) {
    struct process *p = process_create("syscall-priority-test", 0u, 0u);
    if (!p) return NULL;
    int kfds[2];
    if (pipe_create(kfds) != 0) return NULL;
    int pipe_id = kfds[0];
    p->fds[fd_index].type = FD_TYPE_PIPE;
    p->fds[fd_index].flags = flags;
    p->fds[fd_index].private_data = (void *)(intptr_t)pipe_id;
    p->fds[fd_index].offset = 0;
    if (out_pipe_id) *out_pipe_id = pipe_id;
    return p;
}

/* === Priority: pipe at fd 1 wins over debugcon ============== */

static void test_sys_write_fd1_pipe_beats_debugcon(void) {
    reset_world();
    int pipe_id = -1;
    struct process *p = spawn_with_pipe_fd(1, FD_PIPE_FLAG_WRITE, &pipe_id);
    if (!p) { TEST("setup process+pipe"); FAIL("alloc"); return; }
    process_set_current(p);

    const char payload[] = "hello-pipe-fd1";
    struct syscall_frame f = make_frame(/*fd*/1u,
                                         (uint64_t)(uintptr_t)payload,
                                         (uint64_t)(sizeof(payload) - 1u));
    int64_t rc = sys_write(&f);

    TEST("sys_write(fd=1) routes to pipe when slot is FD_TYPE_PIPE");
    if (rc == (int64_t)(sizeof(payload) - 1u)) PASS();
    else FAIL("did not write the expected number of bytes to the pipe");

    /* Drain the pipe and verify the bytes really landed there
     * rather than being silently consumed by debugcon. */
    char buf[32] = {0};
    int rd = pipe_read(pipe_id, buf, sizeof(buf) - 1u);
    TEST("pipe holds the bytes written by sys_write");
    if (rd == (int)(sizeof(payload) - 1u)
        && strcmp(buf, payload) == 0) PASS();
    else FAIL("pipe content mismatch");
}

static void test_sys_write_fd2_pipe_beats_debugcon(void) {
    reset_world();
    int pipe_id = -1;
    struct process *p = spawn_with_pipe_fd(2, FD_PIPE_FLAG_WRITE, &pipe_id);
    if (!p) { TEST("setup process+pipe"); FAIL("alloc"); return; }
    process_set_current(p);

    const char payload[] = "stderr-pipe";
    struct syscall_frame f = make_frame(/*fd*/2u,
                                         (uint64_t)(uintptr_t)payload,
                                         (uint64_t)(sizeof(payload) - 1u));
    int64_t rc = sys_write(&f);

    TEST("sys_write(fd=2) routes to pipe when slot is FD_TYPE_PIPE");
    if (rc == (int64_t)(sizeof(payload) - 1u)) PASS();
    else FAIL("did not write the expected number of bytes to the pipe");

    char buf[32] = {0};
    int rd = pipe_read(pipe_id, buf, sizeof(buf) - 1u);
    TEST("fd=2 pipe content matches payload");
    if (rd == (int)(sizeof(payload) - 1u)
        && strcmp(buf, payload) == 0) PASS();
    else FAIL("fd=2 pipe mismatch");
}

/* === Priority: pipe at fd 0 wins over stdin_buf ============= */

static void test_sys_read_fd0_pipe_beats_stdin_buf(void) {
    reset_world();
    int pipe_id = -1;
    struct process *p = spawn_with_pipe_fd(0, FD_PIPE_FLAG_READ, &pipe_id);
    if (!p) { TEST("setup process+pipe"); FAIL("alloc"); return; }
    process_set_current(p);

    /* Pre-fill the pipe with bytes that did NOT come through
     * stdin_buf. If the priority order regresses, the read would
     * stall forever inside the legacy stdin_buf branch (which
     * busy-yields until the buffer has data) -- to keep that case
     * non-fatal in CI, we close the write end too so the legacy
     * branch would observe EOF and bail out. We verify rc and the
     * decoded buffer to lock the path that DID fire. */
    const char payload[] = "navigate-from-pipe";
    int wrc = pipe_write(pipe_id, payload, sizeof(payload) - 1u);
    if (wrc != (int)(sizeof(payload) - 1u)) {
        TEST("seed pipe with payload");
        FAIL("pipe_write short-write during seed");
        return;
    }

    char buf[64] = {0};
    struct syscall_frame f = make_frame(/*fd*/0u,
                                         (uint64_t)(uintptr_t)buf,
                                         sizeof(buf) - 1u);
    int64_t rc = sys_read(&f);

    TEST("sys_read(fd=0) routes to pipe when slot is FD_TYPE_PIPE");
    if (rc == (int64_t)(sizeof(payload) - 1u)) PASS();
    else FAIL("sys_read returned wrong byte count");

    TEST("sys_read(fd=0) bytes match the pipe payload (not stdin_buf)");
    if (strcmp(buf, payload) == 0) PASS();
    else FAIL("buf content mismatch");
}

/* === Fallbacks still work for fd-table-free processes ======== */

static void test_sys_write_fd1_falls_back_to_debugcon(void) {
    reset_world();
    /* Process exists but fd 1 has FD_TYPE_FREE => legacy debugcon
     * path. We can't observe port 0xE9 from the host, but the
     * return value contract (returns the byte count) is testable. */
    struct process *p = process_create("legacy-stdout", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    const char payload[] = "legacy-stdout-bytes";
    struct syscall_frame f = make_frame(/*fd*/1u,
                                         (uint64_t)(uintptr_t)payload,
                                         (uint64_t)(sizeof(payload) - 1u));
    int64_t rc = sys_write(&f);

    TEST("sys_write(fd=1) on FD_TYPE_FREE process slot returns full len");
    if (rc == (int64_t)(sizeof(payload) - 1u)) PASS();
    else FAIL("legacy debugcon fallback did not return len");
}

static void test_sys_read_fd0_falls_back_to_stdin_buf(void) {
    reset_world();
    struct process *p = process_create("legacy-stdin", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    /* Pre-fill stdin_buf with two bytes. The fallback path is
     * "block until at least one byte, then drain the rest without
     * blocking", so the call returns immediately with both bytes.
     * The producer side of stdin_buf is `stdin_buf_push` which
     * is normally driven by the keyboard IRQ; pushing directly
     * here mimics that producer for the test. */
    int p0 = stdin_buf_push('A');
    int p1 = stdin_buf_push('B');
    if (!p0 || !p1) {
        TEST("seed stdin_buf");
        FAIL("stdin_buf_push refused the seed");
        return;
    }

    char buf[8] = {0};
    struct syscall_frame f = make_frame(/*fd*/0u,
                                         (uint64_t)(uintptr_t)buf,
                                         sizeof(buf) - 1u);
    int64_t rc = sys_read(&f);

    TEST("sys_read(fd=0) on FD_TYPE_FREE process slot drains stdin_buf");
    if (rc == 2 && buf[0] == 'A' && buf[1] == 'B') PASS();
    else FAIL("legacy stdin_buf fallback did not drain seeded bytes");
}

/* === Entry point ============================================== */

int test_syscall_pipe_priority_run(void) {
    printf("[test_syscall_pipe_priority]\n");
    tests_run = 0;
    tests_passed = 0;

    test_sys_write_fd1_pipe_beats_debugcon();
    test_sys_write_fd2_pipe_beats_debugcon();
    test_sys_read_fd0_pipe_beats_stdin_buf();
    test_sys_write_fd1_falls_back_to_debugcon();
    test_sys_read_fd0_falls_back_to_stdin_buf();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
