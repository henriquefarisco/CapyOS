# CapyOS System Roadmap

This roadmap focuses on the next platform improvements after consolidating the
single supported execution track: `UEFI/GPT/x86_64`.

Status date: 2026-03-05.

## Guiding principles

1. Preserve bootability and data safety first.
2. Evolve the x64 path and remove residual legacy debt instead of preserving
   unsupported 32-bit runtime behavior.
3. Land changes with measurable acceptance criteria.
4. Keep host tests and VM smoke tests mandatory for every milestone.

## 1. CAPYFS roadmap

### P0 - Reliability and recoverability
- Add filesystem superblock versioning with compatibility flags.
- Introduce journal/WAL for metadata operations.
- Add `fsck.CAPYFS` host tool to detect and repair synthetic corruption.
- Add mount-time recovery replay after unclean shutdown.

Acceptance targets:
- power-loss simulation must recover without metadata corruption
- `fsck.CAPYFS` must repair known corruption samples

### P1 - Scalability and feature growth
- Replace fixed directory entry model with variable-length names + indexing.
- Introduce extent-based data mapping.
- Add online free-space accounting and fragmentation stats.
- Add file quotas by user/group.

### P2 - Advanced data services
- Snapshots.
- Copy-on-write for metadata trees.
- Optional compression policies.

## 2. Encryption roadmap

### P0 - Cryptographic hardening
- Keep AES-XTS for block confidentiality but add integrity protection for
  metadata.
- Add authenticated metadata blocks to detect tampering.
- Implement constant-time compare for password hash verification.
- Add key versioning in on-disk metadata.

### P1 - Key management and rotation
- Introduce key hierarchy and key slots.
- Add secure rotation without full reformat.
- Add recovery/admin slot workflow.

### P2 - Platform trust
- UEFI measured-boot integration.
- Secure Boot signing workflow for `BOOTX64.EFI` and kernel payloads.

## 3. Performance roadmap

### P0 - Core path optimization
- Profile hot paths in VFS/CAPYFS and block cache.
- Add read-ahead and delayed writeback.
- Reduce synchronous flush points in metadata-heavy code paths.

### P1 - Storage and I/O
- NVMe queue depth tuning and multi-queue support.
- Batch operations in CLI commands.
- Add filesystem benchmark commands.

### P2 - CPU and scheduling
- Introduce cooperative tasks first, then preemptive scheduler.
- Move expensive maintenance jobs to worker threads.

## 4. Security roadmap

### P0 - Baseline hardening
- Lockout policy, password minimums and configurable KDF iterations.
- Audit logging for login failures and privilege changes.
- Centralize privilege checks.

### P1 - Access control evolution
- ACL support.
- Role policies with command-level restrictions.
- Immutable/append-only flags for critical paths.

### P2 - Defensive engineering
- Security test suite for parsers and on-disk metadata.
- Build-time hardening profile for release targets.

## 5. Multiuser roadmap

### P0 - User model completion
- Complete `add-user`, `del-user`, `add-group`, `passwd` workflows.
- Ensure per-user home provisioning and ownership.
- Add password change workflow with verification/history policy.

### P1 - Session isolation
- Multiple simultaneous sessions with independent TTY contexts.
- Session expiration and idle timeout.
- Better ownership propagation for new files.

### P2 - Admin operations
- Delegated admin scopes.
- Recovery/admin bootstrap flow without weakening normal auth path.

## 6. CLI roadmap

### P0 - Usability
- Command parser improvements for quoting/escaping.
- Persistent command history per user.
- Autocomplete for commands and filesystem paths.

### P1 - Power features
- Pipelines and redirection.
- Job control.
- Script runner for boot/session automation.

### P2 - Observability
- Diagnostics commands (`trace-fs`, `trace-io`, `trace-auth`).
- Structured output mode for host automation.

## 7. Scheduler roadmap

### P0 - Foundations
- Task struct, run queue and context switch primitives.
- Timer-driven scheduler tick.
- Basic synchronization primitives.

### P1 - Concurrency model
- Worker pools for storage, crypto and CLI background jobs.
- Lock ordering policy and deadlock detection hooks.
- IRQ-safe queueing for deferred work.

### P2 - SMP readiness
- Per-CPU scheduler state.
- IPI and core startup flow.
- Optional NUMA-aware policies.

## 8. Release plan

### Milestone R1 - Hardening baseline
- CAPYFS metadata journal prototype
- auth/integrity hardening
- crash recovery checks

### Milestone R2 - x64 platform hardening
- stable storage/auth flow from provisioned disk to login
- stable keyboard/input stack on Hyper-V/UEFI
- dedicated smoke for ISO installer path

### Milestone R3 - Concurrency and advanced CLI
- scheduler + worker threads
- job control and pipelines
- storage performance uplift

### Milestone R4 - Security and trust chain
- key rotation + multiple slots
- signed boot artifacts workflow
- expanded ACL and auditing

## 9. Suggested immediate backlog

1. Remove the remaining hybrid boot dependency and complete native x64 input.
2. Add dedicated smoke coverage for `ISO -> install -> HDD boot -> login`.
3. Add CAPYFS metadata journal prototype with replay test harness.
4. Add regression tests for user DB parser/auth edge cases.
5. Add CI profile that runs x64 host tests, `make inspect-disk` and HDD boot
   smoke in VM.
