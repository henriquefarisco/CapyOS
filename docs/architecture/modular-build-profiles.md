# Modular build profiles (alpha.241)

**Status:** authoritative since 2026-05-19 (alpha.241).
**Audience:** anyone who builds the CapyOS kernel ELF or maintains
the cross-repo source layout with the sibling CapyUI repository.

## 1. Profiles

The CapyOS Makefile selects what goes into the kernel ELF via the
`PROFILE` variable.

| PROFILE      | Default | Includes                                                          | Purpose                                  |
|--------------|---------|-------------------------------------------------------------------|------------------------------------------|
| `full`       | yes     | core + services + desktop session + window manager + apps         | end-user installations                   |
| `core-only`  | no      | core + services + capysh only                                     | headless / lab / size budget exploration |

```bash
make all64                  # PROFILE=full (default)
make all64 PROFILE=core-only
```

When `PROFILE=core-only`:

- `DESKTOP_OBJS`, `WINDOW_OBJS`, `APPS_OBJS` are empty;
- `CAPYOS_PROFILE_CORE_ONLY` is defined for the whole build;
- `src/shell/commands/extended.c` excludes every GUI/apps command
  source via `#ifndef CAPYOS_PROFILE_CORE_ONLY` and falls back to
  a single `cmd_desktop_unavailable` handler that prints a stable
  error message;
- `kernel_module_desktop_session_available()` returns 0
  unconditionally — the activation gate fails closed because the
  symbols simply do not exist.

`core-only` is a **build-time** decision. Switching profiles requires
a full rebuild (`make clean && make all64 PROFILE=...`) because
per-source `.d` files do not track preprocessor flags.

## 2. Source ownership

Since alpha.241 the desktop session, window manager and apps source
trees live in the sibling `CapyUI` repository even though they are
linked into the CapyOS kernel ELF. Headers stay in
`CapyOS/include/` as the shared ABI contract.

| Concept                  | Source path                  | Owner       |
|--------------------------|------------------------------|-------------|
| widget primitives        | `CapyUI/src/widget/`         | CapyUI      |
| desktop session/taskbar  | `CapyUI/src/desktop/`        | CapyUI      |
| window manager + dispatch| `CapyUI/src/window/`         | CapyUI      |
| apps (calc, FM, editor, …)| `CapyUI/src/apps/`          | CapyUI      |
| desktop/apps/widget headers (shared contract) | `CapyOS/include/{gui,apps}/` | CapyOS |

The CapyOS Makefile finds CapyUI via:

```make
CAPYUI_DIR ?= $(realpath $(CURDIR)/../CapyUI)
```

Override `CAPYUI_DIR` to point elsewhere if your checkout layout
differs from the standard `parent/CapyOS` + `parent/CapyUI` shape.

Build object directories make the boundary obvious in build logs:

- `build/x86_64/capyui-desktop/*.o`
- `build/x86_64/capyui-window/*.o`
- `build/x86_64/capyui-apps/*.o`

## 3. Fallback when CapyUI is missing

If `CAPYUI_DIR` does not resolve, or it points to a checkout that
has not yet run `tools/scripts/migrate_to_capyui.py --apply`, the
Makefile falls back to the legacy in-tree paths
(`src/gui/desktop/`, `src/gui/window/`, `src/apps/`) so existing
checkouts keep building. This fallback is intentionally silent so
CI does not flap when CapyUI is bumped independently.

The fallback is meant for transition only. New work belongs in
`CapyUI/src/...` from the start.

## 4. Packaging from CapyUI

`make package` (inside CapyUI) emits two capypkg manifests:

| Module name                       | Payload contents                                       |
|-----------------------------------|--------------------------------------------------------|
| `org.capyos.ui.widget-core`       | `src/widget/` + docs + VERSION                         |
| `org.capyos.ui.desktop-session`   | `src/desktop/` + `src/window/` + `src/apps/` + docs + VERSION |

`org.capyos.ui.desktop-session` declares `depends=org.capyos.ui.widget-core`.

## 5. Activation gate

Even when `PROFILE=full` builds the desktop into the kernel ELF, the
session does NOT start automatically. `kernel_module_desktop_session_available()`
checks whether the module is staged at
`/var/capypkg/org.capyos.ui.desktop-session/installed` before any
attempt to call `desktop_runtime_start()`. Without that marker the
shell prints a stable directive:

```
Desktop module not installed. Run `capy install org.capyos.ui.desktop-session`
or rerun `capy wizard`.
```

The marker is written by `capypkg_install` after the manifest +
SHA-256 + (eventual) signature checks pass. The first-boot wizard
emits the same install via `capypkg_bootstrap_run_with_progress`.

## 6. Build outputs that survive the hygiene

What still ships in the kernel ELF when `PROFILE=full`:

- everything in `src/kernel`, `src/services`, `src/auth`, `src/fs`,
  `src/net`, `src/security`, `src/drivers`, `src/memory`;
- compositor + font + terminal primitives in `src/gui/core` and
  `src/gui/widgets`/`src/gui/terminal` (still in CapyOS because
  they are kernel-level framebuffer plumbing the wizard itself
  uses);
- embedded user binaries: `/bin/hello`, `/bin/exectarget`,
  `/bin/capysh`.

What is now sourced from CapyUI when present:

- `src/desktop/*`, `src/window/*`, `src/apps/*` from CapyUI.

## 7. Migration playbook (one-shot)

```bash
# from CapyOS root
python3 tools/scripts/migrate_to_capyui.py --dry-run
python3 tools/scripts/migrate_to_capyui.py --apply

make layout-audit
make all64 PROFILE=full   # validates the cross-repo build path
```

After validation, optionally clean the forwarding stubs left in
CapyOS:

```bash
find src/gui/desktop src/gui/window src/apps -name '*.c' \
  -exec grep -l '/* MIGRATED:' {} + | xargs rm
```

## 8. References

- [`source-layout.md`](source-layout.md)
- [`first-boot-wizard.md`](first-boot-wizard.md)
- [`capypkg-adapter.md`](capypkg-adapter.md)
- [`../reference/integration/compatibility-matrix.md`](../reference/integration/compatibility-matrix.md)
- [`../operations/manual-module-deploy-runbook.md`](../operations/manual-module-deploy-runbook.md)
- `Makefile` (`PROFILE`, `CAPYUI_DIR`, pattern rules for `capyui-*`).
