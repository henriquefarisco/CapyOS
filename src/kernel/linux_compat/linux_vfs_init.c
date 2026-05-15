#include "kernel/linux_compat/linux_vfs.h"
#include "kernel/linux_compat/linux_vfs_router.h"

/* Boot wiring for `linux_vfs`. Excluded from host tests.
 *
 * Installs the routing layer (`linux_vfs_router`) which
 * dispatches syscalls to the concrete backends:
 *
 *   /dev/{null,zero,full,urandom,random}  -> linux_devfs
 *   /dev/shm/<name>                       -> linux_shm
 *
 * Future prefixes (capyfs, /tmp tmpfs, /proc) plug into the
 * router, not into this trampoline.
 */

#if !defined(UNIT_TEST)

void linux_vfs_init_boot(void) {
    linux_vfs_router_install();
}

#endif /* !UNIT_TEST */
