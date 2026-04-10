#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/task.h"
#include "memory/vmm.h"

#define PROCESS_MAX 128
#define PROCESS_FD_MAX 32
#define PROCESS_NAME_MAX 64
#define PROCESS_ARG_MAX 256

enum process_state {
  PROC_STATE_UNUSED = 0,
  PROC_STATE_EMBRYO,
  PROC_STATE_RUNNING,
  PROC_STATE_SLEEPING,
  PROC_STATE_ZOMBIE
};

struct file_descriptor {
  int type;
  uint32_t flags;
  uint64_t offset;
  void *private_data;
};

struct process {
  uint32_t pid;
  uint32_t ppid;
  char name[PROCESS_NAME_MAX];
  enum process_state state;
  struct vmm_address_space *address_space;
  struct task *main_thread;
  struct file_descriptor fds[PROCESS_FD_MAX];
  uint32_t uid;
  uint32_t gid;
  int exit_code;
  uint64_t brk;
  uint64_t heap_start;
  uint64_t stack_top;
  struct process *parent;
  struct process *children;
  struct process *next_sibling;
};

void process_system_init(void);
struct process *process_create(const char *name, uint32_t uid, uint32_t gid);
struct process *process_fork(struct process *parent);
int process_exec(struct process *proc, const char *path, const char **argv);
void process_exit(int code) __attribute__((noreturn));
int process_wait(uint32_t pid, int *status);
struct process *process_current(void);
struct process *process_by_pid(uint32_t pid);
int process_kill(uint32_t pid, int signal);
int process_fd_alloc(struct process *proc);
void process_fd_free(struct process *proc, int fd);
size_t process_count(void);
void process_list(void (*print)(const char *));

#endif /* KERNEL_PROCESS_H */
