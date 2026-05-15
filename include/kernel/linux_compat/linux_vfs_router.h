#ifndef KERNEL_LINUX_COMPAT_LINUX_VFS_ROUTER_H
#define KERNEL_LINUX_COMPAT_LINUX_VFS_ROUTER_H

/* Routing layer that connects `linux_vfs` (the syscall front
 * door for open/close/read/write/lseek) to the concrete shim
 * backends already in the tree:
 *
 *   /dev/{null,zero,full,urandom,random}  -> linux_devfs
 *   /dev/shm/<name>                       -> linux_shm
 *   /proc/{cpuinfo,meminfo,self/...}      -> linux_procfs
 *   /tmp/<name>                           -> linux_tmpfs
 *
 * Why a separate file rather than putting the dispatch table
 * directly in `linux_vfs.c`:
 *   - `linux_vfs.c` is the syscall front door; it should not
 *     know which backends exist. Adding new prefixes (e.g.
 *     `/tmp/...` for tmpfs, `/proc/...` for proc) means
 *     touching only this router.
 *   - Tests can drive the router with the real backends
 *     instead of fakes, validating end-to-end behaviour.
 *   - Keeps the front door simple (NULL ops -> ENOSYS still
 *     works in early-boot scenarios).
 *
 * The router owns no state: every operation is a stateless
 * dispatch driven by path prefix (open) or fd range
 * (close/read/write/lseek). State lives in the backends.
 *
 * Usage:
 *
 *   linux_vfs_router_install();
 *
 * After this call, `linux_vfs_open("/dev/urandom", O_RDONLY,0)`
 * routes to `linux_devfs_open`, etc. The function is idempotent
 * (re-installs the same ops bundle).
 */

void linux_vfs_router_install(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_VFS_ROUTER_H */
