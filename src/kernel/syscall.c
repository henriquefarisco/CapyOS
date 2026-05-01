#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "kernel/user_task_init.h"
#include "kernel/embedded_progs.h"
#include "kernel/pipe.h"
#include "kernel/stdin_buf.h"
#include "fs/vfs.h"
#include <stddef.h>

/* M5 phase D: process FD types. Type 0 stays "free slot"; type 1
 * is the existing VFS file binding (sys_open). Type 2 is a pipe
 * end produced by sys_pipe; the `flags` field carries the
 * direction (FD_PIPE_FLAG_READ for the read end, FD_PIPE_FLAG_WRITE
 * for the write end) and `private_data` carries the kernel pipe
 * id cast through (void *).
 *
 * Adding a new FD type means picking the next integer here, then
 * extending the switch in sys_read/sys_write/sys_close. */
#define FD_TYPE_FREE 0
#define FD_TYPE_VFS  1
#define FD_TYPE_PIPE 2

#define FD_PIPE_FLAG_READ  0x1u
#define FD_PIPE_FLAG_WRITE 0x2u

static syscall_handler_fn syscall_table[SYSCALL_COUNT];

extern void syscall_init_msr(void);

/* M5 phase C.1: SYS_EXIT now routes through `process_exit` so the
 * process slot is flipped to PROC_STATE_ZOMBIE before the underlying
 * task is yielded out. Without this, a parent calling `process_wait`
 * would spin forever on `child->state != PROC_STATE_ZOMBIE` because
 * `task_exit` only mutated task state, leaving the process slot at
 * PROC_STATE_RUNNING. `process_exit` is __attribute__((noreturn)). */
static int64_t sys_exit(struct syscall_frame *f) {
  process_exit((int)f->rdi);
}

/* M5 phase D: pipe-aware sys_read.
 *
 * fd dispatch:
 *   - PIPE FDs with FD_PIPE_FLAG_READ: pipe_read; if buffer empty
 *     and write end still open, busy-yield until data or EOF.
 *   - Anything else: -1 (stdin / VFS-file read not modelled yet). */
static int64_t sys_read(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  void *buf = (void *)f->rsi;
  size_t len = (size_t)f->rdx;
  if (!buf || len == 0) return 0;

  /* M5 phase E.2: fd 0 (stdin) drains the kernel stdin_buf with
   * cooperative blocking. We yield until at least 1 byte is
   * available, then return whatever is currently buffered (up to
   * `len`). This matches POSIX read-1-or-more semantics on a
   * blocking tty -- callers expecting line buffering implement it
   * in user space (capysh accumulates until '\n'). */
  if (fd == 0) {
    uint8_t *dst = (uint8_t *)buf;
    size_t copied = 0;
    /* Block until we get at least one byte. */
    for (;;) {
      char c;
      if (stdin_buf_pop(&c)) {
        dst[copied++] = (uint8_t)c;
        break;
      }
      task_yield();
    }
    /* Drain the rest WITHOUT blocking; if the buffer becomes empty
     * we return what we already have. */
    while (copied < len) {
      char c;
      if (!stdin_buf_pop(&c)) break;
      dst[copied++] = (uint8_t)c;
    }
    return (int64_t)copied;
  }

  struct process *proc = process_current();
  if (!proc || fd < 0 || fd >= PROCESS_FD_MAX) return -1;
  struct file_descriptor *slot = &proc->fds[fd];

  if (slot->type == FD_TYPE_PIPE && (slot->flags & FD_PIPE_FLAG_READ)) {
    int pipe_id = (int)(intptr_t)slot->private_data;
    for (;;) {
      int rd = pipe_read(pipe_id, buf, len);
      if (rd > 0) return (int64_t)rd;
      if (rd == 0) return 0;            /* EOF: write end closed */
      /* rd < 0 means "would block" (buffer empty and write end
       * still open). Yield and retry. The yield path is
       * cooperative; preemptive scheduling lets the writer
       * make progress in the meantime. */
      task_yield();
    }
  }
  return -1;
}

/* M5 phase D: pipe-aware sys_write.
 *
 * fd dispatch:
 *   - 1 (stdout) / 2 (stderr): debug-console (port 0xE9). Same as
 *     before for parity with the boot-time `hello` smoke chain.
 *   - PIPE FDs with FD_PIPE_FLAG_WRITE: pipe_write; on full buffer
 *     busy-yield until space (or until reader closes the pipe ->
 *     -1 broken pipe).
 *   - Anything else: -1. */
static int64_t sys_write(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  const void *buf = (const void *)f->rsi;
  size_t len = (size_t)f->rdx;
  if (!buf || len == 0) return 0;

  if (fd == 1 || fd == 2) {
    const char *s = (const char *)buf;
    for (size_t i = 0; i < len; i++) {
      __asm__ volatile("outb %0, %1"
                       : : "a"((uint8_t)s[i]), "Nd"((uint16_t)0xE9));
    }
    return (int64_t)len;
  }

  struct process *proc = process_current();
  if (!proc || fd < 0 || fd >= PROCESS_FD_MAX) return -1;
  struct file_descriptor *slot = &proc->fds[fd];

  if (slot->type == FD_TYPE_PIPE && (slot->flags & FD_PIPE_FLAG_WRITE)) {
    int pipe_id = (int)(intptr_t)slot->private_data;
    size_t written = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (written < len) {
      int wr = pipe_write(pipe_id, src + written, len - written);
      if (wr > 0) {
        written += (size_t)wr;
        continue;
      }
      /* wr < 0: either buffer full (would block) or read end
       * closed. We disambiguate by yielding once and retrying;
       * if the caller has already partially written we return
       * the partial count rather than -1 to match POSIX-ish
       * semantics. */
      if (written > 0) return (int64_t)written;
      task_yield();
    }
    return (int64_t)written;
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
  proc->fds[fd].type = FD_TYPE_VFS;
  proc->fds[fd].flags = flags;
  proc->fds[fd].offset = 0;
  return fd;
}

/* M5 phase D: pipe-aware sys_close.
 *
 * Behaviour by fd type:
 *   - VFS file: vfs_close + free slot.
 *   - PIPE read end: pipe_close_read + free slot.
 *   - PIPE write end: pipe_close_write + free slot.
 *
 * Either end of a pipe being closed flips the corresponding
 * `read_open`/`write_open` flag; future reads see EOF (rd == 0)
 * and future writes see broken-pipe (-1). The kernel pipe slot
 * itself is reclaimed lazily once BOTH ends are closed (caller
 * calls sys_close on each end). */
static int64_t sys_close(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  struct process *proc = process_current();
  if (!proc || fd < 0 || fd >= PROCESS_FD_MAX) return -1;
  struct file_descriptor *slot = &proc->fds[fd];
  if (slot->type == FD_TYPE_FREE) return -1;

  if (slot->type == FD_TYPE_VFS) {
    struct file *file = (struct file *)slot->private_data;
    if (file) vfs_close(file);
  } else if (slot->type == FD_TYPE_PIPE) {
    int pipe_id = (int)(intptr_t)slot->private_data;
    if (slot->flags & FD_PIPE_FLAG_READ) {
      pipe_close_read(pipe_id);
    } else if (slot->flags & FD_PIPE_FLAG_WRITE) {
      pipe_close_write(pipe_id);
    }
  }
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

/* M5 phase A.3: SYS_FORK.
 *
 * Splits the current process into two: parent gets the child PID
 * back in RAX, child gets 0 in RAX (synthesized via the fork-frame
 * builder) and resumes execution at the same user-space instruction
 * the parent is about to return to (one past `syscall`).
 *
 * Steps:
 *   1. Resolve the calling process via `process_current()`. A NULL
 *      result means SYS_FORK was invoked from a context without a
 *      process (kernel-only thread, init path); we refuse with -1.
 *   2. `process_fork` allocates a new process slot, links it as a
 *      child of the parent, and CoW-clones the parent's address
 *      space (M4 phase 7c). Returns NULL on OOM or table-full.
 *   3. The freshly allocated child task already has a kernel stack
 *      from `task_create`. We arm it via `user_task_arm_for_fork`
 *      so its first context_switch lands on the user-first trampoline
 *      and `iretq`s into ring 3 with rax=0 and the parent's
 *      RIP/RSP/RFLAGS.
 *   4. `scheduler_add` makes the child runnable. The parent stays
 *      on-CPU and returns immediately with the child PID.
 *
 * Failure modes:
 *   - parent NULL                          -> -1.
 *   - process_fork failed                  -> -1.
 *   - child has no main_thread or stack    -> destroy + -1. */
static int64_t sys_fork(struct syscall_frame *f) {
  struct process *parent = process_current();
  if (!parent) return -1;
  struct process *child = process_fork(parent);
  if (!child) return -1;
  if (!child->main_thread || !child->main_thread->kernel_stack) {
    process_destroy(child);
    return -1;
  }
  user_task_arm_for_fork(child->main_thread, f);
  scheduler_add(child->main_thread);
  return (int64_t)child->pid;
}

/* M5 phase D: SYS_PIPE.
 *
 * Allocates a fresh kernel pipe via `pipe_create` and binds its
 * two ends to two newly allocated process FDs. The user-supplied
 * `int fds[2]` (rdi) receives the read end at index 0 and the
 * write end at index 1, matching POSIX `pipe(2)` semantics.
 *
 * Failure modes (return -1, leave fds[] untouched as far as we
 * know -- partial state is rolled back if the second FD alloc
 * fails after the first succeeds):
 *
 *   - no current process,
 *   - pipe_create returns -1 (table full),
 *   - process_fd_alloc returns -1 for either end (FD table full).
 *
 * On success returns 0. */
static int64_t sys_pipe(struct syscall_frame *f) {
  int *user_fds = (int *)f->rdi;
  if (!user_fds) return -1;

  struct process *proc = process_current();
  if (!proc) return -1;

  int kfds[2];
  if (pipe_create(kfds) != 0) return -1;
  int kernel_pipe_id = kfds[0]; /* both halves alias the same pipe id;
                                   pipe_create returns kfds[1] = id+256
                                   only as a host-side disambiguator. */

  int read_fd = process_fd_alloc(proc);
  if (read_fd < 0) {
    pipe_close_read(kernel_pipe_id);
    pipe_close_write(kernel_pipe_id);
    return -1;
  }
  proc->fds[read_fd].type = FD_TYPE_PIPE;
  proc->fds[read_fd].flags = FD_PIPE_FLAG_READ;
  proc->fds[read_fd].private_data = (void *)(intptr_t)kernel_pipe_id;
  proc->fds[read_fd].offset = 0;

  int write_fd = process_fd_alloc(proc);
  if (write_fd < 0) {
    process_fd_free(proc, read_fd);
    pipe_close_read(kernel_pipe_id);
    pipe_close_write(kernel_pipe_id);
    return -1;
  }
  proc->fds[write_fd].type = FD_TYPE_PIPE;
  proc->fds[write_fd].flags = FD_PIPE_FLAG_WRITE;
  proc->fds[write_fd].private_data = (void *)(intptr_t)kernel_pipe_id;
  proc->fds[write_fd].offset = 0;

  user_fds[0] = read_fd;
  user_fds[1] = write_fd;
  return 0;
}

/* M5 phase C.2: SYS_WAIT.
 *
 * Block the calling process until the child identified by `pid`
 * (rdi) reaches PROC_STATE_ZOMBIE, then write the child's exit
 * code through the user pointer `status` (rsi) and reap the slot.
 *
 * Arguments:
 *   rdi = uint32_t pid
 *   rsi = int *status      (NULL = caller does not care)
 *
 * Return value:
 *   On success: the reaped child's PID (matches POSIX waitpid).
 *   On failure: -1 (no current process, pid does not name an
 *               existing process slot, etc.).
 *
 * Today `process_wait` busy-yields via `task_yield()` until the
 * child becomes a zombie; a future sub-phase will replace that
 * with a `task_block` on the child's process address as wait
 * channel and have `process_exit` wake the parent. */
static int64_t sys_wait(struct syscall_frame *f) {
  uint32_t pid = (uint32_t)f->rdi;
  int *status = (int *)f->rsi;

  struct process *parent = process_current();
  if (!parent) return -1;

  if (process_wait(pid, status) != 0) return -1;
  return (int64_t)pid;
}

/* M5 phase B.4: SYS_EXEC.
 *
 * Replaces the calling process's address space with a fresh image
 * resolved by `path` against the in-kernel embedded-progs registry
 * and overrides the syscall return frame so the impending sysret
 * lands on the new entry point with a fresh user RSP.
 *
 * Arguments:
 *   rdi = const char *path  (kernel-trusted; user pointer)
 *   rsi = const char **argv (ignored for now -- argv packing lands
 *                            in a later sub-phase)
 *
 * Return value:
 *   On success the function does NOT effectively return to the
 *   caller's user-space code: the syscall return path's sysret
 *   uses the OVERRIDDEN frame->rcx (user RIP), frame->r11 (user
 *   RFLAGS) and frame->rsp (user RSP) and lands on the new image's
 *   `_start`. We still return 0 from C so the asm tail's
 *   `addq $8, %rsp` (skip rax slot) sees a defined value, but it
 *   is never observable from userspace.
 *
 *   On failure (NULL path, no current process, lookup miss, ELF
 *   validation fails, AS replacement fails) the function returns
 *   -1 and leaves the syscall_frame untouched, so sysret resumes
 *   the caller right after `syscall` with rax=-1. The caller's
 *   user-space stub (`capy_exec`) propagates -1 to its C return
 *   value verbatim.
 *
 * Failure modes:
 *   - path NULL                -> -1
 *   - no current process       -> -1
 *   - lookup miss              -> -1
 *   - process_exec_replace fail-> -1 (rolled back; AS untouched) */
static int64_t sys_exec(struct syscall_frame *f) {
  const char *path = (const char *)f->rdi;
  /* (const char **)f->rsi is argv; intentionally unused this phase. */
  if (!path) return -1;

  struct process *proc = process_current();
  if (!proc || !proc->main_thread) return -1;

  const uint8_t *data = NULL;
  size_t size = 0;
  if (embedded_progs_lookup(path, &data, &size) != 0) return -1;

  if (process_exec_replace(proc, data, size) != 0) return -1;

  /* Override the syscall return so sysret lands at the new image's
   * entry point with a fresh user RSP. The asm tail of
   * syscall_entry pops user RIP from frame->rcx, RFLAGS from
   * frame->r11, and user RSP from frame->rsp. */
  f->rcx = proc->main_thread->context.rip;
  f->rsp = proc->main_thread->context.rsp;
  f->r11 = (uint64_t)USER_TASK_USER_RFLAGS;  /* IF=1, IOPL=0 */
  return 0;
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
  syscall_table[SYS_FORK]    = sys_fork;
  syscall_table[SYS_EXEC]    = sys_exec;
  syscall_table[SYS_WAIT]    = sys_wait;
  syscall_table[SYS_PIPE]    = sys_pipe;

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
