# NoirOS System Roadmap

This roadmap focuses on the next improvements requested for the platform:
Noir File System (NFS/NoirFS), end-to-end encryption, performance, security,
multiuser model, CLI evolution and multithreading.

Status date: 2026-02-10.

## Guiding principles

1. Preserve bootability and data safety first.
2. Evolve 64-bit path without regressing the working 32-bit path.
3. Land changes with measurable acceptance criteria.
4. Keep host tests and VM smoke tests mandatory for every milestone.

## 1. NFS (Noir File System) roadmap

### P0 - Reliability and recoverability
- Add filesystem superblock versioning with compatibility flags.
- Introduce journal/WAL for metadata operations (create, rename, unlink, chmod).
- Add `fsck.noirfs` host tool to detect and repair orphaned inodes and bitmap
  mismatch.
- Add mount-time recovery replay after unclean shutdown.

Acceptance targets:
- Power-loss simulation must recover without metadata corruption.
- `fsck.noirfs` must repair known synthetic corruption cases.

### P1 - Scalability and feature growth
- Replace fixed directory entry model with variable-length names + index support.
- Introduce extent-based data mapping (reduce fragmentation and metadata overhead).
- Add online free-space accounting and fragmentation stats.
- Add file quotas by user/group.

Acceptance targets:
- Create/list/read/write workloads with >100k files in test image.
- Large file throughput must improve compared to direct+indirect baseline.

### P2 - Advanced data services
- Snapshots (read-only first, then writable clones).
- Copy-on-write for metadata trees.
- Optional compression policies per directory tree.

## 2. End-to-end encryption roadmap

### P0 - Cryptographic hardening
- Keep AES-XTS for block confidentiality but add integrity path for metadata.
- Add authenticated metadata blocks (MAC/tag) to detect tampering.
- Implement constant-time compare for password hash verification.
- Add key version field in on-disk metadata.

### P1 - Key management and rotation
- Introduce key hierarchy:
  - disk master key
  - filesystem key-encryption keys
  - per-user wrapped credentials
- Add secure key rotation without full reformat.
- Add support for multiple key slots (admin recovery scenario).

### P2 - Platform trust
- UEFI measured-boot integration (PCR-style measurements roadmap).
- Secure Boot signing workflow for `BOOTX64.EFI` and kernel payloads.

## 3. Performance roadmap

### P0 - Core path optimization
- Profile hot paths in VFS/NoirFS and block cache (read/write/open/lookup).
- Add read-ahead and delayed writeback policy in buffer cache.
- Reduce synchronous flush points in metadata-heavy code paths.

### P1 - Storage and I/O
- NVMe queue depth tuning and multi-queue support.
- Batch operations in CLI commands (`clone`, `find`, `hunt-*`).
- Add filesystem benchmark command set for repeatable measurement.

### P2 - CPU and scheduling
- Introduce cooperative tasks first, then preemptive scheduler.
- Move expensive maintenance jobs (flush, reclaim, journal replay) to worker
  threads.

## 4. Security roadmap

### P0 - Baseline hardening
- Strengthen defaults: lockout policy, password minimum policy,
  configurable KDF iterations.
- Add audit logging for login failures, privilege changes and file deletions.
- Normalize privilege checks around one central policy layer.

### P1 - Access control evolution
- Expand from basic mode bits to ACL support.
- Add role policies (admin/operator/user) with command-level restrictions.
- Add immutable/append-only file flags for critical system paths.

### P2 - Defensive engineering
- Security test suite (parser fuzzing, malformed on-disk metadata, CLI parser
  fuzz inputs).
- Build-time hardening profile for release targets.

## 5. Multiuser roadmap

### P0 - User model completion
- Implement user/group management commands (`add-user`, `del-user`,
  `add-group`, `passwd`, etc.).
- Ensure per-user home provisioning and ownership on creation.
- Add password change workflow with verification and history policy.

### P1 - Session isolation
- Multiple simultaneous sessions with independent TTY contexts.
- Session expiration and idle timeout.
- Better ownership propagation for files created via shell commands.

### P2 - Admin operations
- Delegated admin policies (limited admin scopes).
- Recovery/admin bootstrap flow without weakening normal auth path.

## 6. CLI roadmap

### P0 - Usability
- Command parser improvements for quoting edge cases and escape handling.
- Persistent command history (per user).
- Autocomplete for commands and filesystem paths.

### P1 - Power features
- Pipelines and redirection (`|`, `>`, `>>`, `<`).
- Job control (`run-bg`, `run-fg`, `print-jobs`, `kill-ps`).
- Native script runner for boot/session automation.

### P2 - Observability
- Diagnostics commands (`trace-fs`, `trace-io`, `trace-auth`).
- Structured output mode for host automation.

## 7. Multithreading and scheduler roadmap

### P0 - Foundations
- Task struct, run queue and context switch primitives.
- Kernel timer-driven scheduler tick.
- Basic synchronization primitives (spinlock, mutex, semaphore).

### P1 - Concurrency model
- Separate worker pools for storage, crypto and CLI background jobs.
- Lock ordering policy and deadlock detection hooks.
- IRQ-safe queueing for deferred work.

### P2 - SMP readiness
- Per-CPU scheduler state.
- Inter-processor interrupts and core startup flow.
- NUMA-aware policies (optional phase).

## 8. Release plan (proposed)

### Milestone R1 - Hardening baseline
- NFS journal/WAL (metadata)
- Auth/integrity hardening
- Improved tests + crash recovery checks

### Milestone R2 - 64-bit functional parity
- NoirFS mount + auth flow in x86_64 runtime
- Stable keyboard/input stack on Hyper-V/UEFI
- CLI parity for core command groups

### Milestone R3 - Concurrency and advanced CLI
- Scheduler + worker threads
- job control and pipelines
- storage performance uplift

### Milestone R4 - Security and trust chain
- key rotation + multiple slots
- signed boot artifacts workflow
- expanded ACL and auditing

## 9. Suggested immediate backlog (next sprint)

1. Unify one shared auth+filesystem service layer usable by both kernels.
2. Finish USB/HID keyboard path in x86_64 and revalidate Hyper-V input fallback.
3. Add NoirFS metadata journal prototype with replay test harness.
4. Add regression tests for user DB parser/auth edge cases.
5. Add CI profile that runs BIOS + UEFI smoke boots in VM.
