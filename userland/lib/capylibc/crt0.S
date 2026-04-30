# capylibc C runtime startup (crt0).
#
# Provides the `_start` symbol that the kernel ELF loader sets as the
# entry point for a freshly-loaded user binary. Today the kernel
# loads the program but does NOT yet build a user-mode argc/argv/envp
# stack frame (that comes in fork/exec, M4 phase 6); _start therefore
# treats main() as `int main(void)` and ignores any incoming
# argc/argv. When phase 6 arrives, _start will read [%rsp] = argc and
# [%rsp+8] = argv before calling main().
#
# When main returns, _start invokes capy_exit() with main's return
# value. capy_exit is __attribute__((noreturn)) so we never reach the
# trailing `hlt` loop; it exists only so a buggy kernel that lets
# SYSCALL return after SYS_EXIT does not silently fall off the end
# of the .text section into whatever happens to follow.

#include "kernel/syscall_numbers.h"

.text
.global _start
_start:
    # Anchor the frame so any future stack-walker emits a sane backtrace.
    xorq %rbp, %rbp

    # call int main(void); future M4 phase 6 will load argc/argv from
    # the SysV process stack here.
    call main

    # main's return value is in %eax; sign-extend into %rdi for the
    # syscall (the kernel takes the exit code as int64_t in arg0).
    movslq %eax, %rdi
    movq $SYS_EXIT, %rax
    syscall

    # Defensive halt loop in case the kernel ever lets SYS_EXIT
    # return; should never execute.
1:  hlt
    jmp 1b
.size _start, .-_start

.section .note.GNU-stack,"",@progbits
