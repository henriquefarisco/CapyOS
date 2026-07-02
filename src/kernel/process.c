#include "kernel/process.h"
#include "kernel/task.h"
#include "kernel/elf_loader.h"
#include "kernel/pipe.h"
#include "fs/vfs.h"
#include "memory/vmm.h"
#include "memory/kmem.h"
#include <stddef.h>

#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
#include "kernel/thread_crash_smoke.h"
#endif

#ifdef CAPYOS_TLS_HANDSHAKE_SMOKE
#include "kernel/tls_handshake_smoke.h"
#endif

#ifdef CAPYOS_CAPYBROWSE_SMOKE
#include "kernel/capybrowse_text_smoke.h"
#endif

#if defined(CAPYOS_GFX_SMOKE) || defined(CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE)
#include "kernel/capygfx_smoke.h"
#endif

#ifdef CAPYOS_MULTIFETCH_SMOKE
#include "kernel/capymultifetch_smoke.h"
#endif

/* 2026-05-02: FD type and pipe direction constants now live in
 * include/kernel/process.h (FD_TYPE_*, FD_PIPE_FLAG_*). Removed
 * the local copies that duplicated them. */

static void process_dbgcon_putc(uint8_t c) {
#if defined(__x86_64__) && !defined(UNIT_TEST)
  __asm__ volatile("outb %0, $0xE9" : : "a"(c));
#else
  (void)c;
#endif
}

static struct process proc_table[PROCESS_MAX];
static uint32_t next_proc_pid = 1;
static struct process *current_proc = NULL;

/* Etapa 7 / Slice 7.2: process-teardown observer (e.g. syscall_gfx_release_owner)
 * called with the dying pid from process_exit / process_destroy. Declared here,
 * ahead of those functions; registered via process_register_teardown_observer
 * below. NULL by default (host tests + no-GUI profile pay nothing). */
static process_teardown_fn g_teardown_fn = NULL;

static void proc_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void proc_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

/* M4 phase 6.5: process-tree linkage helpers.
 *
 * The `struct process` carries `parent`, `children`, and
 * `next_sibling` fields that model the kernel's process tree, but
 * before phase 6.5 only `parent` was populated. The children list
 * was dead code, and `process_destroy` could not orphan children
 * or detach itself from its parent. These two helpers centralise
 * the discipline so all entry points (process_create, process_fork,
 * process_destroy) keep the tree consistent.
 *
 * Discipline:
 *   - `child->parent` is the live owner of `child->next_sibling`.
 *     NULL means the child has been orphaned or never had a parent.
 *   - A process's `children` head is the most recently linked child
 *     (LIFO ordering). The order is not part of the public contract;
 *     tests must not depend on it.
 *   - All three fields are mutated only while holding the future
 *     SMP per-parent lock. Today the kernel is single-threaded so
 *     no lock exists yet. */
static void process_link_child(struct process *child,
                               struct process *parent) {
  /* Caller guarantees the child is currently detached:
   *   parent == NULL && next_sibling == NULL. */
  child->parent = parent;
  child->ppid = parent ? parent->pid : 0;
  if (parent) {
    child->next_sibling = parent->children;
    parent->children = child;
  } else {
    child->next_sibling = NULL;
  }
}

static void process_unlink_child(struct process *child) {
  if (!child->parent) {
    child->next_sibling = NULL;
    return;
  }
  struct process **pp = &child->parent->children;
  while (*pp && *pp != child) pp = &(*pp)->next_sibling;
  if (*pp == child) *pp = child->next_sibling;
  child->next_sibling = NULL;
  child->parent = NULL;
}

void process_system_init(void) {
  proc_memset(proc_table, 0, sizeof(proc_table));
  next_proc_pid = 1;
  current_proc = NULL;
}

struct process *process_create(const char *name, uint32_t uid, uint32_t gid) {
  /* 2026-05-02: resolve the implicit parent via process_current()
   * (now dynamic) instead of reading the never-updated `current_proc`
   * directly. Pre-fix every spawn ended up with ppid=0 because
   * `current_proc` stayed NULL forever; now ppid mirrors the actual
   * task that called process_create. */
  struct process *implicit_parent = process_current();
  for (int i = 0; i < PROCESS_MAX; i++) {
    if (proc_table[i].state == PROC_STATE_UNUSED) {
      struct process *p = &proc_table[i];
      proc_memset(p, 0, sizeof(*p));
      p->pid = next_proc_pid++;
      p->ppid = implicit_parent ? implicit_parent->pid : 0;
      proc_strcpy(p->name, name ? name : "unnamed", PROCESS_NAME_MAX);
      p->state = PROC_STATE_EMBRYO;
      p->uid = uid;
      p->gid = gid;
      p->address_space = vmm_create_address_space();
      if (!p->address_space) {
        process_dbgcon_putc('P');
        p->state = PROC_STATE_UNUSED;
        return NULL;
      }
      p->main_thread = task_create(p->name, NULL, NULL, TASK_PRIORITY_NORMAL);
      if (!p->main_thread) {
        vmm_destroy_address_space(p->address_space);
        p->address_space = NULL;
        p->state = PROC_STATE_UNUSED;
        return NULL;
      }
      /* Phase 6.5: link into the implicit parent (`process_current()`).
       * `proc_memset` already zeroed `parent`/`children`/`next_sibling`
       * a few lines above, so the helper's detached-input invariant
       * is satisfied. */
      process_link_child(p, implicit_parent);
      return p;
    }
  }
  return NULL;
}

struct process *process_fork(struct process *parent) {
  if (!parent) return NULL;
  struct process *child = process_create(parent->name, parent->uid, parent->gid);
  if (!child) return NULL;
  /* Phase 6.5: process_create linked the child into `current_proc`,
   * but fork wants the caller-supplied parent regardless of who is
   * scheduled. Re-link before any other state copy so the tree
   * stays consistent if a future fork()-time hook walks children. */
  process_unlink_child(child);
  process_link_child(child, parent);

  /* M4 phase 7c: replace the empty AS that process_create allocated
   * with a CoW clone of the parent's AS. The clone bumps the
   * underlying frames' refcounts so the existing parent mappings
   * stay valid; both AS now share writable pages as RO+COW so the
   * next write from either side faults into the recovery path.
   *
   * On clone failure we tear down the partially-built child via
   * process_destroy so callers do not observe a half-initialised
   * process slot. */
  if (parent->address_space) {
    struct vmm_address_space *cloned =
        vmm_clone_address_space(parent->address_space);
    if (!cloned) {
      process_destroy(child);
      return NULL;
    }
    if (child->address_space) {
      vmm_destroy_address_space(child->address_space);
    }
    child->address_space = cloned;
  }

  child->brk = parent->brk;
  child->heap_start = parent->heap_start;
  child->stack_top = parent->stack_top;

  /* M5 phase D: inherit the parent's open file descriptors so any
   * pipe ends opened pre-fork are observable from BOTH branches.
   * For pipe FDs this means both processes hold a handle to the
   * same kernel pipe (private_data carries the pipe id); kernel
   * pipe_close_read/_write are NOT refcounted today, so a smoke
   * that closes either end in only ONE branch will affect the
   * other -- the canonical fork+pipe smoke (M5 phase D) avoids
   * the issue by leaving both ends open until process exit. */
  for (int i = 0; i < PROCESS_FD_MAX; i++) {
    child->fds[i] = parent->fds[i];
  }
  return child;
}

int process_exec(struct process *proc, const char *path, const char **argv) {
  (void)argv;
  if (!proc || !path) return -1;
  extern int elf_load_from_file(struct process *proc, const char *path);
  return elf_load_from_file(proc, path);
}

/* M5 phase B.3: full AS replacement. See header for contract. */
int process_exec_replace(struct process *proc, const uint8_t *data,
                         size_t size) {
  if (!proc || !data || !proc->main_thread) return -1;
  if (elf_validate(data, size) != 0) return -1;

  struct vmm_address_space *old_as = proc->address_space;
  struct vmm_address_space *new_as = vmm_create_address_space();
  if (!new_as) return -1;

  /* Swap in NEW AS so elf_load_into_process maps into it and
   * primes proc->main_thread->context.rip/rsp accordingly. */
  proc->address_space = new_as;

  if (elf_load_into_process(proc, data, size) != 0) {
    /* Rollback: keep the old AS as the live one, free the
     * half-built new AS (frees its private frames; the old AS's
     * frames retain their refcounts because nothing CoW-shared was
     * ever published into new_as). */
    proc->address_space = old_as;
    vmm_destroy_address_space(new_as);
    return -1;
  }

  /* Reset per-process bookkeeping that the new image owns. argv
   * packing for SYS_EXEC is deferred to a later phase; for now the
   * fresh user stack is empty (RSP-8 sentinel from elf_loader) and
   * the entry point will see RAX=0 (set by sys_exec just before
   * sysret), so capylibc's crt0 forwards 0 -> rdi -> main(0). */
  proc->exit_code = 0;

  /* Activate the new AS on this CPU before tearing the old one
   * down. Order matters: while CR3 still points at old_as, the
   * destroy walks page tables via the kernel HHDM (not user-half
   * mappings) and frees user frames. The kernel half is shared
   * across all ASes so this is safe, but switching first is the
   * defensive path for future refactors that might add per-AS
   * kernel-half state. */
  vmm_switch_address_space(new_as);
  if (old_as) vmm_destroy_address_space(old_as);
  return 0;
}

void process_exit(int code) {
  /* 2026-05-02: resolve via process_current() (dynamic) so the
   * dying task's process slot is correctly identified. Pre-fix this
   * read `current_proc` directly which was always NULL, so every
   * call to process_exit hit the noreturn halt loop and the actual
   * process slot stayed RUNNING forever -- waiters spun. */
  struct process *me = process_current();
  if (!me) {
#if defined(UNIT_TEST) || !defined(__x86_64__)
    /* Host unit tests should never reach this guard with current_proc=NULL,
     * but the function still must compile on non-x86 hosts. */
    for (;;) { /* unreachable in tests */ }
#else
    for (;;) __asm__ volatile("hlt");
#endif
  }
  me->exit_code = code;
  me->state = PROC_STATE_ZOMBIE;
  /* Mirror process_kill(): the process slot may remain as a zombie
   * until a parent wait()/orphan reap, but its owned descriptors must
   * close at exit time. Otherwise peers never observe pipe EOF. */
  for (int i = 0; i < PROCESS_FD_MAX; i++) {
    process_fd_free(me, i);
  }
  /* Etapa 7 / Slice 7.2: release any ring-3 compositor windows this process
   * owns before it becomes a zombie (no-op unless the GUI wiring registered
   * the observer). */
  if (g_teardown_fn) g_teardown_fn(me->pid);
  /* Etapa 4 Fase E: feed the thread-crash-survives smoke latch.
   * `code >= 128` is the POSIX-style "death by signal" encoding used
   * by `process_exit(128 + vector)` in x64_exception_dispatch when a
   * user-mode fault is contained. Voluntary exits with `code < 128`
   * are ignored by the latch because they do not prove the kernel
   * survived an asynchronous fault. The latch returns 1 on the edge
   * that flips the gate from "not ready" to "ready"; on that edge we
   * emit the COM1 marker exactly once. The body is wrapped in a
   * compile-time flag so production builds pay zero cost. */
#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE
  if (thread_crash_smoke_try_latch_exit_global((int32_t)code)) {
    thread_crash_smoke_emit_marker();
  }
#endif
  /* Etapa 5 / Slice 5.6: the userland tls_smoke program signals a fully
   * validated handshake (valid HTTPS GET ok + invalid-cert GET refused) by
   * exiting 0. Emit the COM1 marker exactly once on that success. In the
   * smoke build tls_smoke is the only user process (boot init), so the first
   * code==0 exit is its success. Gated so production builds pay zero cost. */
#ifdef CAPYOS_TLS_HANDSHAKE_SMOKE
  if (tls_handshake_smoke_try_latch_exit_global((int32_t)code)) {
    tls_handshake_smoke_emit_marker();
  }
#endif
  /* Etapa 6 / Slice 6.4: the userland capybrowse program signals a successful
   * fetch + HTML-to-text render by exiting 0. Emit the COM1 marker exactly once
   * on that success. In the smoke build capybrowse is the boot init process, so
   * the first code==0 exit is its success. Gated so production pays zero cost. */
#ifdef CAPYOS_CAPYBROWSE_SMOKE
  if (capybrowse_text_smoke_try_latch_exit_global((int32_t)code)) {
    capybrowse_text_smoke_emit_marker();
  }
#endif
  /* Etapa 7 / Slice 7.2.2: the userland capygfx program signals that every
   * ring-3 graphical syscall succeeded by exiting 0. Emit the COM1 marker
   * exactly once on that success (capygfx is the boot init process in the smoke
   * build). Gated so production pays zero cost.
   *
   * Etapa 7 / Slice 7.5 (alpha.304): CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE
   * reuses the SAME marker for a DIFFERENT launch path: capygfx spawned via
   * kernel_spawn_capygfx_desktop (armed for first dispatch + scheduler_add,
   * queued BEHIND another ring-3 process already in ring 3) instead of being
   * the boot init process itself. The underlying capability the marker
   * documents (capygfx ran every graphical syscall to completion and exited
   * 0) is the same either way; only how it got scheduled differs. */
#if defined(CAPYOS_GFX_SMOKE) || defined(CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE)
  if (capygfx_smoke_try_latch_exit_global((int32_t)code)) {
    capygfx_smoke_emit_marker();
  }
#endif
  /* Etapa 7 / Slice 7.5: the userland capymultifetch program signals a
   * successful multi-fetch cache short-circuit (2nd visit served without a
   * 2nd network transport call) by exiting 0. Emit the COM1 marker exactly
   * once on that success (capymultifetch is the boot init process in the
   * smoke build). Gated so production pays zero cost. */
#ifdef CAPYOS_MULTIFETCH_SMOKE
  if (capymultifetch_smoke_try_latch_exit_global((int32_t)code)) {
    capymultifetch_smoke_emit_marker();
  }
#endif
  task_exit(code);
}

int process_wait(uint32_t pid, int *status) {
  struct process *child = process_by_pid(pid);
  if (!child) return -1;
  while (child->state != PROC_STATE_ZOMBIE) {
    task_yield();
  }
  if (status) *status = child->exit_code;
  process_destroy(child);
  return 0;
}

void process_destroy(struct process *p) {
  if (!p) return;
  if (p->state == PROC_STATE_UNUSED) return;

  /* Phase 6.5: detach from the process tree BEFORE tearing down the
   * payload (address space, FDs, main thread). A future SMP walker
   * holding the parent's lock must never observe a child whose
   * `parent`/`next_sibling` still point into a half-destroyed slot.
   *
   * Step 1: walk our own children and orphan each one. There is no
   * init/PID 1 today, so an orphan keeps running with `parent=NULL`
   * and `ppid=0`. POSIX-correct re-parenting to PID 1 is a future
   * concern (and would simply replace these two assignments with a
   * `process_link_child(child, init_proc)` call). The walk reads
   * `next_sibling` BEFORE clobbering it, so the chain stays valid
   * while the loop runs. */
  struct process *child = p->children;
  while (child) {
    struct process *next = child->next_sibling;
    child->parent = NULL;
    child->ppid = 0;
    child->next_sibling = NULL;
    child = next;
  }
  p->children = NULL;

  /* Step 2: detach ourselves from our parent's children list. The
   * helper handles the singly-linked-list traversal and resets
   * `p->parent` / `p->next_sibling` so the slot is fully detached
   * before the rest of teardown runs. */
  process_unlink_child(p);

  /* Kill the main thread first so it cannot run on a half-torn-down
   * address space. `task_kill` is idempotent and tolerates an
   * already-dead pid (returns -1 silently in that case). */
  if (p->main_thread) {
    task_kill(p->main_thread->pid);
    p->main_thread = NULL;
  }

  if (p->address_space) {
    vmm_destroy_address_space(p->address_space);
    p->address_space = NULL;
  }

  for (int i = 0; i < PROCESS_FD_MAX; i++) {
    process_fd_free(p, i);
  }

  /* Etapa 7 / Slice 7.2: release any ring-3 compositor windows owned by this
   * pid (idempotent: a clean exit already released them in process_exit; a
   * force-destroyed/reaped owner is cleaned up here). No-op unless registered. */
  if (g_teardown_fn) g_teardown_fn(p->pid);

  /* Finally release the slot for reuse. We deliberately do NOT
   * zero the entire struct: `process_create` re-initialises the
   * slot from scratch, and leaving `pid` / `name` legible until
   * the next allocation makes post-mortem debugging easier when a
   * crash happens between destroy() and the next create(). */
  p->state = PROC_STATE_UNUSED;
}

/* 2026-05-02: ROOT-CAUSE fix for pipe-backed user processes missing request frames
 * (and capy_fork / capy_exec / capy_open / capy_pipe / SYS_BRK / fault
 * classifier all silently failed because process_current() returned
 * NULL).
 *
 * Pre-fix: `current_proc` was a static pointer set only by
 * `process_system_init` (to NULL) and `process_set_current` (a setter
 * that NO production code ever called). The scheduler updates
 * `current_task` via `task_set_current` on every context switch but
 * never touched `current_proc`, so `process_current()` returned NULL
 * for the entire kernel lifetime. The fact that fork/exec/pipe smokes
 * "passed" in CI was a chain of coincidences: the syscall handlers
 * that depended on a non-NULL process all returned -1, and the smokes
 * matched markers emitted via the legacy stdin_buf/debugcon fallbacks
 * that fired BEFORE the FD-table check in sys_read/sys_write.
 *
 * The pipe-backed user process path uncovered the bug because it depends
 * on `process_current()` returning the right process so sys_read(0)
 * routes to its request pipe, not stdin_buf.
 *
 * Fix: resolve dynamically from `task_current()`. The current task
 * was always tracked correctly by the scheduler; we just walk the
 * process table to find the slot whose `main_thread` is that task.
 * O(PROCESS_MAX=128) per call, only invoked from syscall and fault
 * slow paths, dwarfed by syscall entry/exit overhead.
 *
 * `current_proc` is kept as a TEST-ONLY override: host unit tests
 * that exercise sys_read/sys_write directly without driving a full
 * task can call `process_set_current(p)` to fake the resolution. In
 * production it stays NULL and the dynamic path always fires. */
struct process *process_current(void) {
  if (current_proc) return current_proc; /* test override */
  struct task *t = task_current();
  if (!t) return NULL;
  for (int i = 0; i < PROCESS_MAX; i++) {
    struct process *p = &proc_table[i];
    if (p->state == PROC_STATE_UNUSED) continue;
    if (p->main_thread == t) return p;
  }
  return NULL;
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

struct process *process_at_index(size_t index) {
  if (index >= (size_t)PROCESS_MAX) return NULL;
  return &proc_table[index];
}

int process_kill(uint32_t pid, int signal) {
  struct process *p = process_by_pid(pid);
  struct process *current;
  if (!p) return -1;
  current = process_current();
  if ((current && current == p) ||
      (p->main_thread && p->main_thread == task_current())) {
    return -2;
  }
  /* Phase 6.6: record a POSIX-ish exit code (128 + signal & 0x7F)
   * so an eventual `wait()` consumer sees a sensible value. The
   * 0x7F mask matches WTERMSIG semantics; signal numbers above
   * 127 are clamped because the encoding only has 7 bits.
   *
   * Two-step kill so the slot is consistent for any walker that
   * runs between state assignment and thread teardown:
   *   1. flip state to ZOMBIE so process_count() / iterators stop
   *      treating the slot as runnable.
   *   2. kill the main thread (idempotent; tolerates double-kill).
   *   3. clear the main_thread pointer so a subsequent reap does
   *      not double-kill.
   *
   * Auto-reap when there is no parent (orphan or root). Without
   * this, boot-time-spawned processes (e.g. the embedded hello
   * binary that segfaults under CAPYOS_HELLO_FAULT) leak their
   * address space + FDs forever because no parent will ever call
   * process_wait(). Parented children stay ZOMBIE so the parent's
   * wait() can read exit_code; the periodic
   * `process_reap_orphans()` tick will only catch them if the
   * parent itself dies first (which orphans them at that moment). */
  p->exit_code = 128 + (signal & 0x7F);
  p->state = PROC_STATE_ZOMBIE;
  if (p->main_thread) {
    task_kill(p->main_thread->pid);
    p->main_thread = NULL;
  }
  /* 2026-05-02: release FDs IMMEDIATELY on kill, even for parented
   * processes that stay ZOMBIE waiting for wait(). The slot itself
   * survives so wait() can still read exit_code, but the kernel
   * resources backing the FDs (pipe ends, VFS files) must be
   * freed at kill time so peers observing the other end of a
   * pipe see EOF / broken-pipe deterministically. Without this,
   * task-manager-initiated kill of a pipe-backed user process left
   * the response_pipe write end held by the ZOMBIE slot; the supervisor
   * polled forever after kill. parent->wait() reaps the slot via
   * process_destroy which is a no-op on already-freed FDs. */
  for (int i = 0; i < PROCESS_FD_MAX; i++) {
    process_fd_free(p, i);
  }
  if (!p->parent) {
    process_destroy(p);
  }
  return 0;
}

size_t process_reap_orphans(void) {
  size_t reaped = 0;
  /* Single linear sweep. Process_destroy mutates the table (flips
   * state to UNUSED) but does NOT shift slots, so the loop index
   * stays valid across destroy(). We snapshot the table size up
   * front for clarity even though it is constant. */
  for (int i = 0; i < PROCESS_MAX; i++) {
    struct process *p = &proc_table[i];
    if (p->state == PROC_STATE_ZOMBIE && p->parent == NULL) {
      process_destroy(p);
      reaped++;
    }
  }
  return reaped;
}

int process_fd_alloc(struct process *proc) {
  if (!proc) return -1;
  for (int i = 3; i < PROCESS_FD_MAX; i++) {
    if (proc->fds[i].type == 0) return i;
  }
  return -1;
}

/* F4 seção c (2026-05-08): socket close hook -- registered by the
 * production boot wiring (`syscall_net_init.c`) after the net stack
 * is ready. Host tests either install a fake or leave it NULL, in
 * which case FD_TYPE_SOCKET close just reclaims the slot. */
static process_fd_socket_close_fn g_socket_close_fn = NULL;

void process_fd_register_socket_close(process_fd_socket_close_fn fn) {
  g_socket_close_fn = fn;
}

process_fd_socket_close_fn process_fd_socket_close_get(void) {
  return g_socket_close_fn;
}

/* Etapa 7 / Slice 7.2: register the process-teardown observer (e.g.
 * syscall_gfx_release_owner) so a process that owns ring-3 compositor windows
 * has them destroyed when it exits or is reaped -- a dying pid must not leak a
 * window or leave a handle owned by a pid the table can reuse. The storage
 * (g_teardown_fn) is declared near the top of this file, ahead of process_exit /
 * process_destroy which consume it. Mirrors the socket-close hook above. */
void process_register_teardown_observer(process_teardown_fn fn) {
  g_teardown_fn = fn;
}

void process_fd_free(struct process *proc, int fd) {
  if (!proc || fd < 0 || fd >= PROCESS_FD_MAX) return;
  /* 2026-05-02: release the underlying kernel resource backing
   * this FD before clearing the slot. The pre-fix path silently
   * dropped pipe ends, leaking them across process_destroy.
   * Concretely this broke the Task Manager kill flow for pipe-backed
   * user processes: when the user invoked process_kill(pid, 9)
   * the response_pipe write end stayed open in the pipe table, so
   * the supervisor never observed EOF on its read end. Mirrors sys_close
   * (src/kernel/syscall.c)
   * for VFS files and pipe ends. New FD types added later must
   * extend this switch in lockstep with sys_close.
   *
   * 2026-05-08 (F4 seção c): FD_TYPE_SOCKET dispatched through
   * the registered socket-close hook so process_destroy / SYS_EXIT
   * reach the net stack without process.c gaining a hard link
   * dependency on `socket_close`. */
  struct file_descriptor *slot = &proc->fds[fd];
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
  } else if (slot->type == FD_TYPE_SOCKET) {
    if (g_socket_close_fn) {
      int sock_fd = (int)(intptr_t)slot->private_data;
      if (sock_fd >= 0) (void)g_socket_close_fn(sock_fd);
    }
  }
  slot->type = FD_TYPE_FREE;
  slot->private_data = NULL;
  slot->flags = 0;
  slot->offset = 0;
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
