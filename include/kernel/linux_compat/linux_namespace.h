#ifndef KERNEL_LINUX_COMPAT_LINUX_NAMESPACE_H
#define KERNEL_LINUX_COMPAT_LINUX_NAMESPACE_H

/* Linux ABI namespace + mount syscalls.
 *
 *   int unshare(int flags);
 *   int mount  (const char *source, const char *target,
 *                const char *fstype, unsigned long flags,
 *                const void *data);
 *   int umount2(const char *target, int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox content sandbox calls unshare(CLONE_NEWUSER |
 *     CLONE_NEWNET | CLONE_NEWIPC) to isolate the renderer
 *     before exec'ing it; -ENOSYS makes the sandbox layer
 *     fail-closed and refuse to spawn content processes.
 *   - bubblewrap + flatpak rely on unshare to establish their
 *     mount namespaces; without it, the Firefox flatpak doesn't
 *     boot.
 *   - mount/umount2 are used by bubblewrap to set up the
 *     sandbox's tmpfs+bind-mount layout. Most are stubs we
 *     accept-as-success in the single-process Marco M1 world.
 *
 * Linux semantics:
 *   - unshare flags must be a subset of known CLONE_* bits
 *     that map to namespaces. Unknown bits -> -EINVAL.
 *   - mount accepts a wide variety of fstype strings; for
 *     Marco M1 we accept the strings Firefox actually issues
 *     (tmpfs, proc, devpts, none) and reject others with
 *     -ENODEV (Linux behaviour for unknown fs).
 *   - umount2 flag whitelist: MNT_FORCE | MNT_DETACH |
 *     MNT_EXPIRE | UMOUNT_NOFOLLOW. Other bits -> -EINVAL.
 *
 * Marco M1 has no real namespace machinery; we accept the
 * calls structurally so Firefox sandbox's "did the kernel
 * support what I asked for?" probe takes its happy path. */

#include <stdint.h>
#include <stddef.h>

/* CLONE_* bits that unshare understands, from sched.h. */
#define LINUX_CLONE_NEWNS         0x00020000
#define LINUX_CLONE_NEWCGROUP     0x02000000
#define LINUX_CLONE_NEWUTS        0x04000000
#define LINUX_CLONE_NEWIPC        0x08000000
#define LINUX_CLONE_NEWUSER       0x10000000
#define LINUX_CLONE_NEWPID        0x20000000
#define LINUX_CLONE_NEWNET        0x40000000
#define LINUX_CLONE_NEWTIME       0x00000080
#define LINUX_CLONE_FILES         0x00000400
#define LINUX_CLONE_FS            0x00000200
#define LINUX_CLONE_SIGHAND       0x00000800
#define LINUX_CLONE_SYSVSEM       0x00040000
#define LINUX_CLONE_THREAD        0x00010000
#define LINUX_CLONE_VM            0x00000100

#define LINUX_UNSHARE_KNOWN_FLAGS \
    (LINUX_CLONE_NEWNS | LINUX_CLONE_NEWCGROUP | LINUX_CLONE_NEWUTS | \
     LINUX_CLONE_NEWIPC | LINUX_CLONE_NEWUSER | LINUX_CLONE_NEWPID | \
     LINUX_CLONE_NEWNET | LINUX_CLONE_NEWTIME | LINUX_CLONE_FILES | \
     LINUX_CLONE_FS | LINUX_CLONE_SYSVSEM | LINUX_CLONE_THREAD | \
     LINUX_CLONE_SIGHAND | LINUX_CLONE_VM)

/* mount(2) flags subset (from include/uapi/linux/mount.h). */
#define LINUX_MS_RDONLY      0x00000001
#define LINUX_MS_NOSUID      0x00000002
#define LINUX_MS_NODEV       0x00000004
#define LINUX_MS_NOEXEC      0x00000008
#define LINUX_MS_SYNCHRONOUS 0x00000010
#define LINUX_MS_REMOUNT     0x00000020
#define LINUX_MS_MANDLOCK    0x00000040
#define LINUX_MS_DIRSYNC     0x00000080
#define LINUX_MS_NOSYMFOLLOW 0x00000100
#define LINUX_MS_NOATIME     0x00000400
#define LINUX_MS_NODIRATIME  0x00000800
#define LINUX_MS_BIND        0x00001000
#define LINUX_MS_MOVE        0x00002000
#define LINUX_MS_REC         0x00004000
#define LINUX_MS_SILENT      0x00008000
#define LINUX_MS_PRIVATE     0x00040000
#define LINUX_MS_SLAVE       0x00080000
#define LINUX_MS_SHARED      0x00100000
#define LINUX_MS_RELATIME    0x00200000
#define LINUX_MS_STRICTATIME 0x01000000
#define LINUX_MS_LAZYTIME    0x02000000

#define LINUX_MS_KNOWN_FLAGS                        \
    (LINUX_MS_RDONLY | LINUX_MS_NOSUID | LINUX_MS_NODEV |   \
     LINUX_MS_NOEXEC | LINUX_MS_SYNCHRONOUS | LINUX_MS_REMOUNT | \
     LINUX_MS_MANDLOCK | LINUX_MS_DIRSYNC | LINUX_MS_NOSYMFOLLOW | \
     LINUX_MS_NOATIME | LINUX_MS_NODIRATIME | LINUX_MS_BIND | \
     LINUX_MS_MOVE | LINUX_MS_REC | LINUX_MS_SILENT | \
     LINUX_MS_PRIVATE | LINUX_MS_SLAVE | LINUX_MS_SHARED | \
     LINUX_MS_RELATIME | LINUX_MS_STRICTATIME | LINUX_MS_LAZYTIME)

/* umount2 flags. */
#define LINUX_MNT_FORCE          0x00000001
#define LINUX_MNT_DETACH         0x00000002
#define LINUX_MNT_EXPIRE         0x00000004
#define LINUX_UMOUNT_NOFOLLOW    0x00000008

#define LINUX_UMOUNT_KNOWN_FLAGS \
    (LINUX_MNT_FORCE | LINUX_MNT_DETACH | \
     LINUX_MNT_EXPIRE | LINUX_UMOUNT_NOFOLLOW)

int64_t linux_unshare(int flags);
int64_t linux_mount  (const char *source, const char *target,
                      const char *fstype, uint64_t flags,
                      const void *data);
int64_t linux_umount2(const char *target, int flags);

void linux_namespace_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_NAMESPACE_H */
