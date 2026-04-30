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
#define SYSCALL_COUNT 41

#endif /* KERNEL_SYSCALL_NUMBERS_H */
