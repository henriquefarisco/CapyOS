# musl-on-CapyOS Compatibility Matrix

This document tracks the alignment between the syscalls musl
issues and the syscall surface CapyOS provides via
`linux_compat`. **252 Linux NRs** are defined in
`include/kernel/linux_compat/linux_syscall_nrs.h` (sessão 37;
`LINUX_NR_MAX` is a table-size sentinel and is not counted).

The matrix below classifies each NR by **operational state**:

- **WIRED**   syscall handler registered, returns real result
- **STUB**    syscall handler registered, returns -ENOSYS or
              partial implementation that musl tolerates
- **MISSING** no handler; syscall returns -ENOSYS via the
              dispatcher's default

## Required for musl bring-up (Marco M1)

These are the NRs musl issues during static `libc.a` start-up
or basic `printf("hello\n")`. All must be at least STUB before
musl can boot. ✅ = available today.

| NR  | Name                | State        | Notes                  |
|-----|---------------------|--------------|------------------------|
| 0   | `read`              | ✅ WIRED     | linux_vfs (provider hygiene) |
| 1   | `write`             | ✅ WIRED     | linux_vfs (provider hygiene) |
| 2   | `open`              | ✅ WIRED     | linux_vfs router (provider hygiene) |
| 3   | `close`             | ✅ WIRED     | linux_vfs (provider hygiene) |
| 8   | `lseek`             | ✅ WIRED     | linux_vfs (provider hygiene) |
| 4   | `stat`              | ✅ STUB      | known pseudo paths; unknown -> -ENOSYS |
| 5   | `fstat`             | ✅ WIRED     | linux_stat (synthetic) |
| 6   | `lstat`             | ✅ STUB      | known pseudo paths; `/proc/self/exe` -> S_IFLNK |
| 17  | `pread64`           | ✅ WIRED     | linux_io (seek+rw)     |
| 18  | `pwrite64`          | ✅ WIRED     | linux_io (seek+rw)     |
| 19  | `readv`             | ✅ WIRED     | linux_io (scatter)     |
| 20  | `writev`            | ✅ WIRED     | linux_io (gather)      |
| 9   | `mmap`              | ✅ WIRED     | linux_mmap (provider hygiene) |
| 10  | `mprotect`          | ✅ WIRED     | linux_mmap (provider hygiene) |
| 11  | `munmap`            | ✅ WIRED     | linux_mmap (provider hygiene) |
| 12  | `brk`               | ✅ WIRED     | linux_brk (provider hygiene; 256 MiB heap)|
| 13  | `rt_sigaction`      | ✅ STUB      | linux_signal storage   |
| 14  | `rt_sigprocmask`    | ✅ STUB      | linux_signal storage   |
| 16  | `ioctl`             | ✅ WIRED     | linux_ioctl (-ENOTTY)  |
| 22  | `pipe`              | ✅ WIRED     | linux_fd -> pipe2(flags=0) |
| 24  | `sched_yield`       | ✅ WIRED     | linux_process          |
| 28  | `madvise`           | ✅ WIRED     | linux_mmap             |
| 35  | `nanosleep`         | ✅ WIRED     | linux_clock spin-wait  |
| 39  | `getpid`            | ✅ WIRED     | linux_process          |
| 56  | `clone`             | ✅ STUB      | -ENOSYS until S1.4     |
| 60  | `exit`              | ✅ WIRED     | linux_exit (provider hygiene) |
| 63  | `uname`             | ✅ WIRED     | linux_process utsname  |
| 72  | `fcntl`             | ✅ WIRED     | linux_fcntl (subset)   |
| 96  | `gettimeofday`      | ✅ WIRED     | linux_clock            |
| 102 | `getuid`            | ✅ WIRED     | linux_process (root=0) |
| 110 | `getppid`           | ✅ WIRED     | linux_process (init=1) |
| 104 | `getgid`            | ✅ WIRED     | linux_process          |
| 107 | `geteuid`           | ✅ WIRED     | linux_process          |
| 108 | `getegid`           | ✅ WIRED     | linux_process          |
| 158 | `arch_prctl`        | ✅ WIRED     | TLS setup (FS/GS MSRs; provider hygiene) |
| 186 | `gettid`            | ✅ WIRED     | linux_process (callback hygiene) |
| 202 | `futex`             | ✅ WIRED     | linux_futex (provider hygiene) |
| 218 | `set_tid_address`   | ✅ WIRED     | linux_process          |
| 228 | `clock_gettime`     | ✅ WIRED     | linux_clock            |
| 231 | `exit_group`        | ✅ WIRED     | linux_exit (provider hygiene; = exit) |
| 257 | `openat`            | ✅ WIRED     | linux_vfs (AT_FDCWD; provider hygiene) |
| 79  | `getcwd`            | ✅ STUB      | linux_path -> "/"      |
| 89  | `readlink`          | ✅ STUB      | linux_path /proc/self/exe via provider hygiene |
| 217 | `getdents64`        | ✅ STUB      | linux_dirent (always EOF) |
| 267 | `readlinkat`        | ✅ STUB      | linux_path AT_FDCWD -> readlink (provider hygiene) |
| 332 | `statx`             | ✅ WIRED     | fstat + known path projection |
| 291 | `epoll_create1`     | ✅ WIRED     | linux_epoll (provider hygiene; fd table) |
| 233 | `epoll_ctl`         | ✅ WIRED     | linux_epoll (interest list) |
| 232 | `epoll_wait`        | ✅ WIRED     | linux_epoll (eventfd/timerfd/pipe readiness) |
| 281 | `epoll_pwait`       | ✅ WIRED     | linux_epoll (eventfd/timerfd/pipe readiness; sigmask validated) |
| 284 | `eventfd`           | ✅ WIRED     | linux_eventfd (provider hygiene; -> eventfd2) |
| 290 | `eventfd2`          | ✅ WIRED     | linux_eventfd (provider hygiene; counter fd) |
| 289 | `signalfd4`         | ✅ WIRED     | storage fd, read -> EAGAIN until delivery |
| 283 | `timerfd_create`    | ✅ WIRED     | timerfd fd + VFS/epoll readiness |
| 286 | `timerfd_settime`   | ✅ WIRED     | one-shot/periodic timerfd |
| 287 | `timerfd_gettime`   | ✅ WIRED     | timerfd remaining time |
| 21  | `access`            | ✅ STUB      | linux_at known pseudo paths |
| 269 | `faccessat`         | ✅ STUB      | AT_FDCWD -> known-path access |
| 262 | `fstatat`           | ✅ WIRED     | AT_EMPTY_PATH + known AT_FDCWD paths |
| 32  | `dup`               | ✅ STUB      | linux_dup ENOSYS (callback hygiene) |
| 33  | `dup2`              | ✅ STUB      | linux_dup oldfd==newfd functional |
| 95  | `umask`             | ✅ WIRED     | linux_umask (9-bit mask) |
| 61  | `wait4`             | ✅ STUB      | linux_wait -> -ECHILD (no children) |
| 247 | `waitid`            | ✅ STUB      | linux_wait -> -ECHILD |
| 62  | `kill`              | ✅ STUB      | linux_kill (provider hygiene; self/pgrp/broadcast) |
| 234 | `tgkill`            | ✅ STUB      | linux_kill (provider hygiene; self functional) |
| 200 | `tkill`             | ✅ STUB      | linux_kill (provider hygiene; self functional) |
| 76  | `truncate`          | ✅ STUB      | linux_trunc (provider hygiene; path -ENOSYS) |
| 77  | `ftruncate`         | ✅ STUB      | linux_trunc (provider hygiene; fd resize hook) |
| 230 | `clock_nanosleep`   | ✅ WIRED     | linux_clock per-clock spin-wait + ABSTIME |
| 99  | `sysinfo`           | ✅ STUB      | linux_sysinfo (provider hygiene; providers injectable) |
| 98  | `getrusage`         | ✅ STUB      | linux_sysinfo (zero-filled, valid `who`) |
| 140 | `getpriority`       | ✅ WIRED     | linux_priority (encoded 20 - nice) |
| 141 | `setpriority`       | ✅ WIRED     | linux_priority (clamps NICE_MIN..MAX) |
| 109 | `setpgid`           | ✅ STUB      | linux_pgrp (provider hygiene; self functional) |
| 121 | `getpgid`           | ✅ STUB      | linux_pgrp (provider hygiene; self) |
| 111 | `getpgrp`           | ✅ WIRED     | linux_pgrp (provider hygiene) |
| 112 | `setsid`            | ✅ STUB      | linux_pgrp (provider hygiene; first ok) |
| 124 | `getsid`            | ✅ STUB      | linux_pgrp (provider hygiene; self) |
| 83  | `mkdir`             | ✅ STUB      | linux_fs_mut (provider hygiene; provider injection) |
| 258 | `mkdirat`           | ✅ STUB      | linux_fs_mut (provider hygiene; AT_FDCWD -> mkdir) |
| 84  | `rmdir`             | ✅ STUB      | linux_fs_mut (provider hygiene; provider injection) |
| 87  | `unlink`            | ✅ STUB      | linux_fs_mut (provider hygiene; provider injection) |
| 263 | `unlinkat`          | ✅ STUB      | linux_fs_mut (provider hygiene; AT_REMOVEDIR routing) |
| 82  | `rename`            | ✅ STUB      | linux_fs_mut (provider hygiene; -> renameat2) |
| 264 | `renameat`          | ✅ STUB      | linux_fs_mut (provider hygiene; -> renameat2) |
| 316 | `renameat2`         | ✅ STUB      | linux_fs_mut (provider hygiene; NOREPLACE/EXCHANGE/WHITEOUT) |
| 149 | `mlock`             | ✅ WIRED     | linux_mlock (no swap; pinned) |
| 150 | `munlock`           | ✅ WIRED     | linux_mlock |
| 151 | `mlockall`          | ✅ WIRED     | linux_mlock (MCL flag validation) |
| 152 | `munlockall`        | ✅ WIRED     | linux_mlock |
| 115 | `getgroups`         | ✅ STUB      | linux_creds (zero supplementary groups) |
| 116 | `setgroups`         | ✅ STUB      | linux_creds (no-op success) |
| 90  | `chmod`             | ✅ STUB      | linux_fs_meta (provider hygiene; mode clamped 07777) |
| 91  | `fchmod`            | ✅ STUB      | linux_fs_meta (provider hygiene; fd-based provider) |
| 268 | `fchmodat`          | ✅ STUB      | linux_fs_meta (provider hygiene; AT_FDCWD; flags=0 only) |
| 92  | `chown`             | ✅ STUB      | linux_fs_meta (provider hygiene; follow=1) |
| 93  | `fchown`            | ✅ STUB      | linux_fs_meta (provider hygiene; fd-based provider) |
| 94  | `lchown`            | ✅ STUB      | linux_fs_meta (provider hygiene; follow=0) |
| 260 | `fchownat`          | ✅ STUB      | linux_fs_meta (provider hygiene; NOFOLLOW/EMPTY_PATH validated) |
| 86  | `link`              | ✅ STUB      | linux_link (provider hygiene; -> linkat) |
| 265 | `linkat`            | ✅ STUB      | linux_link (provider hygiene; AT_SYMLINK_FOLLOW honored) |
| 88  | `symlink`           | ✅ STUB      | linux_link (provider hygiene; -> symlinkat) |
| 266 | `symlinkat`         | ✅ STUB      | linux_link (provider hygiene) |
| 162 | `sync`              | ✅ WIRED     | linux_sync (provider hygiene; RAM-only no-op) |
| 306 | `syncfs`            | ✅ WIRED     | linux_sync (provider hygiene; no-op success) |
| 74  | `fsync`             | ✅ WIRED     | linux_sync (provider hygiene; data_only=0) |
| 75  | `fdatasync`         | ✅ WIRED     | linux_sync (provider hygiene; data_only=1) |
| 280 | `utimensat`         | ✅ STUB      | linux_utime (provider hygiene; UTIME_NOW/OMIT) |
| 132 | `utime`             | ✅ STUB      | linux_utime (provider hygiene; NULL buf only) |
| 235 | `utimes`            | ✅ STUB      | linux_utime (provider hygiene; NULL buf only) |
| 261 | `futimesat`         | ✅ STUB      | linux_utime (provider hygiene; NULL buf only) |
| 105 | `setuid`            | ✅ WIRED     | linux_setid (root only; -EPERM otherwise) |
| 106 | `setgid`            | ✅ WIRED     | linux_setid (root only) |
| 117 | `setresuid`         | ✅ WIRED     | linux_setid (-1 sentinel honoured) |
| 119 | `setresgid`         | ✅ WIRED     | linux_setid |
| 118 | `getresuid`         | ✅ WIRED     | linux_setid (returns 0/0/0) |
| 120 | `getresgid`         | ✅ WIRED     | linux_setid |
| 80  | `chdir`             | ✅ STUB      | linux_chdir (provider hygiene; provider injection) |
| 81  | `fchdir`            | ✅ STUB      | linux_chdir (provider hygiene; fd-based provider) |
| 188 | `setxattr`          | ✅ STUB      | linux_xattr (-EOPNOTSUPP; tmpfs no xattr storage) |
| 189 | `lsetxattr`         | ✅ STUB      | linux_xattr |
| 190 | `fsetxattr`         | ✅ STUB      | linux_xattr |
| 191 | `getxattr`          | ✅ STUB      | linux_xattr (-ENODATA; attribute missing) |
| 192 | `lgetxattr`         | ✅ STUB      | linux_xattr |
| 193 | `fgetxattr`         | ✅ STUB      | linux_xattr |
| 194 | `listxattr`         | ✅ WIRED     | linux_xattr (returns 0 attributes) |
| 195 | `llistxattr`        | ✅ WIRED     | linux_xattr |
| 196 | `flistxattr`        | ✅ WIRED     | linux_xattr |
| 197 | `removexattr`       | ✅ STUB      | linux_xattr (-ENODATA) |
| 198 | `lremovexattr`      | ✅ STUB      | linux_xattr |
| 199 | `fremovexattr`      | ✅ STUB      | linux_xattr |
| 137 | `statfs`            | ✅ WIRED     | linux_statfs (provider hygiene; tmpfs synthesized) |
| 138 | `fstatfs`           | ✅ WIRED     | linux_statfs (provider hygiene; tmpfs synthesized) |
| 221 | `fadvise64`         | ✅ WIRED     | linux_advise (advisory no-op) |
| 285 | `fallocate`         | ✅ STUB      | linux_advise (-EOPNOTSUPP; tmpfs no preallocation) |
| 40  | `sendfile`          | ✅ STUB      | linux_advise (provider hygiene; default -ENOSYS) |
| 97  | `getrlimit`         | ✅ WIRED     | linux_rlimit_legacy (provider hygiene; synthesised defaults) |
| 160 | `setrlimit`         | ✅ WIRED     | linux_rlimit_legacy (provider hygiene; no-op success) |
| 125 | `capget`            | ✅ WIRED     | linux_caps (provider hygiene; root all-caps default) |
| 126 | `capset`            | ✅ WIRED     | linux_caps (provider hygiene; self-only) |
| 36  | `getitimer`         | ✅ WIRED     | linux_itimer (provider hygiene; 3-slot table) |
| 37  | `alarm`             | ✅ WIRED     | linux_itimer (provider hygiene; storage-only) |
| 38  | `setitimer`         | ✅ WIRED     | linux_itimer (provider hygiene; usec validation) |
| 100 | `times`             | ✅ WIRED     | linux_itimer (provider hygiene; zero per-task ticks) |
| 73  | `flock`             | ✅ WIRED     | linux_lock (provider hygiene; 32-slot per-fd state) |
| 326 | `copy_file_range`   | ✅ STUB      | linux_lock (provider hygiene; default -ENOSYS) |
| 142 | `sched_setparam`    | ✅ WIRED     | linux_sched_prio (priority validation per policy) |
| 143 | `sched_getparam`    | ✅ WIRED     | linux_sched_prio |
| 144 | `sched_setscheduler`| ✅ WIRED     | linux_sched_prio (policy + priority storage) |
| 145 | `sched_getscheduler`| ✅ WIRED     | linux_sched_prio (returns stored policy) |
| 146 | `sched_get_priority_max` | ✅ WIRED | linux_sched_prio (FIFO/RR -> 99; others -> 0) |
| 147 | `sched_get_priority_min` | ✅ WIRED | linux_sched_prio (FIFO/RR -> 1; others -> 0) |
| 222 | `timer_create`      | ✅ WIRED     | linux_posix_timer (16-slot table; clockid+sigevent) |
| 223 | `timer_settime`     | ✅ WIRED     | linux_posix_timer (TIMER_ABSTIME flag honored) |
| 224 | `timer_gettime`     | ✅ WIRED     | linux_posix_timer (read-back stored spec) |
| 225 | `timer_getoverrun`  | ✅ WIRED     | linux_posix_timer (returns 0; no fires yet) |
| 226 | `timer_delete`      | ✅ WIRED     | linux_posix_timer (frees slot) |
| 201 | `time`              | ✅ WIRED     | linux_time_legacy (provider hygiene; seconds fallback 0) |
| 309 | `getcpu`            | ✅ WIRED     | linux_time_legacy (provider hygiene; cpu=0, node=0) |
| 161 | `chroot`            | ✅ WIRED     | linux_sandbox (provider hygiene; single-root no-op) |
| 135 | `personality`       | ✅ WIRED     | linux_sandbox (storage; QUERY=0xFFFFFFFF read-only) |
| 122 | `setfsuid`          | ✅ WIRED     | linux_sandbox (returns prev; -1 is probe sentinel) |
| 123 | `setfsgid`          | ✅ WIRED     | linux_sandbox (returns prev; -1 is probe sentinel) |
| 27  | `mincore`           | ✅ WIRED     | linux_mincore (page-aligned validation; all resident) |
| 238 | `set_mempolicy`     | ✅ WIRED     | linux_numa (BIND/INTERLEAVE need mask; single-NUMA) |
| 239 | `get_mempolicy`     | ✅ WIRED     | linux_numa (returns MPOL_DEFAULT, node 0 only) |
| 237 | `mbind`             | ✅ WIRED     | linux_numa (validates flags; no-op binding) |
| 164 | `settimeofday`      | ✅ WIRED     | linux_settod (provider hygiene; usec validation) |
| 324 | `membarrier`        | ✅ WIRED     | linux_jit_aux (QUERY mask; REGISTER state machine) |
| 323 | `userfaultfd`       | ✅ WIRED     | linux_jit_aux (16-slot fd table; VFS lifecycle) |
| 148 | `sched_rr_get_interval` | ✅ WIRED | linux_jit_aux (default RR slice 100 ms) |
| 272 | `unshare`           | ✅ WIRED     | linux_namespace (CLONE_* whitelist; THREAD invariants) |
| 165 | `mount`             | ✅ WIRED     | linux_namespace (fstype whitelist; BIND/MOVE/REMOUNT) |
| 166 | `umount2`           | ✅ WIRED     | linux_namespace (4-flag whitelist; MNT_DETACH etc.) |
| 322 | `execveat`          | ✅ STUB      | linux_exec_ext (validates; -ENOSYS until exec lands) |
| 436 | `close_range`       | ✅ WIRED     | linux_exec_ext (provider-injectable; cap at 4096) |
| 275 | `splice`            | ✅ STUB      | linux_pipe_zero (provider hygiene; default -ENOSYS) |
| 276 | `tee`               | ✅ STUB      | linux_pipe_zero (provider hygiene; default -ENOSYS) |
| 278 | `vmsplice`          | ✅ STUB      | linux_pipe_zero (provider hygiene; default -ENOSYS) |
| 310 | `process_vm_readv`  | ✅ WIRED     | linux_proc_vm (provider hygiene; foreign -> -ESRCH) |
| 311 | `process_vm_writev` | ✅ WIRED     | linux_proc_vm (provider hygiene; foreign -> -EPERM) |
| 312 | `kcmp`              | ✅ WIRED     | linux_proc_vm (FILE/VM/etc. structural compare) |
| 437 | `openat2`           | ✅ STUB      | linux_openat2 (provider hygiene; -ENOSYS default) |
| 439 | `faccessat2`        | ✅ WIRED     | linux_openat2 (provider hygiene; default 0) |
| 329 | `pkey_mprotect`     | ✅ WIRED     | linux_pkey (validates pkey; no-op binding) |
| 330 | `pkey_alloc`        | ✅ WIRED     | linux_pkey (16-slot table; reserve 0/1) |
| 331 | `pkey_free`         | ✅ WIRED     | linux_pkey (rejects reserved 0/1) |
| 444 | `landlock_create_ruleset` | ✅ WIRED | linux_landlock (16-slot rulesets; VFS lifecycle) |
| 445 | `landlock_add_rule`     | ✅ WIRED | linux_landlock (PATH_BENEATH/NET_PORT) |
| 446 | `landlock_restrict_self`| ✅ WIRED | linux_landlock (no-op accept; LSM future) |
| 317 | `seccomp`           | ✅ WIRED     | linux_seccomp (provider hygiene; STRICT/FILTER/etc.) |
| 321 | `bpf`               | ✅ STUB      | linux_seccomp (validates; -ENOSYS until BPF VM) |
| 101 | `ptrace`            | ✅ WIRED     | linux_seccomp (TRACEME ok; foreign -> -ESRCH) |
| 300 | `fanotify_init`     | ✅ WIRED     | linux_fanotify (8-slot fd table; VFS lifecycle) |
| 301 | `fanotify_mark`     | ✅ WIRED     | linux_fanotify (ADD/REMOVE/FLUSH mutex) |
| 425 | `io_uring_setup`    | ✅ STUB      | linux_io_uring (pow-of-2 entries; -ENOSYS default) |
| 426 | `io_uring_enter`    | ✅ STUB      | linux_io_uring (validates; -ENOSYS default) |
| 427 | `io_uring_register` | ✅ STUB      | linux_io_uring (validates; -ENOSYS default) |
| 449 | `futex_waitv`       | ✅ STUB      | linux_modern_misc (-ENOSYS; musl falls back to single FUTEX_WAIT) |
| 305 | `clock_adjtime`     | ✅ WIRED     | linux_modern_misc (TIME_OK; modes whitelist) |
| 447 | `memfd_secret`      | ✅ WIRED     | linux_modern_misc (8-slot fd table; VFS lifecycle) |
| 273 | `set_robust_list`   | ✅ WIRED     | linux_process          |
| 318 | `getrandom`         | ✅ WIRED     | linux_random (source hygiene) |

**No critical gaps remaining** (post-sessão 22).
All syscalls musl issues during `__libc_start_main` and
the core stdio init path are now wired. The matrix above
is complete for Marco M1 (single-thread musl + SpiderMonkey
shell).

1 legacy NR completed in sessão 38:
`pipe` now registers NR 22 and delegates to the existing `pipe2`
path with `flags=0`. This closes the only visible pending entry in
the Marco M1 table without changing fd semantics or adding a second
pipe implementation. Review-only validation this sessão: code paths
and host-test additions were inspected; tests were not executed per
operator instruction.

4 metadata paths refined in sessão 39:
`stat`/`lstat` now synthesise metadata for known pseudo paths
(`/`, `/dev`, `/dev/shm`, `/proc`, `/proc/self`, `/tmp`,
`/dev/{null,zero,full,random,urandom}` and fixed `/proc/*`
files). `lstat("/proc/self/exe")` reports S_IFLNK while
`stat("/proc/self/exe")` follows to S_IFREG. `fstatat(AT_FDCWD,
path, ...)` and `statx(AT_FDCWD, path, ...)` reuse the same known
path projection; AT_SYMLINK_NOFOLLOW uses the lstat form for
`/proc/self/exe`. Unknown paths still return -ENOSYS to preserve
open+fstat fallback until namei lands. Review-only validation this
sessão: code paths and host-test additions were inspected; tests
were not executed per operator instruction.

2 permission probes refined in sessão 40:
`access`/`faccessat` now use the same conservative known pseudo
path set as stat/lstat through `linux_stat_path_is_known()` for
existence and permission probes. This keeps shell/configure/
dynamic-loader probes aligned with metadata projection while
unknown paths still return -ENOENT until namei lands. Review-only
validation this sessão: code paths and host-test additions were
inspected; tests were not executed per operator instruction.

3 fd-backed readiness primitives refined in sessão 41:
`signalfd4` no longer returns -ENOSYS for well-formed calls. It now
creates or updates a storage-only signalfd fd, validates flags and
`sizemask == 8`, strips SIGKILL/SIGSTOP from the stored mask and
returns -EAGAIN on read until signal delivery exists. `read(2)`,
`write(2)`, `close(2)` and `lseek(2)` now route eventfd/signalfd/
timerfd fd ranges through the VFS router, so libevent-style generic
fd operations work for `eventfd2` and `timerfd_create` outputs.
Review-only validation this sessão: implementation, router paths and
host-test additions were inspected; tests were not executed per
operator instruction.

4 fd lifecycle refined in sessão 42:
`memfd_secret` fds are now recognised by the generic VFS router.
`close(2)` releases the 8-slot table entry for reuse, while
`read(2)`, `write(2)` and `lseek(2)` on live secretmem fds return
-ENOSYS until a backing/mmap implementation lands instead of being
misclassified as -EBADF. Review-only validation this sessão:
implementation, router paths and host-test additions were inspected;
tests were not executed per operator instruction.

5 fd lifecycle refined in sessão 43:
`inotify` fds are now recognised by the generic VFS router. `close(2)`
releases the inotify instance, `read(2)` on a live instance returns
-EAGAIN until fs-notifier event queues exist, `write(2)` returns
-EINVAL and `lseek(2)` returns -ESPIPE. Review-only validation this
sessão: implementation, router paths and host-test additions were
inspected; tests were not executed per operator instruction.

6 fd lifecycle refined in sessão 44:
`epoll` fds are now recognised by the generic VFS router. `close(2)`
releases the epoll instance, `read(2)` and `write(2)` on live epoll
fds return -EINVAL, and `lseek(2)` returns -ESPIPE while
`epoll_wait`/`epoll_pwait` remain the event retrieval path.
Review-only validation this sessão: implementation, router paths and
host-test additions were inspected; tests were not executed per
operator instruction.

7 fd lifecycle refined in sessão 45:
`fanotify` fds are now recognised by the generic VFS router. `close(2)`
releases the fanotify instance, `read(2)` on a live fd returns
-EAGAIN until fs-notifier event queues exist, `write(2)` returns
-EINVAL and `lseek(2)` returns -ESPIPE. Review-only validation this
sessão: implementation, router paths and host-test additions were
inspected; tests were not executed per operator instruction.

8 fd lifecycle refined in sessão 46:
`memfd_create` and `pidfd_open` fds are now recognised by the generic
VFS router. `close(2)` releases the owning slot for reuse. Generic
`read(2)`/`write(2)`/`lseek(2)` on live memfd fds return -ENOSYS until
real backing/ftruncate/mmap support exists. Generic `read(2)` and
`write(2)` on pidfd fds return -EINVAL, while `lseek(2)` returns
-ESPIPE. Review-only validation this sessão: implementation, router
paths and host-test additions were inspected; tests were not executed
per operator instruction.

9 fd lifecycle refined in sessão 47:
`userfaultfd` fds are now recognised by the generic VFS router.
`close(2)` releases the 16-slot table entry for reuse, `read(2)` on a
live fd returns -EAGAIN until page-fault event queues exist,
`write(2)` returns -EINVAL and `lseek(2)` returns -ESPIPE.
Review-only validation this sessão: implementation, router paths and
host-test additions were inspected; tests were not executed per
operator instruction.

10 fd lifecycle refined in sessão 48:
`landlock_create_ruleset` fds are now recognised by the generic VFS
router. `close(2)` releases the 16-slot ruleset fd entry for reuse,
`read(2)` and `write(2)` on a live ruleset fd return -EINVAL, and
`lseek(2)` returns -ESPIPE. Review-only validation this sessão:
implementation, router paths and host-test additions were inspected;
tests were not executed per operator instruction.

fd core callback hygiene refined in sessão 49:
`linux_fd_install_ops(NULL)` now clears the installed callback bundle
instead of leaving stale `pipe2`/`dup3` hooks live. Review-only
validation this sessão: implementation and host-test additions for
pipe and dup3 callback clearing were inspected; tests were not executed
per operator instruction.

exec-ext callback hygiene refined in sessão 50:
`linux_exec_ext_install_ops(NULL)` and `linux_exec_ext_reset_for_tests`
now clear the installed close_range callback bundle instead of only
dropping the installed flag. Review-only validation this sessão:
implementation and host-test additions for close and CLOEXEC callback
clearing were inspected; tests were not executed per operator
instruction.

dup callback hygiene refined in sessão 51:
`linux_dup_install_ops(NULL)` and `linux_dup_reset_for_tests` now clear
the installed dup/dup2 callback bundle instead of only dropping the
installed flag. Review-only validation this sessão: implementation and
host-test additions for dup, dup2 and reset callback clearing were
inspected; tests were not executed per operator instruction.

process callback hygiene refined in sessão 52:
`linux_process_install_ops(NULL)` now clears the installed task
accessor callback bundle. Review-only validation this sessão:
implementation and host-test additions for gettid, sched_yield and
sched_getaffinity callback clearing were inspected; tests were not
executed per operator instruction.

memfd/pidfd provider hygiene refined in sessão 53:
`linux_memfd_install_ops(NULL)` now clears the installed `pid_exists`
provider. Review-only validation this sessão: implementation and
host-test additions for `pidfd_open` and `pidfd_send_signal(sig=0)`
provider clearing were inspected; tests were not executed per operator
instruction.

vfs provider hygiene refined in sessão 54:
`linux_vfs_install_ops(NULL)` now clears the installed file-I/O
provider bundle. Review-only validation this sessão: implementation and
host-test additions for open, close, read/write and lseek provider
clearing were inspected; tests were not executed per operator
instruction.

arch_prctl provider hygiene refined in sessão 55:
`linux_arch_prctl_install_ops(NULL)` and
`linux_arch_prctl_reset_for_tests()` now clear the installed TLS MSR
provider bundle. Review-only validation this sessão: implementation and
host-test additions for SET/GET FS/GS callback clearing were inspected;
tests were not executed per operator instruction.

mmap provider hygiene refined in sessão 56:
`linux_mmap_install_ops(NULL)` now clears the installed memory-provider
callback bundle. Review-only validation this sessão: implementation and
host-test additions for mmap allocation, munmap/mprotect, mremap and
reset callback clearing were inspected; tests were not executed per
operator instruction.

futex provider hygiene refined in sessão 57:
`linux_futex_install_ops(NULL)` now clears the installed synchronization
provider callback bundle. Review-only validation this sessão:
implementation and host-test additions for WAIT, WAKE, REQUEUE and reset
callback clearing were inspected; tests were not executed per operator
instruction.

path provider hygiene refined in sessão 58:
`linux_path_install(NULL)` and `linux_path_reset_for_tests()` now clear
the installed `/proc/self/exe` resolver provider. Review-only validation
this sessão: implementation and host-test additions for `readlink`,
`readlinkat(AT_FDCWD)` and reset provider clearing were inspected; tests
were not executed per operator instruction.

net provider hygiene refined in sessão 59:
`linux_net_install_ops(NULL)` now clears the installed socket-extension
provider bundle. Review-only validation this sessão: implementation and
host-test additions for `accept4`, `recvmmsg`, `sendmmsg` and reset
callback clearing were inspected; tests were not executed per operator
instruction.

openat2 provider hygiene refined in sessão 60:
`linux_openat2_install_ops(NULL)` and
`linux_openat2_reset_for_tests()` now clear the installed hardened
path-resolution provider bundle. Review-only validation this sessão:
implementation and host-test additions for `openat2`, `faccessat2` and
reset callback clearing were inspected; tests were not executed per
operator instruction.

proc_vm provider hygiene refined in sessão 61:
`linux_proc_vm_install_ops(NULL)` and
`linux_proc_vm_reset_for_tests()` now clear the installed process-VM
provider bundle. Review-only validation this sessão: implementation and
host-test additions for `process_vm_readv`, `process_vm_writev` and reset
callback clearing were inspected; tests were not executed per operator
instruction.

pipe_zero provider hygiene refined in sessão 62:
`linux_pipe_zero_install_ops(NULL)` and
`linux_pipe_zero_reset_for_tests()` now clear the installed zero-copy
provider bundle. Review-only validation this sessão: implementation and
host-test additions for `splice`, `tee`, `vmsplice` and reset callback
clearing were inspected; tests were not executed per operator instruction.

rlimit provider hygiene refined in sessão 63:
`linux_rlimit_legacy_install_ops(NULL)` and
`linux_rlimit_legacy_reset_for_tests()` now clear the installed
resource-limit provider bundle. Review-only validation this sessão:
implementation and host-test additions for `getrlimit`, `setrlimit` and
reset callback clearing were inspected; tests were not executed per
operator instruction.

statfs provider hygiene refined in sessão 64:
`linux_statfs_install_providers(NULL)` and `linux_statfs_reset_for_tests()`
now clear the installed filesystem-statistics provider bundle. Review-only
validation this sessão: implementation and host-test additions for
`statfs` and `fstatfs` reset/default fallback were inspected; tests were
not executed per operator instruction.

sysinfo provider hygiene refined in sessão 65:
`linux_sysinfo_install(NULL)` and `linux_sysinfo_reset_for_tests()` now
clear the installed system-info provider bundle. Review-only validation
this sessão: implementation and host-test additions for RAM, uptime and
process-count provider clearing were inspected; tests were not executed
per operator instruction.

fs_mut provider hygiene refined in sessão 66:
`linux_fs_mut_install_ops(NULL)` and `linux_fs_mut_reset_for_tests()` now
clear the installed filesystem-mutation callback bundle. Review-only
validation this sessão: implementation and host-test additions for
`mkdir`, `rmdir`, `unlink`, `rename` and reset callback clearing were
inspected; tests were not executed per operator instruction.

fs_meta provider hygiene refined in sessão 67:
`linux_fs_meta_install_ops(NULL)` and `linux_fs_meta_reset_for_tests()` now
clear the installed filesystem-metadata callback bundle. Review-only
validation this sessão: implementation and host-test additions for
`chmod`, `fchmod`, `chown`, `fchown` and reset callback clearing were
inspected; tests were not executed per operator instruction.

chdir provider hygiene refined in sessão 68:
`linux_chdir_install_ops(NULL)` and `linux_chdir_reset_for_tests()` now
clear the installed working-directory callback bundle. Review-only
validation this sessão: implementation and host-test additions for
`chdir`, `fchdir` and reset callback clearing were inspected; tests were
not executed per operator instruction.

advise provider hygiene refined in sessão 69:
`linux_advise_install_ops(NULL)` and `linux_advise_reset_for_tests()` now
clear the installed sendfile callback bundle. Review-only validation this
sessão: implementation and host-test additions for `sendfile` uninstall,
reset callback clearing and offset preservation were inspected; tests were
not executed per operator instruction.

time_legacy provider hygiene refined in sessão 70:
`linux_time_legacy_install_ops(NULL)` and
`linux_time_legacy_reset_for_tests()` now clear the installed legacy-time
callback bundle. Review-only validation this sessão: implementation and
host-test additions for `time` uninstall/reset fallback and no stale
`now_seconds` callback invocation were inspected; tests were not executed
per operator instruction.

sandbox provider hygiene refined in sessão 71:
`linux_sandbox_install_ops(NULL)` and `linux_sandbox_reset_for_tests()` now
clear the installed sandbox callback bundle. Review-only validation this
sessão: implementation and host-test additions for `chroot` uninstall/reset
fallback and no stale provider invocation were inspected; tests were not
executed per operator instruction.

settod provider hygiene refined in sessão 72:
`linux_settod_install_ops(NULL)` and `linux_settod_reset_for_tests()` now
clear the installed time-setter callback bundle. Review-only validation this
sessão: implementation and host-test additions for `settimeofday`
uninstall/reset fallback and no stale `set_seconds` provider invocation
were inspected; tests were not executed per operator instruction.

link provider hygiene refined in sessão 73:
`linux_link_install_ops(NULL)` and `linux_link_reset_for_tests()` now clear
the installed hard/soft-link callback bundle. Review-only validation this
sessão: implementation and host-test additions for `link`, `symlink` and
reset callback clearing were inspected; tests were not executed per
operator instruction.

utime provider hygiene refined in sessão 74:
`linux_utime_install_ops(NULL)` and `linux_utime_reset_for_tests()` now
clear the installed timestamp callback bundle. Review-only validation this
sessão: implementation and host-test additions for path/fd timestamp
updates, `UTIME_NOW` source clearing and reset callback clearing were
inspected; tests were not executed per operator instruction.

itimer provider hygiene refined in sessão 75:
`linux_itimer_install_ops(NULL)` and `linux_itimer_reset_for_tests()` now
clear the installed interval-timer tick callback bundle. Review-only
validation this sessão: implementation and host-test additions for
`times` uninstall/reset fallback and no stale `now_ticks` callback
invocation were inspected; tests were not executed per operator instruction.

lock provider hygiene refined in sessão 76:
`linux_lock_install_ops(NULL)` and `linux_lock_reset_for_tests()` now clear
the installed `copy_file_range` callback bundle while preserving flock table
reset semantics. Review-only validation this sessão: implementation and
host-test additions for `copy_file_range` uninstall/reset fallback and no
stale provider invocation were inspected; tests were not executed per
operator instruction.

caps provider hygiene refined in sessão 77:
`linux_caps_install_ops(NULL)` and `linux_caps_reset_for_tests()` now clear
the installed capability callback bundle. Review-only validation this
sessão: implementation and host-test additions for `capget`/`capset`
uninstall/reset fallback and no stale capability provider invocation were
inspected; tests were not executed per operator instruction.

kill provider hygiene refined in sessão 78:
`linux_kill_install_ops(NULL)` and `linux_kill_reset_for_tests()` now clear
the installed signal-delivery callback bundle. Review-only validation this
sessão: implementation and host-test additions for `kill`/`tgkill`/`tkill`
uninstall/reset fallback and no stale `deliver` callback invocation were
inspected; tests were not executed per operator instruction.

sync provider hygiene refined in sessão 79:
`linux_sync_install_ops(NULL)` and `linux_sync_reset_for_tests()` now clear
the installed durability callback bundle. Review-only validation this
sessão: implementation and host-test additions for
`sync`/`syncfs`/`fsync`/`fdatasync` uninstall/reset fallback and no stale
flush callback invocation were inspected; tests were not executed per
operator instruction.

brk provider hygiene refined in sessão 80:
`linux_brk_install_ops(NULL)` and `linux_brk_reset_for_tests()` now clear
the installed heap-reservation callback bundle. Review-only validation this
sessão: implementation and host-test additions for grow-after-uninstall and
grow-after-reset fallback with no stale `reserve_pages` invocation were
inspected; tests were not executed per operator instruction.

trunc provider hygiene refined in sessão 81:
`linux_trunc_install_ops(NULL)` and `linux_trunc_reset_for_tests()` now
clear the installed fd-resize callback bundle. Review-only validation this
sessão: implementation and host-test additions/refinements for `ftruncate`
uninstall/reset fallback and no stale provider invocation were inspected;
tests were not executed per operator instruction.

eventfd provider hygiene refined in sessão 82:
`linux_eventfd_install_ops(NULL)` now clears the installed `alloc_fd`
callback bundle instead of leaving a stale fd allocator. Review-only
validation this sessão: implementation and host-test additions for
`eventfd2` uninstall/reset fallback to slot-based fds and no stale
allocator invocation were inspected; tests were not executed per operator
instruction.

seccomp provider hygiene refined in sessão 83:
`linux_seccomp_install_ops(NULL)` and `linux_seccomp_reset_for_tests()` now
clear the installed `install_filter` callback bundle. Review-only validation
this sessão: implementation and host-test additions for
`SECCOMP_SET_MODE_FILTER` uninstall/reset fallback and no stale filter
provider invocation were inspected; tests were not executed per operator
instruction.

epoll provider hygiene refined in sessão 84:
`linux_epoll_install_ops(NULL)` now clears the installed wait callback
bundle. Review-only validation this sessão: implementation and host-test
additions for `epoll_wait` uninstall/reset fallback and no stale
`fd_ready`/`yield` invocation were inspected; tests were not executed per
operator instruction.

exit provider hygiene refined in sessão 85:
`linux_exit_install_ops(NULL)` and `linux_exit_reset_for_tests()` now clear
the installed task-exit callback bundle. Review-only validation this
sessão: implementation and host-test additions for `exit`/`exit_group`
uninstall/reset fallback and no stale `exit_task` invocation were inspected;
tests were not executed per operator instruction.

pgrp provider hygiene refined in sessão 86:
`linux_pgrp_install_ops(NULL)` and `linux_pgrp_reset_for_tests()` now clear
the installed process-id callback bundle. Review-only validation this
sessão: implementation and host-test additions for process group/session
uninstall/reset fallback and no stale `getpid` invocation were inspected;
tests were not executed per operator instruction.

random source hygiene refined in sessão 87:
`linux_random_install_source(NULL)` is now covered by an explicit regression
guard verifying that the installed entropy source is cleared and that
`getrandom` falls back to `-EAGAIN` without invoking the old source.
Review-only validation this sessão: implementation shape and host-test
addition were inspected; tests were not executed per operator instruction.

epoll readiness refined in sessão 88:
`linux_epoll_init_boot()` now wires `fd_ready` to
`linux_eventfd_family_poll_events()`, giving `epoll_wait`/`epoll_pwait`
real readiness for eventfd counters and expired timerfds while preserving
zero-event fallback for signalfd and unknown fd classes. Review-only
validation this sessão: helper API, boot init ordering, and host-test
additions for eventfd/timerfd readiness were inspected; tests were not
executed per operator instruction.

epoll eventfd readiness coverage refined in sessão 89:
host regressions now explicitly cover writable eventfds surfacing `EPOLLOUT`
and drained eventfd counters clearing `EPOLLIN` after read. Review-only
validation this sessão: `linux_eventfd_family_poll_events()` semantics and
`test_linux_epoll.c` additions were inspected; tests were not executed per
operator instruction.

epoll timerfd readiness coverage refined in sessão 90:
host regressions now explicitly cover one-shot timerfds clearing `EPOLLIN`
after `linux_timerfd_read()` and periodic timerfds re-arming the next
`EPOLLIN` after read. Review-only validation this sessão:
`linux_timerfd_read()` disarm/rearm semantics and `test_linux_epoll.c`
additions were inspected; tests were not executed per operator instruction.

eventfd poll oracle coverage refined in sessão 91:
`test_linux_eventfd.c` now directly locks `linux_eventfd_family_poll_events()`
for eventfd read/write readiness, storage-only signalfd no-readiness, and
timerfd expiry/disarm readiness. Review-only validation this sessão:
oracle semantics, header dependencies, and host-test wiring were inspected;
tests were not executed per operator instruction.

eventfd poll oracle edge coverage refined in sessão 92:
direct host regressions now lock saturated eventfd counters as readable but
not writable, and semaphore-mode eventfds as readable until each unit has
been consumed. Review-only validation this sessão:
`linux_eventfd_family_poll_events()` overflow threshold semantics and
semaphore read behavior were inspected; tests were not executed per operator
instruction.

pipe readiness refined in sessão 93:
`pipe_poll_events()` now exposes generic pipe readiness flags, and
`linux_epoll_init_boot()` maps them to `EPOLLIN`/`EPOLLOUT`/`EPOLLERR`/
`EPOLLHUP` after the eventfd/timerfd oracle. Host regressions cover pipe
read/write readiness, full-pipe/closed-end edges, and `epoll_wait` surfacing
pipe read/write readiness. Review-only validation this sessão: helper API,
boot wiring, and host-test integration were inspected; tests were not
executed per operator instruction.

epoll pipe edge readiness refined in sessão 94:
`epoll_wait` now propagates provider-reported `EPOLLERR` and `EPOLLHUP`
even when the registered interest mask does not include them, matching the
Linux-visible contract expected by async loops. Host regressions cover full
pipe write ends not surfacing `EPOLLOUT`, read ends surfacing `EPOLLHUP`,
and write ends surfacing `EPOLLERR`. Review-only validation this sessão:
masking semantics, pipe readiness edges, and test wiring were inspected;
tests were not executed per operator instruction.

epoll pipe oneshot edge coverage refined in sessão 95:
host regressions now lock that provider-reported `EPOLLHUP` and `EPOLLERR`
do not revive entries already disabled by `EPOLLONESHOT`. Review-only
validation this sessão: `epoll_wait` interest-mask gating and pipe HUP/ERR
test ordering were inspected; tests were not executed per operator
instruction.

10 NRs landed in sessão 20:
`getpid`, `getppid`, `getuid`, `geteuid`, `getgid`, `getegid`,
`uname`, `gettimeofday`, `nanosleep`, `openat`.

4 NRs landed in sessão 21 (musl boot blockers):
`brk`, `arch_prctl`, `exit`, `exit_group`.

2 NRs landed in sessão 22 (stdio/fd polish):
`ioctl` (always -ENOTTY for non-tty fds, exactly what musl
stdio init keys off to pick block-buffered mode), `fcntl`
(F_GETFD/F_SETFD/F_GETFL/F_SETFL functional with per-fd
flag storage; F_DUPFD/F_DUPFD_CLOEXEC and locks return
-ENOSYS until proper fd table lands).

**Marco M1 ABI surface complete**: vendoring upstream
musl-1.2.5 (S3.1) is now the natural next step, followed
by the libc.a build (S3.3).

5 NRs landed in sessão 23 (stdio comfort):
`readv`, `writev` (musl `__stdio_write`/`__stdio_read` use
these for buffered flush, without them every printf would
degrade to single-byte unbuffered writes), `pread64`,
`pwrite64` (positioned I/O without disturbing cursor;
used by source-map readers and config loaders), `fstat`
(synthetic mode: stdin/stdout/stderr -> S_IFCHR, others
-> S_IFREG; satisfies the only two fields userland
reliably reads -- st_mode and st_size). `stat`/`lstat`
started as fallback-only and were later refined in sessão 39
to answer known pseudo paths while preserving -ENOSYS for
unknown paths.

11 NRs landed in sessão 37 (seccomp/BPF/ptrace + fanotify + io_uring +
modern misc): `seccomp`/`bpf`/`ptrace` (sandbox + debugger
surface. Firefox content sandbox installs seccomp BPF filter
via seccomp(SECCOMP_SET_MODE_FILTER) antes de exec'ing content
processes; Chromium-derived sandbox usa bpf(BPF_PROG_LOAD)
para compile filter to BPF bytecode; crash reporters usam
ptrace(PTRACE_ATTACH) para capture core dump. Marco M1 sem
real seccomp/BPF infrastructure: seccomp STRICT/FILTER/
GET_ACTION_AVAIL/GET_NOTIF_SIZES wired structurally; FILTER
provider-injectable; GET_ACTION_AVAIL valida 8 SECCOMP_RET_*
actions; GET_NOTIF_SIZES retorna sizes Linux x86_64 (notif=80,
resp=24, data=16). bpf cmd whitelist [0..40); -ENOSYS default
para userland fallback. ptrace TRACEME -> 0; foreign pid ->
-ESRCH; pid==0 self -> -EPERM (Linux behaviour)),
`fanotify_init`/`fanotify_mark` (file access notification.
Snap-packaged Firefox usa fanotify para monitorar cache/profile
dirs; auditd/file-integrity monitors usam fanotify_mark.
8-slot fd table (FD_BASE 0xC000); init flags whitelist (13 bits
incluindo CLOEXEC/CLASS_CONTENT/REPORT_FID/REPORT_DIR_FID/etc.).
mark flags whitelist (11 bits); ADD/REMOVE mutually exclusive;
FLUSH | ADD/REMOVE -> -EINVAL; sem ADD/REMOVE/FLUSH -> -EINVAL;
ADD/REMOVE com NULL pathname -> -EFAULT; FLUSH ignora pathname),
`io_uring_setup`/`io_uring_enter`/`io_uring_register` (high-perf
async I/O. Firefox necko HTTP/3 stack probes para opt into
kernel-side completion-driven I/O; SpiderMonkey IOUringJobBackend
probes; liburing queue_init issues all three. Marco M1 no async
I/O backend: validates structurally + -ENOSYS default forca
userland a epoll+read/write fallback. setup: entries pow-of-2
[1, 4096]; non-pow-2 -> -EINVAL; NULL params -> -EFAULT.
enter: fd<0 -> -EBADF; flags whitelist (5 bits); sigsz != 8
se sig -> -EINVAL. register: opcode [0, 32) whitelist; nr_args
>0 NULL arg -> -EFAULT),
`futex_waitv`/`clock_adjtime`/`memfd_secret` (modern misc.
musl 1.2.4+ pthread mutex/cond pode tentar futex_waitv para
multi-futex wait; chrony/timesyncd usa clock_adjtime para slew
RTC; libsecret 5.14+ usa memfd_secret para credentials buffer.
futex_waitv: nr_futexes [1, 128]; flags=0; clockid REALTIME/
MONOTONIC; -> -ENOSYS para musl fallback to single FUTEX_WAIT
loop. clock_adjtime: clk_id>=0; modes 12 bits whitelist; read
-> TIME_OK; write no-op (root has CAP_SYS_TIME). memfd_secret:
flags whitelist (CLOEXEC); 8-slot fd table (FD_BASE 0xD000);
exhaustion -> -ENFILE; VFS `close` releases slots, VFS
`read`/`write`/`lseek` recognise live fds and return -ENOSYS
until backing/mmap support lands.

11 NRs landed in sessão 36 (process VM + hardened path resolution
+ memory protection keys + Landlock sandbox): `process_vm_readv`/
`process_vm_writev`/`kcmp` (process VM access + kernel resource
comparison. Firefox profiler reads other thread stacks via
process_vm_readv to capture sample frames sem pausing target;
Chromium-derived sandboxes call kcmp(KCMP_FILE) para detect
se fds referem ao mesmo kernel object antes de granting IPC
permissions; GDB-style debuggers usam process_vm_writev para
inject breakpoint instructions. Marco M1 single-task: foreign
pid em readv -> -ESRCH, em writev -> -EPERM (Linux semantics);
self peers (pid==0 ou pid==current) delegam a provider hook;
flags!=0 -> -EINVAL; iovcnt > IOV_MAX (1024) -> -EINVAL.
kcmp: pid<0 -> -EINVAL; type whitelist (FILE/VM/FILES/FS/
SIGHAND/IO/SYSVSEM/EPOLL_TFD); KCMP_FILE compara fds
structurally (idx1 == idx2 -> 0 equal, else 1)),
`openat2`/`faccessat2` (hardened path resolution. Firefox
content sandbox usa openat2 com RESOLVE_BENEATH |
RESOLVE_NO_SYMLINKS para safely abrir files dentro do profile
dir sem traversing symlinks fora; bubblewrap usa faccessat2
com AT_EACCESS para reachability check. open_how struct
versioned via size; size<24 -> -EINVAL; resolve flag whitelist
(NO_XDEV/MAGICLINKS/SYMLINKS/BENEATH/IN_ROOT/CACHED);
BENEATH | IN_ROOT mutually exclusive -> -EINVAL. faccessat2
mode whitelist (R_OK|W_OK|X_OK|F_OK = 0x07); flags whitelist
(AT_EACCESS|AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH). Provider
injection para futuro namei walker; openat2 default -ENOSYS;
faccessat2 default 0 success),
`pkey_alloc`/`pkey_free`/`pkey_mprotect` (memory protection
keys, x86 PKU). SpiderMonkey W^X JIT usa pkey_mprotect para
flip code buffer entre RW e RX without TLB shootdown via
thread-local PKRU register; libsecret usa pkey_alloc para
mark credentials buffer needing explicit access enable. 16-
slot key table; keys 0/1 reservados pelo kernel (not
returnable by alloc); table exhaustion -> -ENOSPC; flags!=0
em pkey_alloc -> -EINVAL; access_rights whitelist
(DISABLE_ACCESS|DISABLE_WRITE = 0x03). pkey_free reserved
key -> -EINVAL; unallocated key -> -EINVAL. pkey_mprotect:
addr page-aligned (4 KiB) -> -EINVAL otherwise; len=0 -> 0
no-op; prot whitelist (READ|WRITE|EXEC = 0x07); pkey=-1 ok
(default key); pkey out of range ou unallocated -> -EINVAL;
Marco M1 cooperative single-thread no enforcement),
`landlock_create_ruleset`/`landlock_add_rule`/
`landlock_restrict_self` (Linux 5.13+ hardened userland
sandbox). Modern Firefox content sandbox usa landlock para
install per-task allow-list de filesystem paths e ports,
then restrict_self para lock down renderer. 16-slot ruleset
fd table (FD_BASE 0xB000); ABI version 4 (Linux 6.10).
VERSION query: flags=VERSION + attr=NULL + size=0 -> ABI
version. Real ruleset creation: handled_access_fs whitelist
(15 bits incluindo READ_FILE/WRITE_FILE/EXECUTE/MAKE_*),
handled_access_net whitelist (BIND_TCP|CONNECT_TCP), all-zero
access -> -ENOMSG (Linux); table exhaustion -> -ENFILE.
add_rule: rule_type whitelist (PATH_BENEATH=1|NET_PORT=2);
invalid fd -> -EBADF; NULL rule_attr -> -EFAULT.
restrict_self: invalid fd -> -EBADF; flags!=0 -> -EINVAL.
Marco M1 sem LSM hook -> accept structurally para Firefox
detectar suporte. ENOMSG (42) added to errno header.

11 NRs landed in sessão 35 (JIT auxiliary + namespaces +
exec extensions + pipe zero-copy): `membarrier`/`userfaultfd`/
`sched_rr_get_interval` (JIT auxiliary. SpiderMonkey JIT calls
membarrier(PRIVATE_EXPECTED_SYNC) before flipping code page
from RW to RX -- without it falls back to mprotect-twice; WASM
page-fault handlers use userfaultfd to implement guard-page
trap handling -- without it WASM falls back to slower bounds-
check JIT; musl pthread_attr_getschedparam reads
sched_rr_get_interval(SCHED_RR) to size time-slice budget --
without it defaults to 100 ms too coarse. membarrier QUERY
reports SUPPORTED bitmask covering 7 PRIVATE/GLOBAL variants;
PRIVATE_EXPEDITED requires REGISTER first per Linux. userfaultfd
allocates fds from 16-slot table (FD_BASE 0xA000) with flags
whitelist (USER_MODE_ONLY/NONBLOCK/CLOEXEC); table exhaustion
-> -ENFILE. sched_rr_get_interval default 100 ms slice),
`unshare`/`mount`/`umount2` (namespace + mount surface. Firefox
content sandbox calls unshare(CLONE_NEWUSER | CLONE_NEWNET |
CLONE_NEWIPC) for renderer isolation; bubblewrap+flatpak rely
on unshare for mount namespaces. Marco M1 single-namespace:
unshare validates CLONE_* whitelist + THREAD/SIGHAND/VM
invariants -> 0 no-op success. mount fstype whitelist (tmpfs/
proc/devpts/sysfs/none); BIND/MOVE/REMOUNT bypass fstype
lookup; unknown fstype -> -ENODEV. umount2 4-flag whitelist
(MNT_FORCE/DETACH/EXPIRE/UMOUNT_NOFOLLOW)),
`execveat`/`close_range` (exec + fd cleanup extensions. musl
posix_spawn uses execveat with dirfd to avoid TOCTOU races;
Firefox content sandbox uses close_range(0, ~0, 0) to scrub
all inherited fds before exec'ing renderer (security-critical).
execveat validates dirfd/path/flags -> -ENOSYS (exec subsystem
not landed; userland reverts to spawn). close_range provider-
injectable; default validation-only success; CLOEXEC variant
delegates to set_cloexec_one callback; last=~0u capped at
4096 to avoid u32 loop),
`splice`/`tee`/`vmsplice` (pipe zero-copy. musl posix_fadvise
fallback uses splice; Firefox cache uses splice between
SOCK_STREAM and file fd to avoid userspace bounce; vmsplice
for performance-critical IPC. Marco M1 no zero-copy plumbing
yet -- provider-injectable, default -ENOSYS forces userland
to read+write fallback. Validation: fd<0 -> -EBADF, unknown
flags fora MOVE|NONBLOCK|MORE|GIFT -> -EINVAL, vmsplice
nr_segs > IOV_MAX (1024) -> -EINVAL).

9 NRs landed in sessão 34 (sandbox surface + memory residency
+ NUMA policy + legacy time setter): `chroot`/`personality`/
`setfsuid`/`setfsgid` (Firefox sandbox surface. Firefox content
sandbox calls chroot("/") after seccomp filter installation
to remove path-based attack surface from renderer; glibc/musl
probe personality(PER_LINUX | ADDR_NO_RANDOMIZE) to disable
ASLR; setfsuid/setfsgid used by NFS clients and setuid
helpers. Marco M1 root-as-self world: chroot validates path
+ no-op success (provider-injectable for future per-task root);
personality stores value, returns prev; QUERY sentinel
(0xFFFFFFFF) reads without writing; setfsuid/setfsgid store
value, return prev; -1 is probe sentinel that doesn't change),
`mincore` (memory residency. Firefox JIT IonMonkey uses mincore
on trampoline buffer page right before flipping it RX; PGO
loaders skip already-resident pages; glibc posix_spawn probes
stack COW safety. addr must be page-aligned (4 KiB) -> -EINVAL
otherwise; length=0 is no-op; addr+length overflow -> -ENOMEM;
NULL vec with length>0 -> -EFAULT. Marco M1 no swap so all
RAM-backed pages are 'always resident' -> bit 0 set in every
byte of vec),
`set_mempolicy`/`get_mempolicy`/`mbind` (NUMA memory policy.
Firefox WebRender uses get_mempolicy on GL command buffer to
detect NUMA topology; SpiderMonkey GC uses set_mempolicy
(MPOL_PREFERRED) on hot heap pages; libnuma probes during
init. Linux modes MPOL_DEFAULT/PREFERRED/BIND/INTERLEAVE/
LOCAL accepted; BIND/INTERLEAVE/PREFERRED need non-empty
nodemask. mbind flags MF_STRICT/MOVE/MOVE_ALL whitelist.
Marco M1 single-NUMA -> get_mempolicy returns MPOL_DEFAULT
with bit 0 set in nodemask; set_mempolicy/mbind validate +
no-op success),
`settimeofday` (legacy time setter. musl clock_settime
(CLOCK_REALTIME) falls back to settimeofday on older kernels;
NTP daemons use it directly. tz parameter ignored since 2.6.x;
tv_usec [0, 1e6) and tv_sec >=0 validated; provider-injectable
for future RTC writeback; NULL tv with NULL tz -> 0 no-op).

11 NRs landed in sessão 33 (real-time scheduler priorities + POSIX
timers + legacy time + getcpu): `sched_setscheduler`/
`sched_getscheduler`/`sched_setparam`/`sched_getparam`/
`sched_get_priority_max`/`sched_get_priority_min` (real-time
scheduler API. Firefox audio thread queries
sched_get_priority_max(SCHED_FIFO) before bumping to avoid
audio glitches; Firefox compositor uses sched_setscheduler
(SCHED_FIFO) best-effort to elevate priority; musl
pthread_getschedparam reads via sched_getscheduler. Linux
policies SCHED_OTHER/FIFO/RR/BATCH/IDLE/DEADLINE accepted;
priority [1, 99] for FIFO/RR, must be 0 for others. Marco M1
stores policy + priority but cooperative scheduler doesn't
actually honour real-time semantics; Linux fast path through
Firefox is preserved),
`timer_create`/`timer_settime`/`timer_gettime`/
`timer_getoverrun`/`timer_delete` (POSIX timers. Firefox
profiler uses timer_create with SIGEV_THREAD to sample stacks
at fixed intervals; SpiderMonkey GC heuristics use
timer_create with CLOCK_MONOTONIC for incremental marking.
16-slot timer table with 1-based ids; clockid whitelist
(REALTIME/MONOTONIC/PROCESS_CPUTIME/THREAD_CPUTIME/MONOTONIC
_RAW/REALTIME_COARSE/MONOTONIC_COARSE/BOOTTIME); sigev_notify
whitelist (SIGNAL/NONE/THREAD/THREAD_ID); timer_settime
validates tv_nsec [0, 1e9), tv_sec >= 0; TIMER_ABSTIME flag
honored. Marco M1 stores spec but timers don't actually fire
until per-task signal subsystem lands -- Firefox profiler
detects success and proceeds with its initialisation),
`time`/`getcpu` (legacy time + CPU id queries. time(NULL) is
simplest "what time is it" call; musl falls back to raw
syscall when vDSO unavailable. getcpu used by Firefox
profiler to label samples with CPU and SpiderMonkey GC for
NUMA hints. time wired to provider-injectable now_seconds()
callback (defaults to 0/epoch); getcpu returns 0/0 always
(Marco M1 single-CPU).

10 NRs landed in sessão 32 (resource limits legacy + capabilities
+ interval timers + advisory locking): `getrlimit`/`setrlimit`
(legacy resource limits. Bash startup probes RLIMIT_NOFILE;
musl pthread_create queries RLIMIT_STACK to size thread
stacks; Firefox sandbox checks RLIMIT_AS for JIT decisions.
Modern userland prefers prlimit64; we synthesise sane defaults
(NOFILE 1024/4096, STACK 8 MiB/INFINITY, NPROC 1024/1024,
CORE 0/INFINITY, others INFINITY/INFINITY) when no provider
is registered; cur > max -> -EINVAL),
`capget`/`capset` (Linux capability syscalls. Firefox sandbox
calls capset() to drop ALL caps before exec'ing content
processes; bubblewrap/firejail capget to determine current
set; libcap probes via cap_get_proc. v3 (0x20080522) is the
preferred version; v1/v2 also accepted. Unknown version
rewrites hdr->version and returns -EINVAL (Linux probe
behaviour). capset only targets self (pid != 0 -> -EPERM
per Linux 2.6.25). Marco M1 root-with-all-caps default;
provider-injectable for per-task storage),
`alarm`/`getitimer`/`setitimer`/`times` (interval timers +
alarm + process times. musl sigtimedwait fallback uses
alarm; Firefox compositor watchdog uses ITIMER_REAL via
setitimer; bash/ps/time(1) use times. 3-slot itimer table
for REAL/VIRTUAL/PROF; tv_usec validated [0, 1e6); tv_sec
>= 0; setitimer with old_value returns previous; times
returns provider ticks with zero per-task accounting),
`flock`/`copy_file_range` (advisory file lock + kernel
copy. Firefox profile lock (.parentlock) uses flock(LOCK_EX
| LOCK_NB); SQLite uses flock as fcntl record lock fallback
on tmpfs; Firefox cache uses copy_file_range. flock 32-slot
per-fd state machine; LOCK_SH/EX/UN modes mutually exclusive
(LOCK_SH | LOCK_EX -> -EINVAL); 33rd distinct fd -> -ENOLCK;
single-process world means LOCK_NB never blocks. copy_file_
range validates fds + flags == 0; provider injection;
default -ENOSYS so userland falls back to read+write).

17 NRs landed in sessão 31 (extended attributes + filesystem
stats + file advise/preallocation/sendfile): `setxattr`/
`lsetxattr`/`fsetxattr`/`getxattr`/`lgetxattr`/`fgetxattr`/
`listxattr`/`llistxattr`/`flistxattr`/`removexattr`/
`lremovexattr`/`fremovexattr` (full xattr family. Firefox
quarantine logic uses `setxattr("user.xdg.origin.url", ...)`
for downloaded files; SELinux-aware code probes via
getxattr/listxattr; musl `cp -a`-style helpers use
listxattr to discover attribute sets. Marco M1 has no xattr
storage so we follow Linux convention: setxattr ->
-EOPNOTSUPP, getxattr -> -ENODATA, listxattr -> 0 attrs,
removexattr -> -ENODATA. All 12 paths share validation:
NULL/empty path, NULL/empty name, name > 255 chars, value
size > 64 KiB, unknown flag bits beyond CREATE|REPLACE),
`statfs`/`fstatfs` (filesystem stats. Firefox "free space"
check before downloads; SQLite WAL space pressure detect;
GIO volume labelling. Synthesised 120-byte Linux struct
with TMPFS_MAGIC (0x01021994), 4 KiB block size, defaults
16384 blocks/1024 inodes (provider-injectable to report
real RAM). f_fsid and f_spare zeroed),
`posix_fadvise`/`fallocate`/`sendfile` (file advise +
preallocation + kernel-copy. SQLite calls
posix_fadvise(POSIX_FADV_RANDOM) on databases for
page-cache hints; Firefox downloader uses fallocate to
reserve disk space. fadvise validates advice bounds [0,
5] and offset/len >= 0, returns 0 (advisory no-op);
fallocate validates mode whitelist + PUNCH_HOLE requires
KEEP_SIZE (Linux fs/open.c rule), returns -EOPNOTSUPP
(tmpfs default per Linux); sendfile -ENOSYS by default
so userland falls back to read+write).

12 NRs landed in sessão 30 (timestamp mutations + identity
changes + working directory): `utimensat`/`utime`/`utimes`/
`futimesat` (timestamp mutations honouring Linux's UTIME_NOW
(0x3FFFFFFF) and UTIME_OMIT (0x3FFFFFFE) sentinels. Without
them, Firefox HTTP cache cannot stamp files with their
`Last-Modified` header so every navigation re-fetches; musl's
`futimens(fd, ts)` is implemented as `utimensat(fd, NULL, ts,
0)` which we route to the fd provider; both UTIME_OMIT short-
circuits to 0 without provider call (Linux fast path);
UTIME_NOW expanded against the injectable `now()` callback;
tv_nsec validated [0, 1e9) plus the two sentinels;
AT_SYMLINK_NOFOLLOW honoured. Legacy utime/utimes/futimesat
with populated buf -> -ENOSYS forces userland to utimensat),
`setuid`/`setgid`/`setresuid`/`setresgid`/`getresuid`/
`getresgid` (identity-changing syscalls. musl `initgroups()`
run by the dynamic linker for setuid binaries calls
resuid/resgid as part of credential scrubbing; sandbox
wrappers (firejail-style) issue setresuid to drop to
unprivileged uid before exec. Marco M1 is single root user
(uid=gid=0); set*(0)/setres*(-1, ...) succeed; non-root
targets -> -EPERM until per-task credentials land; getres*
stores zeros and validates buffer pointers (-> -EFAULT for
NULL)), `chdir`/`fchdir` (working directory mutations.
Firefox profile setup chdir's into ~/.mozilla/firefox/<id>
before loading components with relative paths; bash and
./configure use chdir constantly. Marco M1 has no per-task
cwd yet; provider injection (`linux_chdir_install_ops`) lets
a future fs plug in real semantics. Without provider:
-ENOSYS for deterministic userland behaviour. NULL ->
-EFAULT, empty -> -ENOENT, fd<0 -> -EBADF up front).

15 NRs landed in sessão 29 (filesystem metadata mutations +
links + durability barriers): `chmod`/`fchmod`/`fchmodat`/
`chown`/`fchown`/`lchown`/`fchownat` (path/fd-based mode and
owner mutations through `linux_fs_meta_install_ops`. Without
chmod, Firefox profile loader's sanity check on cache dir
permissions (mkdir + chmod 0700) aborts; without fchmod,
musl `mkstemp()` post-open hardening fails. Mode clamped to
07777 before provider; uid/gid forwarded verbatim including
Linux's `(uid_t)-1` "don't change" sentinel; lchown is
fchownat with NOFOLLOW; fchmodat rejects all flag bits as
-EINVAL per Linux fs/namei.c contract; fchownat accepts
AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH whitelist; AT_EMPTY_PATH
with AT_FDCWD -> -EINVAL since AT_FDCWD has no fd to operate
on), `link`/`linkat`/`symlink`/`symlinkat` (hard- and
soft-link family. Firefox uses link(tmpfile, finalfile) +
unlink(tmpfile) for crash-safe atomic cache updates; musl
`realpath()` walks symlink chains for content-deduplicated
download targets. linkat AT_SYMLINK_FOLLOW honored; AT_FDCWD
only today; sym_link target NULL -> -EFAULT and empty ->
-ENOENT validated up front), `sync`/`syncfs`/`fsync`/
`fdatasync` (durability barriers. SQLite (places.sqlite,
cookies.sqlite) calls fsync after every WAL checkpoint;
-ENOSYS triggers SQLite's "disk I/O error" path that
corrupts the database. Marco M1 has no persistent backing
store so durability is trivially satisfied; we accept all
four with full Linux fd validation -- `fsync(-1)` ->
-EBADF -- and return 0. Provider injection lets a future
on-disk fs flush real device caches when persistence lands).

14 NRs landed in sessão 28 (filesystem mutations + memory
locking + supplementary groups): `mkdir`/`mkdirat`/`rmdir`/
`unlink`/`unlinkat`/`rename`/`renameat`/`renameat2` (path-based
filesystem mutations through a provider-injection point;
tmpfs can plug in once its mutation API lands. Without these,
Firefox profile setup -- which calls mkdir on `~/.mozilla/
firefox/<id>` -- and the cache index advance -- which uses
atomic rename(tmpfile, finalfile) every commit -- both abort.
Validation is done up front: NULL path -> -EFAULT, empty ->
-ENOENT; unlinkat with unknown flags -> -EINVAL; AT_REMOVEDIR
routes to rmdir; renameat2 with NOREPLACE+EXCHANGE -> -EINVAL
(Linux mutually-exclusive); non-AT_FDCWD dirfds -> -ENOTDIR
until real directory fds land), `mlock`/`munlock`/`mlockall`/
`munlockall` (Marco M1 has no swap so locks are trivially
satisfied; SpiderMonkey JIT calls mlock on its W^X executable
pages and falls back to thrashing madvise heuristics on
-ENOSYS; musl `pthread_setspecific` and TLS bring-up call
mlock on the TLS area; addr+len wraparound caught with -EINVAL;
mlockall(0) -> -EINVAL since Linux requires at least one MCL
bit; mlockall flags outside MCL_CURRENT|FUTURE|ONFAULT ->
-EINVAL), `getgroups`/`setgroups` (Marco M1 has zero
supplementary groups; getgroups(0, NULL) returns 0 verbatim
-- the documented Linux idiom for querying count without
copying; setgroups validates size <= NGROUPS_MAX (65536) and
list != NULL when size > 0, then silently accepts since root
implicit has CAP_SETGID; without these, musl `initgroups()`
run by the dynamic linker for setuid programs aborts the
credential-scrubbing path).

10 NRs landed in sessão 27 (system info + scheduling +
sessions): `clock_nanosleep` (per-clock sleep with TIMER_ABSTIME
flag; pthread cond-var timed waits go through this), `sysinfo`
(112-byte struct with uptime/loads/totalram/freeram/procs;
providers injectable so boot can plug in real numbers from
linux_proc), `getrusage` (RUSAGE_SELF/CHILDREN/THREAD
accepted, struct zero-filled -- musl/glibc tolerate that as
"no usage recorded"), `getpriority`/`setpriority` (Linux-encoded
nice value 20 - actual_nice; clamped to [-20, +19] on set;
bash uses these for `renice`/`nice` builtins; PRIO_PROCESS,
PRIO_PGRP, PRIO_USER all accepted with single-task semantics),
`setpgid`/`getpgid`/`getpgrp`/`setsid`/`getsid` (POSIX process
groups + sessions: shells use these every job control fork;
Marco M1 has one task so we model self-only operations:
setpgid on self succeeds and updates state; setpgid on others
-EPERM; getpgid/getsid on self return stored value, else
-ESRCH; setsid first call succeeds and becomes leader, second
call -EPERM since already a leader). When task_clone_thread
lands these all hook into per-task tables.

8 NRs landed in sessão 26 (process control + truncation):
`wait4`/`waitid` (Marco M1 has no children; both return
-ECHILD which is the documented Linux answer for
"nothing to wait for" -- musl `popen()`, busybox
`system()`, and shell job control all handle this
gracefully whereas -ENOSYS would make them fail-fast),
`kill`/`tkill`/`tgkill` (signal sending: self-signal
delegates to provider or returns 0 no-op; pid==0 and
pid==-1 succeed silently as broadcast/pgrp "no peers";
other pids return -ESRCH; sig==0 alive-probe answers
faithfully; signal range validated 0..LINUX_NSIG),
`truncate`/`ftruncate` (path-based truncate returns
-ENOSYS until namei walker; fd-based ftruncate routes
through `linux_trunc_install_ops` provider with
validation of fd>=0 and length>=0 -- tmpfs can install
a real resize hook).

6 NRs landed in sessão 25 (filesystem comfort):
`access`/`faccessat` (existence + permission probes
that shells, dynamic linkers and `./configure` issue
before opening; without them everything aborts at
-ENOSYS), `fstatat` (the AT-family stat that musl
`stat()` and glibc both call internally; AT_EMPTY_PATH
or empty path projects the synthetic fstat onto
`struct stat` so userland code that issues fstatat
instead of fstat works without fallback), `dup`/
`dup2` (POSIX fd duplication; `dup2(fd, fd)` answers
faithfully without an fd table -- shells `2>&1` style
plumbing partially works for the no-op case), and
`umask` (file mode creation mask; musl's `__init_libc`
reads it during early startup so we must answer with
a valid value -- default 0022 like Linux).

5 NRs landed in sessão 24 (path/metadata polish):
`getcwd` (returns "/" until cwd-per-process tracking lands;
musl `getcwd(3)` works for the common "chdir-less" case
that covers headless tools), `readlink` (special-cases
`/proc/self/exe` via an injected provider so userland that
resolves its own image path -- shells, `gdb`, profile
loaders -- gets a real answer; non-/proc paths return
-EINVAL meaning "not a symlink", matching Linux semantics
for regular files), `readlinkat` (delegates AT_FDCWD to
readlink), `getdents64` (always returns 0/EOF on Marco M1
so `opendir`/`readdir` reports an empty directory cleanly
instead of -ENOSYS, which is the kindest degradation
until a directory fd table lands), and `statx` (modern
stat with full ABI struct: under AT_EMPTY_PATH or empty
path we project the synthetic fstat onto the 256-byte
`struct statx`; known pseudo paths are projected through
`linux_stat`; unknown path-based statx returns -ENOSYS,
which causes musl/glibc to fall back to the open+fstat
path that already works).

## Currently registered (full list)

These are the NRs that have a handler installed
(`linux_syscall_register`) and dispatch to a `linux_compat`
module. Default for unregistered NRs is `-ENOSYS`.

```
0   read         ✅
1   write        ✅
2   open         ✅
3   close        ✅
8   lseek        ✅
9   mmap         ✅
10  mprotect     ✅
11  munmap       ✅
13  rt_sigaction
14  rt_sigprocmask
15  rt_sigreturn (-ENOSYS, no delivery)
24  sched_yield
25  mremap
28  madvise
56  clone        (-ENOSYS, validation only)
57  fork         (-ENOSYS)
58  vfork        (-ENOSYS)
131 sigaltstack
157 prctl        (parcial)
186 gettid
202 futex
203 sched_setaffinity
204 sched_getaffinity
213 epoll_create
218 set_tid_address
228 clock_gettime
12  brk
60  exit
158 arch_prctl
231 exit_group
16  ioctl
72  fcntl
5   fstat
4   stat
6   lstat
17  pread64
18  pwrite64
19  readv
20  writev
96  gettimeofday
35  nanosleep
39  getpid
104 getgid
107 geteuid
108 getegid
110 getppid
102 getuid
63  uname
257 openat
232 epoll_wait
233 epoll_ctl
253 inotify_init
254 inotify_add_watch
255 inotify_rm_watch
273 set_robust_list
281 epoll_pwait
283 timerfd_create
284 eventfd
286 timerfd_settime
287 timerfd_gettime
288 accept4      (-ENOSYS, validation/provider hygiene)
289 signalfd4    (storage-only; read EAGAIN until delivery)
290 eventfd2
291 epoll_create1
292 dup3         (-ENOSYS, validation only)
293 pipe2
294 inotify_init1
299 recvmmsg     (-ENOSYS, validation/provider hygiene)
302 prlimit64
307 sendmmsg     (-ENOSYS, validation/provider hygiene)
318 getrandom
319 memfd_create
424 pidfd_send_signal
434 pidfd_open
435 clone3       (-ENOSYS, validation only)
79  getcwd
89  readlink
217 getdents64
267 readlinkat
332 statx
21  access
269 faccessat
262 fstatat
32  dup
33  dup2
95  umask
61  wait4
247 waitid
62  kill
234 tgkill
200 tkill
76  truncate
77  ftruncate
230 clock_nanosleep
99  sysinfo
98  getrusage
140 getpriority
141 setpriority
109 setpgid
111 getpgrp
112 setsid
121 getpgid
124 getsid
83  mkdir
258 mkdirat
84  rmdir
87  unlink
263 unlinkat
82  rename
264 renameat
316 renameat2
149 mlock
150 munlock
151 mlockall
152 munlockall
115 getgroups
116 setgroups
90  chmod
91  fchmod
268 fchmodat
92  chown
93  fchown
94  lchown
260 fchownat
86  link
265 linkat
88  symlink
266 symlinkat
162 sync
306 syncfs
74  fsync
75  fdatasync
280 utimensat
132 utime
235 utimes
261 futimesat
105 setuid
106 setgid
117 setresuid
119 setresgid
118 getresuid
120 getresgid
80  chdir
81  fchdir
188 setxattr
189 lsetxattr
190 fsetxattr
191 getxattr
192 lgetxattr
193 fgetxattr
194 listxattr
195 llistxattr
196 flistxattr
197 removexattr
198 lremovexattr
199 fremovexattr
137 statfs
138 fstatfs
221 fadvise64
285 fallocate
40  sendfile
97  getrlimit
160 setrlimit
125 capget
126 capset
36  getitimer
37  alarm
38  setitimer
100 times
73  flock
326 copy_file_range
142 sched_setparam
143 sched_getparam
144 sched_setscheduler
145 sched_getscheduler
146 sched_get_priority_max
147 sched_get_priority_min
222 timer_create
223 timer_settime
224 timer_gettime
225 timer_getoverrun
226 timer_delete
201 time
309 getcpu
161 chroot
135 personality
122 setfsuid
123 setfsgid
27  mincore
238 set_mempolicy
239 get_mempolicy
237 mbind
164 settimeofday
324 membarrier
323 userfaultfd
148 sched_rr_get_interval
272 unshare
165 mount
166 umount2
322 execveat
436 close_range
275 splice
276 tee
278 vmsplice
310 process_vm_readv
311 process_vm_writev
312 kcmp
437 openat2
439 faccessat2
329 pkey_mprotect
330 pkey_alloc
331 pkey_free
444 landlock_create_ruleset
445 landlock_add_rule
446 landlock_restrict_self
317 seccomp
321 bpf
101 ptrace
300 fanotify_init
301 fanotify_mark
425 io_uring_setup
426 io_uring_enter
427 io_uring_register
449 futex_waitv
305 clock_adjtime
447 memfd_secret
```

(See the official validation script in each session's
`STATUS.md` entry for assert counts and coverage; total
**1121/1121** asserts across 69 linux_compat suites at the
time of this commit (sessão 37: +19 seccomp, +14 fanotify,
+15 io_uring, +15 modern_misc = +63).)

## Adding a new NR

To wire a new syscall to a CapyOS handler:

1. Add `LINUX_NR_<name>` to
   `include/kernel/linux_compat/linux_syscall_nrs.h`.
2. Implement the handler in the relevant `linux_compat`
   module's `_register_syscalls` function, calling
   `linux_syscall_register(LINUX_NR_<name>, sys_<name>)`.
3. Add host tests with deterministic fakes.
4. Update this file (`COMPAT.md`) marking the NR as `WIRED`
   or `STUB` as appropriate.
