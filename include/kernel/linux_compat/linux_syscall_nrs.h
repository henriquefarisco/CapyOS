#ifndef KERNEL_LINUX_COMPAT_LINUX_SYSCALL_NRS_H
#define KERNEL_LINUX_COMPAT_LINUX_SYSCALL_NRS_H

/* Linux x86_64 syscall numbers as exposed to userland under Strategy A.
 *
 * These are the canonical NR_<name> values from
 * `arch/x86/entry/syscalls/syscall_64.tbl` in linux-6.x. The kernel
 * does not own them (the userland binaries do), so changing any of
 * these breaks Firefox/SpiderMonkey ELFs built against
 * `--target=x86_64-unknown-linux-musl`.
 *
 * Coverage policy: this header lists the syscalls we plan to wire
 * through the platform shim (S1.x of
 * `docs/archive/firefox-port-exploration/firefox-port-platform-shim.md`).
 * Numbers we do not implement still live here (so the dispatcher can
 * return -ENOSYS instead of -EINVAL when Firefox tries them) but only
 * for the slices we expect to hit. The full Linux syscall table has
 * 400+ entries; we add as we need.
 *
 * Naming: prefix `LINUX_NR_<name>` to avoid collisions with the
 * CapyOS-native `SYS_<name>` numbers in
 * `include/kernel/syscall_numbers.h`. Both spaces are disjoint by
 * design (Linux ABI dispatch is a separate path from the native
 * dispatcher in `src/kernel/syscall.c`).
 *
 * Reference: https://filippo.io/linux-syscall-table/ ; cross-checked
 * against `arch/x86/entry/syscalls/syscall_64.tbl` upstream.
 */

#include <stdint.h>

/* --- File I/O ------------------------------------------------------ */
#define LINUX_NR_read                  0
#define LINUX_NR_write                 1
#define LINUX_NR_open                  2
#define LINUX_NR_close                 3
#define LINUX_NR_stat                  4
#define LINUX_NR_fstat                 5
#define LINUX_NR_lstat                 6
#define LINUX_NR_lseek                 8
#define LINUX_NR_pread64               17
#define LINUX_NR_pwrite64              18
#define LINUX_NR_readv                 19
#define LINUX_NR_writev                20
#define LINUX_NR_truncate              76
#define LINUX_NR_ftruncate             77
#define LINUX_NR_rename                82
#define LINUX_NR_mkdir                 83
#define LINUX_NR_rmdir                 84
#define LINUX_NR_unlink                87
#define LINUX_NR_mkdirat               258
#define LINUX_NR_renameat2             316
#define LINUX_NR_chmod                 90
#define LINUX_NR_fchmod                91
#define LINUX_NR_chown                 92
#define LINUX_NR_fchown                93
#define LINUX_NR_lchown                94
#define LINUX_NR_fchmodat              268
#define LINUX_NR_fchownat              260
#define LINUX_NR_link                  86
#define LINUX_NR_symlink               88
#define LINUX_NR_linkat                265
#define LINUX_NR_symlinkat             266
#define LINUX_NR_sync                  162
#define LINUX_NR_syncfs                306
#define LINUX_NR_fsync                 74
#define LINUX_NR_fdatasync             75
#define LINUX_NR_utime                 132
#define LINUX_NR_utimes                235
#define LINUX_NR_futimesat             261
#define LINUX_NR_utimensat             280
#define LINUX_NR_chdir                 80
#define LINUX_NR_fchdir                81
#define LINUX_NR_setxattr              188
#define LINUX_NR_lsetxattr             189
#define LINUX_NR_fsetxattr             190
#define LINUX_NR_getxattr              191
#define LINUX_NR_lgetxattr             192
#define LINUX_NR_fgetxattr             193
#define LINUX_NR_listxattr             194
#define LINUX_NR_llistxattr            195
#define LINUX_NR_flistxattr            196
#define LINUX_NR_removexattr           197
#define LINUX_NR_lremovexattr          198
#define LINUX_NR_fremovexattr          199
#define LINUX_NR_statfs                137
#define LINUX_NR_fstatfs               138
#define LINUX_NR_fadvise64             221
#define LINUX_NR_fallocate             285
#define LINUX_NR_sendfile              40
#define LINUX_NR_getrlimit             97
#define LINUX_NR_setrlimit             160
#define LINUX_NR_capget                125
#define LINUX_NR_capset                126
#define LINUX_NR_getitimer             36
#define LINUX_NR_alarm                 37
#define LINUX_NR_setitimer             38
#define LINUX_NR_times                 100
#define LINUX_NR_flock                 73
#define LINUX_NR_copy_file_range       326
#define LINUX_NR_sched_setparam        142
#define LINUX_NR_sched_getparam        143
#define LINUX_NR_sched_setscheduler    144
#define LINUX_NR_sched_getscheduler    145
#define LINUX_NR_sched_get_priority_max 146
#define LINUX_NR_sched_get_priority_min 147
#define LINUX_NR_timer_create          222
#define LINUX_NR_timer_settime         223
#define LINUX_NR_timer_gettime         224
#define LINUX_NR_timer_getoverrun      225
#define LINUX_NR_timer_delete          226
#define LINUX_NR_time                  201
#define LINUX_NR_getcpu                309
#define LINUX_NR_chroot                161
#define LINUX_NR_personality           135
#define LINUX_NR_setfsuid              122
#define LINUX_NR_setfsgid              123
#define LINUX_NR_mincore               27
#define LINUX_NR_set_mempolicy         238
#define LINUX_NR_get_mempolicy         239
#define LINUX_NR_mbind                 237
#define LINUX_NR_settimeofday          164
#define LINUX_NR_membarrier            324
#define LINUX_NR_userfaultfd           323
#define LINUX_NR_sched_rr_get_interval 148
#define LINUX_NR_unshare               272
#define LINUX_NR_mount                 165
#define LINUX_NR_umount2               166
#define LINUX_NR_execveat              322
#define LINUX_NR_close_range           436
#define LINUX_NR_splice                275
#define LINUX_NR_tee                   276
#define LINUX_NR_vmsplice              278
#define LINUX_NR_process_vm_readv      310
#define LINUX_NR_process_vm_writev     311
#define LINUX_NR_kcmp                  312
#define LINUX_NR_openat2               437
#define LINUX_NR_faccessat2            439
#define LINUX_NR_pkey_mprotect         329
#define LINUX_NR_pkey_alloc            330
#define LINUX_NR_pkey_free             331
#define LINUX_NR_landlock_create_ruleset 444
#define LINUX_NR_landlock_add_rule     445
#define LINUX_NR_landlock_restrict_self 446
#define LINUX_NR_seccomp               317
#define LINUX_NR_bpf                   321
#define LINUX_NR_ptrace                101
#define LINUX_NR_fanotify_init         300
#define LINUX_NR_fanotify_mark         301
#define LINUX_NR_io_uring_setup        425
#define LINUX_NR_io_uring_enter        426
#define LINUX_NR_io_uring_register     427
#define LINUX_NR_futex_waitv           449
#define LINUX_NR_clock_adjtime         305
#define LINUX_NR_memfd_secret          447
#define LINUX_NR_dup                   32
#define LINUX_NR_dup2                  33
#define LINUX_NR_pipe                  22
#define LINUX_NR_pipe2                 293
#define LINUX_NR_dup3                  292
#define LINUX_NR_openat                257
#define LINUX_NR_fstatat               262
#define LINUX_NR_unlinkat              263
#define LINUX_NR_renameat              264
#define LINUX_NR_fcntl                 72
#define LINUX_NR_ioctl                 16
#define LINUX_NR_getcwd                79
#define LINUX_NR_readlink              89
#define LINUX_NR_getdents64            217
#define LINUX_NR_readlinkat            267
#define LINUX_NR_statx                 332
#define LINUX_NR_access                21
#define LINUX_NR_faccessat             269
#define LINUX_NR_umask                 95

/* --- Memory -------------------------------------------------------- */
#define LINUX_NR_mmap                  9
#define LINUX_NR_mprotect              10
#define LINUX_NR_munmap                11
#define LINUX_NR_brk                   12
#define LINUX_NR_madvise               28
#define LINUX_NR_mremap                25
#define LINUX_NR_memfd_create          319
#define LINUX_NR_mlock                 149
#define LINUX_NR_munlock               150
#define LINUX_NR_mlockall              151
#define LINUX_NR_munlockall            152

/* --- Time ---------------------------------------------------------- */
#define LINUX_NR_clock_gettime         228
#define LINUX_NR_clock_getres          229
#define LINUX_NR_clock_nanosleep       230
#define LINUX_NR_nanosleep             35
#define LINUX_NR_gettimeofday          96

/* --- Process ------------------------------------------------------- */
#define LINUX_NR_clone                 56
#define LINUX_NR_clone3                435
#define LINUX_NR_fork                  57
#define LINUX_NR_vfork                 58
#define LINUX_NR_execve                59
#define LINUX_NR_exit                  60
#define LINUX_NR_wait4                 61
#define LINUX_NR_kill                  62
#define LINUX_NR_waitid                247
#define LINUX_NR_tkill                 200
#define LINUX_NR_getpid                39
#define LINUX_NR_gettid                186
#define LINUX_NR_getppid               110
#define LINUX_NR_setpgid               109
#define LINUX_NR_getpgrp               111
#define LINUX_NR_setsid                112
#define LINUX_NR_getpgid               121
#define LINUX_NR_getsid                124
#define LINUX_NR_getpriority           140
#define LINUX_NR_setpriority           141
#define LINUX_NR_getuid                102
#define LINUX_NR_getgid                104
#define LINUX_NR_geteuid               107
#define LINUX_NR_getegid               108
#define LINUX_NR_getgroups             115
#define LINUX_NR_setgroups             116
#define LINUX_NR_setuid                105
#define LINUX_NR_setgid                106
#define LINUX_NR_setresuid             117
#define LINUX_NR_getresuid             118
#define LINUX_NR_setresgid             119
#define LINUX_NR_getresgid             120
#define LINUX_NR_set_tid_address       218
#define LINUX_NR_set_robust_list       273
#define LINUX_NR_exit_group            231
#define LINUX_NR_prctl                 157
#define LINUX_NR_arch_prctl            158
#define LINUX_NR_prlimit64             302
#define LINUX_NR_getrusage             98
#define LINUX_NR_sched_yield           24
#define LINUX_NR_sched_getaffinity     204
#define LINUX_NR_sched_setaffinity     203

/* --- Signals ------------------------------------------------------- */
#define LINUX_NR_rt_sigaction          13
#define LINUX_NR_rt_sigprocmask        14
#define LINUX_NR_rt_sigreturn          15
#define LINUX_NR_sigaltstack           131
#define LINUX_NR_tgkill                234

/* --- Threading / sync --------------------------------------------- */
#define LINUX_NR_futex                 202

/* --- Networking ---------------------------------------------------- */
#define LINUX_NR_socket                41
#define LINUX_NR_connect               42
#define LINUX_NR_accept                43
#define LINUX_NR_accept4               288
#define LINUX_NR_sendto                44
#define LINUX_NR_recvfrom              45
#define LINUX_NR_sendmsg               46
#define LINUX_NR_recvmsg               47
#define LINUX_NR_shutdown              48
#define LINUX_NR_bind                  49
#define LINUX_NR_listen                50
#define LINUX_NR_getsockname           51
#define LINUX_NR_getpeername           52
#define LINUX_NR_socketpair            53
#define LINUX_NR_setsockopt            54
#define LINUX_NR_getsockopt            55
#define LINUX_NR_recvmmsg              299
#define LINUX_NR_sendmmsg              307

/* --- IPC ----------------------------------------------------------- */
#define LINUX_NR_eventfd               284
#define LINUX_NR_eventfd2              290
#define LINUX_NR_signalfd              282
#define LINUX_NR_signalfd4             289
#define LINUX_NR_timerfd_create        283
#define LINUX_NR_timerfd_settime       286
#define LINUX_NR_timerfd_gettime       287
#define LINUX_NR_epoll_create          213
#define LINUX_NR_epoll_create1         291
#define LINUX_NR_epoll_ctl             233
#define LINUX_NR_epoll_wait            232
#define LINUX_NR_epoll_pwait           281
#define LINUX_NR_inotify_init          253
#define LINUX_NR_inotify_init1         294
#define LINUX_NR_inotify_add_watch     254
#define LINUX_NR_inotify_rm_watch      255
#define LINUX_NR_pidfd_open            434
#define LINUX_NR_pidfd_send_signal     424

/* --- Random / system ---------------------------------------------- */
#define LINUX_NR_getrandom             318
#define LINUX_NR_uname                 63
#define LINUX_NR_sysinfo               99

/* Upper bound for the dispatcher table. The table is sparse: most
 * slots are NULL and the dispatcher returns -ENOSYS for those. We
 * size to the highest NR we use today plus a small headroom. Bump
 * when adding a higher-numbered syscall. */
#define LINUX_NR_MAX                   512

#endif /* KERNEL_LINUX_COMPAT_LINUX_SYSCALL_NRS_H */
