# musl libc port for CapyOS

This directory hosts the **musl libc** port for CapyOS userland.
The strategy is to **vendor upstream `musl-1.2.5`** with the
smallest possible patch surface, and provide CapyOS-specific
adaptations only in `arch/x86_64/` (the architecture sysroot)
plus a few build-system shims.

## Status (sessão 19, 2026-05-06)

Currently only the **arch adapter seed** is committed:

| Component                                | State            |
|------------------------------------------|------------------|
| `arch/x86_64/syscall_arch.h`             | ✅ committed     |
| Upstream `musl-1.2.5` source vendoring   | ⏳ next session  |
| Build system (configure + make rules)    | ⏳ later         |
| sysroot install                          | ⏳ later         |
| Test suite (`libc-test`)                 | ⏳ later         |

The seed file lives at the path musl expects (`arch/x86_64/`)
so when vendoring lands the adapter slots in unchanged.

## Strategy

CapyOS provides a **Linux ABI** via the `linux_compat` modules
(see `include/kernel/linux_compat/`). The Linux ABI is
implemented using x86_64 `syscall` instruction with NRs that
match Linux 6.x. **105 NRs are registered** at the time of
this commit (see `linux_syscall_nrs.h` and `COMPAT.md`).

For a deeper rationale see also
`docs/plans/active/musl-port-strategy.md`.

musl hits the same ABI: it expects to call `syscall(NR, args)`
and get the same errno conventions Linux uses. So the bulk of
musl compiles **verbatim** against CapyOS — no source changes
required for most TUs.

The only place CapyOS-specific work is needed:

1. `arch/x86_64/syscall_arch.h` — inline-asm macros that issue
   the actual `syscall` instruction. Identical to upstream
   musl's x86_64 file *because we follow the same ABI*. The
   file is reproduced here verbatim from upstream
   (commit-hash to be filled in when vendoring lands) so we
   can validate the contract before pulling in 5+ MB of upstream
   source.

2. `arch/x86_64/atomic_arch.h` — atomics (xchg/cas) — these are
   architecture-specific but ABI-independent, so we'll
   eventually copy upstream verbatim.

3. **Build patches**: `Makefile`, `configure` may need
   tweaks to build against CapyOS's syscall surface (e.g.
   disabling syscalls we don't yet provide).

4. **Sysroot install**: `make install DESTDIR=...` to populate
   `userland/sysroot/` with libc.a, headers, etc.

## Why musl vs glibc

- **Smaller**: musl is ~600 KB compiled vs glibc's 2+ MB.
- **Cleaner code**: easier to audit and patch.
- **Linux ABI native**: musl makes raw `syscall` calls; glibc
  has more abstraction layers that complicate porting.
- **License**: MIT vs LGPL (both compatible with CapyOS but
  MIT integrates more naturally).
- **Used by Alpine, OpenWrt**: well-tested in production
  alternative-libc deployments.

## Anti-goals

- **Not** forking musl. We track upstream verbatim and
  patch only via the `arch/` adapter and minimal build hooks.
- **Not** providing a glibc-compatible interface. Programs
  that use `__GNU_SOURCE` extensions need to be patched or
  built against musl directly.
- **Not** supporting threading until S1.4 (`task_clone_thread`)
  lands. musl pthread will receive `-ENOSYS` from `clone(2)`
  and degrade gracefully (single-threaded mode).

## Roadmap

| Step | Task                                        | Sessions |
|------|---------------------------------------------|----------|
| 1    | `arch/x86_64/syscall_arch.h` adapter (seed) | 1 (this) |
| 2    | Vendor `musl-1.2.5` source + license        | 1        |
| 3    | Build system: `Makefile`/`configure` hooks  | 2-3      |
| 4    | First successful `libc.a` link              | 1-2      |
| 5    | Stub-out missing syscalls via libc shims    | 1-2      |
| 6    | sysroot install + headers                   | 1        |
| 7    | First "hello world" linked against musl     | 1-2      |
| 8    | `libc-test` smoke run                       | 2-3      |

Total: ~10-15 sessions to get a working musl build for x86_64.

## References

- Upstream musl: <https://musl.libc.org/>
- Source tarball: <https://musl.libc.org/releases/musl-1.2.5.tar.gz>
- Architecture porting guide:
  <https://wiki.musl-libc.org/architecture-porting-guide.html>
- CapyOS Linux ABI surface: `include/kernel/linux_compat/`
  and `linux_syscall_nrs.h` (105 NRs as of 2026-05-06).
- Strategy doc: `docs/plans/active/musl-port-strategy.md`.
