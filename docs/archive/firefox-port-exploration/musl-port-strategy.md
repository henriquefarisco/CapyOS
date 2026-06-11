# musl libc port strategy for CapyOS

> **Status (sessão 19, 2026-05-06)**: arch adapter seed
> committed; vendoring + build hooks pending.

This document captures the decisions and contingency plans for
porting **musl-1.2.5** to CapyOS as the primary userland C
library. It complements:

- `userland/lib/musl/README.md` — top-level overview
- `userland/lib/musl/COMPAT.md` — NR matrix
- `docs/plans/active/firefox-port-platform-shim.md` — S3.* tasks

## Why this is the next major step

After **S1+S2 = 28/28 = 100%** (sessão 18), every Linux ABI
contract Marco M1 needs is landed. The next gating step is a
**real C library** that programs can link against. Without
musl:

- We can write kernel-internal code only.
- Userland programs would need to bring their own `crt0` and
  hand-roll syscalls (we have `userland/lib/capylibc/`, but
  that is too small and CapyOS-specific to compile real code
  like SpiderMonkey).
- Marco M1 (`SpiderMonkey shell roda`) cannot be reached.

## High-level approach

1. **Vendor upstream verbatim**: copy `musl-1.2.5` source into
   `userland/lib/musl/` with the upstream tree shape preserved.
   We treat the source as **read-only third-party code**; any
   patches must live outside the upstream tree (in
   `userland/lib/musl/capyos-patches/` if needed).

2. **Architecture adapter only in `arch/`**: the only file we
   author is `arch/x86_64/syscall_arch.h` (the seed for this
   sessão), plus possibly `arch/x86_64/atomic_arch.h` if
   upstream's needs CapyOS-specific tuning. The rest of musl
   compiles unchanged because we provide a **byte-for-byte
   Linux 6.x x86_64 syscall ABI**.

3. **Build via upstream's `configure` + `make`**: musl's build
   system is well-tested and minimal. We invoke it from a
   CapyOS-side `Makefile` rule that sets `--prefix=/usr` and
   `--target=x86_64-linux-musl`, then installs into
   `userland/sysroot/`.

4. **Stub out missing syscalls**: any NR musl uses that we
   don't yet provide receives `-ENOSYS` from the dispatcher.
   musl handles ENOSYS reasonably for many syscalls (returns
   the same to userland); for the few it can't tolerate (e.g.
   `arch_prctl` for TLS setup), we either:
   - implement them in `linux_compat` first, or
   - patch musl with a CapyOS-specific stub in
     `capyos-patches/` (last resort).

5. **No threading until S1.4 lands**: musl pthread_create
   issues `clone(CLONE_VM|CLONE_FS|...)` which currently
   returns `-ENOSYS`. musl falls back to "single-threaded
   mode" reasonably for non-pthread workloads. SpiderMonkey
   shell startup is single-threaded; threading lights up
   later.

## Why musl vs alternatives

### musl (chosen)

- ✅ Linux ABI native: makes raw `syscall` calls.
- ✅ Small: ~600 KB compiled.
- ✅ MIT license (compatible with CapyOS).
- ✅ Well-audited: used in Alpine Linux, OpenWrt.
- ✅ Easy to port: configure script understands `--target`.

### glibc (rejected)

- ❌ Massive: 2+ MB compiled.
- ❌ LGPL: viable but more constraints.
- ❌ Many syscall layers: harder to debug.
- ❌ Build system requires real GNU make + autotools chain.

### Custom libc (rejected)

- ❌ Reinventing the wheel.
- ❌ SpiderMonkey/Firefox use libc functions we don't have.
- ❌ Maintenance burden.

### Newlib (rejected)

- ❌ Embedded-focused, missing many POSIX features Firefox needs.
- ❌ Threading model incompatible with Linux pthread.

### Bionic (rejected)

- ❌ Android-specific assumptions.
- ❌ Tight coupling to Android linker.

## Vendoring procedure (sessão 20+)

```bash
# Inside userland/lib/musl/
curl -O https://musl.libc.org/releases/musl-1.2.5.tar.gz
tar xzf musl-1.2.5.tar.gz --strip-components=1
rm musl-1.2.5.tar.gz
# arch/x86_64/syscall_arch.h is preserved (we wrote it before)
```

The vendored tree retains its `Makefile`, `configure` script,
and license file. We add an `_capyos.mk` snippet at the top
level that the CapyOS root Makefile includes to drive the
build.

## Build integration (sessão 21+)

The CapyOS root Makefile gains a target:

```
musl: $(SYSROOT)/usr/lib/libc.a

$(SYSROOT)/usr/lib/libc.a: userland/lib/musl/Makefile
	cd userland/lib/musl && \
	./configure \
	  --prefix=/usr \
	  --target=x86_64-linux-musl \
	  --disable-shared \
	  CC=$(CC64) \
	  CFLAGS="-DCAPYOS_LINUX_ABI" && \
	$(MAKE) install DESTDIR=$(SYSROOT)
```

The `-DCAPYOS_LINUX_ABI` define lets us conditionally tweak
musl source if absolutely necessary (last-resort patches).

## Testing strategy

1. **Compile-only**: assert that `syscall_arch.h` compiles
   cleanly under `-Wall -Werror`. (Done in this sessão's
   validation script.)
2. **`libc-test` smoke**: musl ships a small test suite at
   `libc-test/`. We run it in QEMU/VMware as a CapyOS
   userland program once the build lands.
3. **Hello-world**: smallest C program (`int main() { puts(...);
   return 0; }`) linked statically against musl. Exercises
   `_start` → `main` → `printf` → `write(stdout, ...)` → CapyOS
   `linux_vfs` write handler.
4. **SpiderMonkey shell**: the Marco M1 milestone. Builds
   SpiderMonkey against musl + CapyOS sysroot, runs the JS
   shell `js -e 'print(1+1)'`.

## Risks and mitigations

| Risk                                        | Mitigation                                         |
|---------------------------------------------|---------------------------------------------------|
| musl makes a syscall we haven't wired       | Add the NR's handler; usually trivial             |
| `arch_prctl` (TLS) blocks startup           | Wire it early in S3 (small NR)                    |
| musl's `crt1.o` calls into kernel structs   | musl is libc-only; crt1 just calls main()          |
| Threading: pthread_create gets -ENOSYS      | musl degrades to single-threaded; acceptable for M1|
| Vendoring 5+ MB of code in one commit       | Separate commit per major bump; clear PR title    |
| musl version drift                          | Pin to 1.2.5; document upgrade procedure          |

## Out of scope (later sessions)

- glibc fallback or compatibility layer
- Full pthread support (waits on S1.4 implementation)
- Locale support (musl ships `C` locale only by default)
- IPv6 networking (waits on real BSD sockets in S1.14)
- Dynamic linking (we use static linking only for now)

## Decision log

| Date       | Decision                                  | Rationale                          |
|------------|-------------------------------------------|------------------------------------|
| 2026-05-06 | Pick musl over glibc/newlib/bionic        | Linux ABI native, small, MIT       |
| 2026-05-06 | Pin to musl-1.2.5                         | Latest stable                      |
| 2026-05-06 | Vendor verbatim, no fork                  | Minimize maintenance               |
| 2026-05-06 | arch adapter is the only authored file    | Clear separation upstream/CapyOS   |
| 2026-05-06 | Static linking only for Marco M1          | No dynamic linker yet              |
