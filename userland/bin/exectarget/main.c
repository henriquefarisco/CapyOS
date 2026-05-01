/*
 * userland/bin/exectarget/main.c -- minimal target binary for SYS_EXEC.
 *
 * M5 phase B.1: this binary exists only to prove that `capy_exec()`
 * actually replaces the calling process's address space with a
 * DIFFERENT image, not just a re-launch of the same one.
 *
 * It emits a unique marker (`[exec-ok]`) one-shot via SYS_WRITE,
 * then exits cleanly. The marker is intentionally distinct from
 * any string emitted by `hello` so the smoke
 * (`smoke_x64_exec.py`) can match `[exec-ok]` and prove that:
 *
 *   1. The kernel-side process_exec_replace teardown of the old
 *      AS + creation of a new AS + ELF load + CR3 reload all
 *      succeeded.
 *   2. The syscall return path landed at the NEW entry point
 *      (this binary's `_start`) with the NEW user RSP, instead
 *      of resuming where SYS_EXEC was called from.
 *
 * Style: same constraints as hello -- no globals beyond .rodata,
 * no allocations, no .bss reliance (the loader does not yet zero
 * .bss). Single SYS_WRITE + SYS_EXIT (the latter via crt0).
 */
#include <capylibc/capylibc.h>

static const char k_msg[] = "[exec-ok]\n";

int main(int rank) {
    (void)rank;
    capy_write(1, k_msg, sizeof(k_msg) - 1);
    return 0;
}
