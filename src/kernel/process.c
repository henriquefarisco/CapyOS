#include "kernel/process.h"
#include "kernel/task.h"
#include "memory/vmm.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct process proc_table[PROCESS_MAX];
static uint32_t next_proc_pid = 1;
static struct process *current_proc = NULL;

static void proc_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void proc_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

void process_system_init(void) {
  proc_memset(proc_table, 0, sizeof(proc_table));
  next_proc_pid = 1;
  current_proc = NULL;
}

struct process *process_create(const char *name, uint32_t uid, uint32_t gid) {
  for (int i = 0; i < PROCESS_MAX; i++) {
    if (proc_table[i].state == PROC_STATE_UNUSED) {
      struct process *p = &proc_table[i];
      proc_memset(p, 0, sizeof(*p));
      p->pid = next_proc_pid++;
      p->ppid = current_proc ? current_proc->pid : 0;
      proc_strcpy(p->name, name ? name : "unnamed", PROCESS_NAME_MAX);
      p->state = PROC_STATE_EMBRYO;
      p->uid = uid;
      p->gid = gid;
      p->address_space = vmm_create_address_space();
      if (!p->address_space) {
        p->state = PROC_STATE_UNUSED;
        return NULL;
      }
      p->parent = current_proc;
      return p;
    }
  }
  return NULL;
}

struct process *process_fork(struct process *parent) {
  if (!parent) return NULL;
  struct process *child = process_create(parent->name, parent->uid, parent->gid);
  if (!child) return NULL;
  child->ppid = parent->pid;
  child->parent = parent;
  child->brk = parent->brk;
  child->heap_start = parent->heap_start;
  child->stack_top = parent->stack_top;
  return child;
}

int process_exec(struct process *proc, const char *path, const char **argv) {
  (void)argv;
  if (!proc || !path) return -1;
  extern int elf_load_from_file(struct process *proc, const char *path);
  return elf_load_from_file(proc, path);
}

void process_exit(int code) {
  if (!current_proc) {
    for (;;) __asm__ volatile("hlt");
  }
  current_proc->exit_code = code;
  current_proc->state = PROC_STATE_ZOMBIE;
  task_exit(code);
}

int process_wait(uint32_t pid, int *status) {
  struct process *child = process_by_pid(pid);
  if (!child) return -1;
  while (child->state != PROC_STATE_ZOMBIE) {
    task_yield();
  }
  if (status) *status = child->exit_code;
  if (child->address_space) {
    vmm_destroy_address_space(child->address_space);
    child->address_space = NULL;
  }
  child->state = PROC_STATE_UNUSED;
  return 0;
}

struct process *process_current(void) {
  return current_proc;
}

void process_set_current(struct process *p) {
  current_proc = p;
}

struct process *process_by_pid(uint32_t pid) {
  for (int i = 0; i < PROCESS_MAX; i++) {
    if (proc_table[i].state != PROC_STATE_UNUSED && proc_table[i].pid == pid)
      return &proc_table[i];
  }
  return NULL;
}

int process_kill(uint32_t pid, int signal) {
  struct process *p = process_by_pid(pid);
  if (!p) return -1;
  (void)signal;
  p->state = PROC_STATE_ZOMBIE;
  if (p->main_thread) task_kill(p->main_thread->pid);
  return 0;
}

int process_fd_alloc(struct process *proc) {
  if (!proc) return -1;
  for (int i = 3; i < PROCESS_FD_MAX; i++) {
    if (proc->fds[i].type == 0) return i;
  }
  return -1;
}

void process_fd_free(struct process *proc, int fd) {
  if (!proc || fd < 0 || fd >= PROCESS_FD_MAX) return;
  proc->fds[fd].type = 0;
  proc->fds[fd].private_data = NULL;
  proc->fds[fd].flags = 0;
  proc->fds[fd].offset = 0;
}

size_t process_count(void) {
  size_t n = 0;
  for (int i = 0; i < PROCESS_MAX; i++) {
    if (proc_table[i].state != PROC_STATE_UNUSED) n++;
  }
  return n;
}

void process_list(void (*print)(const char *)) {
  if (!print) return;
  print("PID  PPID STATE   UID  NAME\n");
  char buf[128];
  for (int i = 0; i < PROCESS_MAX; i++) {
    struct process *p = &proc_table[i];
    if (p->state == PROC_STATE_UNUSED) continue;
    int pos = 0;
    char tmp[12];
    int tp;
    /* pid */
    tp = 0; uint32_t v = p->pid;
    if (v == 0) tmp[tp++] = '0';
    else { char r[12]; int rp = 0; while (v) { r[rp++] = '0' + v % 10; v /= 10; }
           for (int j = rp - 1; j >= 0; j--) tmp[tp++] = r[j]; }
    for (int j = 0; j < tp && pos < 120; j++) buf[pos++] = tmp[j];
    while (pos < 5) buf[pos++] = ' ';
    /* ppid */
    tp = 0; v = p->ppid;
    if (v == 0) tmp[tp++] = '0';
    else { char r[12]; int rp = 0; while (v) { r[rp++] = '0' + v % 10; v /= 10; }
           for (int j = rp - 1; j >= 0; j--) tmp[tp++] = r[j]; }
    for (int j = 0; j < tp && pos < 120; j++) buf[pos++] = tmp[j];
    while (pos < 10) buf[pos++] = ' ';
    /* state */
    const char *st = "?";
    switch (p->state) {
      case PROC_STATE_EMBRYO: st = "embryo"; break;
      case PROC_STATE_RUNNING: st = "run"; break;
      case PROC_STATE_SLEEPING: st = "sleep"; break;
      case PROC_STATE_ZOMBIE: st = "zombie"; break;
      default: break;
    }
    for (int j = 0; st[j] && pos < 120; j++) buf[pos++] = st[j];
    while (pos < 18) buf[pos++] = ' ';
    /* uid */
    tp = 0; v = p->uid;
    if (v == 0) tmp[tp++] = '0';
    else { char r[12]; int rp = 0; while (v) { r[rp++] = '0' + v % 10; v /= 10; }
           for (int j = rp - 1; j >= 0; j--) tmp[tp++] = r[j]; }
    for (int j = 0; j < tp && pos < 120; j++) buf[pos++] = tmp[j];
    while (pos < 23) buf[pos++] = ' ';
    /* name */
    for (int j = 0; p->name[j] && pos < 126; j++) buf[pos++] = p->name[j];
    buf[pos++] = '\n'; buf[pos] = 0;
    print(buf);
  }
}
