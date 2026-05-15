#include "kernel/linux_compat/linux_xattr.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static size_t bounded_strlen(const char *s, size_t max) {
    size_t i = 0;
    while (i < max && s[i]) i++;
    return i;
}

static int64_t validate_path(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    return 0;
}

static int64_t validate_name(const char *name) {
    if (!name) return -LINUX_EFAULT;
    /* Linux: empty xattr name -> -ERANGE per kernel
     * fs/xattr.c. */
    if (name[0] == '\0') return -LINUX_ERANGE;
    size_t n = bounded_strlen(name, LINUX_XATTR_NAME_MAX + 1);
    if (n > LINUX_XATTR_NAME_MAX) return -LINUX_ENAMETOOLONG;
    return 0;
}

static int64_t set_common(const char *name, const void *value,
                          size_t size, int flags) {
    int64_t rc = validate_name(name);
    if (rc) return rc;
    if (size > LINUX_XATTR_SIZE_MAX) return -LINUX_E2BIG;
    /* Linux: NULL value with size > 0 is -EFAULT. With size == 0
     * the value pointer is irrelevant. */
    if (size > 0 && !value) return -LINUX_EFAULT;
    if ((unsigned)flags & ~(unsigned)LINUX_XATTR_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    /* Marco M1: no xattr storage. Filesystems that don't support
     * xattrs return -ENOTSUP from setxattr per Linux convention
     * (musl cp -a, selinux probes etc. all handle this). */
    return -LINUX_EOPNOTSUPP;
}

int64_t linux_setxattr(const char *path, const char *name,
                       const void *value, size_t size, int flags) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return set_common(name, value, size, flags);
}

int64_t linux_lsetxattr(const char *path, const char *name,
                        const void *value, size_t size, int flags) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return set_common(name, value, size, flags);
}

int64_t linux_fsetxattr(int fd, const char *name,
                        const void *value, size_t size, int flags) {
    if (fd < 0) return -LINUX_EBADF;
    return set_common(name, value, size, flags);
}

static int64_t get_common(const char *name, void *value, size_t size) {
    int64_t rc = validate_name(name);
    if (rc) return rc;
    /* Linux: if size > 0 and value is NULL, -EFAULT. With
     * size == 0 we are just probing for the attribute size. */
    if (size > 0 && !value) return -LINUX_EFAULT;
    /* Marco M1: no xattr storage. We don't know the attribute,
     * so return -ENODATA (no such attribute). Linux returns this
     * even on filesystems that fully support xattrs when the
     * specific attribute is missing. */
    return -LINUX_ENODATA;
}

int64_t linux_getxattr(const char *path, const char *name,
                       void *value, size_t size) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return get_common(name, value, size);
}

int64_t linux_lgetxattr(const char *path, const char *name,
                        void *value, size_t size) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return get_common(name, value, size);
}

int64_t linux_fgetxattr(int fd, const char *name,
                        void *value, size_t size) {
    if (fd < 0) return -LINUX_EBADF;
    return get_common(name, value, size);
}

static int64_t list_common(char *list, size_t size) {
    if (size > 0 && !list) return -LINUX_EFAULT;
    /* Marco M1: zero attributes. listxattr returns 0 (no bytes
     * written to buffer). userland that calls with size == 0
     * gets the same answer (0 attributes total). */
    return 0;
}

int64_t linux_listxattr(const char *path, char *list, size_t size) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return list_common(list, size);
}

int64_t linux_llistxattr(const char *path, char *list, size_t size) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return list_common(list, size);
}

int64_t linux_flistxattr(int fd, char *list, size_t size) {
    if (fd < 0) return -LINUX_EBADF;
    return list_common(list, size);
}

static int64_t remove_common(const char *name) {
    int64_t rc = validate_name(name);
    if (rc) return rc;
    /* Marco M1: no xattr storage; the named attribute can never
     * have been set, so removexattr returns -ENODATA per Linux
     * fs/xattr.c. */
    return -LINUX_ENODATA;
}

int64_t linux_removexattr(const char *path, const char *name) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return remove_common(name);
}

int64_t linux_lremovexattr(const char *path, const char *name) {
    int64_t rc = validate_path(path);
    if (rc) return rc;
    return remove_common(name);
}

int64_t linux_fremovexattr(int fd, const char *name) {
    if (fd < 0) return -LINUX_EBADF;
    return remove_common(name);
}

/* --- syscall adapters ------------------------------------------- */

#define ARG_PATH(i)  ((const char *)(uintptr_t)a->a##i)
#define ARG_NAME(i)  ((const char *)(uintptr_t)a->a##i)
#define ARG_VAL(i)   ((const void *)(uintptr_t)a->a##i)
#define ARG_LIST(i)  ((char *)(uintptr_t)a->a##i)
#define ARG_BUF(i)   ((void *)(uintptr_t)a->a##i)

static int64_t sys_setxattr(const struct linux_syscall_args *a) {
    return linux_setxattr(ARG_PATH(0), ARG_NAME(1), ARG_VAL(2),
                          (size_t)a->a3, (int)a->a4);
}
static int64_t sys_lsetxattr(const struct linux_syscall_args *a) {
    return linux_lsetxattr(ARG_PATH(0), ARG_NAME(1), ARG_VAL(2),
                           (size_t)a->a3, (int)a->a4);
}
static int64_t sys_fsetxattr(const struct linux_syscall_args *a) {
    return linux_fsetxattr((int)a->a0, ARG_NAME(1), ARG_VAL(2),
                           (size_t)a->a3, (int)a->a4);
}
static int64_t sys_getxattr(const struct linux_syscall_args *a) {
    return linux_getxattr(ARG_PATH(0), ARG_NAME(1),
                          ARG_BUF(2), (size_t)a->a3);
}
static int64_t sys_lgetxattr(const struct linux_syscall_args *a) {
    return linux_lgetxattr(ARG_PATH(0), ARG_NAME(1),
                           ARG_BUF(2), (size_t)a->a3);
}
static int64_t sys_fgetxattr(const struct linux_syscall_args *a) {
    return linux_fgetxattr((int)a->a0, ARG_NAME(1),
                           ARG_BUF(2), (size_t)a->a3);
}
static int64_t sys_listxattr(const struct linux_syscall_args *a) {
    return linux_listxattr(ARG_PATH(0), ARG_LIST(1), (size_t)a->a2);
}
static int64_t sys_llistxattr(const struct linux_syscall_args *a) {
    return linux_llistxattr(ARG_PATH(0), ARG_LIST(1), (size_t)a->a2);
}
static int64_t sys_flistxattr(const struct linux_syscall_args *a) {
    return linux_flistxattr((int)a->a0, ARG_LIST(1), (size_t)a->a2);
}
static int64_t sys_removexattr(const struct linux_syscall_args *a) {
    return linux_removexattr(ARG_PATH(0), ARG_NAME(1));
}
static int64_t sys_lremovexattr(const struct linux_syscall_args *a) {
    return linux_lremovexattr(ARG_PATH(0), ARG_NAME(1));
}
static int64_t sys_fremovexattr(const struct linux_syscall_args *a) {
    return linux_fremovexattr((int)a->a0, ARG_NAME(1));
}

void linux_xattr_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_setxattr,     sys_setxattr);
    (void)linux_syscall_register(LINUX_NR_lsetxattr,    sys_lsetxattr);
    (void)linux_syscall_register(LINUX_NR_fsetxattr,    sys_fsetxattr);
    (void)linux_syscall_register(LINUX_NR_getxattr,     sys_getxattr);
    (void)linux_syscall_register(LINUX_NR_lgetxattr,    sys_lgetxattr);
    (void)linux_syscall_register(LINUX_NR_fgetxattr,    sys_fgetxattr);
    (void)linux_syscall_register(LINUX_NR_listxattr,    sys_listxattr);
    (void)linux_syscall_register(LINUX_NR_llistxattr,   sys_llistxattr);
    (void)linux_syscall_register(LINUX_NR_flistxattr,   sys_flistxattr);
    (void)linux_syscall_register(LINUX_NR_removexattr,  sys_removexattr);
    (void)linux_syscall_register(LINUX_NR_lremovexattr, sys_lremovexattr);
    (void)linux_syscall_register(LINUX_NR_fremovexattr, sys_fremovexattr);
}
