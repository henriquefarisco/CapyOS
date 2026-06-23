#ifndef KERNEL_SYSCALL_NUMBERS_H
#define KERNEL_SYSCALL_NUMBERS_H

/* Single source of truth for syscall numbers.
 *
 * This header is `__ASSEMBLER__`-clean: it contains nothing but
 * preprocessor `#define`s so that both kernel C code (via
 * `kernel/syscall.h`) and userland asm stubs
 * (`userland/lib/capylibc/syscall_stubs.S`) include the same numbers
 * from one place. tests/test_capylibc_abi.c statically asserts that
 * the userland header `userland/include/capylibc/syscall_numbers.h`
 * (or whatever wrapper capylibc adopts) agrees with these values.
 *
 * Adding a new syscall:
 *   1. Bump SYSCALL_COUNT.
 *   2. Add a SYS_<NAME> #define above SYSCALL_COUNT.
 *   3. Wire a handler in `src/kernel/syscall.c` (`syscall_init`).
 *   4. Add a stub to `userland/lib/capylibc/syscall_stubs.S` if the
 *      call is supposed to be reachable from user space.
 *
 * Removing or renumbering a syscall is an ABI break: all userland
 * binaries shipped with the OS must be rebuilt. */

#define SYS_EXIT      0
#define SYS_READ      1
#define SYS_WRITE     2
#define SYS_OPEN      3
#define SYS_CLOSE     4
#define SYS_STAT      5
#define SYS_FSTAT     6
#define SYS_LSEEK     7
#define SYS_MMAP      8
#define SYS_MUNMAP    9
#define SYS_BRK       10
#define SYS_FORK      11
#define SYS_EXEC      12
#define SYS_WAIT      13
#define SYS_GETPID    14
#define SYS_GETPPID   15
#define SYS_KILL      16
#define SYS_YIELD     17
#define SYS_SLEEP     18
#define SYS_DUP       19
#define SYS_DUP2      20
#define SYS_PIPE      21
#define SYS_MKDIR     22
#define SYS_RMDIR     23
#define SYS_UNLINK    24
#define SYS_RENAME    25
#define SYS_GETCWD    26
#define SYS_CHDIR     27
#define SYS_SOCKET    28
#define SYS_BIND      29
#define SYS_LISTEN    30
#define SYS_ACCEPT    31
#define SYS_CONNECT   32
#define SYS_SEND      33
#define SYS_RECV      34
#define SYS_GETUID    35
#define SYS_GETGID    36
#define SYS_SETUID    37
#define SYS_SETGID    38
#define SYS_TIME      39
#define SYS_IOCTL     40
/* F4 seção c parte 3/3 (2026-05-08): hostname -> IPv4 resolver.
 * Userland passes a NUL-terminated DNS name (`rdi`), a pointer to
 * a `uint32_t *out_ip` (`rsi`) populated host-order on success, and
 * a flags slot (`rdx`, reserved, must be 0). Returns 0 on hit, -1
 * on miss / invalid args / no installed resolver backend.
 *
 * Backend dispatch lives in `src/kernel/syscall_net.c`; production
 * routes to the in-kernel `dns_cache_lookup` from
 * `src/net/services/dns_cache.c`. A miss is NOT auto-promoted to
 * an active DNS query in this iteration: the cache must already
 * contain the entry (seeded via `dns_cache_insert` from a future
 * resolver service or via the initial DHCP exchange). The active
 * resolver lands together with the libcapy-net DNS client. */
#define SYS_DNS_RESOLVE 41
/* Etapa 5 / Slice 5.1: userland CSPRNG entropy, backed by the audited
 * in-tree csprng (src/security/csprng.c). Required so libcapy-tls can
 * seed BearSSL's DRBG in ring 3 — the gap that blocked Etapa 5.
 * Handler: src/kernel/syscall.c::sys_getrandom. */
#define SYS_GETRANDOM   42
/* Etapa 5 / Slice 5.x: userland wall-clock (seconds since the Unix epoch),
 * backed by the kernel RTC (rtc_unix_timestamp). Needed so libcapy-tls can
 * check X.509 certificate validity in ring 3 — SYS_TIME returns APIC ticks
 * since boot, NOT wall-clock, so it cannot drive cert expiry.
 * Handler: src/kernel/syscall.c::sys_clock_realtime. */
#define SYS_CLOCK_REALTIME 43
/* Etapa 6 / Slice 6.7: active session UI language for ring-3 apps. Lets a
 * userland app (CapyBrowse Text) localize its user-facing diagnostics to the
 * logged-in user's language instead of a hardcoded base. No arguments; returns
 * a small stable code (see CAPY_SESSION_LANG_* below).
 * Handler: src/kernel/syscall.c::sys_get_session_lang. */
#define SYS_GET_SESSION_LANG 44
#define SYSCALL_COUNT   45

/* Stable return codes for SYS_GET_SESSION_LANG (additive ABI). PT_BR is the
 * no-session default, matching app_current_language() and the locked
 * "selection default is PT-BR" invariant; EN stays the string-fallback base
 * the userland diagnostics use when a specific translation is missing. */
#define CAPY_SESSION_LANG_PT_BR 0
#define CAPY_SESSION_LANG_EN    1
#define CAPY_SESSION_LANG_ES    2

#endif /* KERNEL_SYSCALL_NUMBERS_H */
