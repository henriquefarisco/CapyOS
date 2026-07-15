# External core repositories

**Status:** migration registry aligned to CapyOS `0.8.0-alpha.315+20260715`; Etapas 1-7 closed and Etapa 8 active. Current pins and ABI status are authoritative in the compatibility matrix.
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
| `CapyBrowser` | `0.6.7` | static text/graphical browser core | Etapas 6-7 delivered through versioned CapyOS adapters; JavaScript/WebAssembly remain outside the static scope |
| `CapyLang` | `0.1.12` | language parser, bytecode/IR, VM and host ABI | host-only until Etapa 15 |
| `CapyAgent` | `0.0.10` | package format, resolver, component index and Ed25519 signer | signer published and CapyOS verifier registered in `alpha.276`; production trust anchor + external KAT remain fail-closed gates |
| `CapyCodecs` | `0.0.12` | portable image/audio/video codec cores | `capy-codec-image` v2 integrated for Etapas 6-7; audio/video remain later-stage work |
| `CapyUI` | `2.24.1` | retained widgets, desktop session, window manager and apps | desktop/session worker hardening integrated; compositor/font/input plumbing stays in CapyOS |
| `CapyBenchmark` | `0.0.11` | report, replay and baseline core | host-only until Etapas 15-16 |
| `CapyAI` | `0.2.0` | governed orchestrator, reproducible model and `capy-ai-core` artifact v0 | TaskPlan/file/app/power integration in alpha.315; remaining typed capabilities fail closed; package is part of the eight-repo workspace aggregate |

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
`capypkg_verify_signature_fn`. The CapyOS binder in
`src/arch/x86_64/kernel_services_capypkg.c` registers the verifier since
`alpha.276`. Two promotion gates remain: (1) external known-answer-test
validation (RFC 8032 + FIPS 180-4); (2) pinning a production publisher public
key through the supported trust API and proving signed install end to end.
Without both, the adapter rejects `signed` repositories fail-closed. Lab
installs with `--unsigned` repositories are possible but must never be promoted
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
- CapyAgent in-tree adapter (`services/capypkg`): available; signer and verifier
  are present, while production public-key trust + external KAT remain
  fail-closed Etapa 8-9 gates.
- CapyBrowser text/static graphical core: Etapas 6-7 delivered.
- CapyCodecs image codecs: Etapas 6-7 delivered through the dedicated adapter.
- CapyUI widget/display-list/desktop model: Etapas 4, 6 and 7 delivered.
- CapyAI core/model and async integration: Etapa 7 delivered; aggregate/release
  regression in Etapa 8.
- CapyLang VM/host ABI/benchmarks: Etapa 15.
- Benchmark regression baseline: Etapa 16.

## Alpha distribution note

Early modular install/update contract work uses GitHub release tags
plus the compatibility index in `tag-release-component-index.md`. The
in-tree `capypkg` adapter is the active runtime boundary for remote
package installs from `capysh`; signatures over the canonical package
descriptor must be present before the flow becomes an official release
trust chain.
