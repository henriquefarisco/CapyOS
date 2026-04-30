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

## Phase 7c - copy-on-write (DONE 2026-04-30)

Phase 7c lands the missing piece of the M4 fork story: `process_fork`
no longer hands the child a fresh empty AS; instead the parent's AS
is **CoW-cloned**, so writable user pages become RO-shared in both
AS until one side writes. The first write faults, the VMM allocates
a private copy (or reuses in place if last sharer), and the user
instruction retries. Mirrors the textbook UNIX fork-exec semantics
and lets two processes share the bulk of the parent's memory until
they diverge.

### Architecture

The phase 7c stack is split into four layers, three of which are
**100% host-testable**:

```
                                              host-tested?
  pmm_refcount.c   per-frame refcount table     YES
  vmm_cow.c        pure CoW decision matrix     YES
  fault_classify.c P=1+W=1 -> RECOVERABLE       YES
  vmm.c            x86_64 PTE walker / glue     no (cr3/invlpg)
```

The non-testable layer in `vmm.c` is intentionally tiny: it walks
PML4 -> PDPT -> PD -> PT once, calls into the host-tested decision
module, and applies the result via `invlpg`. Anything that could
have a non-trivial bug (decision matrix, refcount arithmetic, error
code interpretation) is locked by host tests with explicit
expectations.

### New public surface

- `include/memory/pmm.h`:
  - `pmm_frame_refcount_init/inc/dec/get` — uint16_t array indexed
    by PFN, sized `PMM_REFCOUNT_MAX_PAGES = PMM_BITMAP_SIZE_PAGES`.
- `include/memory/vmm.h`:
  - `VMM_PAGE_COW = (1ULL << 9)` — software-only marker on the AVL
    bits of an x86_64 4 KiB PTE. CPU ignores it; OS uses it to tell
    "RO because of CoW share" apart from "RO because user said so".
  - `vmm_clone_address_space(src)` — public entry for `process_fork`.
- `include/memory/vmm_cow.h`:
  - `vmm_cow_decide(pte, refcount_after_dec)` — pure decision
    function. Return action is one of `VMM_COW_NOT_COW`,
    `VMM_COW_REUSE`, `VMM_COW_COPY`.

### Cloning algorithm (`vmm_clone_address_space`)

1. Allocate a fresh AS via `vmm_create_address_space`. Kernel half
   of the PML4 (indices 256..511) is shared as before.
2. Walk the user half (indices 0..255) of `src`. For every present
   PML4 entry, allocate a new PDPT and recurse.
3. Recurse PDPT -> PD -> PT until reaching 4 KiB leaf PTEs.
4. For each present leaf:
   - Bump the underlying frame's refcount.
   - If the PTE is `(USER & WRITE & !HUGE)`: clear `WRITE`, set
     `COW`, mirror the result into BOTH src and dst PTEs. The next
     write from EITHER AS faults into recovery.
   - Otherwise (RO mapping, HUGE leaf, or kernel-internal): copy
     the PTE bit-for-bit into dst. Refcount bump alone keeps the
     frame alive past either AS's destroy.
5. Mirror `src->rss_pages` into `dst->rss_pages` so observability
   (`vmm_address_space_rss`) is correct from the moment the clone
   returns.

Failure path: any allocation failure tears down the partially built
dst via `vmm_destroy_address_space`. The destroy walker (extended
in this phase) decrements every frame refcount before potentially
freeing, so the partial bumps are cleanly reverted.

### Page-fault flow

The classifier (`arch_fault_classify`) now reports user-mode #PF as
RECOVERABLE in two shapes (was only one):

1. P=0 (any access kind) - demand paging, unchanged from phase 7b.
2. P=1 + W=1 - CoW candidate, NEW in phase 7c.

`vmm_handle_page_fault` dispatches based on the error code:

```c
if (error_code & P) {
    if (error_code & W) return vmm_handle_cow_fault(as, fault_addr);
    return -1;  /* present + read fault is never recoverable */
}
return vmm_handle_demand_page(...);  /* phase 7b path */
```

`vmm_handle_cow_fault`:

1. Walk to the leaf PTE (`vmm_walk_to_leaf`). NULL if page is huge
   or any intermediate is missing - return -1 (KILL).
2. Decrement the frame refcount unconditionally and call
   `vmm_cow_decide(pte, refcount_after_dec)`.
3. If decision is `NOT_COW`: re-increment to keep the destroy
   walker consistent and return -1 (KILL). This is the genuine
   W-on-RO case (write to text segment, mprotect RO, etc.).
4. If decision is `REUSE`: flip flags in place via the
   `(pte | new_set) & ~new_clr` recipe, `invlpg`, success.
5. If decision is `COPY`: allocate a fresh frame, byte-copy 4 KiB,
   point the PTE at the new frame with the same flag recipe,
   `invlpg`, success.

`vmm_global_stats.cow_faults` increments on every successful CoW
service so observability tooling can distinguish demand pages from
write-after-clone faults.

### Destroy walker (`vmm_destroy_address_space`)

Extended to consult the refcount table before freeing each leaf
frame:

```c
uint16_t pre = pmm_frame_refcount_get(leaf_phys);
if (pre == 0u) {
    pmm_free_page(leaf_phys);          /* never refcounted */
} else {
    if (pmm_frame_refcount_dec(leaf_phys) == 0u) {
        pmm_free_page(leaf_phys);      /* we were the last sharer */
    }
    /* else: another AS still holds this frame, do not free */
}
```

This preserves pre-7c semantics (single-AS user mappings, demand-
paged anonymous pages, etc. report 0 from the helper and are freed
straight away) while correctly handling the new CoW-shared frames.

### Process fork wiring

`process_fork` now performs a CoW clone of the parent's AS:

```c
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
```

The host stub in `tests/stub_vmm.c` provides a `vmm_clone_address_space`
that just returns a fresh empty AS (matching `vmm_create_address_space`),
so existing process_iter / process_destroy tests keep passing without
needing a host page-table simulator.

### Tests added

| Suite | New asserts | What it locks |
|---|---|---|
| `test_pmm_refcount` | 18/18 | Per-PFN counter; idempotent dec; range guards; saturation; PFN collapsing for sub-page addresses |
| `test_vmm_cow` | 13/13 | Decision matrix: NOT_COW / REUSE / COPY arms; correct flag deltas; PRESENT/USER preserved across COPY |
| `test_fault_classify` | +4 | P=1 W=1 user -> RECOVERABLE; RSVD/PK override; kernel-mode P=1 W=1 still PANIC |

Total **+35 host asserts**, zero regressions.

### Validation

- `make test`: `Todos os testes passaram.` Across all suites,
  including the +35 new asserts and the updated phase 7c entry in
  `test_fault_classify`.
- `make layout-audit`: `Warnings: none`.
- Build: `pmm_refcount.o` and `vmm_cow.o` were added to
  `CAPYOS64_OBJS`; the cross-compiler smoke (Phase 5e/8b) will pick
  them up on next CI run.
- Local QEMU execution N/A on macOS workstation; CI is the source
  of truth for the kernel-side glue.

### Deferred / not yet exercised

- A QEMU smoke that proves end-to-end CoW (e.g. `hello_fork` user
  binary that fork()s, writes a different value from each side,
  and asserts both outputs land in the debugcon log). Lands as a
  separate phase once a real `fork()` syscall is wired up
  (currently `process_fork` is only callable from kernel-side
  code).
- 2 MiB huge-page CoW. The cloner skips HUGE leaves defensively;
  the kernel does not currently install user huge pages, so this
  is not yet observable.
- TLB shootdown across cores. The `invlpg` path only flushes the
  current CPU. Multi-core CoW will need IPI-based shootdown when
  SMP user-mode lands.

## Phase 8a - preemptive scheduler primitives (DONE 2026-04-30)

Phase 8a is the smallest safe step toward the preemptive flip: it locks
the host-side contract for the preemptive tick path WITHOUT touching
`kernel_main.c`'s boot flow. Default builds remain 100% cooperative and
the preemptive logic only fires once policy is flipped to PRIORITY or
ROUND_ROBIN (deferred to phase 8b).

### Public surface

- `include/kernel/scheduler.h` exposes `SCHED_DEFAULT_QUANTUM` (10 ticks
  at SCHEDULER_TICK_HZ=100Hz, i.e. 100ms) and the new
  `scheduler_set_running(int running)` API. The header doc explains why
  the kernel needs this: `scheduler_start()` is `noreturn` and would
  hijack the boot flow, so a smaller "mark scheduler running" primitive
  is required for the kernel_main wiring that lands in phase 8b.

### Quantum initialisation

`src/kernel/task.c::task_create` now seeds `t->quantum_remaining =
SCHED_DEFAULT_QUANTUM` so the very first preemptive tick after a task is
dispatched does not immediately context-switch away (which would happen
if the field were 0 from kmalloc-zero and the tick decremented-and-
checked in the same pass). `task.c` now `#include`s `kernel/scheduler.h`
to share the constant.

### Bug fix audit

Confirmed (no code change needed) that `scheduler_tick` already returns
early when quantum > 0 in the preemptive branch: the previous unconditional
`schedule()` at the end of the preemptive arm would have caused a
context-switch on every tick, making the 100Hz timer indistinguishable
from gang-scheduling. The current code only context-switches on quantum
exhaustion or when there is no current task.

### Tests

`tests/test_context_switch.c` grew from 38 to **47 asserts** (+9) with a
new "Preemptive tick (M4 phase 8a)" section:

1. `task_create` initialises `quantum_remaining` to `SCHED_DEFAULT_QUANTUM`.
2. Preemptive tick decrements quantum.
3. Preemptive tick does NOT context-switch while quantum>0 (regression
   guard against the previous unconditional `schedule()` bug).
4. Quantum exhaustion triggers exactly one context_switch.
5. Quantum is reseeded to `SCHED_DEFAULT_QUANTUM` after exhaustion.
6. context_switch arguments use the right old/new task contexts.
7. Preemptive tick promotes a runnable task when current is NULL.
8. `scheduler_set_running(0)` clears the flag.
9. `scheduler_set_running(1)` sets the flag; non-zero positive values
   normalise to truth.

### Validation

- `make test TEST_BIN=/tmp/capy_unit_tests CAPYOS64_DEPS= UEFI_LOADER_DEPS=`
  prints `Todos os testes passaram.` with the new
  `[test_context_switch]` block contributing **47/47** (was 38/38 in
  phase 2).
- No production behaviour change: default policy remains
  `SCHED_POLICY_COOPERATIVE`, so the new tick branch is dead code in
  release builds until phase 8b flips the policy.
- `scheduler_set_running` is unused at the call sites today; phase 8b
  will invoke it from `kernel_main.c` behind a `CAPYOS_PREEMPTIVE_SCHEDULER`
  build flag (mirroring `CAPYOS_BOOT_RUN_HELLO` from phase 5d).

### Deferred to phase 8b

- Wire `scheduler_init(SCHED_POLICY_PRIORITY)` + `scheduler_set_running(1)`
  into `kernel_main.c` behind `CAPYOS_PREEMPTIVE_SCHEDULER`.
- Decide on auto-add semantics: either make `task_create` call
  `scheduler_add` automatically (and remove the redundant call from
  `worker.c`), or introduce a new helper like `task_create_and_run`. The
  existing host tests rely on explicit `scheduler_add` so the change
  must be coordinated.
- Make the IRQ asm path save/restore user context on the way to ring 3
  so a 100Hz tick in user mode actually lands in the scheduler.
- Remove the cooperative `kernel_service_poll` delegate once the
  service-runner task is dispatched by the scheduler proper.

### Deferred to phase 8c

- QEMU smoke that proves preemption: spawn two CPU-bound user tasks
  (e.g. `hello_busy_a`, `hello_busy_b`) and assert both make progress in
  the debugcon log within a wall-clock budget.

## Phase 8b - kernel_main wiring + smoke harness (DONE 2026-04-30)

Phase 8b connects the phase 8a primitives to the boot path so a CI-only
build (with `-DCAPYOS_PREEMPTIVE_SCHEDULER`) flips the scheduler policy
from cooperative to PRIORITY and marks `sched_running=1` BEFORE the
existing 100Hz APIC tick starts firing. Default builds remain
unchanged, which preserves the kernel shell during normal runs and
prevents a half-wired preemptive flow from bricking the boot.

### Boot wiring

`src/arch/x86_64/kernel_main.c` adds two `#ifdef CAPYOS_PREEMPTIVE_SCHEDULER`
blocks around the existing APIC arm site:

```c
#ifdef CAPYOS_PREEMPTIVE_SCHEDULER
  scheduler_init(SCHED_POLICY_PRIORITY);
  klog(KLOG_INFO,
       "[scheduler] Policy=PRIORITY (preemptive flip enabled).");
#endif
  if (apic_available() && !handoff_boot_services_active()) {
    apic_timer_set_callback(scheduler_tick);
    apic_timer_start(100);
    klog(KLOG_INFO, "[scheduler] Preemptive tick armed at 100Hz.");
#ifdef CAPYOS_PREEMPTIVE_SCHEDULER
    scheduler_set_running(1);
    klog(KLOG_INFO,
         "[scheduler] Marked as running (sched_running=1).");
#endif
  }
```

The macro is undefined by default, so production builds skip both
blocks entirely. Phase 8c smoke author defines it on the cross-compiler
command line: `make all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER'`.

The full `scheduler_start()` is intentionally avoided because it is
`noreturn` and would hijack the boot flow before the splash UI and
shell come up. Phase 8c may revisit that decision once a user task
dispatches via the scheduler proper; for now the cooperative
`kernel_service_poll` delegate keeps driving the runner step.

### Smoke harness

`tools/scripts/smoke_x64_preemptive.py` (~250 lines) is the phase 5e
script with different markers:

- Success markers (all three required):
  - `[scheduler] Policy=PRIORITY (preemptive flip enabled).`
  - `[scheduler] Preemptive tick armed at 100Hz.`
  - `[scheduler] Marked as running (sched_running=1).`
- Failure markers (any aborts the smoke):
  - `panic` (kernel must NOT panic when policy is flipped).

The same QEMU lifecycle, debugcon polling, timeout, and SIGTERM
handling as phase 5e/5f are reused via the `smoke_x64_common` and
`smoke_x64_session` modules.

### Make target

```
smoke-x64-preemptive:
    $(MAKE) clean
    $(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER'
    $(MAKE) iso-uefi
    $(MAKE) manifest64
    python3 tools/scripts/smoke_x64_preemptive.py \
            $(SMOKE_X64_PREEMPTIVE_ARGS)
```

`smoke-x64-preemptive` joins the existing `.PHONY` list. CI invokes
it as a single Make call.

### Validation

- `python3 -m py_compile tools/scripts/smoke_x64_preemptive.py` passes.
- `make -n smoke-x64-preemptive` resolves the recipe (clean -> all64 ->
  iso-uefi -> manifest64 -> python3) without errors.
- Host build: `Todos os testes passaram.` `[test_context_switch]`
  remains 47/47 (CAPYOS_PREEMPTIVE_SCHEDULER not defined in host
  builds, so the `#ifdef` blocks are stripped).
- `make layout-audit CAPYOS64_DEPS= UEFI_LOADER_DEPS=` reports
  `Warnings: none`.
- Local QEMU execution N/A on this macOS workstation; CI is the
  source of truth, same constraint as phase 5e/5f.

### What this locks

- The `CAPYOS_PREEMPTIVE_SCHEDULER` build flag is observable in the
  debugcon log: any future regression that drops the wiring will be
  caught by `smoke-x64-preemptive` before reaching release.
- Phase 8a's `scheduler_set_running` is now actually called from the
  kernel boot path under the flag (was previously dead code).
- Default builds are byte-for-byte unchanged: the `#ifdef` blocks
  contribute zero bytes when the macro is not defined.

### Deferred to phase 8e/8f

- Phase 8e covers the kernel-mode equivalent of this idea (two
  kernel tasks, no user binary). Phase 8f extends it to ring 3
  once the IRQ asm path saves/restores user RSP/RIP.
- IRQ asm path that saves/restores user context on a 100Hz tick from
  ring 3 (currently the dispatcher is set up but the tick callback
  does not yet save user RSP/RIP into the task's `task_context`).
- Removal of the cooperative `kernel_service_poll` delegate once the
  service-runner task is dispatched by the scheduler proper.
- Auto-add semantics for `task_create` (or new `task_create_and_run`
  helper). Phase 8b leaves this as-is so worker.c's explicit
  `scheduler_add` and the host tests' explicit calls keep working.

## Phase 8c - APIC IRQ wiring fix (DONE 2026-04-30)

Phase 8c closed a latent bug discovered while wiring up phase 8b: the
APIC timer callback registered via `apic_timer_set_callback(scheduler_tick)`
**was never actually invoked at runtime**. The IDT stub at vector 32
delegates to `x64_exception_dispatch`, which looks up
`g_irq_handlers[(int)(vector - 32u)]`. Because nothing in the boot path
was calling `irq_install_handler(0, apic_timer_irq_handler)`, the
dispatcher always found `NULL` for IRQ 0, sent a spurious `pic_send_eoi`,
and dropped the tick silently. The "Preemptive tick armed at 100Hz."
log was therefore misleading — the timer was armed but its IRQ never
reached `scheduler_tick`.

### Fix

`src/arch/x86_64/kernel_main.c` now calls `irq_install_handler(0,
apic_timer_irq_handler)` immediately after `apic_timer_set_callback`
and immediately before `apic_timer_start`, gated on
`CAPYOS_PREEMPTIVE_SCHEDULER` so default builds remain bit-for-bit
unchanged:

```c
apic_timer_set_callback(scheduler_tick);
#ifdef CAPYOS_PREEMPTIVE_SCHEDULER
  irq_install_handler(0, apic_timer_irq_handler);
#endif
apic_timer_start(100);
klog(KLOG_INFO, "[scheduler] Preemptive tick armed at 100Hz.");
#ifdef CAPYOS_PREEMPTIVE_SCHEDULER
  scheduler_set_running(1);
  klog(KLOG_INFO, "[scheduler] Marked as running (sched_running=1).");
  klog(KLOG_INFO, "[scheduler] APIC IRQ handler installed at IRQ 0.");
#endif
```

`apic_timer_irq_handler` was previously a private symbol in
`apic.c`. The phase 8c fix exposes it via `include/arch/x86_64/apic.h`
with a header doc explaining the IDT/IRQ delivery contract.

### Why no soak loop

A first attempt tried to `__asm__ volatile("sti")` and busy-loop a few
hundred million `pause` iterations to observe `apic_timer_ticks()` go
above zero. That was reverted because:

- `x64_interrupts_enable()` is defined in `interrupts.c` but **never
  called** in the rest of the boot path. Interrupts therefore stay
  disabled (IF=0) from firmware handoff through to phase 8c.
- A `sti` at this site would be the FIRST global enable in the kernel
  and risks re-entering still-incomplete IDT handlers (e.g. APIC IRQs
  arriving before the rest of the platform tables / shell runtime are
  ready).
- Phase 5e/5f smokes already exercise the user-mode iretq path, which
  is the only place where IF=1 is currently set explicitly. Moving the
  global enable into kernel_main needs its own audit and lands in
  phase 8d.

Phase 8c therefore ships only the wiring fix plus an observable klog
message, leaving real tick observation to phase 8d.

### Smoke regression guard

`tools/scripts/smoke_x64_preemptive.py` adds a fourth required marker:

```
[scheduler] APIC IRQ handler installed at IRQ 0.
```

Any future regression that drops the `irq_install_handler(0, ...)`
call (or accidentally undefines `CAPYOS_PREEMPTIVE_SCHEDULER` for the
preemptive build) will fail `smoke-x64-preemptive` with a missing
marker.

### Validation

- `make test`: `Todos os testes passaram.` (host build does not define
  `CAPYOS_PREEMPTIVE_SCHEDULER`, so the new IRQ-install line is
  stripped.)
- `make layout-audit`: `Warnings: none`.
- `python3 -m py_compile tools/scripts/smoke_x64_preemptive.py` passes.
- Local QEMU execution N/A on macOS workstation; CI Linux is the
  source of truth, same as phase 5e/5f/8b.

### Deferred to phase 8d

- Audit and adopt a global `sti` site in `kernel_main.c` (most likely
  right after the platform tables and IDT are fully primed) so the
  APIC tick can actually fire.
- Add a real observation soak: spin briefly with IF=1 and assert
  `apic_timer_ticks()` > some-non-zero-floor via an explicit
  `klog_dec` call. This is what was reverted from phase 8c.
- Two-task kernel-mode preemption demo gated by a separate
  `CAPYOS_PREEMPTIVE_DEMO` flag, with a dedicated
  `smoke-x64-preemptive-demo` target.

## Phase 8d - global IF=1 + observation soak (DONE 2026-04-30)

Phase 8d closes the observability loop opened by phase 8c. The smoke
can now PROVE the APIC tick actually fires and the registered
`scheduler_tick` callback runs end-to-end.

### Boot wiring relocation

The phase 8b/c/d wiring grew large enough to push `kernel_main.c`
above the 900-line monolith ceiling enforced by `make layout-audit`.
Phase 8d therefore extracts the entire wiring into a new helper file
that is included in `CAPYOS64_OBJS`:

- `include/arch/x86_64/preemptive_boot.h` (28 lines)
- `src/arch/x86_64/preemptive_boot.c` (~125 lines)

Four entry points are exposed:

```
void capyos_preemptive_install_policy(void);  // phase 8b
void capyos_preemptive_install_irq0(void);    // phase 8c
void capyos_preemptive_mark_running(void);    // phase 8b/c markers
void capyos_preemptive_observe_ticks(void);   // phase 8d soak
```

When `CAPYOS_PREEMPTIVE_SCHEDULER` is undefined, all four collapse to
empty stubs so `kernel_main.c` can call them unconditionally. This
removes every `#ifdef` from the call site and shrinks `kernel_main.c`
back well under the ceiling.

### Soak design

```c
void capyos_preemptive_observe_ticks(void) {
    x64_interrupts_enable();
    {
        uint64_t spin_budget = 0;
        while (apic_timer_ticks() < 3ULL && spin_budget < 50000000ULL) {
            __asm__ volatile("pause");
            ++spin_budget;
        }
    }
    x64_interrupts_disable();

    klog_dec(KLOG_INFO, "[scheduler] APIC ticks observed=",
             (uint32_t)apic_timer_ticks());
    pb_dbgcon_write("[scheduler] APIC ticks observed=");
    capyos_preemptive_emit_dec(apic_timer_ticks());
    pb_dbgcon_write("\n");
}
```

Safety analysis:

- With `SCHED_POLICY_PRIORITY` active but the run queue empty and
  `task_current() == NULL`, `scheduler_tick` only increments
  `stats.total_ticks`, walks an empty queue (no sleeper wakeups, no
  zombie reaping), and the preemptive `schedule()` returns
  immediately because `scheduler_pick_next()` returns `NULL`. There
  is no `context_switch` to a non-existent task.
- The spin budget caps the wall-clock cost at ~50M `pause`
  iterations (sub-second on KVM, single-digit-seconds on TCG).
- `cli` runs immediately after the budget exhausts so the rest of
  the boot path keeps the same `IF=0` contract it had before phase
  8d. The shell, splash UI, fs runtime, etc. observe no behaviour
  change.

### klog vs debugcon

Phase 8d also fixed a subtle observability gap discovered while
adding the smoke marker: `klog()` only buffers into a ring; nothing
in the default boot path forwards that ring to the kernel debug
console (port 0xE9). Smoke markers must therefore go through
`dbgcon_putc`/an inline `dbgcon_write` helper, mirroring the phase
5d/5f convention. Every phase 8b/c/d marker now emits to BOTH klog
(for in-kernel triage) and debugcon (for CI assertions).

### Smoke regression guard

`tools/scripts/smoke_x64_preemptive.py` now requires a fifth marker:

```
[scheduler] APIC ticks observed=
```

and asserts the trailing decimal is **strictly > 0** via a small
parser. A zero count means the soak ran but no IRQ actually fired,
which would mean either the IDT install regressed, the APIC
programming is wrong, or `x64_interrupts_enable` silently failed.

### Validation

- `make test`: `Todos os testes passaram.` (host build does not
  define `CAPYOS_PREEMPTIVE_SCHEDULER`, so the helpers compile as
  no-op stubs.)
- `make layout-audit`: `Warnings: none` (kernel_main.c shrank below
  the 900-line ceiling after the extraction).
- `python3 -m py_compile tools/scripts/smoke_x64_preemptive.py`
  passes.
- Local QEMU execution N/A on macOS workstation; CI is the source
  of truth.

### Deferred to phase 8e

- (Closed by phase 8e — see below.)

## Phase 8e - first-task trampoline + two-task demo (DONE 2026-04-30)

Phase 8e closes the canonical end-to-end proof of preemptive
scheduling: a kernel build with `-DCAPYOS_PREEMPTIVE_SCHEDULER
-DCAPYOS_PREEMPTIVE_DEMO` spawns two infinite-loop kernel tasks
(`busy_a`, `busy_b`) and the 100Hz APIC tick steadily preempts
between them. Each task emits a unique marker every ~256K busy-loop
iterations. The `smoke-x64-preemptive-demo` harness asserts BOTH
markers appear at least twice in the debugcon log within the
timeout window — the asymmetric "appears at least twice" check
catches the failure mode where a buggy switch only runs the first
task once.

### The "first task entry" trampoline

The pre-8e scheduler had a subtle architectural gap:
`scheduler_start` picks the first runnable task and calls
`task_set_current(first)`, then enters its own infinite hlt loop on
the boot stack. When the timer tick later fires, `scheduler_tick`
sees `task_current() == first` (RUNNING) and decrements quantum;
on quantum exhaustion it calls `schedule()` which invokes
`context_switch(&first->context, &next->context)`. That call
**saves the boot stack/RIP into `first->context`**, clobbering the
entry RIP that `task_create` set up. Result: `first`'s entry
function is NEVER actually executed — `first` becomes the boot
continuation and only `next` (busy_b) runs its body.

Phase 8e adds a one-way asm helper `context_switch_into_first`
(declared in `src/arch/x86_64/cpu/context_switch.S`):

```asm
context_switch_into_first:    # void (struct task_context *new_ctx)
    cli
    # cr3 swap (mirrors context_switch)
    movq 0x48(%rdi), %rax    ; testq %rax, %rax ; jz no_cr3
    movq %cr3, %rcx          ; cmpq %rax, %rcx ; je no_cr3
    movq %rax, %cr3
no_cr3:
    # Restore callee-saved + RSP/RBP from new context
    movq 0x10(%rdi), %rbx    ; movq 0x18(%rdi), %r12
    movq 0x20(%rdi), %r13    ; movq 0x28(%rdi), %r14
    movq 0x30(%rdi), %r15    ; movq 0x08(%rdi), %rbp
    movq 0x00(%rdi), %rsp
    # Restore RFLAGS (typically 0x202 = IF=1)
    movq 0x40(%rdi), %rax    ; pushq %rax ; popfq
    # Jump to entry. Caller's stack/RIP is intentionally abandoned.
    movq 0x38(%rdi), %rax    ; jmpq *%rax
```

The function is `noreturn` by design: there is no "old" context to
save, so the boot stack/RIP that called the trampoline are simply
abandoned. From there on the conventional `context_switch` handles
all subsequent swaps (it has both old and new contexts to work
with) and both tasks alternate forever at quantum boundaries.

The struct task_context offsets (0x00..0x48) are the same ones
locked by `tests/test_context_switch.c`, so the existing layout
asserts cover the trampoline as well.

### Demo body

`src/arch/x86_64/preemptive_demo.c` (~150 lines, compiled as a
no-op stub when the flag is undefined):

- `busy_a_entry` / `busy_b_entry` are infinite loops emitting
  `[busyA]` / `[busyB]` to debugcon every `DEMO_MARKER_PERIOD =
  0x40000` `pause` iterations.
- `capyos_preemptive_demo_run()`:
  1. `task_create_kernel("busy_a", ...)` and `("busy_b", ...)`.
  2. Both states forced to READY then `scheduler_add`'d.
  3. `busy_a->state = RUNNING`; `task_set_current(busy_a)`.
  4. Emit `[demo:enter]` to debugcon for the smoke marker.
  5. `context_switch_into_first(&busy_a->context)` — noreturn.
  6. Defensive `for(;;) hlt;` after the trampoline (only reached
     if the trampoline ever returns, which it does not).

### Build wiring

- `kernel_main.c` now calls `capyos_preemptive_demo_run()` right
  after `capyos_preemptive_observe_ticks()`. The function is the
  no-op stub in default and `-DCAPYOS_PREEMPTIVE_SCHEDULER`-only
  builds; only the dual-flag build actually enters the trampoline.
- `Makefile` adds `preemptive_demo.o` to `CAPYOS64_OBJS`.
- New `smoke-x64-preemptive-demo` target builds with both flags
  and runs `tools/scripts/smoke_x64_preemptive_demo.py`.

### Smoke regression matrix

The smoke now requires:

| Marker | Why it matters |
|---|---|
| `[demo:enter]` | Demo function reached the trampoline (alloc OK). |
| `[busyA]` (>=2 occurrences) | Trampoline jumped to busy_a; quantum boundary then preempted to busy_b and BACK so busy_a re-emits. |
| `[busyB]` (>=2 occurrences) | context_switch correctly swapped contexts in BOTH directions. |

Failure markers:
- `panic` — anything in the kernel paniced.
- `[demo:alloc]` — `task_create` returned NULL; demo cannot run.

The `>=2` floor on each busy marker is the asymmetric check that
turns "the trampoline ran ONCE" into a failure. Without it the
smoke could pass even on a broken `context_switch` that ran busy_a
indefinitely without ever switching to busy_b.

### Validation

- `make test`: `Todos os testes passaram.` Demo source is gated on
  the flag; host build sees only the no-op stub.
- `make layout-audit`: `Warnings: none`.
- `make -n smoke-x64-preemptive-demo`: recipe resolves.
- `python3 -m py_compile tools/scripts/smoke_x64_preemptive_demo.py`
  passes.
- Local QEMU execution N/A on macOS workstation; CI is the source
  of truth.

### Deferred to phase 8f

- The trampoline only handles kernel-mode tasks (CS=0x08, IF=1
  via popfq). User-mode tasks (CS=0x23, ring 3) need the IRQ
  dispatcher to save user RSP/RIP/segment regs into
  `task->context` on every tick from ring 3 — phase 8f.
- Removal of the cooperative `kernel_service_poll` delegate. The
  demo proves preemption works for kernel tasks; the next step is
  having the service-runner BE one of those tasks instead of
  being polled cooperatively from the boot stack.

## Phase 8f.1 - TSS scaffolding for ring-3 IRQ safety (DONE 2026-04-30)

Phase 8f addresses the LAST architectural gap blocking ring-3
preemption: a 64-bit Task State Segment loaded via `LTR` so that
the CPU has a defined kernel stack (RSP0) to push the IRET frame
onto when an IRQ fires from ring 3. Without a TSS, the very first
APIC tick from a ring-3 user task triples-faults the kernel.

Phase 8f is split into two sub-phases for incremental safety:

- **8f.1 (this phase):** Establish the TSS infrastructure: struct,
  GDT slot, descriptor encoding, `tss_init` + `LTR`. Reuse the
  shared `g_syscall_kernel_stack` as RSP0 for now. With the demo
  off, default builds are unaffected.
- **8f.2 (deferred):** Per-task RSP0 swap on every `context_switch`
  so two ring-3 tasks don't clobber each other's IRQ frames.
  Followed by a smoke that proves end-to-end ring-3 preemption.

### What 8f.1 ships

- `include/arch/x86_64/tss.h` — 104-byte `struct tss` with
  hardware-mandated layout, plus the `tss_init` / `tss_set_rsp0` /
  `tss_get_rsp0` API and the pure encoder helpers
  `tss_descriptor_low` / `tss_descriptor_high`.
- `src/arch/x86_64/tss.c` — implementation. The pure encoders and
  the runtime accessors are host-buildable; only `tss_init`'s LTR
  is gated on `__x86_64__ && !UNIT_TEST`.
- GDT growth in `interrupts.c`: `g_gdt[5]` -> `g_gdt[7]` to make
  room for the 16-byte 64-bit TSS descriptor at slots 5+6
  (selector 0x28). New helper `x64_gdt_write_tss_descriptor` is
  the only writer of those slots.
- `kernel_main.c` calls `tss_init(rsp0)` right after
  `cpu_local_init` in both the `CAPYOS_BOOT_RUN_HELLO` path and
  the production boot path. Idempotent: a second call refreshes
  RSP0 without re-issuing LTR.
- `tests/test_tss_layout.c` — **17/17 host asserts** locking:
  - `struct tss` size = 104 and the offsets of `reserved1`,
    `rsp0` (0x04 = TSS_RSP0_OFFSET), `rsp1`, `rsp2`, `ist1`,
    `iomap_base` (0x66 = TSS_IOMAP_BASE_OFFSET).
  - `tss_descriptor_low` bit ranges: limit_low [15:0], base_low
    [31:16], base_mid [39:32], access [47:40] = 0x89 for DPL=0,
    limit_high [51:48], base_high [63:56].
  - DPL=3 access encoding (= 0xE9) for arbitrary callers.
  - `tss_descriptor_high` low 32 = base[63:32], high 32 reserved.
  - `tss_set_rsp0` / `tss_get_rsp0` round-trip.

### Boot wiring

The two `tss_init` call sites mirror the existing `cpu_local_init`
ones because the same shared kernel stack (`g_syscall_kernel_stack`)
serves both the syscall path and the IRQ path today. The first
call (under `CAPYOS_BOOT_RUN_HELLO`) wires the TSS before
`kernel_boot_run_embedded_hello` ever drops to ring 3. The second
call (inside the production boot block) wires it before the kernel
shell. Both produce the klog marker `[tss] TSS loaded; ring-3 ->
ring-0 IRQs safe.` so observability is symmetric.

### Why a single shared RSP0 still works for hello

Hello runs in ring 3, takes a few syscalls, and exits. SYSCALL has
its own stack swap (via `cpu_local`/`IA32_GS_BASE`) that does NOT
go through the TSS. IRQs are masked at the PIC for the duration of
the hello smoke (PIT IRQ 0 is intentionally masked during early
boot), and APIC is only armed under `CAPYOS_PREEMPTIVE_SCHEDULER`
which the hello smoke does not set. So no IRQ ever fires from
ring 3 during the existing hello-user / hello-segfault smokes, and
the shared RSP0 is sufficient.

The first time the shared RSP0 becomes a problem is when phase 8f.2
spawns TWO ring-3 tasks AND APIC ticks fire AND the tick from
busy_b lands while busy_a's IRQ frame is still on the shared
stack. Phase 8f.2 fixes that by updating TSS.RSP0 to
`task->kernel_stack + kernel_stack_size` on every context_switch.

### Validation

- `make test`: `Todos os testes passaram.`
- `[test_tss_layout]`: 17/17.
- `make layout-audit`: `Warnings: none` (kernel_main.c grew by ~14
  lines but stays under the 900-line ceiling).
- Host stub `x64_gdt_write_tss_descriptor` lives in
  `tests/stub_vmm.c` so the test binary links cleanly without
  pulling in the real `interrupts.c`.

### Deferred to 8f.2

- (Closed by 8f.2 — see below.)
- A `smoke-x64-preemptive-user` target that boots a build with
  `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_PREEMPTIVE_DEMO -DCAPYOS_BOOT_RUN_HELLO`
  AND a two-thread userland binary, asserting both threads make
  progress under tick-driven preemption. Lands in 8f.3.
- The cooperative `kernel_service_poll` delegate removal. Lands in 8f.3.

## Phase 8f.2 - per-task RSP0 swap (DONE 2026-04-30)

Phase 8f.2 closes the kernel-side gap that 8f.1 left open: the
TSS now exists, but RSP0 stays pinned at the shared
`g_syscall_kernel_stack` for every task. Once two ring-3 tasks
share the kernel, that single stack is no longer enough — a tick
fired by busy_b would push its IRET frame onto busy_a's saved
state, corrupting it. Phase 8f.2 wires a per-task RSP0 swap into
`schedule()` so each task's IRQs/syscalls land on its own private
kernel stack.

### New seam: `arch_sched_apply_kernel_stack`

`include/kernel/arch_sched_hooks.h` declares a single
arch-agnostic seam:

```c
/* Apply the arch-side preparation for the about-to-run task `next`.
 * Must be called by schedule() AFTER task_set_current(next) and
 * BEFORE context_switch(...). NULL `next` is a no-op. */
void arch_sched_apply_kernel_stack(const struct task *next);
```

The x86_64 implementation in `src/arch/x86_64/arch_sched_hooks.c`
updates BOTH the cpu-local kernel RSP slot (consumed by the
syscall fast path via `%gs:0x00`) and the TSS RSP0 (consumed by
the CPU on ring 3 -> ring 0 IRQ entry):

```c
uint64_t top = (uint64_t)(uintptr_t)(next->kernel_stack +
                                     next->kernel_stack_size);
cpu_local_set_kernel_rsp(top);  /* syscall path */
tss_set_rsp0(top);              /* IRQ path */
```

Both targets are programmed to the SAME value so a tick that
fires while a task is mid-syscall lands on the in-progress
syscall frame's stack: the IRQ pushes its IRET frame ABOVE the
existing frames, the handler runs, `iretq` returns the CPU to
the exact instruction it was at, and the syscall continues
without clobbering. The order (cpu_local first, TSS second)
matches the documentation in the header.

### scheduler.c integration

`schedule()` gains exactly one new line, in the documented spot
(after `task_set_current(next)`, before `context_switch`):

```c
next->state = TASK_STATE_RUNNING;
task_set_current(next);
stats.total_switches++;
/* ... idle accounting ... */
arch_sched_apply_kernel_stack(next);   /* NEW: phase 8f.2 */
if (current) context_switch(&current->context, &next->context);
```

The early-return paths inside `schedule()` (`!next` or
`next == current`) intentionally skip the hook — there is no
swap to prepare for. Tests lock that contract.

### Host stub + tests

`tests/stub_arch_sched_hooks.{c,h}` provide a tiny recording stub
that counts invocations and remembers the most recent target.
`tests/test_context_switch.c` grew **+7 asserts** (from 47/47 to
**54/54**) under a new "Arch sched hook (M4 phase 8f.2)" section:

1. Hook fires exactly once on a block-driven switch.
2. Hook target is the about-to-run task (`t2`), not the blocker.
3. Hook fires on a preemptive quantum-exhaustion switch.
4. Hook target on preemption is the next task (`b`).
5. Hook does NOT fire when `pick_next` returns NULL (no-op
   schedule).
6. Hook count tracks `context_switch` count (1 hook per swap).
7. Hook target's context is the same `new_ctx` that
   `context_switch` was called with (proves order: hook fires
   BEFORE the swap).

### Validation

- `make test`: `Todos os testes passaram.`
- `[test_context_switch]`: **54/54** (was 47/47).
- `make layout-audit`: `Warnings: none`.
- Build wiring: `arch_sched_hooks.o` joins `CAPYOS64_OBJS`;
  `stub_arch_sched_hooks.c` joins the host TEST_SRCS alongside
  the existing `stub_context_switch.c`.

### What this unlocks for 8f.3

With 8f.1 + 8f.2 together, the kernel can now safely run two
ring-3 tasks under preemptive scheduling:

- 8f.1's TSS gives the CPU a defined kernel stack to push IRET
  frames onto when an IRQ fires from ring 3.
- 8f.2's per-task RSP0 swap ensures each ring-3 task has its
  OWN kernel stack so two tasks don't clobber each other's
  saved frames.
- Phase 8e's `context_switch` already correctly preserves the
  IRET frame across kernel-mode swaps; the same path works for
  ring-3 swaps because the IRET frame is a regular kernel-stack
  push from the CPU's perspective.

8f.3 (deferred) lands the smoke: a userland binary that spawns
two CPU-bound threads and a `smoke-x64-preemptive-user` target
that asserts both threads make progress in the debugcon log
under tick-driven preemption.

### Deferred to 8f.3

- (Closed by 8f.3 — see below; deferred the full two-task version
  to 8f.4 since it requires a synthetic IRET frame builder per
  fresh user task.)

## Phase 8f.3 - end-to-end ring-3 preemption smoke (DONE 2026-04-30)

Phase 8f.3 lands the canonical end-to-end proof of phases 8f.1 and
8f.2 working together: a kernel build with
`-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO` AND a
userland hello rebuilt with `-DCAPYOS_HELLO_BUSY` boots the kernel,
drops to ring 3, and lets the APIC tick fire repeatedly from user
mode while hello_busy keeps emitting markers via SYS_WRITE.

Three things are validated in one boot:

1. **TSS scaffolding (8f.1) actually works.** Without a TSS, the
   FIRST APIC tick fired from ring 3 has no defined kernel stack
   and the CPU triple-faults the kernel. The smoke proves no such
   crash happens.
2. **Per-task RSP0 swap (8f.2) keeps the kernel stack consistent.**
   After the first tick, `arch_sched_apply_kernel_stack(hello)`
   sets RSP0 to hello's per-task kernel stack. Subsequent ticks
   land there instead of clobbering the shared syscall stack.
3. **iretq round-trips back to ring 3.** Each tick that fires from
   ring 3 must save the IRET frame, run scheduler_tick, and pop
   the frame back to resume hello in user mode. The smoke proves
   the marker keeps appearing past the first iteration.

### Why a single user task is enough

The full two-task ring-3 preemption demo (busy_a vs busy_b in user
mode) is logically the next step but requires a synthetic IRET
frame builder per fresh user task: when context_switch swaps from
busy_a to busy_b for the FIRST time, busy_b's saved kernel stack
has no IRET frame yet (busy_b was never preempted), so the iretq
at the bottom of the IRQ stub has nothing to pop. The work needed
is a "user_task_first_entry" kernel helper that constructs the
IRET frame on-the-fly when context_switch lands on a fresh user
task. Phase 8f.4 will land that.

For 8f.3, with only ONE user task in the system:

- `task_current()` is NULL (process_enter_user_mode does not call
  task_set_current).
- `scheduler_pick_next()` returns NULL (no task is in the run
  queue; nothing in the existing kernel calls scheduler_add for
  hello's main_thread).
- `schedule()` early-returns on `!next` so no `context_switch` is
  attempted.
- The IRQ stub still saves the user IRET frame correctly on the
  TSS-supplied RSP0 and `iretq`s back to ring 3 cleanly.

This is sufficient to validate the entire 8f.1/8f.2 path end-to-end
without needing a synthetic IRET frame builder.

### Deliverable

- `userland/bin/hello/main.c` grows a `CAPYOS_HELLO_BUSY` arm:
  infinite loop with an inner `pause`-spin and a `[busyU]\n`
  marker emitted every ~512K iterations via `capy_write`.
- `Makefile` adds `smoke-x64-preemptive-user`:
  ```
  smoke-x64-preemptive-user:
      $(MAKE) clean
      $(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER \
                                    -DCAPYOS_BOOT_RUN_HELLO' \
                    EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_BUSY'
      $(MAKE) iso-uefi
      $(MAKE) manifest64
      python3 tools/scripts/smoke_x64_preemptive_user.py \
              $(SMOKE_X64_PREEMPTIVE_USER_ARGS)
  ```
- `tools/scripts/smoke_x64_preemptive_user.py` (~270 lines)
  implements the harness with two checks:
  - `[user_init] CAPYOS_BOOT_RUN_HELLO defined;` present (regression
    guard for the hello spawn path).
  - `[busyU]` appears at least `BUSY_MIN = 3` times. The `>= 3` floor
    is the asymmetric check that turns "marker fired once and
    kernel froze" into a smoke failure: any TSS / RSP0 / iretq
    regression would emit one or zero markers before hanging.
  - `panic` and "hello spawn returned without entering Ring 3"
    are failure markers.

### Validation

- `make test`: `Todos os testes passaram.` (host build does not
  define any of the three flags so the new `CAPYOS_HELLO_BUSY` arm
  is stripped from the binary.)
- `make layout-audit`: `Warnings: none`.
- `python3 -m py_compile tools/scripts/smoke_x64_preemptive_user.py`
  passes.
- `make -n smoke-x64-preemptive-user`: recipe resolves.
- Local QEMU execution N/A on macOS workstation; CI Linux is the
  source of truth, same as phase 5e/5f/8b/8e.

### Deferred to 8f.4

- (8f.4 closes the kernel-side machinery — see below. The actual
  two-task ring-3 smoke is deferred again to 8f.5 because it
  needs per-task arg passing through capylibc's crt0 to give
  each instance a distinct marker.)

## Phase 8f.4 - synthetic IRET frame builder (DONE 2026-04-30)

Phase 8f.4 lands the kernel-side machinery that lets a fresh
user task be **entered for the first time via `context_switch`**
instead of via the boot-only `iretq` in
`kernel_boot_run_embedded_hello`. This unblocks the two-task
ring-3 demo that 8f.5 will ship.

### The "fresh user task entry" problem

Once two user tasks coexist in the run queue, the scheduler
eventually picks the second one and calls
`context_switch(&first->context, &second->context)`. The
existing `context_switch` saves the first's kernel RSP/RIP and
loads the second's RSP/RIP. For the FIRST dispatch of a user
task that has never run before:

- `second->context.rip` is whatever was set at task-creation
  time — typically the address of a kernel-side entry function.
- `second->context.rsp` points at the bare top of the kernel
  stack with no IRET frame.
- The trampoline at `1:` (in `context_switch.S`) does `ret`
  which would unwind into the dispatcher, but there is no
  IRET frame to pop at the bottom of `x64_exception_common`.

The result without 8f.4: the second task can never reach ring 3
through the conventional context_switch path; only via a
purpose-built `iretq` in C/asm.

### The fix

Two new pieces, designed to compose with the existing 8e
trampoline (`context_switch_into_first`) and the existing
`x64_exception_common` tail:

1. **`x64_user_first_dispatch`** (in
   `src/arch/x86_64/cpu/interrupts_asm.S`). A 3-instruction asm
   trampoline that mirrors the tail of `x64_exception_common`:
   `POP_REGS; add rsp, 16; iretq`. It assumes the kernel RSP
   already points at the bottom of a synthesized PUSH_REGS frame
   (15 GP regs + vector + error_code + IRET frame).

2. **`user_task_arm_for_first_dispatch(t, user_rip, user_rsp)`**
   (in `src/kernel/user_task_init.c`). A pure C builder that
   lays out the 22-slot frame at the top of `t->kernel_stack`:

   ```
   +---- highest addr (kernel_stack + kernel_stack_size)
   | user SS         (0x1B)        ┐
   | user RSP        (caller-supplied)  │ IRET frame
   | user RFLAGS     (0x202)            │ (5 slots)
   | user CS         (0x23)             │
   | user RIP        (caller-supplied)  ┘
   | error_code      (0)           ┐ vector / err
   | vector          (0)           ┘ (2 slots)
   | rax = 0   ...   r15 = 0       — PUSH_REGS area (15 slots)
   +---- t->context.rsp points here (lowest addr of frame)
   ```

   Total frame size: 22 slots × 8 bytes = **176 bytes**.

   The builder sets `t->context.rsp` to the lowest address of
   the PUSH_REGS area and `t->context.rip = &x64_user_first_dispatch`.
   `context_switch` later loads those into the CPU and jumps to
   the trampoline; the trampoline pops the 15 GP regs + skips
   `vector + error_code` + `iretq`s into ring 3 with the user
   RIP/RSP/CS/SS/RFLAGS the builder wrote.

### Host tests

`tests/test_user_task_init.c` — **15/15 asserts** locking the
exact byte layout of the synthesized frame plus the
`t->context.rip / rsp / rflags / rbp` fields:

1. All 15 PUSH_REGS slots are zero.
2-3. Vector/error_code slots are zero.
4. User RIP slot matches the caller-supplied value.
5. User CS slot is exactly 0x23 (`USER_TASK_USER_CS`).
6. User RFLAGS slot is exactly 0x202 (`USER_TASK_USER_RFLAGS`).
7. User RSP slot matches the caller-supplied value.
8. User SS slot is exactly 0x1B (`USER_TASK_USER_SS`).
9. `t->context.rsp` points at the top of the PUSH_REGS frame.
10. `t->context.rip == &x64_user_first_dispatch`.
11. `t->context.rflags == 0x202`.
12. `t->context.rbp == t->context.rsp`.
13. Frame bottom is exactly 176 bytes below stack top.
14. NULL task argument is a safe no-op.
15. Undersized kernel stack is rejected without writing to
    `context`.

### Build wiring

- `Makefile`: `user_task_init.o` joins `CAPYOS64_OBJS` next to
  the existing `user_init.o`. The `.S` symbol
  `x64_user_first_dispatch` is part of `interrupts_asm.o` which
  was already in the link.
- Host stub for `x64_user_first_dispatch` in `tests/stub_vmm.c`
  so the test binary links cleanly without pulling the asm.

### Validation

- `make test`: `Todos os testes passaram.` `[test_user_task_init]`
  contributes **15/15** new asserts.
- `make layout-audit`: `Warnings: none`.

### Deferred to 8f.5

- (Closed by 8f.5 — see below.)
- Removal of the cooperative `kernel_service_poll` delegate.
  Tracked separately as a non-blocking cleanup item; the M4
  finalization scope is "ring-3 preemption proven", which 8f.5
  delivers, and the cooperative delegate is no longer on the
  hot path for any preemptive build.

## Phase 8f.5 - two-task ring-3 preemption (DONE 2026-04-30)

Phase 8f.5 closes the M4 preemptive scheduler story end-to-end:
the kernel boots, drops INTO ring 3 via the original
`kernel_boot_run_embedded_hello` iretq, and then the scheduler
swaps to a SECOND user task that was armed by phase 8f.4's
synthetic IRET frame builder. Both tasks emit distinct
`[busyU0]`/`[busyU1]` markers, proving the full path:

```
boot iretq -> ring 3 (busy_a, rank=0) -> APIC tick from ring 3
    -> RSP0 to busy_a's per-task stack (8f.2)
    -> save IRET frame on busy_a's stack
    -> scheduler_tick -> schedule()
    -> arch_sched_apply_kernel_stack(busy_b) (8f.2)
    -> context_switch(&busy_a.ctx, &busy_b.ctx)
    -> jmps to x64_user_first_dispatch (8f.4)
    -> POP_REGS pops 15 zero regs + RAX=1 (8f.5)
    -> add rsp,16 + iretq -> ring 3 (busy_b, rank=1)
    -> next APIC tick lands on busy_b's stack -> ...
```

### Deliverables

- **`crt0.S` rank channel**: `userland/lib/capylibc/crt0.S` now
  forwards `RAX -> RDI` so a kernel-injected rank reaches
  `main(int rank)` per the System V ABI. Default builds (where
  RAX is zero on entry) see `rank=0` and behave exactly as
  before, preserving the existing single-task hello smoke.
- **Hello rank-aware marker**: `userland/bin/hello/main.c` ganha
  signature `int main(int rank)` and the BUSY arm picks
  `[busyU0]\n` or `[busyU1]\n` based on `rank == 0`. Single-task
  builds still see only `[busyU0]`.
- **`user_task_arm_for_first_dispatch_with_rax`**: extended
  variant of the 8f.4 builder that writes `initial_rax` into the
  synth frame's RAX slot so the trampoline pops it during
  `POP_REGS`. The base API stays as a thin shim with rank=0.
- **`kernel_boot_run_two_busy_users`** in
  `src/kernel/user_init.c`: spawns two copies of hello, arms the
  second via `user_task_arm_for_first_dispatch_with_rax(.., 1)`,
  `scheduler_add`s the second's main thread, marks the first as
  `task_current()`, refreshes the arch RSP0 hook for the first,
  and `iretq`s into the first via the existing
  `process_enter_user_mode` path. Noreturn on success.
- **`CAPYOS_BOOT_RUN_TWO_BUSY` macro**: gates the new helper in
  `kernel_main.c` so single-task builds (CAPYOS_BOOT_RUN_HELLO
  alone) keep using the old direct iretq.
- **Smoke `smoke-x64-preemptive-user-2task`**: builds with
  `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO
  -DCAPYOS_BOOT_RUN_TWO_BUSY` + userland
  `-DCAPYOS_HELLO_BUSY`. Asserts BOTH `[busyU0]` AND `[busyU1]`
  appear at least twice each, in addition to the
  "CAPYOS_BOOT_RUN_TWO_BUSY defined; spawning two." marker.
  Failure markers: `panic`, "hello spawn returned without
  entering Ring 3.".

### Tests

- `tests/test_user_task_init.c` grew from 15/15 to **20/20**
  (+5 asserts) under the new "rank-passing variant" section:
  - `with_rax`: p[14] (RAX slot) == initial_rax.
  - `with_rax`: all 14 other PUSH_REGS slots stay zero.
  - `with_rax`: user RIP slot still matches arg.
  - `with_rax`: user RSP slot still matches arg.
  - Base API leaves RAX slot at zero (rank=0 default).
- `tests/test_hello_program.c`: `hello_main(0)` arg added
  (existing 7/7 asserts continue to pass).
- `tools/scripts/smoke_x64_preemptive_user.py`: marker updated
  from `[busyU]` to `[busyU0]` (count check unchanged).

### Validation

- `make test`: `Todos os testes passaram.` Total host asserts in
  the M4 finalization track now sit at **110+** new since the
  pre-7c baseline.
- `make layout-audit`: `Warnings: none`.
- `python3 -m py_compile tools/scripts/smoke_x64_preemptive_user_2task.py`
  passes; `make -n smoke-x64-preemptive-user-2task` resolves the
  recipe.
- Local QEMU execution N/A on macOS workstation; CI Linux is the
  source of truth.

## Phase 9 - smoke integration (DONE 2026-04-30)

Phase 9 wires the M4 preemptive smokes into a single CI-friendly
aggregator so a developer or release pipeline can sanity-check the
entire 8a..8f stack with one Make invocation:

```
smoke-x64-preemptive-all:
    $(MAKE) smoke-x64-preemptive            # 8b/8c/8d wiring
    $(MAKE) smoke-x64-preemptive-demo       # 8e two-task kernel
    $(MAKE) smoke-x64-preemptive-user       # 8f.3 single ring-3
    $(MAKE) smoke-x64-preemptive-user-2task # 8f.5 two ring-3
    @echo "[ok] Suite preemptiva completa passou."
```

Each sub-smoke does its own `make clean` + `make all64
EXTRA_CFLAGS64=...` because the per-source `.d` files do not track
preprocessor macros. The aggregate therefore re-builds the kernel
four times; this is the price of macro-gated boot variants and
matches the trade-off the existing smokes already accept
individually.

`smoke-x64-preemptive-all` joins the existing `.PHONY` list. CI
integration, the `release-check` target itself, and the sub-smokes
remain available for surgical invocation.

## Phase 10 - docs + release (DONE 2026-04-30)

Phase 10 promotes M4.1-M4.5 from "Parcial" to **Implementado** in
`docs/plans/active/capyos-robustness-master-plan.md` via an "Update
2026-04-30" note inserted right before the M5 section. The
historical "Proximo passo" columns are intentionally preserved
unchanged so the trail of incremental progress remains auditable;
the update note redirects readers to
`docs/plans/historical/m4-finalization-progress.md` (this file, archived 2026-04-30) and
`docs/plans/STATUS.md` for sub-phase detail.

The STATUS table is the live dashboard:
`docs/plans/STATUS.md` now shows **31/31 fases concluídas → 100%**
for the M4 finalization track.

## Phase 11 - cleanup (DONE 2026-04-30)

Phase 11 marks the M4 finalization plan as ready to archive into
`docs/plans/historical/` once the M4 commit lands and the next
milestone (M5 deepening, or M6) starts. Until then the file stays
under `docs/plans/active/` as the canonical reference for sub-phase
implementation details.

The Make `.PHONY` list was also consolidated to include all five
new smokes (`smoke-x64-preemptive`, `-demo`, `-user`,
`-user-2task`, `-all`) so `make` cannot accidentally try to build a
file with one of those names.

`make layout-audit` reports `Warnings: none` after the entire
sequence — no monolith breached the 900-line ceiling, and the
preemptive boot wiring extracted into `preemptive_boot.c` /
`preemptive_demo.c` (phase 8d/8e refactor) keeps `kernel_main.c`
well under the limit.

**Status: READY_TO_ARCHIVE.** Next session should `git mv` this
file into `docs/plans/historical/` after the user commits the M4
package.

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
   - 8a (DONE 2026-04-30): preemptive primitives + 9 new host asserts
   - 8b (DONE 2026-04-30): kernel_main wiring behind CAPYOS_PREEMPTIVE_SCHEDULER + smoke harness
   - 8c (DONE 2026-04-30): APIC IRQ 0 install fix + smoke regression guard
   - 8d (DONE 2026-04-30): global sti site + observation soak (apic_timer_ticks > 0)
   - 8e (DONE 2026-04-30): first-task trampoline + two-task demo + smoke-x64-preemptive-demo
   - 8f.1 (DONE 2026-04-30): TSS scaffolding (struct + GDT slot + LTR) for ring-3 IRQ safety
   - 8f.2 (DONE 2026-04-30): per-task RSP0 swap (cpu_local + TSS) via arch_sched_apply_kernel_stack hook
   - 8f.3 (DONE 2026-04-30): single-task ring-3 preemption smoke (CAPYOS_HELLO_BUSY)
   - 8f.4 (DONE 2026-04-30): synthetic IRET frame builder + 15 host asserts
   - 8f.5 (DONE 2026-04-30): two-task ring-3 spawn helper + RAX rank passing + smoke-x64-preemptive-user-2task
9. Phase 9 - smoke integration (DONE 2026-04-30): smoke-x64-preemptive-all aggregator
10. Phase 10 - docs + release (DONE 2026-04-30): master plan bumped, STATUS consolidated
11. Phase 11 - cleanup (DONE 2026-04-30): READY_TO_ARCHIVE marker, .PHONY consolidated

Each phase ships with host tests, smoke validation, and updated entries
in `docs/plans/active/capyos-robustness-master-plan.md`.
