# Core migration quarantine

**Status:** resolved. The quarantine flag `CAPYOS_ENABLE_LEGACY_MIGRATED`
and the `capyos-legacy-migrated` Make target have been retired together
with their guarded objects and tombstone headers.
**Current roadmap gate:** Etapa 9 — the in-tree adapter
`services/capypkg` is the receiving boundary for remote Capy packages.

## What changed

The CapyOS core no longer ships in-tree legacy code that was migrated
to external repositories. The following files were removed entirely
from the tree as part of the Etapa 9 total-hygiene pass:

| Removed source | Removed public header | External owner |
|---|---|---|
| `src/services/package_manager.c` | `include/services/package_manager.h`, `include/legacy/services/package_manager.h` | `CapyAgent` |
| `src/gui/core/bmp_loader.c` | `include/gui/bmp_loader.h`, `include/legacy/gui/bmp_loader.h` | `CapyCodecs` |
| `src/gui/core/png_loader.c` | `include/gui/png_loader.h`, `include/legacy/gui/png_loader.h` | `CapyCodecs` |
| `src/gui/core/jpeg_loader.c` | `include/gui/jpeg_loader.h`, `include/legacy/gui/jpeg_loader.h` | `CapyCodecs` |
| `src/lang/capylang.c` | `include/lang/capylang.h`, `include/legacy/lang/capylang.h` | `CapyLang` |
| `src/installer/installer_main.c` | — | retired 32-bit installer (UEFI-only now) |

Together with these files, the Makefile lost the
`CAPYOS_ENABLE_LEGACY_MIGRATED` variable, the
`CAPYOS_LEGACY_MIGRATED_OBJS` list and the `capyos-legacy-migrated`
phony target.

## What replaced them

The CapyOS core now exposes a small, versioned in-tree adapter for the
package boundary so the system can boot to a clean shell and still
receive remote Capy packages installed/updated via `capysh` even when
no graphical UI is mounted.

| New surface | Owner |
|---|---|
| `include/services/capypkg.h` | CapyOS base — public adapter contract |
| `src/services/capypkg/` (state, manifest, repo, install) | CapyOS base — runtime |
| `SYSTEM_SERVICE_CAPYPKG` in `services/service_manager` | CapyOS base — supervisor entry |
| `pkg-list`, `pkg-info`, `pkg-fetch`, `pkg-install`, `pkg-remove`, `pkg-update`, `pkg-source-list`, `pkg-source-add`, `pkg-source-remove` | CapyOS base — shell commands |

The adapter never executes installed bytes from this path. Installations
only stage verified content into the encrypted VFS under
`/var/capypkg/<name>/` and record metadata in
`/system/capypkg/db.idx`. Activation of binaries is deferred to a
later stage with a sandboxed loader.

## Active CapyOS boundaries (unchanged)

These objects remain linked in the default kernel image because they
own active base-system safety paths:

| CapyOS object | Reason |
|---|---|
| `services/update_agent.o` | active update service state, VFS/network fetch boundary and status singleton |
| `services/update_agent_parse.o` | active manifest parsing, downgrade checks and signature/hash validation |
| `services/update_agent_apply.o` | active catalog/staging state machine used by shell and service manager |
| `services/update_agent_prepare.o` | active fetch/download/stage preparation path used by update shell commands |
| `services/update_agent_transact.o` | active boot-slot apply/confirm/rollback integration |
| `gui/widgets/widget.o`, `gui/widgets/context_menu.o`, `gui/widgets/inline_prompt.o` | active desktop/app callers; CapyUI owns portable widget model but CapyOS still owns compositor/window/font/input plumbing |

## Retained CapyOS-owned surfaces

| Surface | Owner | Reason |
|---|---|---|
| `browser_homepage` in `struct system_settings` and `/system/config.ini` | CapyOS base | retained Settings/system preference for future browser adapters |
| default HTTP `User-Agent` in `src/net/services/http/url_request_builder.c` | CapyOS base | active network client identity; future browser adapters set their own header explicitly |

## Rules going forward

- Do not reintroduce legacy package manager, image loader or CapyLang
  prototype sources in-tree. New work targets the external owners and
  their documented ABIs.
- Any new adapter must be small, versioned, fail-closed by default and
  placed in the owning CapyOS domain (for example `services/capypkg`
  for package operations).
- Network access from adapters goes through `net/http.h` (HTTPS via
  BearSSL) only. Payload digests must be verified with SHA-256, and
  signatures must use the canonical descriptor binding name, version,
  payload URL and payload SHA-256.
- If no repositories are configured, the adapter returns
  `CAPYPKG_ERR_NO_SOURCE` and the shell falls back cleanly.

## External validation recommendation

Because the Makefile object list and tree shape changed, external
validation should run:

- `make layout-audit`
- `make all64`
- `make test`
- `make release-check` before any promotion

The `capyos-legacy-migrated` fallback target no longer exists; do not
reintroduce it.
