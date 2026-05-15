#include "kernel/linux_compat/linux_tmpfs.h"

/* Boot wiring for `linux_tmpfs`. Excluded from host tests.
 *
 * The module owns its own state; no external callbacks are
 * needed (unlike linux_procfs which sources kernel state via
 * providers). All we do at boot is reset the slot table so
 * the kernel starts with an empty `/tmp/`.
 */

#if !defined(UNIT_TEST)

void linux_tmpfs_init_boot(void) {
    linux_tmpfs_reset_for_tests();
}

#endif /* !UNIT_TEST */
