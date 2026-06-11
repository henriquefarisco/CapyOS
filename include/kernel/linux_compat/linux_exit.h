#ifndef KERNEL_LINUX_COMPAT_LINUX_EXIT_H
#define KERNEL_LINUX_COMPAT_LINUX_EXIT_H

/* Linux ABI `exit(2)` and `exit_group(2)`.
 *
 * `exit(status)` terminates the *calling thread*. In Linux
 * pre-NPTL semantics this also terminated the process; in
 * modern Linux + glibc/musl it only kills the thread, leaving
 * sibling threads alive. Since CapyOS has no thread groups yet
 * (S1.4 in `docs/archive/firefox-port-exploration/firefox-port-platform-shim.md`),
 * `exit` and
 * `exit_group` are equivalent for us today.
 *
 * `exit_group(status)` terminates *all threads* in the calling
 * thread's group. This is what musl calls when `main` returns
 * (the C runtime invokes `exit_group(rc)` internally) and what
 * we want as the canonical "process is done" syscall for Marco
 * M1.
 *
 * Both syscalls are noreturn from userland's perspective. The
 * kernel side must not return either: control transfers to the
 * scheduler which picks another task. We model this with an
 * injected callback `exit_fn` that the test binds to a stub
 * (which uses setjmp/longjmp or a flag) and that production
 * binds to `task_exit(code)`.
 *
 * Errno: Linux exit cannot fail in any meaningful way. The
 * status code's low byte is what `wait4` reports (`WEXITSTATUS`
 * extracts it). We pass the raw int through. */

#include <stdint.h>

struct linux_exit_ops {
    /* Terminate the current task with the given exit code.
     * Must NOT return. Production maps to `task_exit(code)`.
     * Tests map to a stub that records the code and longjmps
     * out so the test runner keeps going. */
    void (*exit_task)(int code);
};

void linux_exit_install_ops(const struct linux_exit_ops *ops);
void linux_exit_reset_for_tests(void);

/* Both calls invoke the same `exit_task` callback today (single-
 * threaded model). Marked noreturn so the compiler can elide
 * dead code after the dispatch site. The functions actually
 * return only when no callback is installed (test setups before
 * `_install_ops`); in that case they fall through with a sentinel
 * value of -LINUX_ENOSYS so tests can observe the missing-op
 * case explicitly.
 *
 * Tests that DO install an exit_fn must arrange for it to NOT
 * actually be noreturn (e.g. via setjmp/longjmp); the prototype
 * here keeps `__attribute__((noreturn))` off so test stubs link. */
int64_t linux_exit(int code);
int64_t linux_exit_group(int code);

void linux_exit_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_EXIT_H */
