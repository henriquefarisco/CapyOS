#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

/* Sparse handler table. NULL == unregistered -> -ENOSYS. */
static linux_syscall_fn g_table[LINUX_NR_MAX];
static size_t           g_registered_count = 0;
static int              g_initialised = 0;

/* Per-module init hooks. Declared `weak` so test builds (which only
 * compile linux_syscall.c standalone) link without pulling every
 * module. The kernel build pulls each module via CAPYOS64_OBJS so
 * its `_register` symbol resolves to a real definition. Each hook
 * is responsible for calling `linux_syscall_register` for the NRs
 * it owns.
 *
 * IMPORTANT: when adding a new module, declare its hook here AND
 * call it from linux_syscall_init() below. This is the single
 * source of truth for "what is registered at boot". */
__attribute__((weak)) void linux_clock_register_syscalls(void)   {}
__attribute__((weak)) void linux_random_register_syscalls(void)  {}
__attribute__((weak)) void linux_process_register_syscalls(void) {}
__attribute__((weak)) void linux_fd_register_syscalls(void)      {}
__attribute__((weak)) void linux_mmap_register_syscalls(void)    {}
__attribute__((weak)) void linux_futex_register_syscalls(void)   {}
__attribute__((weak)) void linux_eventfd_register_syscalls(void) {}
__attribute__((weak)) void linux_net_register_syscalls(void)     {}
__attribute__((weak)) void linux_epoll_register_syscalls(void)   {}
__attribute__((weak)) void linux_signal_register_syscalls(void)  {}
__attribute__((weak)) void linux_memfd_register_syscalls(void)   {}
__attribute__((weak)) void linux_inotify_register_syscalls(void) {}
__attribute__((weak)) void linux_clone_register_syscalls(void)   {}
__attribute__((weak)) void linux_vfs_register_syscalls(void)     {}
__attribute__((weak)) void linux_brk_register_syscalls(void)     {}
__attribute__((weak)) void linux_arch_prctl_register_syscalls(void) {}
__attribute__((weak)) void linux_exit_register_syscalls(void)    {}
__attribute__((weak)) void linux_ioctl_register_syscalls(void)   {}
__attribute__((weak)) void linux_fcntl_register_syscalls(void)   {}
__attribute__((weak)) void linux_io_register_syscalls(void)      {}
__attribute__((weak)) void linux_stat_register_syscalls(void)    {}
__attribute__((weak)) void linux_path_register_syscalls(void)    {}
__attribute__((weak)) void linux_statx_register_syscalls(void)   {}
__attribute__((weak)) void linux_dirent_register_syscalls(void)  {}
__attribute__((weak)) void linux_at_register_syscalls(void)      {}
__attribute__((weak)) void linux_dup_register_syscalls(void)     {}
__attribute__((weak)) void linux_umask_register_syscalls(void)   {}
__attribute__((weak)) void linux_wait_register_syscalls(void)    {}
__attribute__((weak)) void linux_kill_register_syscalls(void)    {}
__attribute__((weak)) void linux_trunc_register_syscalls(void)   {}
__attribute__((weak)) void linux_sysinfo_register_syscalls(void) {}
__attribute__((weak)) void linux_priority_register_syscalls(void){}
__attribute__((weak)) void linux_pgrp_register_syscalls(void)    {}
__attribute__((weak)) void linux_fs_mut_register_syscalls(void)  {}
__attribute__((weak)) void linux_mlock_register_syscalls(void)   {}
__attribute__((weak)) void linux_creds_register_syscalls(void)   {}
__attribute__((weak)) void linux_fs_meta_register_syscalls(void) {}
__attribute__((weak)) void linux_link_register_syscalls(void)    {}
__attribute__((weak)) void linux_sync_register_syscalls(void)    {}
__attribute__((weak)) void linux_utime_register_syscalls(void)   {}
__attribute__((weak)) void linux_setid_register_syscalls(void)   {}
__attribute__((weak)) void linux_chdir_register_syscalls(void)   {}
__attribute__((weak)) void linux_xattr_register_syscalls(void)   {}
__attribute__((weak)) void linux_statfs_register_syscalls(void)  {}
__attribute__((weak)) void linux_advise_register_syscalls(void)  {}
__attribute__((weak)) void linux_rlimit_legacy_register_syscalls(void) {}
__attribute__((weak)) void linux_caps_register_syscalls(void)    {}
__attribute__((weak)) void linux_itimer_register_syscalls(void)  {}
__attribute__((weak)) void linux_lock_register_syscalls(void)    {}
__attribute__((weak)) void linux_sched_prio_register_syscalls(void) {}
__attribute__((weak)) void linux_posix_timer_register_syscalls(void) {}
__attribute__((weak)) void linux_time_legacy_register_syscalls(void) {}
__attribute__((weak)) void linux_sandbox_register_syscalls(void) {}
__attribute__((weak)) void linux_mincore_register_syscalls(void) {}
__attribute__((weak)) void linux_numa_register_syscalls(void)    {}
__attribute__((weak)) void linux_settod_register_syscalls(void)  {}
__attribute__((weak)) void linux_jit_aux_register_syscalls(void) {}
__attribute__((weak)) void linux_namespace_register_syscalls(void) {}
__attribute__((weak)) void linux_exec_ext_register_syscalls(void) {}
__attribute__((weak)) void linux_pipe_zero_register_syscalls(void) {}
__attribute__((weak)) void linux_proc_vm_register_syscalls(void)  {}
__attribute__((weak)) void linux_openat2_register_syscalls(void)  {}
__attribute__((weak)) void linux_pkey_register_syscalls(void)     {}
__attribute__((weak)) void linux_landlock_register_syscalls(void) {}
__attribute__((weak)) void linux_seccomp_register_syscalls(void)  {}
__attribute__((weak)) void linux_fanotify_register_syscalls(void) {}
__attribute__((weak)) void linux_io_uring_register_syscalls(void) {}
__attribute__((weak)) void linux_modern_misc_register_syscalls(void) {}

void linux_syscall_init(void) {
    if (g_initialised) return;

    for (size_t i = 0; i < LINUX_NR_MAX; i++) g_table[i] = NULL;
    g_registered_count = 0;

    linux_clock_register_syscalls();
    linux_random_register_syscalls();
    linux_process_register_syscalls();
    linux_fd_register_syscalls();
    linux_mmap_register_syscalls();
    linux_futex_register_syscalls();
    linux_eventfd_register_syscalls();
    linux_net_register_syscalls();
    linux_epoll_register_syscalls();
    linux_signal_register_syscalls();
    linux_memfd_register_syscalls();
    linux_inotify_register_syscalls();
    linux_clone_register_syscalls();
    linux_vfs_register_syscalls();
    linux_brk_register_syscalls();
    linux_arch_prctl_register_syscalls();
    linux_exit_register_syscalls();
    linux_ioctl_register_syscalls();
    linux_fcntl_register_syscalls();
    linux_io_register_syscalls();
    linux_stat_register_syscalls();
    linux_path_register_syscalls();
    linux_statx_register_syscalls();
    linux_dirent_register_syscalls();
    linux_at_register_syscalls();
    linux_dup_register_syscalls();
    linux_umask_register_syscalls();
    linux_wait_register_syscalls();
    linux_kill_register_syscalls();
    linux_trunc_register_syscalls();
    linux_sysinfo_register_syscalls();
    linux_priority_register_syscalls();
    linux_pgrp_register_syscalls();
    linux_fs_mut_register_syscalls();
    linux_mlock_register_syscalls();
    linux_creds_register_syscalls();
    linux_fs_meta_register_syscalls();
    linux_link_register_syscalls();
    linux_sync_register_syscalls();
    linux_utime_register_syscalls();
    linux_setid_register_syscalls();
    linux_chdir_register_syscalls();
    linux_xattr_register_syscalls();
    linux_statfs_register_syscalls();
    linux_advise_register_syscalls();
    linux_rlimit_legacy_register_syscalls();
    linux_caps_register_syscalls();
    linux_itimer_register_syscalls();
    linux_lock_register_syscalls();
    linux_sched_prio_register_syscalls();
    linux_posix_timer_register_syscalls();
    linux_time_legacy_register_syscalls();
    linux_sandbox_register_syscalls();
    linux_mincore_register_syscalls();
    linux_numa_register_syscalls();
    linux_settod_register_syscalls();
    linux_jit_aux_register_syscalls();
    linux_namespace_register_syscalls();
    linux_exec_ext_register_syscalls();
    linux_pipe_zero_register_syscalls();
    linux_proc_vm_register_syscalls();
    linux_openat2_register_syscalls();
    linux_pkey_register_syscalls();
    linux_landlock_register_syscalls();
    linux_seccomp_register_syscalls();
    linux_fanotify_register_syscalls();
    linux_io_uring_register_syscalls();
    linux_modern_misc_register_syscalls();

    g_initialised = 1;
}

int linux_syscall_register(uint32_t nr, linux_syscall_fn handler) {
    if (nr >= LINUX_NR_MAX) return -1;
    if (!handler)           return -1;
    if (g_table[nr] != NULL) return -1; /* already registered: refuse */
    g_table[nr] = handler;
    g_registered_count++;
    return 0;
}

linux_syscall_fn linux_syscall_lookup(uint32_t nr) {
    if (nr >= LINUX_NR_MAX) return NULL;
    return g_table[nr];
}

size_t linux_syscall_registered_count(void) {
    return g_registered_count;
}

int64_t linux_syscall_dispatch(uint32_t nr,
                               const struct linux_syscall_args *args) {
    if (!args) return -LINUX_EFAULT;
    if (nr >= LINUX_NR_MAX) return -LINUX_ENOSYS;
    linux_syscall_fn h = g_table[nr];
    if (!h) return -LINUX_ENOSYS;
    return h(args);
}

void linux_syscall_reset_for_tests(void) {
    for (size_t i = 0; i < LINUX_NR_MAX; i++) g_table[i] = NULL;
    g_registered_count = 0;
    g_initialised = 0;
}
