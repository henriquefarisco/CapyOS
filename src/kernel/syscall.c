#include "kernel/syscall.h"
#include "kernel/syscall_net.h"
#include "kernel/syscall_gfx.h"
#include "kernel/task.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "kernel/user_task_init.h"
#include "kernel/embedded_progs.h"
#include "kernel/pipe.h"
#include "kernel/stdin_buf.h"
#include "fs/vfs.h"
#include "security/csprng.h"
#include "drivers/rtc/rtc.h"
#include "auth/session.h"
#include <stddef.h>

/* 2026-05-02: FD type discriminators (FD_TYPE_FREE/VFS/PIPE) and
 * pipe direction flags (FD_PIPE_FLAG_READ/WRITE) now centralised
 * in include/kernel/process.h. They were previously duplicated
 * here, in src/kernel/process.c and a removed pipe-backed spawn path.
 * Adding a new FD type still requires extending sys_read /
 * sys_write / sys_close in this file AND process_fd_free in
 * src/kernel/process.c in lockstep. */

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
 * fd dispatch (in priority order):
 *   - process fd table slot has FD_TYPE_PIPE + FD_PIPE_FLAG_READ:
 *     pipe_read; if buffer empty and write end still open, busy-yield
 *     until data or EOF. THIS RUNS FIRST so that processes that
 *     install an explicit pipe at fd 0 (e.g. a pipe-backed user
 *     process) get the pipe semantics --
 *     not the legacy stdin_buf shortcut. Without this priority,
 *     the process `capy_read(0, ...)` would silently drain the
 *     kernel keyboard buffer instead of its request pipe and never
 *     observe its request frame.
 *   - fd == 0 with FD_TYPE_FREE slot: drain kernel stdin_buf (legacy
 *     M5 stdin behaviour for hello/capysh). Yields until at least
 *     1 byte arrives, returning whatever is currently buffered.
 *   - VFS / anything else: -1 (not modelled yet). */
int64_t sys_read(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  void *buf = (void *)f->rsi;
  size_t len = (size_t)f->rdx;
  if (!buf || len == 0) return 0;

  struct process *proc = process_current();
  if (proc && fd >= 0 && fd < PROCESS_FD_MAX) {
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
    /* F4 seção c (2026-05-08): POSIX read on a socket FD delegates
     * to the registered net backend (default = socket_recv). The
     * backend internally polls TCP / drains UDP datagrams, mirroring
     * the recv path exactly so a userland binary doing
     * read(sockfd, ...) and recv(sockfd, ..., 0) observe identical
     * semantics. */
    if (slot->type == FD_TYPE_SOCKET) {
      int kernel_fd = (int)(intptr_t)slot->private_data;
      return syscall_net_fd_read(kernel_fd, buf, len);
    }
  }

  /* Legacy fd 0 stdin_buf shortcut: only fires if the caller did NOT
   * install an explicit pipe at fd 0 (i.e. slot type is FD_TYPE_FREE
   * or the caller has no process context). hello/capysh rely on this. */
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
    copied += stdin_buf_pop_many((char *)(dst + copied), len - copied);
    return (int64_t)copied;
  }

  return -1;
}

/* M5 phase D: pipe-aware sys_write.
 *
 * fd dispatch (in priority order):
 *   - process fd table slot has FD_TYPE_PIPE + FD_PIPE_FLAG_WRITE:
 *     pipe_write; on full buffer busy-yield until space (or until
 *     the reader closes the pipe -> -1 broken pipe). THIS RUNS
 *     FIRST so that processes that install an explicit pipe at
 *     fd 1 (e.g. a pipe-backed user process writing events to its response
 *     pipe via fd 1) get the pipe semantics -- not the legacy
 *     debugcon shortcut. Without this priority, every event the
 *     process emits would silently land on port 0xE9 instead of
 *     reaching its supervising runtime.
 *   - fd == 1 or fd == 2 with FD_TYPE_FREE slot: debug-console
 *     (port 0xE9). Legacy stdout/stderr behaviour kept for hello /
 *     capysh and any other binary that has not installed an
 *     explicit fd 1/2 mapping.
 *   - Anything else: -1. */
int64_t sys_write(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  const void *buf = (const void *)f->rsi;
  size_t len = (size_t)f->rdx;
  if (!buf || len == 0) return 0;

  struct process *proc = process_current();
  if (proc && fd >= 0 && fd < PROCESS_FD_MAX) {
    struct file_descriptor *slot = &proc->fds[fd];
    /* F4 seção c (2026-05-08): POSIX write on a socket FD delegates
     * to the registered net backend (default = socket_send). */
    if (slot->type == FD_TYPE_SOCKET) {
      int kernel_fd = (int)(intptr_t)slot->private_data;
      return syscall_net_fd_write(kernel_fd, buf, len);
    }
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
         * closed. Pipes are an IPC transport and large
         * frames must not devolve into dozens of partial user/kernel
         * returns. Cooperatively yield until the reader drains; only
         * fail when the read end is actually closed. A stalled reader
         * blocks this writer task, not the whole system, because the
         * scheduler continues running other tasks between yields. */
        if (pipe_read_end_open(pipe_id) != 1)
          return written > 0 ? (int64_t)written : -1;
        task_yield();
      }
      return (int64_t)written;
    }
  }

  /* Legacy fd 1/2 debugcon shortcut: only fires if the caller did NOT
   * install an explicit pipe at fd 1/2 (i.e. slot type is FD_TYPE_FREE
   * or the caller has no process context). hello/capysh use this. */
  if (fd == 1 || fd == 2) {
#if defined(__x86_64__) && !defined(UNIT_TEST)
    const char *s = (const char *)buf;
    for (size_t i = 0; i < len; i++) {
      __asm__ volatile("outb %0, %1"
                       : : "a"((uint8_t)s[i]), "Nd"((uint16_t)0xE9));
    }
#else
    /* Host unit tests cannot emit IO ports; behave like a sink so
     * the contract "returns len bytes consumed" still holds. The
     * tests/test_syscall_pipe_priority.c regression locks this
     * fallback contract. */
    (void)buf;
#endif
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
  /* FD_TYPE_SOCKET cleanup is handled inside `process_fd_free` via
   * the registered close hook (process_fd_register_socket_close).
   * sys_close therefore does NOT call socket_close directly here:
   * doing so would either double-close (process_fd_free runs the
   * hook again) or require a flag to skip the second call. The
   * single-source-of-truth in process_fd_free is simpler. */
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

/* Etapa 5 / Slice 5.1: SYS_GETRANDOM — userland CSPRNG entropy.
 *
 * Fills the caller's buffer with up to SYS_GETRANDOM_MAX_PER_CALL bytes
 * from the audited in-tree CSPRNG (src/security/csprng.c). This is the
 * userland entropy source libcapy-tls needs to seed BearSSL's DRBG in
 * ring 3 — the prerequisite gap for Etapa 5. TLS itself is untouched in
 * this slice.
 *
 * ABI: rdi = buf, rsi = len, rdx = flags (reserved, must be 0).
 * Returns the number of bytes written (0..min(len, MAX)); -1 on a
 * non-zero `flags` or a NULL buffer with len > 0. The per-call cap
 * bounds the in-kernel fill so a hostile `len` cannot turn one syscall
 * into an unbounded kernel-side write; callers needing more loop. */
#define SYS_GETRANDOM_MAX_PER_CALL 256u
static int64_t sys_getrandom(struct syscall_frame *f) {
  void *buf = (void *)f->rdi;
  size_t len = (size_t)f->rsi;
  unsigned int flags = (unsigned int)f->rdx;
  if (flags != 0u) return -1;
  if (len == 0u) return 0;
  if (!buf) return -1;
  if (len > SYS_GETRANDOM_MAX_PER_CALL) len = SYS_GETRANDOM_MAX_PER_CALL;
  csprng_get_bytes(buf, len);
  return (int64_t)len;
}

/* Etapa 5 / Slice 5.x: SYS_CLOCK_REALTIME — userland wall-clock.
 *
 * Returns seconds since the Unix epoch from the kernel RTC. Unlike
 * SYS_TIME (which returns APIC ticks since boot), this is real calendar
 * time — required by libcapy-tls to evaluate X.509 certificate validity
 * (notBefore/notAfter) in ring 3. No arguments. */
static int64_t sys_clock_realtime(struct syscall_frame *f) {
  (void)f;
  return (int64_t)rtc_unix_timestamp();
}

/* Etapa 6 / Slice 6.7: SYS_GET_SESSION_LANG — active session UI language.
 *
 * Returns a small stable code (CAPY_SESSION_LANG_*) for the logged-in
 * session's language so a ring-3 app (CapyBrowse Text) can localize its
 * user-facing diagnostics instead of hardcoding a base language. No session
 * (or an empty language) maps to PT-BR, mirroring app_current_language()'s
 * default and the locked selection-default invariant; an unrecognized
 * non-empty language falls back to EN, the universal string-fallback base.
 * Reads the session string directly (auth/session is always linked). No
 * arguments. es-before-en because both begin with 'e'. */
static int64_t sys_get_session_lang(struct syscall_frame *f) {
  (void)f;
  struct session_context *sess = session_active();
  const char *lang = sess ? session_language(sess) : NULL;
  if (!lang || !lang[0]) return CAPY_SESSION_LANG_PT_BR;
  if ((lang[0] == 'e' || lang[0] == 'E') && (lang[1] == 's' || lang[1] == 'S'))
    return CAPY_SESSION_LANG_ES;
  if (lang[0] == 'e' || lang[0] == 'E') return CAPY_SESSION_LANG_EN;
  if (lang[0] == 'p' || lang[0] == 'P') return CAPY_SESSION_LANG_PT_BR;
  return CAPY_SESSION_LANG_EN;
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
  syscall_table[SYS_GETRANDOM] = sys_getrandom;
  syscall_table[SYS_CLOCK_REALTIME] = sys_clock_realtime;
  syscall_table[SYS_GET_SESSION_LANG] = sys_get_session_lang;
  syscall_table[SYS_FORK]    = sys_fork;
  syscall_table[SYS_EXEC]    = sys_exec;
  syscall_table[SYS_WAIT]    = sys_wait;
  syscall_table[SYS_PIPE]    = sys_pipe;

  /* F4 seção c (2026-05-08): wire SYS_SOCKET (28) .. SYS_RECV (34)
   * through the dedicated module. The actual net backend is wired
   * separately by `syscall_net_install_default_ops` during boot,
   * so the table entries fail with -1 until that runs. */
  syscall_net_register_handlers();

  /* Etapa 7 / Slice 7.2: wire SYS_WINDOW_CREATE..SYS_WINDOW_DESTROY (45..50).
   * The compositor-backed backend is installed separately at boot
   * (syscall_gfx_install_default_ops) so the table entries fail with -1 until
   * that runs; host tests inject a fake backend instead. */
  syscall_gfx_register_handlers();

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
