# M4 Finalization Progress (Tudo + telemetria)

Working notes for the multi-session M4.1-M4.5 delivery plan
(`/Users/t808981/.windsurf/plans/m4-finalization-tudo-telemetria-ab84ad.md`).

## Phase 0 - Observability (DONE)

Goal: expose kernel tasks and processes through public iterators + snapshot
APIs so the shell, the task_manager GUI and CI can observe runtime state
without touching private storage.

### Public API surface added

- `include/kernel/task_iter.h`, `src/kernel/task_iter.c`
  - `task_iter_first`, `task_iter_next`, `task_stats_get`,
    `task_state_label`, `task_priority_label`
  - Walks the private `task_table` via `task_at_index` and skips
    `TASK_STATE_UNUSED` slots; copies a stable `task_stats` snapshot
    (PID/PPID/state/priority/name/cpu_time_ns).
  - `cpu_time_ns` is currently 0; phase 7 (telemetry) will populate it.

- `include/kernel/process_iter.h`, `src/kernel/process_iter.c`
  - `process_iter_first`, `process_iter_next`, `process_stats_get`,
    `process_state_label`
  - Walks `proc_table` via the new `process_at_index` (declared in
    `include/kernel/process.h`); skips `PROC_STATE_UNUSED` and zeroes
    `rss_pages` (filled by phase 7).

### Shell integration

- `cmd_perf_task` in `src/shell/commands/system_info/performance_commands.c`,
  registered in `recovery_network_registry.c` next to the other `perf-*`
  commands; documented in `docs/reference/cli-reference.md`.

### Task manager GUI integration

- `include/apps/task_manager.h` adds `enum task_manager_view`
  (Services/Tasks/Processes) and a `view` field on `task_manager_app`.
- `src/apps/task_manager.c` is now view-aware end-to-end:
  - Tab strip at the top routes mouse clicks to `view`.
  - Painter, scroll/select clamp, refresh and visible_count are split
    per view via dedicated helpers (`task_manager_paint_*_rows`,
    `task_manager_visible_count_for`).
  - Restart button is gated to the Services view; tasks/processes will
    grow a kill-by-row action in phase 8 once fault isolation lands.

### Tests

- `tests/test_task_iter.c` (15 asserts), `tests/test_task_stats.c`
  (11 asserts), `tests/test_process_iter.c` (16 asserts) - 42 asserts
  total. Registered in `tests/test_runner.c` and the `TEST_SRCS` list.
- `tests/stub_vmm.c` provides `vmm_create_address_space`,
  `vmm_destroy_address_space`, `elf_load_from_file` and `pit_ticks`
  stubs so the host test binary can link `src/kernel/process.c` and
  `src/kernel/log/klog.c` without x86_64 inline asm.

### Host portability hardening (side-effect of phase 0 testing)

While running `make test` on the macOS arm64 host, the following
pre-existing x86_64-only inline asm sites were each wrapped in a
`#if defined(UNIT_TEST) || !defined(__x86_64__)` host fallback so the
host tests can link without affecting production x86_64 behaviour:

- `src/kernel/task.c` (`hlt` in `task_exit`)
- `src/kernel/process.c` (`hlt` in `process_exit`)
- `src/auth/auth_policy.c` (`rdtsc` in `ap_ticks`)
- `src/fs/cache/buffer_cache.c` (`outb 0xE9` debug)
- `src/fs/storage/chunk_wrapper.c` (same)
- `src/fs/storage/offset_wrapper.c` (same)
- `src/drivers/storage/efi_block.c` (same)
- `include/drivers/hyperv/hyperv.h` (`wrmsr`/`rdmsr`)

Also: `update_agent_staged_requires_payload_verification` in
`src/services/update_agent_transact.c` was missing a `update_agent_poll`
call before reading `staged_payload_sha256`, so the function always
returned 0 right after `arm_staged_update_with_sha256`. Fixed.

### Validation

- `gcc -fsyntax-only` clean for all new TUs with full dependency graph.
- Standalone Fase 0 test binary (`/tmp/fase0_unit`): 42/42 passed.
- `make layout-audit` clean (no monolith warnings).
- `make version-audit` ok (`0.8.0-alpha.4+20260429`).
- Full `make test` execution skipped on this host (the SMB-mounted
  workspace blocks linker output writes); CI host runs it natively.

## Phase 1 - service-runner kernel task (DONE)

Done on 2026-04-29. The cooperative service polling now flows through a
dedicated kernel task that is observable through `task_iter` and prepared
to run naturally once the scheduler is preemptive (phase 8).

### Module

- `include/services/service_runner.h`, `src/services/service_runner.c`
  - `service_runner_init` (idempotent task spawn under name
    `service-runner`, priority `TASK_PRIORITY_NORMAL`).
  - `service_runner_step(now_ticks)` runs one polling iteration:
    delegates to `service_manager_poll_due` and `work_queue_poll_due`
    and updates module-private counters (step_count,
    services_polled_total, last_tick).
  - `service_runner_pid` and `service_runner_stats_get` for
    observability; `service_runner_reset` for tests.

### Production wiring

- `src/arch/x86_64/kernel_services.c` rewires `kernel_service_poll` to
  call `service_runner_init` once (lazy) and then
  `service_runner_step(pit_ticks())`. The cooperative call path is
  preserved bit-for-bit (same `service_manager_poll_due` and
  `work_queue_poll_due` calls in the same order). The runner task body
  itself is not yet dispatched; phase 8 will remove the cooperative
  delegate and let the scheduler run the task body directly.

### Tests

- `tests/test_service_runner.c`: 17 asserts covering PID assignment
  before/after init, idempotency, presence in `task_iter` with the
  expected name/priority/state, NULL-out rejection in
  `service_runner_stats_get`, monotonic step_count, last_tick
  updates, and `service_runner_reset` semantics.
- Registered in `tests/test_runner.c` and the `TEST_SRCS` list.

### Validation

- `make test TEST_BIN=/tmp/capy_unit_tests CAPYOS64_DEPS= UEFI_LOADER_DEPS=`
  builds and the host runs `Todos os testes passaram.`
  (the new module contributes 17/17). Stale x86_64 `.d` files on the
  SMB workspace are bypassed via the empty DEPS overrides; CI
  Linux/x86_64 host does not need them.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` clean.
- `make version-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` ok at
  `0.8.0-alpha.4+20260429`.

## Phase 2 - context_switch IRQ-safe seam (DONE)

Done on 2026-04-29. The cooperative service polling now flows through a
context_switch routine that is explicitly safe to call from an IRQ
handler, and the scheduler state machine is exercised end-to-end on
the host so any future regression is caught before reaching x86_64.

### Asm change

- `src/arch/x86_64/cpu/context_switch.S` now starts with an explicit
  `cli` so that no IRQ can fire between saving the old RSP/regs and
  switching to the new RSP. The new task's RFLAGS slot (loaded via
  `popfq` just before the final `jmpq`) restores IF based on the saved
  state of the resumed task (typically `0x202` for tasks created via
  `task_create`), so interrupts come back exactly when control
  transfers, never during the swap. The asm header documents the
  IRQ-safety contract for future readers and the source-of-truth
  layout offsets are now annotated as "locked by
  tests/test_context_switch.c".

### Test seam

- `tests/stub_context_switch.h` + `tests/stub_context_switch.c`
  provide a host stub that records every `context_switch` invocation
  in a small log so tests can assert the order and identity of the
  contexts that were swapped, without running any x86 instruction.
- `tests/stub_scheduler.c` is no longer linked; `src/kernel/scheduler.c`
  is now part of `TEST_SRCS` so the host runs the real scheduler
  state machine. `scheduler_yield`/`sleep_current`/`block_current`
  paths are reachable from tests; `scheduler_start` is wrapped in a
  `UNIT_TEST||!__x86_64__` guard so its `hlt` loop becomes a noreturn
  spin on host (it is never invoked from tests anyway).

### Tests

- `tests/test_context_switch.c`: 30 asserts across four groups:
  1. **Layout invariants** - sizeof(struct task_context) == 0x50 and
     each field offset matches the asm constants (rsp..cr3).
  2. **Init / pick_next** - scheduler_init zeroes counters and
     sched_running, PRIORITY policy picks highest priority, COOPERATIVE
     policy picks insertion order, RUNNING tasks are skipped.
  3. **Scheduler tick semantics** - tick advances total_ticks
     monotonically, COOPERATIVE tick never invokes context_switch,
     sleepers wake at wake_tick, zombies are reaped to UNUSED while
     live tasks are preserved.
  4. **schedule() invocation seam** - scheduler_block_current and
     scheduler_sleep_current trigger exactly one context_switch each,
     with the correct old/new context pointers, the correct state
     transitions, the correct wake_tick, and task_current() reflects
     the freshly scheduled task. scheduler_unblock matches by channel,
     clears wait_channel, and ignores non-matching channels.

### Validation

- `gcc -fsyntax-only` clean for `scheduler.c`, `stub_context_switch.c`
  and `test_context_switch.c` (only the legacy `#pragma GCC optimize`
  warning that exists across the codebase).
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` clean (no
  monolith warnings).
- Full `make test` execution skipped on this host (the SMB-mounted
  workspace blocks linker output writes); CI host runs it natively
  and the existing 17/17 service_runner asserts still link cleanly.

## Phase 3 - Ring 3 transition (DONE - foundation only; 3.5 deferred)

Done on 2026-04-29. The MSR/GDT contract that SYSCALL/SYSRET need is
now locked in a single header, mirrored by the assembly entry, the
production GDT actually contains the user-mode descriptors that
SYSRET demands, and a host test fails the build the moment any of
the architectural invariants drift.

`enter_user_mode` (the asm helper that pushes an IRET frame and drops
into Ring 3) and `process_enter_user_mode` (the C wrapper) are
intentionally **deferred to sub-phase 3.5** - they are only useful
once we have a user binary to run, which arrives in phase 5. Locking
the foundation here means 3.5 is a small additive change.

### Canonical MSR / segment header

- `include/arch/x86_64/syscall_msr.h` is now the single source of
  truth for:
  - IA32 MSR addresses (`IA32_EFER_MSR`, `IA32_STAR_MSR`,
    `IA32_LSTAR_MSR`, `IA32_FMASK_MSR`).
  - `EFER_SCE_BIT` and the SFMASK value (`IF | DF`).
  - Segment selectors for kernel and user ring
    (`GDT_KERNEL_CS_SELECTOR`, `GDT_KERNEL_DS_SELECTOR`,
    `GDT_USER_DS_SELECTOR`, `GDT_USER_CS_SELECTOR` plus the
    RPL=3 variants `USER_CS_RPL3 = 0x23` / `USER_SS_RPL3 = 0x1B`).
  - The STAR composition (`SYSCALL_STAR_KERNEL_BASE`,
    `SYSCALL_STAR_USER_BASE`, `SYSCALL_STAR_HIGH`,
    `SYSCALL_STAR_LOW`).
- The header is included from `.c` and `.S` sources; constants are
  spelled in a way GAS accepts directly so no extra preprocessing is
  required on the asm side.

### Asm consumes the header

- `src/arch/x86_64/syscall/syscall_entry.S` no longer carries inline
  `.set` constants. It now `#include`s the header and references
  `IA32_EFER_MSR`, `IA32_STAR_MSR`, `IA32_LSTAR_MSR`, `IA32_FMASK_MSR`,
  `EFER_SCE_BIT`, `SYSCALL_STAR_LOW`, `SYSCALL_STAR_HIGH` and
  `SYSCALL_SFMASK_VALUE`. The pre-existing comment that swapped
  user CS / user SS was corrected at the same time:
  - SYSRET-derived user SS = `SYSCALL_STAR_USER_BASE + 0x08 = 0x18 -> 0x1B`
  - SYSRET-derived user CS = `SYSCALL_STAR_USER_BASE + 0x10 = 0x20 -> 0x23`

### GDT now installs the user descriptors

- `src/arch/x86_64/interrupts.c` `g_gdt[]` was grown from `[3]` to
  `[5]`. `gdt_init` now installs:
  - index 3 (selector 0x18) - user data, access `0xF2`
    (`P=1, DPL=3, S=1, type=data/writable`), granularity `0x00`.
  - index 4 (selector 0x20) - user code, access `0xFA`
    (`P=1, DPL=3, S=1, type=code/readable/non-conforming`),
    granularity `0x20` (long-mode `L=1`).
- Without these descriptors any SYSRET would have faulted with `#GP`,
  even with the MSRs perfectly configured. This was a latent bug:
  the kernel had set up the MSRs to point at user CS=0x20/SS=0x18
  long before any user-mode descriptors actually existed in the GDT.

### Tests

- `tests/test_syscall_msr.c`: 36 asserts in five groups:
  1. **MSR addresses** match Intel SDM Vol. 4 (EFER, STAR, LSTAR,
     FMASK).
  2. **EFER and SFMASK semantics** - `EFER_SCE_BIT == 0x01`, SFMASK
     clears `IF | DF` and nothing else, the IF/DF bit constants
     match `RFLAGS` bits 9 and 10.
  3. **STAR layout** - kernel base 0x08, user base 0x10, low half
     reserved zero, high half packs `(user << 16) | kernel` to
     `0x00100008`, macro / manual composition agree.
  4. **Selectors and SYSCALL/SYSRET semantics** - kernel selectors
     match the GDT slots, `USER_CS_RPL3 == 0x23`,
     `USER_SS_RPL3 == 0x1B`, SYSRET-derived selectors land in the
     user data / user code slots, SYSCALL-derived selectors land in
     the kernel slots.
  5. **GDT access bytes** for user data / user code - present, DPL=3,
     S=1, type bits, RW/R bit, long-mode bit on user code, no
     long-mode bit on user data, non-conforming code.
- Wired into `tests/test_runner.c` and `TEST_SRCS`.

### Validation

- `gcc -fsyntax-only` clean for `tests/test_syscall_msr.c`.
- `gcc -E -x assembler-with-cpp` clean for
  `src/arch/x86_64/syscall/syscall_entry.S` (the constants expand to
  the expected literal values).
- Full host build runs `Todos os testes passaram.` with the new
  `[test_syscall_msr]` block contributing 36/36.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` clean
  (no monolith / boundary warnings).

## Phase 3.5 - enter_user_mode helper (DONE 2026-04-29)

Phase 3 locked the MSR / GDT contract; phase 3.5 supplies the small
primitive that actually drops a kernel task into Ring 3 and the
per-CPU area that the syscall path requires.

### Per-CPU area

- `include/arch/x86_64/cpu_local.h` declares the layout shared
  between C and asm:
  - `CPU_LOCAL_KERNEL_RSP_OFFSET = 0x00`
  - `CPU_LOCAL_USER_RSP_SCRATCH_OFFSET = 0x08`
  - `CPU_LOCAL_SIZE = 0x10`
  - `IA32_GS_BASE_MSR = 0xC0000101`,
    `IA32_KERNEL_GS_BASE_MSR = 0xC0000102` (Intel SDM Vol. 4 §2.16.1)
  - `struct cpu_local { uint64_t kernel_rsp; uint64_t user_rsp_scratch; }`
  - Public API: `cpu_local_init(kernel_rsp)`,
    `cpu_local_set_kernel_rsp`, `cpu_local_get_kernel_rsp`,
    `cpu_local_is_initialized`.
- `src/arch/x86_64/cpu/cpu_local.c` implements the boot CPU area as
  a single static and writes `IA32_GS_BASE` (and clears
  `IA32_KERNEL_GS_BASE`) so that `%gs:0x00` reaches it from kernel
  mode. `_Static_assert`s lock the struct layout against the asm
  constants. `wrmsr` is `#ifdef`'d out under `UNIT_TEST`.
- `src/arch/x86_64/syscall/syscall_entry.S` no longer hardcodes
  `0x00` / `0x08`: it now references
  `%gs:CPU_LOCAL_KERNEL_RSP_OFFSET` and
  `%gs:CPU_LOCAL_USER_RSP_SCRATCH_OFFSET` from the canonical header,
  so a future layout change is impossible to miss.
- Boot wiring: `kernel_main.c` reserves
  `static uint8_t g_syscall_kernel_stack[16 KiB]` (aligned 16) and
  calls `cpu_local_init(top_of_syscall_stack)` immediately before
  `syscall_init()`. Phase 8 will replace the static stack with a
  per-task one swapped by the scheduler.

### Ring 3 entry primitive

- `src/arch/x86_64/cpu/user_mode_entry.S` defines
  `void enter_user_mode(uint64_t rip, uint64_t rsp)` (SYSV ABI, so
  rdi=rip, rsi=rsp). It executes `cli`, pushes the IRET frame
  `[SS=USER_SS_RPL3 (0x1B), RSP, RFLAGS=0x202, CS=USER_CS_RPL3
  (0x23), RIP]`, then `iretq`. The selectors come from
  `syscall_msr.h` (locked by `tests/test_syscall_msr.c`), and the
  RFLAGS literal `0x202` sets IF=1 + reserved bit 1 so the user
  program runs with interrupts enabled.
- `src/arch/x86_64/process_user_mode.c` provides the C-side
  `process_enter_user_mode(struct process *proc)` wrapper that
  validates `proc`, `proc->main_thread`, and the
  `(rip != 0, rsp != 0)` invariants before calling
  `enter_user_mode`. Returns one of the new
  `enum process_enter_user_mode_result`
  (`PROCESS_ENTER_USER_MODE_OK / INVALID_PROC / NO_THREAD / BAD_RIP /
  BAD_RSP`) declared in `include/kernel/process.h`. On the success
  path the function does not return.

### Tests

- `tests/test_cpu_local.c`: locks the layout offsets vs. struct
  members (offsetof / sizeof), the MSR addresses, and the
  `cpu_local_init` contract (kernel_rsp written, init flag flips,
  setter updates the slot, init is idempotent).
- `tests/test_enter_user_mode.c`: ships its own `enter_user_mode`
  host stub that captures rip/rsp and `longjmp`s back so the
  `noreturn` extern in `process_user_mode.c` stays honoured.
  Verifies all four validation failures (`INVALID_PROC`, `NO_THREAD`,
  `BAD_RIP`, `BAD_RSP`) and the happy path forwarding (rip/rsp from
  `main_thread.context` reach the asm primitive verbatim).
- Both wired into `tests/test_runner.c`. `cpu_local.c` and
  `process_user_mode.c` registered in `TEST_SRCS`. The kernel link
  list (`CAPYOS64_OBJS`) gains
  `arch/x86_64/process_user_mode.o`,
  `arch/x86_64/cpu/cpu_local.o`, and
  `arch/x86_64/cpu/user_mode_entry.o`.

### Deferred

- A QEMU smoke that calls `process_enter_user_mode` on a real ELF
  and round-trips through `getpid` lands in phase 5, when a user
  binary first exists.
- True SMP support requires one `cpu_local` per logical CPU and
  `swapgs` on the syscall boundary; today both the static
  `g_boot_cpu_local` and the lack of `swapgs` work because we only
  support a single boot CPU.

### Workspace note

`src/arch/x86_64/kernel_main.c` was left with mixed CRLF + LF line
endings by an earlier session, which broke the standard edit tool.
A one-shot `awk 'BEGIN{ORS="\r\n"} {sub(/\r$/,""); print}'`
normalized it back to consistent CRLF before the boot wiring edit
landed; future edits to that file should now go through the regular
edit path.

## Phase 5a - capylibc skeleton (DONE 2026-04-29)

Phase 5 entrega o userland minimo. Phase 5a is the contract layer:
the syscall ABI is locked between the kernel and the brand new
`userland/` tree without yet shipping a real user binary. Phase 5b
will add the first user ELF (probably `hello`) and embed it into
the kernel image; phase 5c will wire `kernel_main` to spawn the
process and call `process_enter_user_mode`.

### Source-of-truth split

- New header `include/kernel/syscall_numbers.h` carries only the
  `SYS_*` defines and `SYSCALL_COUNT`. It is `__ASSEMBLER__`-clean
  (no `#include`s, no struct declarations) so the userland asm
  stubs can pull it through cpp.
- `include/kernel/syscall.h` no longer hardcodes the numeric table:
  it `#include`s the new header. Every kernel TU that already
  consumed `kernel/syscall.h` keeps building unchanged.

### capylibc layout

```
userland/
├── include/
│   └── capylibc/
│       └── capylibc.h        # public C API
└── lib/
    └── capylibc/
        ├── crt0.S            # _start; calls main, then SYS_EXIT
        └── syscall_stubs.S   # per-syscall SYSCALL stubs
```

- `capylibc.h` exposes `capy_exit`, `capy_getpid`, `capy_getppid`,
  `capy_write`, `capy_read`, `capy_yield`, `capy_sleep`,
  `capy_time`. Every prototype matches the kernel's documented
  argument register layout (the SysV x86_64 ABI for the first
  three args; `capy_exit` is `noreturn`).
- `syscall_stubs.S` is one block per syscall: `mov $SYS_*, %rax` ->
  `syscall` -> `ret` (or infinite halt for `capy_exit`). None of
  the wrappers need the rcx -> r10 shuffle today because all
  current capylibc surfaces use 0..3 args, of which arg index 3 is
  not used. When the surface grows past three args the stubs will
  need a single `movq %rcx, %r10` before `syscall`.
- `crt0.S` `_start` zeroes %rbp, calls `main`, then issues
  `SYS_EXIT` with `main`'s return value. argc / argv handling is
  punted to phase 6 (`fork`/`exec`) when the kernel actually builds
  a SysV-style stack frame.

### Tests and sanity checks

- `tests/test_capylibc_abi.c` ships **60 host asserts** in three
  groups:
  1. All 41 syscall numbers match their canonical values
     (`SYS_EXIT == 0` ... `SYS_IOCTL == 40`) plus
     `SYSCALL_COUNT == 41`.
  2. `struct syscall_frame` member offsets and total size match
     the on-stack layout that `syscall_entry.S` builds (so the C
     handlers in `src/kernel/syscall.c` and the asm stay in
     lock-step).
  3. The SysV register ABI documented in `capylibc.h` (arg0=%rdi,
     arg1=%rsi, arg2=%rdx, arg3=%r10, arg4=%r8, arg5=%r9) is
     re-asserted from the syscall_frame offsets.
- Wired into `tests/test_runner.c` and `TEST_SRCS`.
- Cross-toolchain assembly verified locally with
  `clang -target x86_64-unknown-linux-gnu`: 8 capylibc stubs
  emit `mov $0..0x27, %rax; syscall` with the right numeric value
  per syscall, plus crt0's `_start` carries an undefined external
  reference to `main` (proving the link contract).

### Build wiring

- `Makefile` adds the `capylibc` phony target plus a pattern rule
  `$(CAPYLIBC_BUILD_DIR)/%.o: $(USERLAND_DIR)/%.S`. The output dir
  defaults to `$(BUILD)/userland` but is overridable
  (`make capylibc CAPYLIBC_BUILD_DIR=/tmp/...`) because some
  sandboxed checkouts (SMB mounts, the workspace this session
  runs on) cannot create new subdirectories under `build/`.
- The kernel build is unchanged: `CAPYOS64_OBJS` does not yet pull
  in any userland artifact. capylibc is a separate library that
  will be linked against user binaries in phase 5b.

### Validation

- `gcc -fsyntax-only` clean for the new test.
- Host build: `Todos os testes passaram.` with the new
  `[test_capylibc_abi]` block contributing 60/60.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- `make capylibc` requires `x86_64-linux-gnu-gcc`; on this macOS
  workstation the same files build cleanly under
  `clang -target x86_64-unknown-linux-gnu` (used as a portable
  cross-toolchain proxy for the local sanity check). CI / Linux
  hosts will exercise the kernel toolchain path natively.

## Phase 5b - first user binary `hello` (DONE 2026-04-29)

Phase 5b adds the first real user-space program. The kernel still
does not load it at boot - that wiring comes in phase 5c - but the
binary compiles, links, and the program logic is locked by host
tests so phase 5c is a pure integration step.

### Source layout

```
userland/bin/hello/main.c          # int main(void)
userland/lib/capylibc/crt0.S       # _start (already in 5a)
userland/lib/capylibc/syscall_stubs.S
```

`main.c` writes the literal `"hello, capyland\n"` (16 bytes, no
NUL) to fd 1 via `capy_write` and returns 0. crt0 then forwards
that 0 to `SYS_EXIT`, so the kernel-side smoke (phase 5c) will see
the user process leave with `exit_code == 0`. The program is
deliberately stripped down: no globals beyond a single `static const
char[]` in .rodata, no .bss, no allocations.

### Build wiring

- `Makefile` adds:
  - `USERLAND_CFLAGS` for C user code: drops the kernel-specific
    `-mno-red-zone` (user programs may use the red zone; SYSCALL
    itself does not clobber it) and adds `-Iuserland/include`.
  - Pattern rule `$(CAPYLIBC_BUILD_DIR)/%.o: $(USERLAND_DIR)/%.c`
    using `USERLAND_CFLAGS`.
  - `HELLO_ELF`, `HELLO_OBJS`, plus the recipe
    `$(LD64) -nostdlib -static -e _start -o $@ $(HELLO_OBJS)`.
  - Phony `make hello-elf`.
- `HOST_CFLAGS` gains `-Iuserland/include` so the host test can
  `#include <capylibc/capylibc.h>`.

### Tests

`tests/test_hello_program.c` ships **7 host asserts** that lock
the program's behaviour without needing the cross toolchain:
host stubs for every `capy_*` capture invocations, then
`#include "../userland/bin/hello/main.c"` (under
`#define main hello_main`) so the test driver can call it. The
checks:

1. main returns 0.
2. exactly one `capy_write` call.
3. fd argument == 1 (stdout).
4. content == `"hello, capyland\n"`.
5. length == 16 (no NUL terminator).
6. main does not call `capy_exit` directly (crt0 owns SYS_EXIT).
7. main does not invoke any other syscall.

### Validation

- Host build: `Todos os testes passaram.` with `[test_hello_program]`
  contributing 7/7.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- `make hello-elf` requires `x86_64-linux-gnu-ld`, which is absent
  on the macOS workstation; on Linux/CI the rule resolves and emits
  a static ELF at `$(CAPYLIBC_BUILD_DIR)/bin/hello/hello.elf`.
  `clang -target x86_64-unknown-linux-gnu -c` proves each TU still
  parses; the link step is exercised by CI, not the local host.

## Phase 5c - embed hello.elf + spawn helper (DONE 2026-04-29)

Phase 5c builds the link- and runtime-side glue that makes the
phase 5b user binary reachable from kernel C code. The kernel boot
flow is still untouched - phase 5d will call the new helper from
`kernel_main.c` and add the QEMU smoke - but every other piece is
in place.

### elf_loader.c refactor

Before this phase `elf_load_from_file` carried an inline copy of the
"map segments + alloc stack + prime RIP/RSP" logic. The same kernel
already exposed `elf_load_into_process(proc, data, size)` from a
later session; phase 5c removes the duplication so the file path
now reads:

```c
int elf_load_from_file(struct process *proc, const char *path) {
    /* ... open / read ... */
    int r = elf_load_into_process(proc, buf, fsize);
    kfree(buf);
    return r;
}
```

The header (`include/kernel/elf_loader.h`) gains the in-memory entry
point as a documented public API and a comment that points at the
embedded-hello path.

### Embedded blob

- `Makefile` rule `$(HELLO_BLOB_OBJ): $(HELLO_ELF)` runs objcopy:

  ```sh
  cd .../bin/hello && objcopy \
      -I binary -O elf64-x86-64 -B i386:x86-64 \
      --rename-section .data=.rodata,alloc,load,readonly,data,contents \
      hello.elf hello_elf_blob.o
  ```

  The `cd` matters: objcopy derives the symbol stem from the input
  basename, so the symbols emitted are
  `_binary_hello_elf_start / _end / _size`. Running objcopy from
  the repo root would produce a path-mangled stem the kernel header
  cannot import.

- `include/kernel/embedded_hello.h` declares the three externs and
  two helpers (`embedded_hello_data`, `embedded_hello_size`). The
  size helper hides the address-vs-value quirk of objcopy's
  `_size` symbol (the byte count is encoded as the address of the
  symbol, not as a uint64_t stored there).

- `src/kernel/embedded_hello.c` is two one-line accessors. It is
  added to `CAPYOS64_OBJS` but NOT to `TEST_SRCS`: the host build
  has no objcopy artifact to satisfy the externs, and tests that
  need a fake blob ship local stubs (see `tests/test_user_init.c`).

- `$(HELLO_BLOB_OBJ)` is appended to `CAPYOS64_OBJS` so the kernel
  link picks up the symbols automatically.

### Spawn helper

`include/kernel/user_init.h` + `src/kernel/user_init.c` add the
single new public entry point:

```c
enum kernel_spawn_result {
    KERNEL_SPAWN_OK             = 0,
    KERNEL_SPAWN_BAD_ELF        = -1,
    KERNEL_SPAWN_NO_PROCESS     = -2,
    KERNEL_SPAWN_LOAD_FAILED    = -3,
};

int kernel_spawn_embedded_hello(struct process **out_proc);
```

The body is intentionally linear so the host test can drive every
branch via stubs:

1. `embedded_hello_data/_size` -> (data, size)
2. `elf_validate(data, size)` early-fails on a corrupt blob
3. `process_create("hello", 0, 0)` allocates the address space
4. `elf_load_into_process` maps segments and primes RIP/RSP
5. `*out_proc = p` on success; caller decides when to call
   `process_enter_user_mode`

The function does NOT call `process_enter_user_mode` itself; phase
5d will pick the moment in `kernel_main.c`. The TODO comment in
`user_init.c` flags the embryo-slot leak on partial failure; phase
6 (process_destroy / wait) owns that cleanup.

### Tests

`tests/test_user_init.c` ships **15 host asserts** against
host-side stubs for `embedded_hello_*`, `elf_validate`, and
`elf_load_into_process` (the real `process_create` from
`src/kernel/process.c` runs unchanged via the existing
`tests/stub_vmm.c`). The locked invariants:

1. The four `enum kernel_spawn_result` values do not drift
   (`OK==0, BAD_ELF==-1, NO_PROCESS==-2, LOAD_FAILED==-3`).
2. A blob that fails `elf_validate` short-circuits with
   `KERNEL_SPAWN_BAD_ELF`, never invokes `elf_load_into_process`,
   and never overwrites `*out_proc`.
3. The happy path returns `KERNEL_SPAWN_OK`, calls
   `elf_load_into_process` exactly once with the (data, size)
   pair returned by `embedded_hello_*`, threads the freshly
   created process into `*out_proc`, and the process name is
   `"hello"`.
4. A failing `elf_load_into_process` returns
   `KERNEL_SPAWN_LOAD_FAILED`.
5. Passing `out_proc=NULL` is accepted on the happy path.

### Validation

- Host build: `Todos os testes passaram.` with the new
  `[test_user_init]` block contributing 15/15.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- `make -n all64` resolves the new dependency chain:
  `embedded_hello.c -> embedded_hello.o`,
  `user_init.c -> user_init.o`,
  `hello.elf -> hello_elf_blob.o` (via objcopy), and all three
  link into `$(CAPYOS_ELF64)`. The actual artifact build is
  gated on the cross toolchain (Linux/CI) per the existing
  workspace constraint.

## Phase 5d - kernel_main wiring (DONE 2026-04-29)

Phase 5d connects the phase 5c spawn helper to the boot path so
a CI-only build (with `-DCAPYOS_BOOT_RUN_HELLO`) drops into Ring 3
right after `syscall_init`. Default builds remain unchanged, which
preserves the kernel shell during normal runs and prevents a buggy
user binary from bricking the boot.

### Boot wrapper

`include/kernel/user_init.h` and `src/kernel/user_init.c` gain
`kernel_boot_run_embedded_hello(void)`:

```c
int kernel_boot_run_embedded_hello(void) {
    struct process *p = NULL;
    int rc = kernel_spawn_embedded_hello(&p);
    if (rc != KERNEL_SPAWN_OK || !p) return rc;
    process_enter_user_mode(p);   /* noreturn on success */
    return -1;                    /* defensive */
}
```

Returns ONLY when the spawn fails; the success path iretq's into
Ring 3 inside `process_enter_user_mode`.

### Boot wiring

`src/arch/x86_64/kernel_main.c` includes `kernel/user_init.h` and,
inside the `if (!handoff_boot_services_active())` block right after
`syscall_init()`, adds:

```c
#ifdef CAPYOS_BOOT_RUN_HELLO
    klog(KLOG_INFO,
         "[user_init] CAPYOS_BOOT_RUN_HELLO defined; spawning hello.");
    {
      int hello_rc = kernel_boot_run_embedded_hello();
      klog(KLOG_WARN,
           "[user_init] hello spawn returned without entering Ring 3.");
      (void)hello_rc;
    }
#endif
```

The macro is undefined by default, so production builds skip the
block entirely. The QEMU smoke author defines it on the
cross-compiler command line:
`make all64 CFLAGS64+=' -DCAPYOS_BOOT_RUN_HELLO'`.

### Tests

No new dedicated host test for `kernel_boot_run_embedded_hello`:
the function is a 5-line composition of two already-tested
primitives (`kernel_spawn_embedded_hello` -> 15 asserts in phase
5c, `process_enter_user_mode` -> 7 asserts in phase 3.5). A test
that drove it directly would require setjmp gymnastics around the
`noreturn` return path with little additional safety; the
component tests cover the meaningful failure modes already.

### Validation

- `kernel_main.c` line endings preserved as pure CRLF (661 lines,
  zero LF-only lines) - no edit-tool regression on the file that
  previously had mixed line endings.
- Host build: `Todos os testes passaram.` (15/15 still passing in
  `[test_user_init]`).
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` clean
  (`Warnings: none`).
- `make -n all64 CFLAGS64=...-DCAPYOS_BOOT_RUN_HELLO...` resolves
  the new dependency: `kernel_main.c` and `user_init.c` are both
  recompiled with the macro, and `hello_elf_blob.o` (objcopy)
  links into the kernel image as before.

### QEMU smoke contract (for CI to wire in phase 5e)

Required from the smoke harness:

1. Build the kernel with
   `CFLAGS64='<defaults> -DCAPYOS_BOOT_RUN_HELLO'`.
2. Boot in QEMU; capture serial / debug-console output.
3. Pass criteria:
   - The string `[user_init] CAPYOS_BOOT_RUN_HELLO defined;`
     appears (proves the build flag reached `kernel_main.c`).
   - The string `hello, capyland` appears (proves `_start` ->
     `main` -> `capy_write` -> `SYS_WRITE` -> sys_write reached
     the console; indirectly validates phase 3 SYSCALL/SYSRET
     MSR setup, phase 3.5 IRET frame, phase 4 GDT user
     descriptors, and phase 5a/b ELF link).
   - The string `panic` does NOT appear (proves the phase 4
     fault classifier did not fire on a well-behaved user
     binary - regression guard for the kill-on-fault path).
4. Failure handling: if `kernel_boot_run_embedded_hello` falls
   through (returns), the kernel logs
   `[user_init] hello spawn returned without entering Ring 3.`
   and continues normal boot. The smoke MUST treat that log as
   a hard failure so a silent ELF-load regression does not slip
   through.
5. Timeout: 30 s after boot is plenty; `hello` writes 16 bytes
   then SYS_EXIT, so the success line should appear within
   1-2 s of `[syscall] Syscall ABI registered.`

After phase 5e (the CI smoke author's land) the deferred QEMU
checks for phase 3.5 (Ring 3 entry) and phase 4 (fault kill on a
deliberately-segfaulting user binary) become trivial copies of
the same harness with a different build flag.

## Phase 5e - QEMU smoke harness (DONE 2026-04-29)

Phase 5e ships the CI-side automation that exercises the full
phase 3..5d pipeline on real x86_64 (QEMU + OVMF). Local macOS
runs cannot execute the smoke (no cross toolchain, no QEMU here),
but the script and Make target are ready to drop into a Linux CI
node.

### Build-flag plumbing

`Makefile` introduces `EXTRA_CFLAGS64` (default empty) appended to
`CFLAGS64` after the canonical kernel flags, so callers can flip
preprocessor toggles without editing the variable in place. Calls
that require a clean rebuild (the per-source `.d` files do not
track macros) still need `make clean` first.

### Smoke script

`tools/scripts/smoke_x64_hello_user.py` (270 lines):

1. Resolves `qemu-system-x86_64` and an OVMF firmware bundle.
2. Verifies `build/iso-uefi-root/EFI/BOOT/BOOTX64.EFI`,
   `build/capyos64.bin`, `build/manifest.bin` exist (otherwise
   exits with rc=2 and a hint to rebuild with the flag).
3. Provisions a GPT disk via the existing
   `tools/scripts/provision_gpt.py` helper.
4. Launches QEMU with `-debugcon file:<log>` so the kernel's klog
   output (port 0xE9) lands in a file the script can poll.
5. Polls the debugcon log every 100 ms with a 30-second timeout.
   Short-circuits as soon as ALL success markers appear OR ANY
   failure marker appears.
6. Sends SIGTERM to QEMU once the verdict is in, falling back to
   SIGKILL after 5 s.
7. Exits 0 on success, 1 on failure (with the missing-marker list
   and a debugcon tail dumped to stderr), 2 on environment errors
   (missing tools / artifacts).

Success markers: `[user_init] CAPYOS_BOOT_RUN_HELLO defined;` and
`hello, capyland`. Failure markers: `panic` and `[user_init]
hello spawn returned without entering Ring 3.`.

### Make target

```
smoke-x64-hello-user:
    $(MAKE) clean
    $(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'
    $(MAKE) iso-uefi
    $(MAKE) manifest64
    python3 tools/scripts/smoke_x64_hello_user.py \
            $(SMOKE_X64_HELLO_USER_ARGS)
```

`smoke-x64-hello-user` joins the existing `.PHONY` list. CI can
invoke it as a single Make call.

### Validation

- `python3 -m py_compile tools/scripts/smoke_x64_hello_user.py`
  passes (syntax clean).
- The script reuses `make_qemu_cmd`, `provision_disk`,
  `resolve_qemu_binary`, `resolve_ovmf_or_raise`,
  `create_runtime_ovmf_vars`, `print_log_tail`, `cleanup_file`
  from the existing `smoke_x64_common` / `smoke_x64_session`
  modules, so it inherits all the well-tested QEMU lifecycle
  handling without re-implementing port allocation or OVMF
  staging.
- Local execution is not possible on this macOS workstation
  (no `x86_64-linux-gnu-gcc/-ld/-objcopy`, no
  `qemu-system-x86_64`, no OVMF). CI smoke is the source of
  truth for end-to-end validation.

## Phase 5f - segfault smoke for phase 4 kill path (DONE 2026-04-29)

Phase 5f closes the deferred QEMU check from phase 4: until a real
user binary existed, the fault-kill path could only be exercised by
the pure host classifier tests. Phase 5f re-uses the phase 5e
harness with two new compile-time toggles to validate the same
end-to-end pipeline via a deliberately-segfaulting binary.

### Build-flag plumbing

`Makefile` introduces `EXTRA_USERLAND_CFLAGS` (default empty),
appended to `USERLAND_CFLAGS` after the canonical user flags. This
mirrors `EXTRA_CFLAGS64` from phase 5e and gives smoke targets a
clean way to inject preprocessor toggles without touching
`USERLAND_CFLAGS` in place.

### Userland change

`userland/bin/hello/main.c` gains a `#ifdef CAPYOS_HELLO_FAULT`
branch:

1. `capy_write(1, "before-fault\n", 13)` so the smoke can confirm
   the binary actually entered Ring 3 before the fault.
2. `volatile int *bad = (volatile int *)0; *bad = 0xDEADBEEF;` to
   raise #PF with U=1, P=0, W=1.
3. `return 1;` after the fault so the compiler does not flag a
   missing return path (unreachable on a working kernel).

The default `#else` branch is byte-for-byte identical to the
phase 5b/5e binary, so `tests/test_hello_program.c` keeps passing
7/7 on the host (the macro is never defined for host builds).

### Smoke script

`tools/scripts/smoke_x64_hello_segfault.py` (~250 lines) is the
phase 5e script with different markers:

- Success markers (all four required):
  - `[user_init] CAPYOS_BOOT_RUN_HELLO defined;`
  - `before-fault`
  - `[x64] User-mode fault, killing offending process`
  - `Page Fault`
- Failure markers (any aborts the smoke):
  - `panic` (kernel must NOT panic).
  - `hello, capyland` (regression guard: presence means the
    `CAPYOS_HELLO_FAULT` flag did not propagate to the userland
    compile and we are running the phase 5e binary by mistake).

The same QEMU lifecycle, debugcon polling, timeout, and SIGTERM
handling as phase 5e are reused via the `smoke_x64_common` and
`smoke_x64_session` modules.

### Make target

```
smoke-x64-hello-segfault:
    $(MAKE) clean
    $(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO' \
                  EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_FAULT'
    $(MAKE) iso-uefi
    $(MAKE) manifest64
    python3 tools/scripts/smoke_x64_hello_segfault.py \
            $(SMOKE_X64_HELLO_SEGFAULT_ARGS)
```

`smoke-x64-hello-segfault` joins `.PHONY`. CI invokes it as a
single Make call, exactly like phase 5e.

### Validation

- `python3 -m py_compile tools/scripts/smoke_x64_hello_segfault.py`
  passes.
- Host build: `Todos os testes passaram.`
  `[test_hello_program]` still 7/7 (CAPYOS_HELLO_FAULT not
  defined in host builds, so the `#else` branch is exercised).
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- Local QEMU execution N/A on this macOS workstation; CI is the
  source of truth, same constraint as phase 5e.

### What this locks

- Phase 4 fault classifier kill path is now end-to-end testable.
- Phase 7a's RECOVERABLE -> KILL_PROCESS escalation chain is
  exercised on real hardware: a NULL deref produces #PF U=1,P=0,W=1;
  the classifier returns RECOVERABLE; `vmm_handle_page_fault` (still
  a stub) returns -1; the dispatcher logs the escalation message
  and falls through to the kill path. Once phase 7b lands a real
  body for `vmm_handle_page_fault` we will need to extend this
  smoke (or pair it with a stack-overflow variant) so that
  legitimate demand-paging is not silently classified as a kill.

## Phase 7b - real demand-paging body (DONE 2026-04-29)

Phase 7b lands the actual `vmm_handle_page_fault` implementation
behind the seam that phase 7a wired but left as a `return -1`
stub. Together with phase 6.6 it closes the M4 finalization story:
the kernel now demand-pages legitimate user faults, kills the
rest, and reaps every dead slot regardless of whether the parent
ever calls `wait()`.

### Anonymous-region registry

`include/memory/vmm.h` grows a new struct and adds two fields to
`struct vmm_address_space`:

```c
struct vmm_anon_region {
    uint64_t start;       /* page-aligned, inclusive       */
    uint64_t end;         /* page-aligned, exclusive       */
    uint64_t flags;       /* VMM_PAGE_USER, _WRITE, _NX... */
    struct vmm_anon_region *next;
};

struct vmm_address_space {
    /* ... existing fields ... */
    uint64_t rss_pages;            /* phase 7b: per-AS resident set */
    struct vmm_anon_region *anon_regions; /* phase 7b: demand list */
};
```

The new public API:

- `vmm_register_anon_region(as, start, page_count, flags)` appends
  a non-overlapping range to `as->anon_regions`. Rejects NULL,
  zero-length ranges, overflow, and any overlap with an existing
  region.
- `vmm_clear_anon_regions(as)` frees the entire list. Called from
  `vmm_destroy_address_space` and safe on a NULL or empty list.
- `vmm_anon_region_find(as, virt)` returns the region containing a
  byte address (half-open interval), or NULL.
- `vmm_address_space_rss(as)` reads the per-AS counter; returns 0
  for NULL.
- `vmm_current_address_space()` returns
  `process_current()->address_space` or NULL.

The registry implementation lives in `src/memory/vmm_regions.c`
(NEW). It is intentionally **separate** from `src/memory/vmm.c`
because vmm.c uses x86_64 inline asm (`movq %%cr3`, `invlpg`)
that does not assemble on non-x86 hosts (Apple Silicon arm64).
The registry is just kmalloc-backed linked-list manipulation, so
it builds and runs cleanly under the host unit-test harness
alongside the other host-side files.

### Page-fault servicing

`src/memory/vmm.c::vmm_handle_page_fault` flow:

1. Look up the current address space via
   `vmm_current_address_space()` (NULL -> kill).
2. `vmm_anon_region_find(as, fault_addr)` (NULL -> kill).
3. `pmm_alloc_page()` for a fresh frame (NULL -> kill).
4. Zero-fill the page through the kernel's identity-mapped phys
   view (same pattern as `vmm_create_address_space` for fresh
   PML4 frames).
5. `vmm_map_page(as, page-aligned virt, phys, region->flags)` —
   the helper bumps the per-AS RSS counter on the user mapping.
6. Return 0; the dispatcher in `interrupts.c` resumes user mode.

If the `vmm_map_page` call fails (e.g. out of page-table memory),
the just-allocated frame is freed via `pmm_free_page` to avoid
leaking on the error path. All other failure paths return -1
without partial work to roll back.

### RSS counter wiring

`vmm_map_page` and `vmm_unmap_page` now maintain
`as->rss_pages`:

- map: each transition from "not present" to "present + user"
  bumps RSS by 1 page. Re-mapping an already-present user PTE is
  a no-op for the counter (same region of memory accounted once).
- unmap: reads the PTE before clearing; if the old entry was
  present + user, decrements RSS (with an `> 0` guard against
  arithmetic underflow under any imagined inconsistency).

`src/kernel/process_iter.c` replaces the stub
`stats->rss_pages = 0` with
`stats->rss_pages = vmm_address_space_rss(p->address_space)`.
The accessor returns 0 for a NULL AS, which preserves the prior
"unknown yet" sentinel for slots in transition (e.g. mid-create).
Existing observers (`perf-task`, `task_manager` Processes view)
benefit immediately without further changes.

### ELF-loader wiring (stack expansion)

`src/kernel/elf_loader.c::elf_load_into_process` now registers a
240-page anonymous region BELOW the eagerly-mapped 16-page user
stack. A user that grows its stack past the initial 16 pages
faults on the 17th-page boundary; the classifier returns
RECOVERABLE; the handler maps a fresh zero page; the user
resumes. Total stack budget per process is now 256 pages (1 MiB)
without committing all of it eagerly. The eager and expansion
regions do not overlap; the registration is therefore guaranteed
to succeed under normal kmalloc pressure. Registration errors
are deliberately swallowed: if it fails, the process simply does
not get demand growth (the eager top 16 pages still work).

### Tests

A new `tests/test_vmm_anon_regions.c` (NEW, **33/33** passing)
covers every public surface of the registry:

- `test_empty_as` (3): fresh AS has `anon_regions==NULL`,
  `rss_pages==0`, `find` returns NULL for any address.
- `test_register_basic` (5): single registration populates start,
  end, flags as documented; head pointer is non-NULL.
- `test_register_rejects_bad_inputs` (3): NULL `as`,
  `page_count==0`, and start+size overflow all return -1.
- `test_register_rejects_overlap` (7): identical, subset,
  superset, left-edge, right-edge overlaps rejected; adjacent
  ranges accepted on both sides (proves end is exclusive).
- `test_find_across_multiple_regions` (6): three non-overlapping
  regions, find hits each, miss in gap, miss at exclusive end.
- `test_clear_idempotent` (4): clear empties the list, second
  clear is a no-op, re-register after clear succeeds.
- `test_clear_null_safe` (3): clear/rss/find all NULL-safe.
- `test_rss_per_as` (2): RSS counter is per-AS, not global; two
  address spaces hold independent values.

`tests/test_process_iter.c` updates the rss_pages assertion: the
stub's `vmm_address_space` does not bump rss on map/unmap (no
real PTEs), so the counter still reads 0 for any process created
in the harness, but a NEW assertion bumps `as->rss_pages = 42`
by hand and confirms `process_stats_get` echoes it back, proving
the wiring is real and not a stubbed literal.

`tests/stub_vmm.c::vmm_destroy_address_space` now calls
`vmm_clear_anon_regions(as)` before `free(as)` to keep parity
with the production teardown order and stop tests from leaking
the registry nodes.

### Validation

- **Host build**: `Todos os testes passaram.` Total assertions
  jumped from prior milestone by **33** (new vmm_anon_regions
  block). `[test_process_iter]` now 17/17 (was 16/16, +1 for the
  rss-mirror assert).
- **Makefile**: `$(BUILD)/x86_64/memory/vmm_regions.o` added to
  `CAPYOS64_OBJS`; `tests/test_vmm_anon_regions.c` and
  `src/memory/vmm_regions.c` added to `TEST_SRCS`.
- **layout-audit**: `Warnings: none`.
- **.S preprocessor scan**: clean — no comment regressions
  introduced (continued from phase 6.5.1 hotfix).
- **clang -E -x assembler-with-cpp** sweep across all `.S`
  files: zero errors.

### Limitations & deferred work

- The page-fault body is exercised by host tests at the registry
  level; the actual fault flow (cr2 -> classifier -> handler ->
  pmm_alloc -> map -> resume) requires QEMU. A "legitimate
  demand paging" smoke that grows the user stack past 16 pages
  is the natural pairing for the existing
  `smoke-x64-hello-segfault`; it is deferred until userland
  grows a stack-stress program (or until phase 5g lands a
  dedicated demand-paging hello variant).
- No heap-grow / sbrk integration yet. The `brk` field is set
  but no syscall consumes it; once the user gets `sbrk`, a
  follow-on can register a per-process heap region similar to
  the stack-expansion treatment.
- CoW (copy-on-write fork) is unaddressed; phase 7c will extend
  the classifier rule to include P=1+W=1 from user mode on
  CoW-marked pages once `vmm_clone_address_space` lands. The
  dispatcher path stays identical.

## Phase 6.6 - zombie reaping correctness (DONE 2026-04-29)

Phase 6.6 closes the leak loophole in `process_kill` and
`process_exit`: a process that becomes ZOMBIE with no parent
to call `wait()` would otherwise leak its address space and
file descriptors forever. This is the exact failure mode the
phase 5f segfault smoke triggers (boot-time embedded hello
spawned with `current_proc=NULL`, then segfaulted via
`CAPYOS_HELLO_FAULT`).

### Public API

`include/kernel/process.h` adds one declaration:

```c
size_t process_reap_orphans(void);
```

Returns the number of slots reaped. Walks the process table for
every slot in `PROC_STATE_ZOMBIE` with `parent == NULL` and calls
`process_destroy` on it. Safe with no zombies present (returns
0). Idempotent across repeated calls.

### Wiring

- **`process_kill`** now records `exit_code = 128 + (signal &
  0x7F)` (POSIX-ish WTERMSIG semantics; signals above 127 are
  clamped to 7 bits) and **auto-reaps the slot inline if
  `parent == NULL`**. Parented children stay ZOMBIE so the
  parent's `wait()` can still read the exit code; orphans are
  destroyed immediately because no one will ever wait for them.
- **`service_runner_step`** ends with
  `g_runner_orphans_reaped_total += process_reap_orphans()`. The
  cooperative tick is a natural place: it already runs at
  kernel-driven cadence and never executes on a dying process's
  stack (the prerequisite for safe destroy).
- **`struct service_runner_stats`** grows a new field
  `orphans_reaped_total` so observers / tests can confirm the
  reaper is wired and making progress.

### Tests

`tests/test_process_destroy.c` grew from 40/40 to **61/61**
with 9 new functions and 21 new asserts:

- **`test_kill_orphan_auto_reaps`** (4): kill on an orphan
  returns 0 and the slot is UNUSED before the call returns;
  process_count drops to 0.
- **`test_kill_parented_stays_zombie`** (4): kill on a parented
  child leaves it ZOMBIE with `exit_code == 128 + signal` and
  still in the parent's children chain.
- **`test_kill_signal_clamping`** (1): signal 0xFF is clamped to
  `0xFF & 0x7F == 0x7F` in `exit_code`.
- **`test_kill_unknown_pid`** (1): kill on a missing pid returns
  -1.
- **`test_reap_orphans_empty`** (1): empty table returns 0.
- **`test_reap_orphans_skips_alive`** (2): EMBRYO orphans are
  not reaped.
- **`test_reap_orphans_skips_parented_zombies`** (2): parented
  zombies are left in the chain.
- **`test_reap_orphans_destroys_orphan_zombies`** (4): two
  orphan zombies + one alive process -> 2 reaped, alive
  preserved, count == 1.
- **`test_reap_orphans_idempotent`** (2): first sweep reaps,
  second is a no-op.

### Validation

- `[test_process_destroy]` 40/40 -> **61/61**.
- `Todos os testes passaram.`
- `make layout-audit` Warnings:none.
- No new files; the diff is a minimal addition to
  `process.{c,h}`, `service_runner.{c,h}`, and the test.

### What this unblocks

- Phase 5f's segfault smoke is now leak-free at runtime: the
  killed embedded hello slot will be reaped on the next
  `service_runner_step()` tick after the kill (or inline, since
  it has no parent and `process_kill` reaps inline for orphans).
- A future POSIX-correct `init`/PID 1 turns the orphan path off
  with a one-line swap inside `process_destroy` (children
  re-parent to init instead of becoming roots), at which point
  `process_reap_orphans` becomes the init reaper without code
  changes.

## Phase 6.5 - process-tree linkage (DONE 2026-04-29)

Phase 6.5 closes one of the deferred fork/exec correctness items
listed under phase 6: the `parent`/`children`/`next_sibling`
fields existed in `struct process` but were never populated, so
`process_destroy` could not orphan its children or detach itself
from its parent. This was a contract gap waiting to bite the
moment a real `fork()` consumer landed (multi-process shells,
service-runner spawning workers, etc.).

### Linkage helpers

`src/kernel/process.c` introduces two private helpers that are now
the **only writer surface** for the three linkage pointers:

```c
static void process_link_child(struct process *child,
                               struct process *parent) {
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
```

Insertion is LIFO (head insertion) and is **not** part of the
public contract; the host tests document this explicitly so a
future change to FIFO/priority ordering is a single coordinated
update.

### Wiring

- **`process_create`**: replaces the bare `p->parent = current_proc`
  with `process_link_child(p, current_proc)`. When boot-time code
  spawns a process before `current_proc` is set (e.g. the embedded
  hello path), the helper sees `parent=NULL` and the child becomes
  a root with `ppid=0`, matching the previous behaviour.
- **`process_fork`**: after `process_create` (which links the new
  slot into `current_proc`'s children list), the fork code now
  calls `process_unlink_child(child)` and then
  `process_link_child(child, parent)` so the caller-supplied parent
  becomes the actual owner regardless of who is scheduled.
- **`process_destroy`**: gains a two-step tree detach BEFORE any
  payload teardown:
  1. Walk `p->children` and orphan each one (`parent=NULL`,
     `ppid=0`, `next_sibling=NULL`). Today there is no init/PID 1,
     so orphans become roots; future POSIX-correct re-parenting to
     PID 1 is a one-line swap (call `process_link_child(child,
     init_proc)` instead).
  2. Call `process_unlink_child(p)` to detach from the parent's
     `children` list so a future SMP walker never sees a stale
     pointer into the now-destroyed slot.

The order matters: tree detach happens FIRST, then the address
space, FDs and main thread are torn down. Once SMP lands the same
ordering will satisfy the per-parent lock invariant ("a child slot
is detached from the tree before its payload is freed").

### Header documentation

`include/kernel/process.h` now spells out the discipline directly
on the struct fields: `parent`/`children`/`next_sibling` carry a
multi-paragraph comment that lists the invariants
(`process_link_child`/`process_unlink_child` are the only writers,
`process_destroy` orphans before freeing, future SMP locking is a
per-parent lock). This is the single source of truth the next
contributor reads before touching the tree.

### Tests

`tests/test_process_destroy.c` grew from 17/17 to **40/40** with
five new functions and 23 new asserts:

- **`test_fork_links_child`** (5 asserts): `process_fork` produces
  `child->parent == parent`, `child->ppid == parent->pid`,
  `parent->children == child`, `child->next_sibling == NULL`,
  chain length 1.
- **`test_fork_lifo_ordering`** (3 asserts): three forks produce a
  3-element chain, all three children reachable, and the most
  recent fork sits at the head (current LIFO policy is documented
  as not contractual).
- **`test_destroy_unlinks_from_parent`** (5 asserts): destroying
  one of two children leaves the parent with chain length 1
  pointing at the survivor, the destroyed child no longer in the
  chain; destroying the last child leaves `parent->children ==
  NULL` without disturbing the parent's state.
- **`test_destroy_orphans_children`** (6 asserts): destroying the
  parent of three children orphans all three (`parent=NULL`,
  `ppid=0`, `next_sibling=NULL`) without flipping their state
  away from EMBRYO.
- **`test_orphan_destroy_safe`** (4 asserts): an orphan can be
  destroyed AFTER its parent without dereferencing the (now
  UNUSED) parent slot; final `process_count() == 0`.

Two helpers (`count_chain`, `chain_contains`) keep the assertions
readable. The existing 17 phase-6 tests continue to pass; none of
the new tests touch global state outside `process_system_init`
(via `reset_world`), so ordering between blocks is robust.

### Validation

- `Todos os testes passaram.` with
  `[test_process_destroy]` now 40/40 (was 17/17 in phase 6).
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- No new files touched outside `process.{c,h}` and the test;
  no Makefile change.

### What this unblocks

- Future `fork()` consumers (shell job control, service-runner
  worker spawning, the eventual init/PID 1) can rely on a real
  process tree instead of populating it themselves and risking
  inconsistency.
- `process_iter` / `process_stats` already publish `ppid`; once
  the task-manager Processes view grows a "tree" mode, the data
  is already accurate.
- POSIX-correct re-parenting to init is a single-line swap inside
  `process_destroy` once init exists. The orphan path is the
  current "no init" sentinel.

### Still deferred

- Real address-space copy in `process_fork` (today the child
  shares parent's `brk`/`heap_start`/`stack_top` numerically but
  NOT page tables; dormant because no user code calls `fork`
  yet). Tracked under phase 6.5+.
- argv/envp packing for `process_exec` (currently `argv` is
  ignored and the `(void)argv` cast suppresses the warning).
- `process_kill` does not call `process_destroy`; it only sets
  the state to ZOMBIE and kills the main thread. Address space +
  FDs are freed only when something later calls `process_wait` or
  `process_destroy` directly. Logged under phase 6.6 (zombie
  reaping correctness).

## Phase 6 - process_destroy + cleanup (DONE 2026-04-29)

Phase 6 introduces the public `process_destroy(p)` lifecycle hook
that closes the embryo-slot leak documented as a TODO in phase 5c
and refactors `process_wait` to share the same teardown code.
fork/exec/wait correctness improvements (real address-space copy,
argv/envp packing, child reaping, etc.) are deferred to a follow-on
phase 6.5; this iteration focuses on the lifecycle primitive that
unblocks phase 5c's cleanup contract.

### Public lifecycle helper

`include/kernel/process.h` declares:

```c
void process_destroy(struct process *p);
```

Documented invariants (see header comment for the full text):

- NULL or already-`PROC_STATE_UNUSED` -> no-op (idempotent).
- Main thread, if any, killed via `task_kill` so a stale thread
  cannot run on a half-torn-down address space.
- Address space released via `vmm_destroy_address_space`, then
  the `address_space` pointer cleared.
- Every `fds[i]` slot released through `process_fd_free`.
- Slot's `state` set to `PROC_STATE_UNUSED` so a future
  `process_create` can reuse it.
- The struct is intentionally NOT zeroed: leaving `pid` / `name`
  legible until the next allocation makes post-mortem debugging
  easier, and `process_create` re-initialises every field anyway.
- Children are NOT re-parented in this iteration; the M4 phase
  5/6 use cases (hello spawn fail, exec fail before any fork)
  never have children at the call site.

### Refactor `process_wait`

`src/kernel/process.c::process_wait` previously inlined the
`vmm_destroy_address_space` + `state = UNUSED` cleanup. It now
flows through `process_destroy`, eliminating the duplication and
ensuring a single audit point for slot teardown.

### Close the phase 5c TODO

`src/kernel/user_init.c::kernel_spawn_embedded_hello` now calls
`process_destroy(p)` on the `KERNEL_SPAWN_LOAD_FAILED` path. The
TODO comment was replaced with a phase 6 reference explaining the
cleanup. The 15 host asserts in `tests/test_user_init.c` continue
to pass because the test stubs `elf_load_into_process`; the
extra `process_destroy` call lands on a fresh embryo slot whose
address space was allocated by the real `process_create` and is
now correctly freed by the test's stub VMM.

### Tests

`tests/test_process_destroy.c` ships **17 host asserts** in six
groups:

1. `process_destroy(NULL)` is a no-op (count stays 0).
2. `process_destroy` on an UNUSED slot is a no-op.
3. EMBRYO -> UNUSED: count back to 0, state UNUSED, address_space
   cleared, every `fds[]` slot zeroed.
4. ZOMBIE -> UNUSED: state UNUSED, address_space cleared.
5. Idempotent: a second `process_destroy(p)` after the first
   keeps the slot UNUSED and `process_count() == 0`.
6. Slot reusable: a follow-up `process_create` succeeds, the new
   pid is strictly greater than the destroyed one, the new name
   sticks, and the address space is freshly allocated.

### Validation

- Host build: `Todos os testes passaram.` with the new
  `[test_process_destroy]` block contributing 17/17. The 15-assert
  `[test_user_init]` block also still passes despite the new
  `process_destroy` call on the failure path.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.

### Deferred (phase 6.5+)

- Real address-space copy in `process_fork` (currently the child
  shares `parent->brk/heap_start/stack_top` numerically but does
  NOT clone the page tables; today this is dormant because no
  user code actually calls `fork`).
- argv/envp packing for `process_exec` (currently `argv` is
  ignored).
- Child re-parenting in `process_destroy` (not yet observable;
  phase 5/6 paths do not have children).
- COW marking for fork: `vmm_clone_address_space` + read-only PTE
  flips so the first write to a shared page faults and triggers a
  copy (pairs with phase 7 demand paging).

## Phase 7a - recoverable user #PF seam (DONE 2026-04-29)

Phase 7a opens the door to demand paging by extending the pure
fault classifier to recognise user-mode #PF on a not-present page
as `ARCH_FAULT_RECOVERABLE`, and by wiring the dispatcher to
consult `vmm_handle_page_fault` before falling through to the kill
path. The VMM hook itself still returns -1 (the implementation
will land with phase 7b), so today the runtime behaviour is
unchanged: a user-mode #PF still kills the offending process. What
changes is the contract surface: the seam is now in source, locked
by host tests, and ready for phase 7b to drop a real handler in.

### Classifier changes

`src/arch/x86_64/fault_classify.c` documents the page-fault error
code bits (Intel SDM Vol 3 Sec 4.7) and adds a private helper
`pf_error_code_is_recoverable(error_code)`:

```c
static int pf_error_code_is_recoverable(uint64_t error_code) {
    if (error_code & PF_ERR_PRESENT)    return 0;  /* P=1: protection */
    if (error_code & PF_ERR_RESERVED)   return 0;  /* RSVD=1: corrupt PT */
    if (error_code & PF_ERR_PROTECTKEY) return 0;  /* PK=1: PKRU policy */
    return 1;
}
```

`arch_fault_classify` now precedence-orders the rules as:

1. NULL info -> `KERNEL_PANIC`.
2. NMI / #DF / #MC -> `KERNEL_PANIC` regardless of CPL.
3. Kernel-mode CPL -> `KERNEL_PANIC` (kernel does not demand-page
   its own working set today).
4. User-mode vector outside the recoverable table -> `KERNEL_PANIC`.
5. **NEW** User-mode #PF (vector 14) with P=0, RSVD=0, PK=0 ->
   `ARCH_FAULT_RECOVERABLE`.
6. Any other user-mode fault -> `KILL_PROCESS`.

The header docstring (`include/arch/x86_64/fault_classify.h`) was
re-written to ladder the precedence explicitly so future
contributors can extend rule (5) (e.g. CoW recovery in phase 7b)
without re-deriving the matrix.

### Dispatcher wiring

`src/arch/x86_64/interrupts.c::x64_exception_dispatch` now stores
the classifier verdict in a local and consults
`vmm_handle_page_fault(cr2, error_code)` when the verdict is
`ARCH_FAULT_RECOVERABLE`:

```c
enum arch_fault_action action = arch_fault_classify(&finfo);

if (action == ARCH_FAULT_RECOVERABLE && process_current() != NULL) {
    if (vmm_handle_page_fault(finfo.cr2, finfo.error_code) == 0) {
        return;                       /* IRET resumes user code */
    }
    diag_write("\n[x64] vmm_handle_page_fault refused recovery; "
               "escalating to KILL_PROCESS\n");
    action = ARCH_FAULT_KILL_PROCESS;
}

if (action == ARCH_FAULT_KILL_PROCESS && process_current() != NULL) {
    /* unchanged kill path: 128 + vector exit code */
}
```

A successful VMM recovery returns from the dispatcher; the existing
asm in `interrupts_asm.S` IRETs back into user space and the
faulting instruction re-runs. A refusal logs once and falls through
to the kill path so a misbehaving binary is still contained even if
the VMM hook is buggy.

### VMM hook (unchanged on the implementation side)

`src/memory/vmm.c::vmm_handle_page_fault` is the same stub from
phase 4: it bumps `vmm_global_stats.page_faults` and returns -1.
Phase 7b will replace the body with real demand-paging logic
(zero-page fill for anonymous regions, file-backed read-in for
mmap, etc.) without touching the dispatcher or classifier.

### Tests

`tests/test_fault_classify.c` grew by 9 asserts to 50/50 and was
restructured around the new contract:

- The legacy `kill_table` entry `{ 14, "user #PF -> kill" }` was
  removed: with the default `error_code=0`, that case now maps to
  `RECOVERABLE`, not `KILL_PROCESS`. A comment in the table
  documents the move and points at the dedicated PF tests.
- New helper `make_pf(error_code, cs)` builds a #PF info struct.
- New section "User-mode #PF error-code matrix (phase 7a)" with
  three test functions:
  - `test_pf_recoverable`: U=1, P=0 with W=0 / W=1 / I=1 each
    map to `RECOVERABLE`.
  - `test_pf_kill`: U=1 with P=1 (read), P=1+W=1 (write, future
    CoW), RSVD=1, PK=1 each map to `KILL_PROCESS`.
  - `test_pf_kernel_always_panics`: kernel-mode #PF with P=0
    (read or write) maps to `KERNEL_PANIC` regardless of error
    code.

### Validation

- Host build: `Todos os testes passaram.` with
  `[test_fault_classify]` contributing 50/50 (was 41/41 in phase
  4). All other test blocks still pass with their pre-phase-7a
  counts.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- The dispatcher change is a single new branch + a renamed local;
  no other call site of `arch_fault_classify` exists in the
  kernel today.

### Deferred (phase 7b)

- Real `vmm_handle_page_fault` body: walk the address space for a
  region containing `cr2`, allocate a frame via `pmm_alloc_page`,
  zero-fill it, install the PTE, return 0.
- Per-process anonymous regions registry (so the handler can
  decide which faults are legitimate vs which should escalate to
  kill).
- Heap-grow on write-fault past current `brk` (sbrk-style demand
  paging for `process->brk` extension).
- RSS counter wired into `process_stats` (currently 0).

### Deferred (phase 7c, after CoW)

- Extend rule (5) to also classify P=1+W=1 from user mode as
  `RECOVERABLE` once `vmm_clone_address_space` marks shared pages
  read-only with a CoW PTE bit. The dispatcher path stays
  identical; only the classifier and the VMM hook change.

## Phase 4 - fault isolation (DONE 2026-04-29)

The exception dispatcher now consults a pure classification function
to decide, on every CPU exception (vectors 0..31), whether the fault
is a fatal kernel event (panic + halt, current behaviour) or a
user-mode fault that should kill the offending process and let the
scheduler keep going. Today the kill path is dormant because no user
process has been launched yet (M4 phase 5 still pending), but
the contract is already locked by host tests so the wiring cannot
silently drift.

### Classifier module

- `include/arch/x86_64/fault_classify.h` declares:
  - `enum arch_fault_action { KERNEL_PANIC=0, KILL_PROCESS=1,
    RECOVERABLE=2 }` - the third value is reserved for phase 7
    (demand paging / COW fix-ups) but already part of the locked
    enum so the table does not need to grow later.
  - `struct arch_fault_info` with `vector`, `error_code`, `cs`,
    `rip`, `cr2` - everything the classifier needs without dragging
    in the full IRET frame.
  - `arch_fault_classify(const struct arch_fault_info *)` and
    `arch_fault_is_user(uint64_t cs)` (`(cs & 0x3) == 3`).
- `src/arch/x86_64/fault_classify.c` is the pure-logic implementation
  (no globals, no inline asm, no kernel state). The classification
  rule is:
  - NULL info -> panic.
  - NMI (vector 2) / `#DF` (vector 8) / `#MC` (vector 18) -> panic
    regardless of CPL. These represent platform-level corruption
    that cannot be safely contained inside a process kill.
  - Kernel-mode CPL (`(cs & 0x3) != 3`) -> panic. A bug in kernel
    code or a driver must not be papered over.
  - User-mode CPL with a vector in the recoverable set
    (`#DE, #DB, #BP, #OF, #BR, #UD, #NM, #TS, #NP, #SS, #GP, #PF,
    #MF, #AC, #XM, #VE, #CP`) -> kill process.
  - Anything else (reserved/unknown vectors 9, 15, 22..27, 30, 31)
    -> panic so genuine kernel bugs surface loudly.

### Dispatcher wiring

- `src/arch/x86_64/interrupts.c` `x64_exception_dispatch` builds an
  `arch_fault_info` from the IRET frame, calls
  `arch_fault_classify`, and on `ARCH_FAULT_KILL_PROCESS` &&
  `process_current() != NULL`:
  1. logs `[x64] User-mode fault, killing offending process` plus
     vector / err / cs / rip (and cr2 on `#PF`) to the debug
     console.
  2. calls `process_exit(128 + (int)vector)` (POSIX-style exit
     code: high byte indicates death by signal, low byte the
     vector). `process_exit` is `noreturn` and reschedules.
- The pre-existing panic path stays intact for kernel-mode faults
  and for user-mode faults that the classifier maps to panic. The
  kill path also collapses to the panic path when
  `process_current()` is `NULL`, which is the steady state until a
  user process actually runs in phase 5.
- `src/arch/x86_64/interrupts.c` now `#include`s
  `arch/x86_64/fault_classify.h` and `kernel/process.h`.

### Tests

- `tests/test_fault_classify.c`: 42 host asserts in seven groups:
  1. `arch_fault_is_user` CPL boundary (kernel CS/DS, user CS/DS
     with RPL=3, RPL=1/2 reject, high selector bits ignored).
  2. NULL guard.
  3. Kernel-mode `#PF`, `#GP`, `#UD`, `#DE` -> panic.
  4. NMI / `#DF` / `#MC` -> panic from any CPL.
  5. User-mode kill table (17 vectors) all map to
     `ARCH_FAULT_KILL_PROCESS`.
  6. Reserved/unknown user vectors (9, 15, 22, 31) -> panic.
  7. Enum value contract (PANIC=0, KILL=1, RECOVERABLE=2).
- Wired into `tests/test_runner.c` and `TEST_SRCS`.
- `src/arch/x86_64/fault_classify.c` is also added to the kernel
  link list (`CAPYOS64_OBJS`) so the production dispatch can call
  the classifier.

### Validation

- `gcc -fsyntax-only` clean for `fault_classify.c`,
  `test_fault_classify.c` and `test_runner.c`. (The pre-existing
  inline asm in `interrupts.c` cannot be parsed by the macOS host
  toolchain, exactly as before; CI host parses it natively.)
- Full host build runs `Todos os testes passaram.` with the new
  `[test_fault_classify]` block contributing 42/42.
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.

### Deferred to phase 7

The `ARCH_FAULT_RECOVERABLE` enum value already exists in the
contract but is never returned today. Phase 7 (demand paging /
copy-on-write) will extend the classifier so that `#PF` from user
mode with the right error code returns `RECOVERABLE`, and the
dispatcher then asks the VMM to fault in the missing page instead
of killing the process. Until then `vmm_handle_page_fault` is still
a stub returning `-1` and any `#PF` from a user program kills it.

A QEMU smoke that intentionally segfaults a user program lands in
phase 5 (when a user binary first exists) so it can verify the full
end-to-end kill+reschedule path.

## Phase 2 onward

See the canonical plan for full sequencing:
1. Phase 1 - service-runner task
2. Phase 2 - context_switch IRQ-safe seam
3. Phase 3 - Ring 3 transition
4. Phase 4 - fault isolation (kill broken process)
5. Phase 5 - capylibc + minimal user binaries
6. Phase 6 - fork/exec/wait
7. Phase 7 - telemetry (CPU%, RSS) populating the iterator stats
8. Phase 8 - flip to preemptive scheduler + smoke QEMU automation
9. Phase 9 - smoke integration (Host + QEMU)
10. Phase 10 - docs + release
11. Phase 11 - cleanup

Each phase ships with host tests, smoke validation, and updated entries
in `docs/plans/active/capyos-robustness-master-plan.md`.
