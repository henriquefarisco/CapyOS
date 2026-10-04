/* Isolated contract for the real direct-hello boot helper, before any timer.
 * Unrelated functions in user_init.c are discarded by --gc-sections. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel/process.h"
#include "kernel/task.h"
#include "kernel/user_init.h"

static struct process process;
static struct task thread;
static unsigned sequence, queued, running, entered, destroyed;
static int failure_mode, invalid;
static const unsigned char blob[64];

const void *embedded_hello_data(void) { return blob; }
size_t embedded_hello_size(void) { return sizeof(blob); }
int elf_validate(const uint8_t *data, size_t size) {
    if (data != blob || size != sizeof(blob)) invalid = 1;
    return failure_mode == 1 ? -1 : 0;
}
struct process *process_create(const char *name, uint32_t uid, uint32_t gid) {
    if (strcmp(name, "hello") || uid || gid) invalid = 1;
    return failure_mode == 2 ? NULL : &process;
}
int elf_load_into_process(struct process *proc, const uint8_t *data, size_t size) {
    if (proc != &process || data != blob || size != sizeof(blob)) invalid = 1;
    return failure_mode == 3 ? -1 : 0;
}
void process_destroy(struct process *proc) {
    if (proc != &process) invalid = 1;
    ++destroyed;
}
void scheduler_add(struct task *task) {
    if (task != &thread || ++sequence != 1) invalid = 1;
    ++queued;
}
void scheduler_set_running(int value) {
    if (value != 1 || ++sequence != 2) invalid = 1;
    ++running;
}
int process_enter_user_mode(struct process *proc) {
    if (proc != &process || ++sequence != 3 || queued != 1 || running != 1)
        invalid = 1;
    ++entered;
    return PROCESS_ENTER_USER_MODE_OK; /* native seam, no privileged IRET */
}
int main(void) {
    unsigned failures = 0;
    for (failure_mode = 0; failure_mode < 4; ++failure_mode) {
        memset(&process, 0, sizeof(process));
        memset(&thread, 0, sizeof(thread));
        process.main_thread = &thread;
        sequence = queued = running = entered = destroyed = 0;
        invalid = 0;
        int result = kernel_boot_run_embedded_hello();
        int expected = failure_mode == 0 ? -1 :
            failure_mode == 1 ? KERNEL_SPAWN_BAD_ELF :
            failure_mode == 2 ? KERNEL_SPAWN_NO_PROCESS : KERNEL_SPAWN_LOAD_FAILED;
        unsigned active = failure_mode == 0;
        if (invalid || result != expected || queued != active || running != active ||
            entered != active || destroyed != (unsigned)(failure_mode == 3)) {
            printf("[boot-user-scheduler] FAIL mode=%d result=%d queued=%u running=%u entered=%u\n",
                   failure_mode, result, queued, running, entered);
            ++failures;
        }
    }
    if (failures) return 1;
    puts("[boot-user-scheduler] 4 cases: parent queued and scheduler live before user entry; errors inert");
    return 0;
}
