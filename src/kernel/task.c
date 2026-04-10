#include "kernel/task.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct task task_table[TASK_MAX_COUNT];
static uint32_t next_pid = 1;
static struct task *current_task = NULL;
static uint32_t task_active_count = 0;

void task_system_init(void) {
  for (int i = 0; i < TASK_MAX_COUNT; i++) {
    task_table[i].state = TASK_STATE_UNUSED;
    task_table[i].pid = 0;
  }
  next_pid = 1;
  current_task = NULL;
  task_active_count = 0;
}

static struct task *task_alloc(void) {
  for (int i = 0; i < TASK_MAX_COUNT; i++) {
    if (task_table[i].state == TASK_STATE_UNUSED) {
      return &task_table[i];
    }
  }
  return NULL;
}

static void task_name_copy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

struct task *task_create(const char *name, task_entry_fn entry, void *arg,
                         enum task_priority priority) {
  struct task *t = task_alloc();
  if (!t) return NULL;

  t->pid = next_pid++;
  t->ppid = current_task ? current_task->pid : 0;
  task_name_copy(t->name, name, TASK_NAME_MAX);
  t->state = TASK_STATE_CREATED;
  t->priority = priority;
  t->uid = 0;
  t->gid = 0;
  t->cpu_time_ns = 0;
  t->wake_tick = 0;
  t->exit_code = 0;
  t->next = NULL;
  t->prev = NULL;
  t->wait_channel = NULL;

  t->kernel_stack_size = TASK_KERNEL_STACK_SIZE;
  t->kernel_stack = (uint8_t *)kmalloc(t->kernel_stack_size);
  if (!t->kernel_stack) {
    t->state = TASK_STATE_UNUSED;
    return NULL;
  }

  t->user_stack = NULL;
  t->user_stack_size = 0;
  t->cr3 = 0;

  uint64_t stack_top = (uint64_t)(t->kernel_stack + t->kernel_stack_size);
  stack_top &= ~0xFULL;

  stack_top -= sizeof(uint64_t);
  *(uint64_t *)stack_top = (uint64_t)arg;
  stack_top -= sizeof(uint64_t);
  *(uint64_t *)stack_top = 0;

  t->context.rsp = stack_top;
  t->context.rbp = stack_top;
  t->context.rip = (uint64_t)entry;
  t->context.rflags = 0x202;
  t->context.rbx = 0;
  t->context.r12 = 0;
  t->context.r13 = 0;
  t->context.r14 = 0;
  t->context.r15 = 0;
  t->context.cr3 = 0;

  t->state = TASK_STATE_READY;
  task_active_count++;
  return t;
}

struct task *task_create_kernel(const char *name, task_entry_fn entry,
                                void *arg) {
  return task_create(name, entry, arg, TASK_PRIORITY_NORMAL);
}

struct task *task_current(void) {
  return current_task;
}

void task_set_current(struct task *t) {
  current_task = t;
}

struct task *task_by_pid(uint32_t pid) {
  for (int i = 0; i < TASK_MAX_COUNT; i++) {
    if (task_table[i].state != TASK_STATE_UNUSED && task_table[i].pid == pid)
      return &task_table[i];
  }
  return NULL;
}

int task_kill(uint32_t pid) {
  struct task *t = task_by_pid(pid);
  if (!t || t->state == TASK_STATE_UNUSED) return -1;
  t->state = TASK_STATE_DEAD;
  if (t->kernel_stack) {
    kfree(t->kernel_stack);
    t->kernel_stack = NULL;
  }
  if (t->user_stack) {
    kfree(t->user_stack);
    t->user_stack = NULL;
  }
  t->state = TASK_STATE_UNUSED;
  if (task_active_count > 0) task_active_count--;
  return 0;
}

void task_exit(int code) {
  if (current_task) {
    current_task->exit_code = code;
    current_task->state = TASK_STATE_ZOMBIE;
  }
  task_yield();
  for (;;) __asm__ volatile("hlt");
}

void task_yield(void) {
  extern void scheduler_yield(void);
  scheduler_yield();
}

void task_sleep(uint64_t ticks) {
  if (!current_task) return;
  extern void scheduler_sleep_current(uint64_t ticks);
  scheduler_sleep_current(ticks);
}

void task_wake(struct task *t) {
  if (!t) return;
  if (t->state == TASK_STATE_SLEEPING || t->state == TASK_STATE_BLOCKED) {
    t->state = TASK_STATE_READY;
  }
}

void task_block(struct task *t, void *channel) {
  if (!t) return;
  t->wait_channel = channel;
  t->state = TASK_STATE_BLOCKED;
}

void task_unblock_channel(void *channel) {
  for (int i = 0; i < TASK_MAX_COUNT; i++) {
    if (task_table[i].state == TASK_STATE_BLOCKED &&
        task_table[i].wait_channel == channel) {
      task_table[i].state = TASK_STATE_READY;
      task_table[i].wait_channel = NULL;
    }
  }
}

size_t task_count(void) {
  return task_active_count;
}

static void task_itoa(uint32_t v, char *buf, int sz) {
  int pos = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
  char tmp[12]; int tp = 0;
  while (v > 0 && tp < 11) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0 && pos < sz - 1; i--) buf[pos++] = tmp[i];
  buf[pos] = 0;
}

static const char *state_names[] = {
  "unused", "created", "ready", "running", "blocked", "sleeping", "zombie", "dead"
};

void task_list(void (*print)(const char *)) {
  char buf[128];
  print("PID  STATE     PRI  NAME\n");
  for (int i = 0; i < TASK_MAX_COUNT; i++) {
    if (task_table[i].state == TASK_STATE_UNUSED) continue;
    char pid_str[8]; task_itoa(task_table[i].pid, pid_str, 8);
    int p = 0;
    for (int j = 0; pid_str[j] && p < 120; j++) buf[p++] = pid_str[j];
    while (p < 5) buf[p++] = ' ';
    const char *st = (task_table[i].state < 8) ? state_names[task_table[i].state] : "?";
    for (int j = 0; st[j] && p < 120; j++) buf[p++] = st[j];
    while (p < 15) buf[p++] = ' ';
    char pri_str[4]; task_itoa(task_table[i].priority, pri_str, 4);
    for (int j = 0; pri_str[j] && p < 120; j++) buf[p++] = pri_str[j];
    while (p < 20) buf[p++] = ' ';
    for (int j = 0; task_table[i].name[j] && p < 126; j++) buf[p++] = task_table[i].name[j];
    buf[p++] = '\n'; buf[p] = 0;
    print(buf);
  }
}
