#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/process.h"
#include "fs/vfs.h"
#include <stddef.h>

static syscall_handler_fn syscall_table[SYSCALL_COUNT];

extern void syscall_init_msr(void);

static int64_t sys_exit(struct syscall_frame *f) {
  task_exit((int)f->rdi);
  return 0;
}

static int64_t sys_read(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  void *buf = (void *)f->rsi;
  size_t len = (size_t)f->rdx;
  (void)fd; (void)buf; (void)len;
  return -1;
}

static int64_t sys_write(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  const void *buf = (const void *)f->rsi;
  size_t len = (size_t)f->rdx;
  if (fd == 1 || fd == 2) {
    const char *s = (const char *)buf;
    for (size_t i = 0; i < len; i++) {
      __asm__ volatile("outb %0, %1" : : "a"((uint8_t)s[i]), "Nd"((uint16_t)0xE9));
    }
    return (int64_t)len;
  }
  return -1;
}

static int64_t sys_open(struct syscall_frame *f) {
  const char *path = (const char *)f->rdi;
  uint32_t flags = (uint32_t)f->rsi;
  struct file *file = vfs_open(path, flags);
  if (!file) return -1;
  struct process *proc = process_current();
  if (!proc) { vfs_close(file); return -1; }
  int fd = process_fd_alloc(proc);
  if (fd < 0) { vfs_close(file); return -1; }
  proc->fds[fd].private_data = file;
  proc->fds[fd].type = 1;
  proc->fds[fd].flags = flags;
  proc->fds[fd].offset = 0;
  return fd;
}

static int64_t sys_close(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  struct process *proc = process_current();
  if (!proc || fd < 0 || fd >= PROCESS_FD_MAX) return -1;
  if (proc->fds[fd].type == 0) return -1;
  struct file *file = (struct file *)proc->fds[fd].private_data;
  if (file) vfs_close(file);
  process_fd_free(proc, fd);
  return 0;
}

static int64_t sys_getpid(struct syscall_frame *f) {
  (void)f;
  struct task *t = task_current();
  return t ? (int64_t)t->pid : (int64_t)-1;
}

static int64_t sys_getppid(struct syscall_frame *f) {
  (void)f;
  struct task *t = task_current();
  return t ? (int64_t)t->ppid : (int64_t)-1;
}

static int64_t sys_yield(struct syscall_frame *f) {
  (void)f;
  task_yield();
  return 0;
}

static int64_t sys_sleep(struct syscall_frame *f) {
  uint64_t ticks = f->rdi;
  task_sleep(ticks);
  return 0;
}

static int64_t sys_getuid(struct syscall_frame *f) {
  (void)f;
  struct process *proc = process_current();
  return proc ? (int64_t)proc->uid : (int64_t)-1;
}

static int64_t sys_getgid(struct syscall_frame *f) {
  (void)f;
  struct process *proc = process_current();
  return proc ? (int64_t)proc->gid : (int64_t)-1;
}

static int64_t sys_brk(struct syscall_frame *f) {
  uint64_t new_brk = f->rdi;
  struct process *proc = process_current();
  if (!proc) return -1;
  if (new_brk == 0) return (int64_t)proc->brk;
  if (new_brk < proc->heap_start) return -1;
  proc->brk = new_brk;
  return (int64_t)proc->brk;
}

static int64_t sys_mkdir(struct syscall_frame *f) {
  const char *path = (const char *)f->rdi;
  struct vfs_metadata meta = {0, 0, 0x1FF};
  return vfs_create(path, VFS_MODE_DIR, &meta);
}

static int64_t sys_unlink(struct syscall_frame *f) {
  const char *path = (const char *)f->rdi;
  return vfs_unlink(path);
}

static int64_t sys_rmdir(struct syscall_frame *f) {
  const char *path = (const char *)f->rdi;
  return vfs_rmdir(path);
}

static int64_t sys_getcwd(struct syscall_frame *f) {
  (void)f;
  return -1;
}

static int64_t sys_chdir(struct syscall_frame *f) {
  (void)f;
  return -1;
}

static int64_t sys_time(struct syscall_frame *f) {
  (void)f;
  extern uint64_t apic_timer_ticks(void);
  return (int64_t)apic_timer_ticks();
}

void syscall_init(void) {
  for (int i = 0; i < SYSCALL_COUNT; i++) syscall_table[i] = NULL;

  syscall_table[SYS_EXIT]    = sys_exit;
  syscall_table[SYS_READ]    = sys_read;
  syscall_table[SYS_WRITE]   = sys_write;
  syscall_table[SYS_OPEN]    = sys_open;
  syscall_table[SYS_CLOSE]   = sys_close;
  syscall_table[SYS_BRK]     = sys_brk;
  syscall_table[SYS_GETPID]  = sys_getpid;
  syscall_table[SYS_GETPPID] = sys_getppid;
  syscall_table[SYS_YIELD]   = sys_yield;
  syscall_table[SYS_SLEEP]   = sys_sleep;
  syscall_table[SYS_MKDIR]   = sys_mkdir;
  syscall_table[SYS_UNLINK]  = sys_unlink;
  syscall_table[SYS_RMDIR]   = sys_rmdir;
  syscall_table[SYS_GETCWD]  = sys_getcwd;
  syscall_table[SYS_CHDIR]   = sys_chdir;
  syscall_table[SYS_GETUID]  = sys_getuid;
  syscall_table[SYS_GETGID]  = sys_getgid;
  syscall_table[SYS_TIME]    = sys_time;

  syscall_init_msr();
}

void syscall_register(uint32_t num, syscall_handler_fn handler) {
  if (num < SYSCALL_COUNT) syscall_table[num] = handler;
}

int64_t syscall_dispatch(struct syscall_frame *frame) {
  uint32_t num = (uint32_t)frame->rax;
  if (num >= SYSCALL_COUNT || !syscall_table[num]) return -1;
  return syscall_table[num](frame);
}
