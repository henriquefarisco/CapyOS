# External core repositories

**Status:** migration registry for decoupled core projects (updated 2026-06-11; CapyOS core `alpha.265` (Etapa 6 ativa) + CapyBrowser `0.6.0` handoff).
**Rule:** external repository progress does not count as CapyOS
roadmap progress until the matching CapyOS stage integrates it through
a versioned in-tree adapter and an external gate.
**Installation boundary:** `modular-installation-architecture.md`
defines how installable external components may enter CapyOS in future
Etapas 8-9.
**Core hygiene:** `core-migration-quarantine.md` documents the
completed in-tree hygiene pass.
**Authoritative matrix:** [`compatibility-matrix.md`](compatibility-matrix.md).

## Visible local repositories

| Repository | Current version | Intended ownership | Migration status |
|---|---|---|---|
| `CapyBrowser` | `0.6.0` | browser core, HTML-to-text, CSS, static layout/display list, download/forms/session | Text package `org.capyos.browser.text` published for Etapa 6 / Slice 6.4; URL parse/normalize/origin + HTML-to-text + image adapter + DOM + CSS cascade + layout/display-list + download/session/forms host-testable delivered; graphical runtime still gated by Etapa 7 |
| `CapyLang` | `0.1.8` | language parser, bytecode/IR, VM, host ABI mock, deterministic benchmarks | in-tree prototype fully removed; CapyLang owns its host ABI work (S1-S7 + S6.3 structs/enums delivered, host-only; +opcodes 0x64-0x66 MakeAggregate/GetField/GetTag + trap V0018) |
| `CapyAgent` | `0.0.7` | package format, resolver, component index, release manifest model, declarative install/rollback plan, **Ed25519 signer** | legacy package manager removed in-tree; CapyOS exposes the `services/capypkg` adapter as the receiving boundary; CapyAgent Ed25519 signer **published host-side in `src/signer/`** (pending external KAT + registration via `capypkg_set_signature_verifier`; verifier slot NULL until Etapa 9) |
| `CapyCodecs` | `0.0.7` | portable image/audio/video codec cores | legacy BMP/PNG/JPEG decoders fully removed in-tree; CapyCodecs owns portable decoders (`capy-codec-image` v2: per-call limits, detect/generic decode, metadata query, QOI) until an image adapter lands |
| `CapyUI` | `2.22.0` | retained widget model + **desktop session, window manager and apps** (`org.capyos.ui.desktop-session` published in `alpha.241`) | widget/display-list model active for Etapa 4 via `capy-ui-widget` v2.22 schema v7 adapter; desktop/window/apps extracted (`capy-ui-desktop-session` v1); compositor/font/input plumbing **stays in CapyOS** |
| `CapyBenchmark` | `0.0.7` | benchmark reports, replay, baseline comparison, CapyLang benchmark contracts | no coupled harness ever shipped in-tree; portable report/baseline evaluator (report/evaluation/replay serialization + baseline fixtures) initialized externally |

## Migrated snapshots

### CapyAgent

The legacy in-tree package manager sources have been **removed in-tree**.
CapyOS now exposes a small in-tree adapter under `services/capypkg` as
the Etapa 9 alpha boundary that receives remote Capy packages:

- `include/services/capypkg.h` — public contract;
- `src/services/capypkg/capypkg_state.c` — singleton state and helpers;
- `src/services/capypkg/capypkg_manifest.c` — manifest parsing and
  payload verification (SHA-256 + Ed25519 over canonical descriptor);
- `src/services/capypkg/capypkg_repo.c` — repository config and
  persistence under `/system/capypkg/repos.cfg`;
- `src/services/capypkg/capypkg_install.c` — index fetch, install,
  remove and update operations.

CapyAgent retains host-testable package/resolver/component-index logic
in its own repo and may publish signed indices that the in-tree adapter
consumes. CapyOS still owns the active release/update safety paths
(manifest parsing, downgrade checks, signature/hash validation, staging,
boot-slot activation and rollback) through the existing `update_agent`
service. The `capypkg` adapter is independent of `update_agent` and
covers application packages rather than core OS updates.

The CapyAgent **Ed25519 signer** is now **published host-side** in
`CapyAgent/src/signer/` (`0.0.7`): SHA-512 (FIPS 180-4) + Ed25519
(RFC 8032) + a canonical-descriptor manifest serializer
(`src/component_index/component_manifest.c`) + the
`capyagent_ed25519_verifier` callback whose signature matches the CapyOS
`capypkg_verify_signature_fn`. **Two gates remain** before `signed`
installs are promotable: (1) external known-answer-test validation
(RFC 8032 + FIPS 180-4) via the CapyAgent `make validate`; (2) CapyOS
registering the verifier through `capypkg_set_signature_verifier` when
Etapa 9 opens. Until both pass, the kernel binder in
`src/arch/x86_64/kernel_services.c::kernel_capypkg_bind_runtime_adapters`
intentionally leaves the verifier slot NULL and the adapter rejects any
`signed` repository install with `CAPYPKG_ERR_SIGNATURE`. Lab installs
with `--unsigned` repositories are possible but must never be promoted
to user-facing release. The canonical Ed25519 descriptor scope
(`name=N|version=V|payload_sha256=H|payload_url=U\n`) is **unchanged**.

External entry points:

- `CapyAgent/docs/compatibility.md` (authoritative contract)
- `CapyAgent/docs/capypkg-publisher-guide.md` (publisher workflow)
- `CapyAgent/docs/component-index-example.md` (high-level JSON index)
- `CapyAgent/docs/tag-release-index.md` (GitHub release tag trust model)
- `CapyAgent/Makefile` (targets `make package` and `make validate`)

The corresponding shell surface is:

- `pkg-list [--installed|--available]`
- `pkg-info <name>`
- `pkg-fetch`
- `pkg-install <name>`
- `pkg-remove <name>`
- `pkg-update [<name>]`
- `pkg-source-list`
- `pkg-source-add <name> <https-url> [--unsigned]`
- `pkg-source-remove <name>`

### CapyCodecs

The legacy in-tree GUI loader sources have been **removed in-tree**.
CapyOS does not ship image decoders by default until a stage-appropriate
adapter under `services/` or `gui/codecs/` replaces them. Portable
decoder behavior is validated in `CapyCodecs` until that CapyOS image
adapter exists.

External repo entry points (unchanged):

- `CapyCodecs/src/image/capy_image.h`
- `CapyCodecs/src/image/image.c`
- `CapyCodecs/src/image/bmp_decode.c`
- `CapyCodecs/src/image/png_decode.c`
- `CapyCodecs/src/image/jpeg_decode.c`

### CapyUI

**Expanded in `alpha.241`.** CapyUI now owns two installable modules:

- `org.capyos.ui.widget-core` — portable retained widget model, layout,
  display-list schema v7, focus traversal, text editing, animation,
  theme tokens and widget extensions (`capy-ui-widget` v2.22; rich-text
  ranges + canvas draw callback + multi-touch pinch/rotate).
- `org.capyos.ui.desktop-session` — desktop runtime, taskbar, window
  manager, dispatcher, notifications and built-in apps (calculator,
  file manager, settings, task manager, text editor)
  (`capy-ui-desktop-session` v1, depends on `widget-core`).

The CapyOS Makefile detects the `../CapyUI` sibling and compiles
`gui/desktop/`, `gui/window/` and `apps/` from there when present.
When the sibling is absent (`PROFILE=core-only` or external builds),
the in-tree fallback under `src/gui/desktop/`, `src/gui/window/` and
`src/apps/` is compiled instead. The owner of feature evolution is
the `CapyUI` repository; the in-tree fallback exists only to sustain
`make all64` without the sibling and to ease the migration path.

For Etapa 4, the Makefile also detects
`../CapyUI/src/widget/capy_display_list.h` and enables the
CapyOS-side display-list adapter under
`include/gui/capyui_display_adapter.h` and
`src/gui/widgets/capyui_display_adapter.c`. The adapter consumes the
real `capy_display_list` / `capy_dl_cmd` ABI instead of defining a
parallel schema.

CapyOS keeps in the core: compositor, fonts, rendering surface, theme
provider, input plumbing, framebuffer, kernel module gate
(`kernel/module_gate.c`) that checks
`/var/capypkg/<canonical-name>/installed` markers and gates the
desktop activation when `CAPYOS_PROFILE_CORE_ONLY` is defined or the
module is missing.

External entry points:

- `CapyUI/src/widget/capy_widget.h` and `CapyUI/src/widget/capy_widget.c`
- `CapyUI/src/widget/capy_display_list.h`
- `CapyUI/src/desktop/desktop_runtime.c` (and the rest of `desktop/`,
  `window/`, `apps/` when migrated)
- `CapyUI/Makefile` (targets `make package` and `make validate`)
- `CapyUI/docs/compatibility.md` (authoritative contract)
- `CapyUI/docs/roadmap/contracts/` (versioned ABI contracts by area)

### CapyBenchmark

No coupled benchmark harness implementation ever shipped in active
CapyOS source. Owned in external repo:

- `CapyBenchmark/src/harness/capy_benchmark.h`
- `CapyBenchmark/src/harness/capy_benchmark.c`

### CapyBrowser

No active browser implementation exists in `src/apps` to migrate.
CapyOS still owns the `browser_homepage` system setting and Settings UI
surface as future browser adapter configuration. That retained
preference is not an active CapyBrowser implementation and has no
runtime browser object to quarantine.

External entry points:

- `CapyBrowser/docs/capyos-migration.md`
- `CapyBrowser/docs/compatibility.md`
- `CapyBrowser/docs/README.md`

`CapyBrowser v0.6.0` is the explicit Etapa 6 handoff: `make package
STAGE=text` emits `org.capyos.browser.text` with an empty `depends=` line so
the text-mode core is not blocked on the image codec package. CapyOS still owns
the adapter, HTTPS/TLS transport, filesystem, sandbox, window/input/render
backend and lifecycle. The graphical package `org.capyos.browser.core` remains
Etapa 7-gated and depends on `org.capyos.codecs.image-basic`.

### CapyLang

The in-tree CapyLang prototype has been **removed in-tree**. CapyLang
owns its parser/IR/VM/host ABI/benchmark work in its own repo until
Etapa 15 introduces a versioned host-ABI adapter.

External entry points:

- `CapyLang/README.md`
- `CapyLang/docs/compatibility.md`
- `CapyLang/docs/integration.md`

## CapyOS integration gates

- Core in-tree hygiene: completed (no more `CAPYOS_ENABLE_LEGACY_MIGRATED`
  variable, no `capyos-legacy-migrated` target, no quarantined objects).
- Modular installation architecture and component selection UI:
  Etapas 8-9.
- CapyAgent in-tree adapter (`services/capypkg`): Etapa 9 alpha —
  available now, signature verifier intentionally fail-closed. CapyAgent's
  Ed25519 signer is now published host-side (`0.0.7`, `src/signer/`) but
  the verifier is not yet wired through `capypkg_set_signature_verifier`
  (pending external KAT + Etapa 9 opening).
- CapyBrowser text/static core: Etapas 6-7.
- CapyCodecs image codecs: Etapas 6-7 (will land as a new
  `gui/codecs/` adapter, not by reintroducing legacy decoders).
- CapyUI widget/display-list model: Etapas 4 and 6.
- CapyLang VM/host ABI/benchmarks: Etapa 15.
- Benchmark regression baseline: Etapa 16.

## Alpha distribution note

Early modular install/update contract work uses GitHub release tags
plus the compatibility index in `tag-release-component-index.md`. The
in-tree `capypkg` adapter is the active runtime boundary for remote
package installs from `capysh`; signatures over the canonical package
descriptor must be present before the flow becomes an official release
trust chain.
