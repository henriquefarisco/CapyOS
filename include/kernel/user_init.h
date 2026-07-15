#ifndef KERNEL_USER_INIT_H
#define KERNEL_USER_INIT_H

#include <stddef.h>
#include <stdint.h>

struct process;

/* Result codes for `kernel_spawn_embedded_hello`. Encoded as
 * negative integers so the caller can distinguish failure modes
 * via simple comparisons. */
enum kernel_spawn_result {
  KERNEL_SPAWN_OK             = 0,
  KERNEL_SPAWN_BAD_ELF        = -1,
  KERNEL_SPAWN_NO_PROCESS     = -2,
  KERNEL_SPAWN_LOAD_FAILED    = -3,
};

/* Creates a new `process` named "hello", validates the embedded
 * `hello.elf` blob (see `kernel/embedded_hello.h`), and loads its
 * segments into the process address space via
 * `elf_load_into_process`. After this call the caller MAY invoke
 * `process_enter_user_mode(*out_proc)` to drop into Ring 3.
 *
 * On any failure the process slot may be left in PROC_STATE_EMBRYO
 * with no segments mapped; phase 6 (process_destroy / wait) will
 * own the cleanup contract. The host tests for phase 5c lock only
 * the success path and the input-validation failures; hardening
 * the partial-failure path is deferred to phase 6.
 *
 * Returns one of `enum kernel_spawn_result`. On `KERNEL_SPAWN_OK`,
 * `*out_proc` (if non-NULL) is set to the freshly created process.
 * On any non-zero return, `*out_proc` is left untouched. */
int kernel_spawn_embedded_hello(struct process **out_proc);

/* Convenience wrapper for the boot path (M4 phase 5d): calls
 * `kernel_spawn_embedded_hello` and, on success, calls
 * `process_enter_user_mode(p)` which is `noreturn`.
 *
 * This function therefore returns ONLY when the spawn fails. The
 * return value is the negative `enum kernel_spawn_result` produced
 * by the spawn helper, so the caller can decide whether to log,
 * panic, or fall through to the kernel shell. On success, the
 * function does not return and the boot CPU enters Ring 3.
 *
 * The kernel_main wiring guards the call with `#ifdef
 * CAPYOS_BOOT_RUN_HELLO`; default builds skip it so the regular
 * shell stays reachable. The QEMU smoke for phase 5d defines that
 * macro on the cross-compiler command line. */
int kernel_boot_run_embedded_hello(void);

/* M4 phase 8f.5: spawn TWO copies of the embedded hello binary,
 * arm each via the synthetic IRET frame builder so they are
 * dispatched through the scheduler (NOT via the boot iretq), and
 * iretq into the FIRST one to bootstrap ring 3. The second is
 * naturally entered by the scheduler at the first quantum
 * exhaustion via `x64_user_first_dispatch`.
 *
 * Each copy receives a distinct `rank` value (0 or 1) in RAX,
 * which capylibc's crt0 forwards to main() so the BUSY arm of
 * hello emits a per-rank marker ([busyU0] or [busyU1]). The
 * `smoke-x64-preemptive-user-2task` harness asserts BOTH markers
 * appear at least N times in the debugcon log -- the canonical
 * end-to-end proof of ring-3 preemption.
 *
 * On allocation/load failure of either copy this function
 * destroys whatever was built and returns the failing
 * `enum kernel_spawn_result` so the caller can fall back to the
 * shell. On success the function does NOT return: ring 3 is
 * entered for the first task and the scheduler takes over. */
int kernel_boot_run_two_busy_users(void);

/* M5 phase E.5: spawn the embedded `/bin/capysh` interactive shell
 * as the boot init process and drop into ring 3.
 *
 * Behaviour mirrors `kernel_boot_run_embedded_hello`: validates the
 * blob, builds a fresh process named "capysh", loads the ELF into
 * its address space, then `process_enter_user_mode`s into it. The
 * function returns only on failure; on success it is `noreturn`.
 *
 * Resolution path: looks `/bin/capysh` up against the
 * embedded_progs registry rather than calling
 * `embedded_hello_data()` directly, so the same code path picks up
 * any future shell binary swapped into the registry. */
int kernel_boot_run_capysh(void);

#ifdef CAPYOS_TLS_HANDSHAKE_SMOKE
/* Etapa 5 / Slice 5.6: spawn the embedded `/bin/tls_smoke` program as the
 * boot init process to drive the userland TLS handshake gate. Same shape
 * as `kernel_boot_run_capysh`; compiled only under the smoke gate. */
int kernel_boot_run_tls_smoke(void);
#endif

#ifdef CAPYOS_CAPYBROWSE_SMOKE
/* Etapa 6 / Slice 6.4: spawn the embedded `/bin/capybrowse` program as the
 * boot init process to drive the CapyBrowse Text smoke gate. Same shape as
 * `kernel_boot_run_tls_smoke`; compiled only under the smoke gate. */
int kernel_boot_run_capybrowse(void);
#endif

#ifdef CAPYOS_MULTIFETCH_SMOKE
/* Etapa 7 / Slice 7.5: spawn the embedded `/bin/capymultifetch` program as the
 * boot init process to drive the browser-multifetch smoke gate. Same shape as
 * `kernel_boot_run_capybrowse`; compiled only under the smoke gate. */
int kernel_boot_run_capymultifetch(void);
#endif

#ifdef CAPYOS_GFX_SMOKE
/* Etapa 7 / Slice 7.2.2: spawn the embedded `/bin/capygfx` program as the boot
 * init process to drive the ring-3 graphical surface smoke gate. The caller
 * initialises the compositor + installs the graphical-syscall backend first.
 * Compiled only under the smoke gate. */
int kernel_boot_run_capygfx(void);
#endif

#if defined(CAPYOS_GFX_SMOKE) || defined(CAPYOS_DESKTOP_GRAPHICAL_BROWSER) || \
    defined(CAPYOS_CAPYGFX_LIFECYCLE_SMOKE)
/* Etapa 7 / Slice 7.5 (alpha.304+): launch the embedded `/bin/capygfx` program
 * as an ORDINARY root process from a caller that is NOT noreturn -- unlike every
 * other `kernel_boot_run_*` above (all boot-time, noreturn on success, mutually
 * exclusive with login/desktop). This is the first spawn path meant to be
 * called from WITHIN an already-running desktop session (e.g. a shell command
 * dispatched from the desktop terminal), so the CALLER keeps running: the new
 * On the first request, a process is created and its main thread is armed for first dispatch
 * (user_task_arm_for_first_dispatch_with_rax) and queued
 * (scheduler_add) exactly like the second process in
 * kernel_boot_run_two_busy_users, but WITHOUT calling process_enter_user_mode
 * (which is noreturn and would hijack the caller's own execution). The
 * scheduler's next voluntary task_yield() (cooperative -- no
 * CAPYOS_PREEMPTIVE_SCHEDULER required; context_switch is agnostic of what
 * triggered it) dispatches the new task via the same synthetic-IRET-frame
 * trampoline (x64_user_first_dispatch) used by preemptive quantum exhaustion.
 * Idempotently installs the production graphical-syscall backend
 * (syscall_gfx_install_default_ops) on every call, since by construction the
 * caller is already inside a live desktop session (compositor already
 * initialised by desktop_init); installing the same ops vtable twice is a
 * harmless no-op. Later requests while that process is live focus/restore its
 * owned compositor window and return success without allocating another
 * process. Once it exits, the root-zombie reaper owns cleanup and the next
 * request creates a fresh instance. Returns a KERNEL_SPAWN_* status (0 = OK);
 * never noreturn. */
int kernel_spawn_capygfx_desktop(void);

/* Governed counterpart to the launcher. Terminates only the registered live
 * capygfx root process; it never accepts a PID or executable name from user
 * input. Returns 0 when closed, -1 when no Browser process is running. */
int kernel_close_capygfx_desktop(void);

/* Observable lifecycle counters for the desktop Browser launcher. Repeated
 * launch requests are coalesced onto the one live `capygfx` process; exited
 * root processes are reaped before a replacement is allocated. The snapshot
 * lets diagnostics/smokes verify that open-close-reopen remains resource
 * bounded instead of inferring health from a single success marker. */
struct kernel_capygfx_lifecycle_stats {
  uint64_t launch_requests;
  uint64_t processes_spawned;
  uint64_t launches_coalesced;
  uint64_t focus_successes;
  uint64_t spawn_failures;
  uint64_t zombies_reaped_before_spawn;
  uint32_t last_pid;
};

int kernel_capygfx_lifecycle_snapshot(
    struct kernel_capygfx_lifecycle_stats *out);
void kernel_capygfx_lifecycle_reset(void);
#endif

#ifdef CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE
/* Etapa 7 / Slice 7.5 (alpha.304): mechanical proof that
 * kernel_spawn_capygfx_desktop actually gets scheduled and runs to completion
 * when queued BEHIND another ring-3 process already occupying "current".
 * Mirrors kernel_boot_run_two_busy_users exactly (pa enters ring 3 directly
 * and is noreturn on success; pb is armed for first dispatch + queued), except
 * pb here IS capygfx via kernel_spawn_capygfx_desktop instead of a second
 * hello copy. This does NOT exercise the real CapyUI desktop_runtime_start
 * loop (that integration remains untested -- see the release notes); it
 * proves the SPAWN + SCHEDULER mechanics in isolation using the same proven
 * primitives (context_switch, user_task_arm_for_first_dispatch_with_rax,
 * scheduler_add) the eventual real integration relies on. The caller
 * (kernel_main) must have already initialised the compositor. */
int kernel_boot_run_capygfx_desktop_spawn_smoke(void);
#endif

#ifdef CAPYOS_CAPYGFX_LIFECYCLE_SMOKE
/* End-to-end Browser lifecycle gate. A kernel supervisor repeatedly performs
 * root spawn -> real context switch to ring 3 -> process exit -> switch back
 * to kernel CR3 -> orphan reap -> respawn, checking PMM/task/process counts
 * after every round. Noreturn on success: emits
 * `[smoke] capygfx-lifecycle ok` and parks. */
int kernel_boot_run_capygfx_lifecycle_smoke(void);
#endif

#ifdef CAPYOS_ELFLOAD_RESPAWN_SMOKE
/* Etapa 7 / Slice 7.5 (alpha.3xx): headless reproducer for the VMware panic on
 * close+reopen of the graphical browser. The real fault was elf_load writing a
 * segment to physical 0x400000 via the kernel identity map, which was PRESENT
 * but READ-ONLY (#PF error 3), i.e. pmm_alloc_page handed out a frame whose
 * kernel identity mapping is read-only. This pulls many frames from the PMM
 * and writes one byte to each THROUGH THE IDENTITY MAP (exactly as elf_load
 * does), scanning well past physical 0x400000, so a read-only free frame
 * faults here in QEMU with a full serial dump -- no desktop/VMware needed.
 * Emits `[smoke] elfload-respawn ok` on COM1 if every frame was writable. */
void kernel_boot_run_elfload_respawn_smoke(void);
#endif

#ifdef CAPYOS_APPS_ROUNDTRIP_SMOKE
/* Etapa 6 / Slice 6.6: run the apps-basic-roundtrip orchestrator in-kernel.
 * The basic desktop apps are in-kernel functions (not ring-3 processes), so
 * this calls each app's headless primary-function smoke via the
 * apps/apps_smoke.h contract (implemented by CapyUI) and feeds the
 * apps_roundtrip_smoke latch, which emits `[smoke] apps-basic-roundtrip ready`
 * on COM1 after APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS clean passes. Returns after
 * running. Compiled only under the smoke gate. */
int kernel_boot_run_apps_roundtrip(void);
#endif

#endif /* KERNEL_USER_INIT_H */
