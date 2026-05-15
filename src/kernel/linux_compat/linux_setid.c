#include "kernel/linux_compat/linux_setid.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

void linux_setid_reset_for_tests(void) {
    /* Marco M1 has no per-task credential storage yet. Reset is
     * a no-op today; the symbol exists so callers can invoke it
     * symmetrically with other linux_compat modules. */
}

/* Marco M1 fixed identity: root with no real/effective/saved
 * distinction. */
#define MARCO_M1_ROOT_UID 0u
#define MARCO_M1_ROOT_GID 0u

int64_t linux_setuid(uint32_t uid) {
    if (uid == MARCO_M1_ROOT_UID) return 0;
    return -LINUX_EPERM;
}

int64_t linux_setgid(uint32_t gid) {
    if (gid == MARCO_M1_ROOT_GID) return 0;
    return -LINUX_EPERM;
}

static int64_t setres_check(uint32_t v, uint32_t target) {
    if (v == LINUX_SETID_UID_NOCHANGE) return 0;
    if (v == target) return 0;
    return -LINUX_EPERM;
}

int64_t linux_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid) {
    int64_t rc;
    rc = setres_check(ruid, MARCO_M1_ROOT_UID); if (rc) return rc;
    rc = setres_check(euid, MARCO_M1_ROOT_UID); if (rc) return rc;
    rc = setres_check(suid, MARCO_M1_ROOT_UID); if (rc) return rc;
    return 0;
}

int64_t linux_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid) {
    int64_t rc;
    rc = setres_check(rgid, MARCO_M1_ROOT_GID); if (rc) return rc;
    rc = setres_check(egid, MARCO_M1_ROOT_GID); if (rc) return rc;
    rc = setres_check(sgid, MARCO_M1_ROOT_GID); if (rc) return rc;
    return 0;
}

int64_t linux_getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid) {
    if (!ruid || !euid || !suid) return -LINUX_EFAULT;
    *ruid = MARCO_M1_ROOT_UID;
    *euid = MARCO_M1_ROOT_UID;
    *suid = MARCO_M1_ROOT_UID;
    return 0;
}

int64_t linux_getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid) {
    if (!rgid || !egid || !sgid) return -LINUX_EFAULT;
    *rgid = MARCO_M1_ROOT_GID;
    *egid = MARCO_M1_ROOT_GID;
    *sgid = MARCO_M1_ROOT_GID;
    return 0;
}

static int64_t sys_setuid(const struct linux_syscall_args *a) {
    return linux_setuid((uint32_t)a->a0);
}
static int64_t sys_setgid(const struct linux_syscall_args *a) {
    return linux_setgid((uint32_t)a->a0);
}
static int64_t sys_setresuid(const struct linux_syscall_args *a) {
    return linux_setresuid((uint32_t)a->a0, (uint32_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_setresgid(const struct linux_syscall_args *a) {
    return linux_setresgid((uint32_t)a->a0, (uint32_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_getresuid(const struct linux_syscall_args *a) {
    return linux_getresuid((uint32_t *)(uintptr_t)a->a0,
                           (uint32_t *)(uintptr_t)a->a1,
                           (uint32_t *)(uintptr_t)a->a2);
}
static int64_t sys_getresgid(const struct linux_syscall_args *a) {
    return linux_getresgid((uint32_t *)(uintptr_t)a->a0,
                           (uint32_t *)(uintptr_t)a->a1,
                           (uint32_t *)(uintptr_t)a->a2);
}

void linux_setid_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_setuid,    sys_setuid);
    (void)linux_syscall_register(LINUX_NR_setgid,    sys_setgid);
    (void)linux_syscall_register(LINUX_NR_setresuid, sys_setresuid);
    (void)linux_syscall_register(LINUX_NR_setresgid, sys_setresgid);
    (void)linux_syscall_register(LINUX_NR_getresuid, sys_getresuid);
    (void)linux_syscall_register(LINUX_NR_getresgid, sys_getresgid);
}
