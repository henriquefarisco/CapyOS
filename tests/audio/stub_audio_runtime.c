#include "kernel/scheduler.h"
#include "kernel/task.h"
#include <assert.h>
#include <string.h>
static struct task current, worker;
static unsigned guard;
int audio_mock_worker_fail;
static unsigned registered;
int x64_platform_timer_start_scheduler(void) { return 0; }
void (*audio_mock_timer_service)(void);
void x64_platform_timer_set_service_hook(void (*hook)(void)) { audio_mock_timer_service = hook; }
void scheduler_preempt_disable(void) { ++guard; }
void scheduler_preempt_enable(void) { assert(guard); --guard; }
int scheduler_preempt_disabled(void) { return guard != 0; }
int scheduler_running(void) { return 1; }
struct task *task_current(void) { return &current; }
struct task *task_create(const char *name, task_entry_fn entry, void *arg,
                         enum task_priority priority) {
    assert(!strcmp(name, "audio-pump") && entry && !arg);
    assert(priority == TASK_PRIORITY_HIGH && guard);
    if (audio_mock_worker_fail) return 0;
    return &worker;
}
void scheduler_add(struct task *task) {
    assert(task == &worker && guard && registered++ == 0);
}
void task_sleep(uint64_t ticks) { (void)ticks; assert(!guard); }
