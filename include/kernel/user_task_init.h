#ifndef KERNEL_USER_TASK_INIT_H
#define KERNEL_USER_TASK_INIT_H

#include <stdint.h>

/*
 * User task initialisation helpers (M4 phase 8f.4).
 *
 * `user_task_arm_for_first_dispatch` synthesises the IRET frame
 * that `x64_user_first_dispatch` (in interrupts_asm.S) consumes
 * the very first time `context_switch` lands on a fresh user task.
 *
 * Call shape:
 *
 *     struct task *t = task_create(...);
 *     ...
 *     user_task_arm_for_first_dispatch(t, user_rip, user_rsp);
 *     scheduler_add(t);
 *
 * After this returns, `t->context.rsp` points at the synthesized
 * post-PUSH_REGS frame, and `t->context.rip` points at
 * `x64_user_first_dispatch`. When the scheduler later picks `t`
 * for the first time, `context_switch(&old, &t->context)` jumps
 * to that trampoline, which pops 15 GP regs + skips vector/err +
 * iretqs into ring 3 with the user RIP/RSP/CS/SS/RFLAGS we wrote
 * into the IRET slots.
 *
 * The builder is pure C and is host-buildable; tests verify the
 * exact byte layout of the synthesized frame.
 */

struct task;

#define USER_TASK_USER_CS 0x23u
#define USER_TASK_USER_SS 0x1Bu
#define USER_TASK_USER_RFLAGS 0x202u

void user_task_arm_for_first_dispatch(struct task *t,
                                      uint64_t user_rip,
                                      uint64_t user_rsp);

/* M4 phase 8f.5: rank-passing variant. Identical to the base
 * builder above but writes `initial_rax` into the synth frame's
 * RAX slot instead of zero. capylibc's crt0 forwards RAX to RDI
 * before calling main(), which lets the kernel pass a per-task
 * "rank" int into main() without wiring up a full argv/envp
 * stack frame (deferred to a later milestone).
 *
 * Used by `kernel_boot_run_two_busy_users` to give two copies of
 * the same hello binary distinct markers so the ring-3 preemption
 * smoke can verify BOTH instances make progress.
 */
void user_task_arm_for_first_dispatch_with_rax(struct task *t,
                                               uint64_t user_rip,
                                               uint64_t user_rsp,
                                               uint64_t initial_rax);

/* M5 phase A.2: arm a child task created by `process_fork` so that
 * when the scheduler first lands on it, control resumes in ring 3 at
 * the parent's syscall return point with `rax = 0` (the canonical
 * "child branch" of fork()) and the parent's user RSP/RFLAGS.
 *
 * The `parent_frame` argument is the kernel-side `struct syscall_frame`
 * captured by `syscall_entry.S` for the parent's SYS_FORK call; the
 * child inherits `frame->rip` (return address past `syscall`) and
 * `frame->rsp` (user stack pointer). Caller-saved GPRs other than RAX
 * are left at zero in the synthesized frame; capylibc's `capy_fork`
 * stub is responsible for spilling/restoring callee-saved regs via
 * the user stack so both branches of fork resume with consistent
 * register state once the child's CoW pages diverge from the parent.
 *
 * No-op if either argument is NULL or the child's kernel stack is
 * too small to host the synthetic frame (same guard as the base
 * builder). Pure C, host-testable. */
struct syscall_frame;
void user_task_arm_for_fork(struct task *child,
                            const struct syscall_frame *parent_frame);

#endif /* KERNEL_USER_TASK_INIT_H */
