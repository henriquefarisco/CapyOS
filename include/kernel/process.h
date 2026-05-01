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
  /* Process-tree linkage (M4 phase 6.5).
   *
   *   parent       : back-pointer to the parent process. NULL when
   *                  the parent has been destroyed (orphan) or this
   *                  process is a root (kernel-spawned with no
   *                  current_proc set). Mirrors `ppid`, which is the
   *                  observable PID for tools / iter snapshots.
   *   children     : head of a singly-linked list of children
   *                  threaded through `next_sibling`. Built by
   *                  process_create (links into current_proc) and
   *                  process_fork (links into the caller-supplied
   *                  parent). The order is LIFO and is NOT part of
   *                  the public contract; tests must not depend on
   *                  insertion order.
   *   next_sibling : owned by the parent's `children` list. NULL
   *                  when this slot is detached (no parent).
   *
   * Invariants enforced by process.c:
   *   - All three pointers are mutated only through the private
   *     helpers `process_link_child` / `process_unlink_child`.
   *   - process_destroy orphans children (parent=NULL, ppid=0)
   *     BEFORE freeing payload, then unlinks itself from its
   *     parent's list, so a walker never sees a partially-destroyed
   *     child.
   *   - When SMP lands, all reads/writes of these fields will be
   *     guarded by the per-parent process tree lock; the helpers
   *     are the only writer surface today. */
  struct process *parent;
  struct process *children;
  struct process *next_sibling;
};

void process_system_init(void);
struct process *process_create(const char *name, uint32_t uid, uint32_t gid);
struct process *process_fork(struct process *parent);
int process_exec(struct process *proc, const char *path, const char **argv);

/* M5 phase B.3: replace `proc`'s entire address space with a fresh
 * one loaded from `(data, size)` (an in-memory ELF image, e.g. one
 * resolved via `embedded_progs_lookup`).
 *
 * Steps:
 *   1. Validate the ELF header (returns -1 on bad magic / wrong arch).
 *   2. Allocate a brand-new `struct vmm_address_space`.
 *   3. Map the new ELF's PT_LOAD segments and a fresh user stack
 *      into the new AS, priming `proc->main_thread->context.rip`
 *      and `.rsp` to the new entry point and stack top.
 *   4. Activate the new AS on the calling CPU (`vmm_switch_address_space`)
 *      so the syscall return path's sysret runs with the new CR3.
 *   5. Destroy the old AS (decrements per-frame refcounts; CoW-shared
 *      pages survive in the parent's AS, exclusive pages are freed).
 *
 * On any failure before step 4 the function rolls back: `proc`'s
 * AS pointer is restored to the original, the half-built new AS is
 * destroyed, and the function returns -1. On step-4 success the
 * old AS is unconditionally destroyed; the caller MUST NOT rely on
 * the prior AS pointer afterwards.
 *
 * Caller invariants:
 *   - `proc` must be the calling kernel context's current process
 *     (i.e. `proc == process_current()`); otherwise CR3 reload will
 *     break the calling task.
 *   - `proc->main_thread` must exist; the loader writes RIP/RSP
 *     into its `context`. */
int process_exec_replace(struct process *proc, const uint8_t *data,
                         size_t size);
void process_exit(int code) __attribute__((noreturn));
int process_wait(uint32_t pid, int *status);
struct process *process_current(void);
struct process *process_by_pid(uint32_t pid);
struct process *process_at_index(size_t index);
int process_kill(uint32_t pid, int signal);

/* Tear a process slot down and release its address space.
 *
 * Designed for the lifecycle endpoints that need to free a process
 * outside of `process_wait` - in particular the partial-failure
 * cleanup paths added in M4 phase 5c (`kernel_spawn_embedded_hello`)
 * and phase 6 (`fork`/`exec` failures). Behaviour:
 *
 *   - NULL or already-UNUSED process -> no-op (idempotent).
 *   - The main thread, if any, is killed via `task_kill` so a stale
 *     thread does not outlive its address space.
 *   - The address space is destroyed via `vmm_destroy_address_space`
 *     and zeroed in the process struct.
 *   - All file descriptors are released via `process_fd_free`.
 *   - The slot's `state` is set to `PROC_STATE_UNUSED` so a future
 *     `process_create` call can reuse it.
 *
 * Children are NOT re-parented in this iteration; that lands with
 * `process_reap_children` in a later phase. For the M4 phase 5/6
 * use cases (hello spawn fail, exec fail before any fork) the
 * parent never actually has children at the call site, so this is
 * not yet observable. */
void process_destroy(struct process *p);

/* Phase 6.6: reap zombie slots that no parent will ever wait() for.
 *
 * Walks the process table and calls `process_destroy` on every slot
 * that is in PROC_STATE_ZOMBIE and has `parent == NULL` (orphan or
 * root). Returns the number of slots reaped.
 *
 * Designed as a periodic-tick hook called from `service_runner_step`
 * so boot-time spawned processes (root processes with no parent) and
 * orphaned children whose parent died first cannot leak their address
 * space + file descriptors forever. Parented zombies are deliberately
 * left alone: their parent will reap them via `process_wait`. The
 * sweep is O(PROCESS_MAX) and is safe to call as often as the tick
 * fires; it is also safe with no zombies present (returns 0).
 *
 * NOT safe to call from the dying process's own context (the caller
 * must already be off the to-be-destroyed slot's stack/AS); the
 * service-runner invocation satisfies this trivially. */
size_t process_reap_orphans(void);
int process_fd_alloc(struct process *proc);
void process_fd_free(struct process *proc, int fd);
size_t process_count(void);
void process_list(void (*print)(const char *));

/* Result codes for process_enter_user_mode(). The function only
 * returns when it refuses to drop into Ring 3 (the success path is
 * `noreturn` from the calling kernel thread's point of view because
 * the asm helper iretq's into user space). */
enum process_enter_user_mode_result {
  PROCESS_ENTER_USER_MODE_OK = 0,
  PROCESS_ENTER_USER_MODE_INVALID_PROC = -1,
  PROCESS_ENTER_USER_MODE_NO_THREAD = -2,
  PROCESS_ENTER_USER_MODE_BAD_RIP = -3,
  PROCESS_ENTER_USER_MODE_BAD_RSP = -4,
};

/* Drop the calling kernel thread into Ring 3 at the entry point and
 * user RSP recorded in `proc->main_thread->context`. The C wrapper
 * validates inputs and updates per-CPU bookkeeping; the actual ring
 * transition lives in src/arch/x86_64/cpu/user_mode_entry.S
 * (`enter_user_mode`).
 *
 * Returns one of `enum process_enter_user_mode_result` on validation
 * failure. On the success path the function does NOT return: control
 * resumes in user space and the kernel thread is suspended until the
 * process traps back via syscall or fault. */
int process_enter_user_mode(struct process *proc);

#endif /* KERNEL_PROCESS_H */
