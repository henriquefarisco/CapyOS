/* Architecture adapter: x86_64 syscall ABI for musl on CapyOS.
 *
 * This header provides the inline-asm macros that musl's libc
 * uses to issue `syscall` instructions. CapyOS implements the
 * Linux x86_64 syscall ABI verbatim (same NRs, same register
 * convention, same errno encoding), so this file is an exact
 * mirror of upstream musl's `arch/x86_64/syscall_arch.h` --
 * deliberately so, to keep the patch surface zero.
 *
 * x86_64 Linux syscall ABI (from kernel-side
 * `arch/x86/entry/calling.h`):
 *
 *   rax = syscall NR (input), return value (output)
 *   rdi = arg0
 *   rsi = arg1
 *   rdx = arg2
 *   r10 = arg3   (NOTE: not rcx, which is clobbered by syscall)
 *   r8  = arg4
 *   r9  = arg5
 *   rcx = clobbered (return address saved by syscall)
 *   r11 = clobbered (saved RFLAGS)
 *
 * Memory clobber is required because the kernel may modify
 * memory through pointer arguments. Without it the compiler
 * could reorder reads/writes around the syscall and break
 * caller's invariants.
 *
 * Errno encoding: kernel returns -errno in rax for failure
 * (small negative numbers, |x| < 4096). Successful results are
 * any other value. musl checks `(unsigned long)ret > -4096UL`
 * to distinguish; CapyOS follows the same convention.
 *
 * This file's path (`userland/lib/musl/arch/x86_64/syscall_arch.h`)
 * matches the path inside an unmodified upstream musl tree, so
 * when we vendor upstream musl-1.2.5 this adapter slots in
 * without source-tree surgery.
 */

#ifndef CAPYOS_USERLAND_LIB_MUSL_ARCH_X86_64_SYSCALL_ARCH_H
#define CAPYOS_USERLAND_LIB_MUSL_ARCH_X86_64_SYSCALL_ARCH_H

/* Use fast (vsyscall-style) clock_gettime path when set. CapyOS
 * does not yet expose a vDSO -- programs always trap into the
 * kernel. Setting the flag to 0 keeps musl on the slow path
 * (regular syscall), which is what we need until vDSO lands. */
#define VDSO_CGT_SYM       "__vdso_clock_gettime"
#define VDSO_CGT_VER       "LINUX_2.6"
#define VDSO_USEFUL        0

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

static __inline long __syscall0(long n) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall1(long n, long a1) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall2(long n, long a1, long a2) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4) {
    unsigned long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3,
                                long a4, long a5) {
    unsigned long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall6(long n, long a1, long a2, long a3,
                                long a4, long a5, long a6) {
    unsigned long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

#define IPC_64 0

#endif /* CAPYOS_USERLAND_LIB_MUSL_ARCH_X86_64_SYSCALL_ARCH_H */
