#include "kernel/process.h"
#include "kernel/task.h"
#include "memory/vmm.h"
#include "memory/kmem.h"
#include <stddef.h>

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
      /* Phase 6.5: link into the implicit parent (`current_proc`).
       * `proc_memset` already zeroed `parent`/`children`/`next_sibling`
       * a few lines above, so the helper's detached-input invariant
       * is satisfied. */
      process_link_child(p, current_proc);
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
#if defined(UNIT_TEST) || !defined(__x86_64__)
    /* Host unit tests should never reach this guard with current_proc=NULL,
     * but the function still must compile on non-x86 hosts. */
    for (;;) { /* unreachable in tests */ }
#else
    for (;;) __asm__ volatile("hlt");
#endif
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

  /* Finally release the slot for reuse. We deliberately do NOT
   * zero the entire struct: `process_create` re-initialises the
   * slot from scratch, and leaving `pid` / `name` legible until
   * the next allocation makes post-mortem debugging easier when a
   * crash happens between destroy() and the next create(). */
  p->state = PROC_STATE_UNUSED;
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

struct process *process_at_index(size_t index) {
  if (index >= (size_t)PROCESS_MAX) return NULL;
  return &proc_table[index];
}

int process_kill(uint32_t pid, int signal) {
  struct process *p = process_by_pid(pid);
  if (!p) return -1;
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
