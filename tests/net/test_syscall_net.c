/*
 * tests/test_syscall_net.c (2026-05-08, F4 seção c)
 *
 * Host-side regression test for the userland socket syscall family
 * (`SYS_SOCKET`..`SYS_RECV`, numbers 28..34) wired in
 * `src/kernel/syscall_net.c`. The test calls the handlers directly
 * with synthetic `struct syscall_frame` instances and a faked
 * `process_current()` (via `process_set_current`) -- mirroring the
 * pattern established by `tests/test_syscall_pipe_priority.c`. It
 * verifies:
 *
 *   1. `sys_socket` rejects non-AF_INET / unknown types.
 *   2. `sys_socket` allocates a process FD slot of type
 *      FD_TYPE_SOCKET pointing at the kernel-side socket fd
 *      returned by the registered backend.
 *   3. The userland-visible fd resolves correctly: `sys_bind`,
 *      `sys_connect`, `sys_send`, `sys_recv` reach the backend with
 *      the embedded kernel fd.
 *   4. `sys_close` (driven via `process_fd_free` on slot release)
 *      runs the registered close hook so the backend's accounting
 *      stays in sync with userland.
 *   5. `sys_read` / `sys_write` on a socket FD delegate to the
 *      registered net backend (POSIX-on-socket semantics).
 *   6. With NO backend installed the entire family deterministically
 *      returns -1 (no crashes, no wild deref).
 *
 * The test does NOT link the production `src/net/services/socket.c`
 * -- the whole point of the injectable backend is to keep
 * net-stack transitive deps out of the test binary. The fake
 * backend below records every call so individual assertions can
 * pin behaviour.
 */

#include "kernel/syscall.h"
#include "kernel/syscall_net.h"
#include "kernel/process.h"
#include "kernel/pipe.h"
#include "kernel/task.h"
#include "kernel/stdin_buf.h"
#include "net/socket.h"

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

/* === Recording fake backend ================================== */

#define FAKE_KERNEL_FD_BASE 100  /* distinct from process FD numbers */

struct fake_call_log {
    int create_calls;
    int bind_calls;
    int listen_calls;
    int connect_calls;
    int send_calls;
    int recv_calls;
    int close_calls;
    int dns_calls;
    int last_kernel_fd_seen;
    int next_kernel_fd_to_return;
    int connect_should_fail;
    int dns_should_miss;
    int send_to_return;
    int recv_to_return;
    char last_send_payload[64];
    size_t last_send_len;
    char recv_canned[64];
    size_t recv_canned_len;
    char last_dns_name[64];
    uint32_t dns_canned_ip;
    struct sockaddr_in last_bind_addr;
    struct sockaddr_in last_connect_addr;
};

static struct fake_call_log g_log;

static void fake_reset(void) {
    memset(&g_log, 0, sizeof(g_log));
    g_log.next_kernel_fd_to_return = FAKE_KERNEL_FD_BASE;
    g_log.send_to_return = 0;     /* default: report 0 bytes accepted */
    g_log.recv_to_return = 0;
}

static int fake_sock_create(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol;
    g_log.create_calls++;
    return g_log.next_kernel_fd_to_return++;
}

static int fake_sock_bind(int fd, const struct sockaddr_in *addr) {
    g_log.bind_calls++;
    g_log.last_kernel_fd_seen = fd;
    if (addr) g_log.last_bind_addr = *addr;
    return 0;
}

static int fake_sock_listen(int fd, int backlog) {
    (void)backlog;
    g_log.listen_calls++;
    g_log.last_kernel_fd_seen = fd;
    return 0;
}

static int fake_sock_accept(int fd, struct sockaddr_in *addr) {
    (void)fd; (void)addr;
    return -1; /* matches production socket_accept until F4 seção d */
}

static int fake_sock_connect(int fd, const struct sockaddr_in *addr) {
    g_log.connect_calls++;
    g_log.last_kernel_fd_seen = fd;
    if (addr) g_log.last_connect_addr = *addr;
    if (g_log.connect_should_fail) return -1;
    return 0;
}

static int fake_sock_send(int fd, const void *buf, size_t len, int flags) {
    (void)flags;
    g_log.send_calls++;
    g_log.last_kernel_fd_seen = fd;
    if (buf && len > 0) {
        size_t copy = len < sizeof(g_log.last_send_payload) - 1 ?
                       len : sizeof(g_log.last_send_payload) - 1;
        memcpy(g_log.last_send_payload, buf, copy);
        g_log.last_send_payload[copy] = '\0';
        g_log.last_send_len = copy;
    }
    return g_log.send_to_return ? g_log.send_to_return : (int)len;
}

static int fake_sock_recv(int fd, void *buf, size_t len, int flags) {
    (void)flags;
    g_log.recv_calls++;
    g_log.last_kernel_fd_seen = fd;
    if (g_log.recv_canned_len > 0 && buf && len > 0) {
        size_t copy = g_log.recv_canned_len < len ? g_log.recv_canned_len : len;
        memcpy(buf, g_log.recv_canned, copy);
        return (int)copy;
    }
    return g_log.recv_to_return;
}

static int fake_sock_close(int fd) {
    g_log.close_calls++;
    g_log.last_kernel_fd_seen = fd;
    return 0;
}

static int fake_dns_resolve(const char *name, uint32_t *out_ip) {
    g_log.dns_calls++;
    if (name) {
        size_t copy = strlen(name);
        if (copy >= sizeof(g_log.last_dns_name)) {
            copy = sizeof(g_log.last_dns_name) - 1;
        }
        memcpy(g_log.last_dns_name, name, copy);
        g_log.last_dns_name[copy] = '\0';
    }
    if (g_log.dns_should_miss) return -1;
    if (out_ip) *out_ip = g_log.dns_canned_ip;
    return 0;
}

static const struct syscall_net_ops g_fake_ops = {
    .sock_create  = fake_sock_create,
    .sock_bind    = fake_sock_bind,
    .sock_listen  = fake_sock_listen,
    .sock_accept  = fake_sock_accept,
    .sock_connect = fake_sock_connect,
    .sock_send    = fake_sock_send,
    .sock_recv    = fake_sock_recv,
    .sock_close   = fake_sock_close,
    .dns_resolve  = fake_dns_resolve,
};

/* === Common reset ============================================= */

static void reset_world(void) {
    pipe_system_init();
    process_system_init();
    task_system_init();
    stdin_buf_init();
    task_set_current((struct task *)0);
    process_set_current((struct process *)0);
    fake_reset();
    syscall_net_install_ops(&g_fake_ops);
    process_fd_register_socket_close(fake_sock_close);
}

static void teardown_world(void) {
    syscall_net_install_ops(NULL);
    process_fd_register_socket_close(NULL);
}

/* === Frame builders =========================================== */

static struct syscall_frame make_frame3(uint64_t rdi, uint64_t rsi,
                                          uint64_t rdx) {
    struct syscall_frame f;
    memset(&f, 0, sizeof(f));
    f.rdi = rdi;
    f.rsi = rsi;
    f.rdx = rdx;
    return f;
}

static struct syscall_frame make_frame4(uint64_t rdi, uint64_t rsi,
                                          uint64_t rdx, uint64_t rcx) {
    struct syscall_frame f = make_frame3(rdi, rsi, rdx);
    f.rcx = rcx;
    return f;
}

/* === Tests ==================================================== */

static void test_socket_rejects_non_af_inet(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    /* domain = 99 (not AF_INET) */
    struct syscall_frame f = make_frame3(99u, SOCK_STREAM, 0u);
    int64_t rc = sys_socket(&f);
    TEST("sys_socket rejects domain != AF_INET");
    if (rc == -1 && g_log.create_calls == 0) PASS();
    else FAIL("did not bypass backend on bad domain");

    teardown_world();
}

static void test_socket_rejects_unknown_type(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame f = make_frame3(AF_INET, 99u, 0u);
    int64_t rc = sys_socket(&f);
    TEST("sys_socket rejects unknown type");
    if (rc == -1 && g_log.create_calls == 0) PASS();
    else FAIL("did not bypass backend on bad type");

    teardown_world();
}

static void test_socket_allocates_fd_slot(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame f = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t fd = sys_socket(&f);

    TEST("sys_socket returns a userland fd >= 3");
    if (fd >= 3 && fd < PROCESS_FD_MAX) PASS();
    else FAIL("unexpected fd");

    TEST("fd slot type is FD_TYPE_SOCKET with kernel fd embedded");
    if (fd >= 0 && p->fds[fd].type == FD_TYPE_SOCKET &&
        (int)(intptr_t)p->fds[fd].private_data == FAKE_KERNEL_FD_BASE)
        PASS();
    else FAIL("slot mis-tagged or private_data wrong");

    teardown_world();
}

static void test_bind_passes_through_to_backend(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame sf = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t fd = sys_socket(&sf);
    if (fd < 0) { TEST("setup socket fd"); FAIL("alloc"); return; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = 0x4242;
    addr.sin_addr = 0xC0A80001u;  /* 192.168.0.1 */
    struct syscall_frame f = make_frame3((uint64_t)fd,
                                          (uint64_t)(uintptr_t)&addr,
                                          (uint64_t)sizeof(addr));
    int64_t rc = sys_bind(&f);

    TEST("sys_bind reaches backend with kernel fd");
    if (rc == 0 && g_log.bind_calls == 1 &&
        g_log.last_kernel_fd_seen == FAKE_KERNEL_FD_BASE) PASS();
    else FAIL("backend not called or kernel fd lost");

    TEST("sys_bind copies sockaddr_in faithfully");
    if (g_log.last_bind_addr.sin_port == 0x4242 &&
        g_log.last_bind_addr.sin_addr == 0xC0A80001u) PASS();
    else FAIL("addr not copied through");

    teardown_world();
}

static void test_bind_rejects_short_addrlen(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame sf = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t fd = sys_socket(&sf);
    if (fd < 0) { TEST("setup socket fd"); FAIL("alloc"); return; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    /* addrlen too small */
    struct syscall_frame f = make_frame3((uint64_t)fd,
                                          (uint64_t)(uintptr_t)&addr,
                                          (uint64_t)4u);
    int64_t rc = sys_bind(&f);

    TEST("sys_bind rejects short addrlen");
    if (rc == -1 && g_log.bind_calls == 0) PASS();
    else FAIL("did not reject short addrlen");

    teardown_world();
}

static void test_bind_rejects_non_socket_fd(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    /* Install a pipe FD at index 5 instead of a socket. */
    int pipe_id;
    int kfds[2];
    if (pipe_create(kfds) != 0) { TEST("setup pipe"); FAIL("alloc"); return; }
    pipe_id = kfds[0];
    p->fds[5].type = FD_TYPE_PIPE;
    p->fds[5].flags = FD_PIPE_FLAG_READ;
    p->fds[5].private_data = (void *)(intptr_t)pipe_id;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    struct syscall_frame f = make_frame3(5u,
                                          (uint64_t)(uintptr_t)&addr,
                                          (uint64_t)sizeof(addr));
    int64_t rc = sys_bind(&f);

    TEST("sys_bind rejects non-socket fd");
    if (rc == -1 && g_log.bind_calls == 0) PASS();
    else FAIL("dispatched bind on a pipe fd");

    teardown_world();
}

static void test_connect_send_recv_round_trip(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame sf = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t fd = sys_socket(&sf);
    if (fd < 0) { TEST("setup socket fd"); FAIL("alloc"); return; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = 0x5050;
    addr.sin_addr = 0x08080808u;
    struct syscall_frame cf = make_frame3((uint64_t)fd,
                                            (uint64_t)(uintptr_t)&addr,
                                            (uint64_t)sizeof(addr));
    int64_t crc = sys_connect(&cf);
    TEST("sys_connect returns 0 on backend success");
    if (crc == 0 && g_log.connect_calls == 1) PASS();
    else FAIL("connect did not pass through");

    const char payload[] = "hello-net";
    struct syscall_frame snd = make_frame4((uint64_t)fd,
                                             (uint64_t)(uintptr_t)payload,
                                             (uint64_t)(sizeof(payload) - 1),
                                             /*flags*/ 0u);
    int64_t srx = sys_send(&snd);
    TEST("sys_send forwards bytes to backend");
    if (srx == (int64_t)(sizeof(payload) - 1) &&
        strcmp(g_log.last_send_payload, "hello-net") == 0) PASS();
    else FAIL("send payload mismatch");

    /* Pre-seed the recv canned buffer. */
    const char canned[] = "echo-back";
    memcpy(g_log.recv_canned, canned, sizeof(canned) - 1);
    g_log.recv_canned_len = sizeof(canned) - 1;
    char buf[32] = {0};
    struct syscall_frame rcv = make_frame4((uint64_t)fd,
                                             (uint64_t)(uintptr_t)buf,
                                             (uint64_t)sizeof(buf),
                                             /*flags*/ 0u);
    int64_t rrx = sys_recv(&rcv);
    TEST("sys_recv copies canned bytes from backend");
    if (rrx == (int64_t)(sizeof(canned) - 1) &&
        strncmp(buf, canned, sizeof(canned) - 1) == 0) PASS();
    else FAIL("recv content mismatch");

    teardown_world();
}

static void test_close_runs_socket_hook(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame sf = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t fd = sys_socket(&sf);
    if (fd < 0) { TEST("setup socket fd"); FAIL("alloc"); return; }

    /* Close via process_fd_free (the path SYS_CLOSE walks through). */
    process_fd_free(p, (int)fd);

    TEST("process_fd_free runs registered socket close hook");
    if (g_log.close_calls == 1 &&
        g_log.last_kernel_fd_seen == FAKE_KERNEL_FD_BASE) PASS();
    else FAIL("hook not invoked or kernel fd lost");

    TEST("FD slot reclaimed (FD_TYPE_FREE) after close");
    if (p->fds[fd].type == FD_TYPE_FREE &&
        p->fds[fd].private_data == NULL) PASS();
    else FAIL("slot not reclaimed");

    teardown_world();
}

static void test_no_backend_returns_minus_one(void) {
    reset_world();
    /* Override the install: no backend at all. The reset_world helper
     * registered the fake; clear it before the actual asserts. */
    syscall_net_install_ops(NULL);
    process_fd_register_socket_close(NULL);
    fake_reset();

    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame f = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t rc = sys_socket(&f);
    TEST("sys_socket returns -1 with no backend");
    if (rc == -1 && g_log.create_calls == 0) PASS();
    else FAIL("backend wrongly reachable");

    /* We can't even create a socket fd to drive bind/connect/etc, so
     * synthesize a fake socket fd manually to verify the recv path
     * also short-circuits to -1. */
    p->fds[3].type = FD_TYPE_SOCKET;
    p->fds[3].private_data = (void *)(intptr_t)42;
    struct syscall_frame rf = make_frame4(3u,
                                            (uint64_t)(uintptr_t)"x",
                                            1u,
                                            0u);
    TEST("sys_recv returns -1 with no backend");
    if (sys_recv(&rf) == -1) PASS();
    else FAIL("recv reached unregistered backend");

    teardown_world();
}

static void test_read_write_dispatch_to_socket(void) {
    reset_world();
    struct process *p = process_create("net-test", 0u, 0u);
    if (!p) { TEST("setup process"); FAIL("alloc"); return; }
    process_set_current(p);

    struct syscall_frame sf = make_frame3(AF_INET, SOCK_STREAM, 0u);
    int64_t fd = sys_socket(&sf);
    if (fd < 0) { TEST("setup socket fd"); FAIL("alloc"); return; }

    /* sys_write on socket fd must go through sock_send. */
    const char payload[] = "via-write";
    struct syscall_frame wf = make_frame3((uint64_t)fd,
                                           (uint64_t)(uintptr_t)payload,
                                           (uint64_t)(sizeof(payload) - 1));
    int64_t wrc = sys_write(&wf);
    TEST("sys_write on socket fd routes to sock_send");
    if (wrc == (int64_t)(sizeof(payload) - 1) &&
        g_log.send_calls == 1 &&
        strcmp(g_log.last_send_payload, "via-write") == 0) PASS();
    else FAIL("write did not dispatch to socket backend");

    /* sys_read on socket fd must go through sock_recv. */
    const char canned[] = "via-read";
    memcpy(g_log.recv_canned, canned, sizeof(canned) - 1);
    g_log.recv_canned_len = sizeof(canned) - 1;
    char buf[16] = {0};
    struct syscall_frame rf = make_frame3((uint64_t)fd,
                                           (uint64_t)(uintptr_t)buf,
                                           (uint64_t)sizeof(buf));
    int64_t rrc = sys_read(&rf);
    TEST("sys_read on socket fd routes to sock_recv");
    if (rrc == (int64_t)(sizeof(canned) - 1) &&
        g_log.recv_calls == 1 &&
        strncmp(buf, canned, sizeof(canned) - 1) == 0) PASS();
    else FAIL("read did not dispatch to socket backend");

    teardown_world();
}

/* === sys_dns_resolve (F4 seção c parte 3/3) =================== */

static void test_dns_resolve_hit(void) {
    reset_world();
    g_log.dns_canned_ip = 0x08080808u; /* 8.8.8.8 host-order */

    uint32_t ip = 0;
    struct syscall_frame f = make_frame3((uint64_t)(uintptr_t)"dns.example",
                                          (uint64_t)(uintptr_t)&ip,
                                          /*flags*/ 0u);
    int64_t rc = sys_dns_resolve(&f);

    TEST("sys_dns_resolve hit: returns 0 and populates *out_ip");
    if (rc == 0 && ip == 0x08080808u && g_log.dns_calls == 1 &&
        strcmp(g_log.last_dns_name, "dns.example") == 0) PASS();
    else FAIL("backend not called or ip not written");

    teardown_world();
}

static void test_dns_resolve_miss(void) {
    reset_world();
    g_log.dns_should_miss = 1;

    uint32_t ip = 0xDEADBEEFu;  /* sentinel: must remain untouched on miss */
    struct syscall_frame f = make_frame3((uint64_t)(uintptr_t)"unknown.example",
                                          (uint64_t)(uintptr_t)&ip,
                                          0u);
    int64_t rc = sys_dns_resolve(&f);

    TEST("sys_dns_resolve miss: returns -1");
    if (rc == -1 && g_log.dns_calls == 1) PASS();
    else FAIL("did not propagate miss");

    teardown_world();
}

static void test_dns_resolve_validation(void) {
    reset_world();

    /* NULL name */
    struct syscall_frame f1 = make_frame3(0u, (uint64_t)(uintptr_t)"x", 0u);
    TEST("sys_dns_resolve rejects NULL name without backend call");
    if (sys_dns_resolve(&f1) == -1 && g_log.dns_calls == 0) PASS();
    else FAIL("NULL name reached backend");

    /* NULL out_ip */
    struct syscall_frame f2 = make_frame3((uint64_t)(uintptr_t)"x", 0u, 0u);
    TEST("sys_dns_resolve rejects NULL out_ip without backend call");
    if (sys_dns_resolve(&f2) == -1 && g_log.dns_calls == 0) PASS();
    else FAIL("NULL out_ip reached backend");

    /* Non-zero flags */
    uint32_t ip = 0;
    struct syscall_frame f3 = make_frame3((uint64_t)(uintptr_t)"x",
                                           (uint64_t)(uintptr_t)&ip,
                                           /*flags=*/ 1u);
    TEST("sys_dns_resolve rejects non-zero flags (reserved)");
    if (sys_dns_resolve(&f3) == -1 && g_log.dns_calls == 0) PASS();
    else FAIL("non-zero flags reached backend");

    teardown_world();
}

static void test_dns_resolve_no_backend(void) {
    reset_world();
    syscall_net_install_ops(NULL);

    uint32_t ip = 0;
    struct syscall_frame f = make_frame3((uint64_t)(uintptr_t)"x",
                                          (uint64_t)(uintptr_t)&ip, 0u);
    TEST("sys_dns_resolve returns -1 with no backend installed");
    if (sys_dns_resolve(&f) == -1) PASS();
    else FAIL("reached unregistered backend");

    teardown_world();
}

/* === Entry point ============================================== */

int test_syscall_net_run(void) {
    printf("[test_syscall_net]\n");
    tests_run = 0;
    tests_passed = 0;

    test_socket_rejects_non_af_inet();
    test_socket_rejects_unknown_type();
    test_socket_allocates_fd_slot();
    test_bind_passes_through_to_backend();
    test_bind_rejects_short_addrlen();
    test_bind_rejects_non_socket_fd();
    test_connect_send_recv_round_trip();
    test_close_runs_socket_hook();
    test_no_backend_returns_minus_one();
    test_read_write_dispatch_to_socket();
    test_dns_resolve_hit();
    test_dns_resolve_miss();
    test_dns_resolve_validation();
    test_dns_resolve_no_backend();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
