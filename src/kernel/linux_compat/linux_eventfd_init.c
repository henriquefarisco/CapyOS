#include "kernel/linux_compat/linux_eventfd.h"

/* Boot wiring for `linux_eventfd`. Excluded from host tests.
 *
 * For Marco M1 we do not yet have a real fd table; the shim
 * itself returns slot+LINUX_EVENTFD_FD_BASE as the fd. The
 * `alloc_fd` callback is left NULL so the default path applies.
 *
 * The `now_ns` oracle for timerfd is wired to
 * linux_clock_gettime(CLOCK_MONOTONIC) so timers fire against
 * the same timeline as clock_gettime callers.
 */

#if !defined(UNIT_TEST)

#include "kernel/linux_compat/linux_clock.h"
#include "kernel/linux_compat/linux_types.h"

#include <stdint.h>

static uint64_t wrap_now_ns(void) {
    struct linux_timespec ts;
    if (linux_clock_gettime(LINUX_CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void linux_eventfd_init_boot(void) {
    linux_eventfd_install_ops(NULL);
    linux_eventfd_install_now_ns(wrap_now_ns);
}

#endif /* !UNIT_TEST */
