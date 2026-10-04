#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/kernel/syscall.c"

static struct process parent, child;
static struct task child_task, parent_task;
static uint8_t stack[1024], expected[512];
static int fork_fail, lookup_fail, exec_fail, published;
struct process *process_current(void) { return &parent; }
struct process *process_fork(struct process *p) {
    assert(p == &parent);
    return fork_fail ? NULL : &child;
}
void process_destroy(struct process *p) { assert(p == &child); }
void user_task_arm_for_fork(struct task *t, const struct syscall_frame *f) {
    assert(t == &child_task && f);
}
void scheduler_add(struct task *t) {
    assert(t == &child_task);
    assert(memcmp(t->context.fx_state, expected, 512) == 0);
    ++published;
}
int embedded_progs_lookup(const char *p, const uint8_t **data, size_t *size) {
    assert(p);
    *data = expected; *size = 512;
    return lookup_fail ? -1 : 0;
}
int process_exec_replace(struct process *p, const uint8_t *data, size_t size) {
    assert(p == &parent && data == expected && size == 512);
    parent_task.context.rip = 0x400000;
    parent_task.context.rsp = 0x800000;
    return exec_fail ? -1 : 0;
}
static int64_t replacement_handler(struct syscall_frame *frame) {
    return (int64_t)frame->rdi;
}

int main(void) {
    uint8_t image[512];
    struct syscall_frame frame = {.rax = SYS_FORK, .rdi = (uintptr_t)"/bin/test"};
    syscall_register(SYS_FORK, sys_fork);
    syscall_register(SYS_EXEC, sys_exec);
    parent.main_thread = &parent_task;
    child.main_thread = &child_task;
    child.pid = 7;
    child_task.kernel_stack = stack;
    for (unsigned i = 0; i < 512; ++i) expected[i] = (uint8_t)(i * 17 + 3);
    memcpy(image, expected, 512);
    assert(syscall_dispatch_with_fp(&frame, image) == 7 && published == 1);
    assert(memcmp(image, expected, 512) == 0);
    child_task.context.fx_state[160] ^= 0xff;
    assert(memcmp(image, expected, 512) == 0);
    fork_fail = 1;
    assert(syscall_dispatch_with_fp(&frame, image) == -1 && published == 1);
    frame.rax = SYS_EXEC;
    lookup_fail = 1;
    assert(syscall_dispatch_with_fp(&frame, image) == -1);
    assert(memcmp(image, expected, 512) == 0);
    lookup_fail = 0; exec_fail = 1;
    assert(syscall_dispatch_with_fp(&frame, image) == -1);
    assert(memcmp(image, expected, 512) == 0);
    exec_fail = 0;
    assert(syscall_dispatch_with_fp(&frame, image) == 0);
    for (unsigned i = 0; i < 512; ++i)
        assert(image[i] == (i == 0 ? 0x7f : i == 1 ? 3 : i == 24 ? 0x80 : i == 25 ? 0x1f : 0));
    assert(frame.rcx == 0x400000 && frame.rsp == 0x800000);
    memcpy(image, expected, 512);
    frame.rdi = 123;
    syscall_register(SYS_EXEC, replacement_handler);
    assert(syscall_dispatch_with_fp(&frame, image) == 123);
    frame.rax = SYS_FORK;
    syscall_register(SYS_FORK, replacement_handler);
    assert(syscall_dispatch_with_fp(&frame, image) == 123);
    assert(published == 1);
    frame.rax = SYS_GETPID;
    assert(syscall_dispatch_with_fp(&frame, image) == -1);
    syscall_register(SYS_GETPID, replacement_handler);
    assert(syscall_dispatch_with_fp(&frame, image) == 123);
    frame.rax = SYSCALL_COUNT;
    assert(syscall_dispatch_with_fp(&frame, image) == -1);
    assert(memcmp(image, expected, 512) == 0);
    puts("[fp-routing] default, replaced, generic, unregistered and invalid handlers: PASS");
    puts("[fp-lifecycle] fork copy before publication, isolation, failed exec preservation, successful exec reset: PASS");
    return 0;
}
