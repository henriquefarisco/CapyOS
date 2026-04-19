#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <stdint.h>
#include <stddef.h>

#define TASK_NAME_MAX 32
#define TASK_MAX_COUNT 64
#define TASK_KERNEL_STACK_SIZE 16384
#define TASK_USER_STACK_SIZE 65536

enum task_state {
  TASK_STATE_UNUSED = 0,
  TASK_STATE_CREATED,
  TASK_STATE_READY,
  TASK_STATE_RUNNING,
  TASK_STATE_BLOCKED,
  TASK_STATE_SLEEPING,
  TASK_STATE_ZOMBIE,
  TASK_STATE_DEAD
};

enum task_priority {
  TASK_PRIORITY_IDLE = 0,
  TASK_PRIORITY_LOW = 1,
  TASK_PRIORITY_NORMAL = 2,
  TASK_PRIORITY_HIGH = 3,
  TASK_PRIORITY_REALTIME = 4
};

struct task_context {
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t rflags;
  uint64_t cr3;
};

struct task {
  uint32_t pid;
  uint32_t ppid;
  char name[TASK_NAME_MAX];
  enum task_state state;
  enum task_priority priority;
  struct task_context context;
  uint8_t *kernel_stack;
  uint8_t *user_stack;
  size_t kernel_stack_size;
  size_t user_stack_size;
  uint64_t cr3;
  uint32_t uid;
  uint32_t gid;
  uint64_t cpu_time_ns;
  uint64_t wake_tick;
  int exit_code;
  uint32_t quantum_remaining;
  struct task *next;
  struct task *prev;
  void *wait_channel;
};

typedef void (*task_entry_fn)(void *arg);

void task_system_init(void);
struct task *task_create(const char *name, task_entry_fn entry, void *arg,
                         enum task_priority priority);
struct task *task_create_kernel(const char *name, task_entry_fn entry,
                                void *arg);
struct task *task_current(void);
struct task *task_by_pid(uint32_t pid);
int task_kill(uint32_t pid);
void task_exit(int code) __attribute__((noreturn));
void task_yield(void);
void task_sleep(uint64_t ticks);
void task_wake(struct task *t);
void task_block(struct task *t, void *channel);
void task_unblock_channel(void *channel);
struct task *task_at_index(size_t index);
size_t task_count(void);
void task_list(void (*print)(const char *));

#endif /* KERNEL_TASK_H */
