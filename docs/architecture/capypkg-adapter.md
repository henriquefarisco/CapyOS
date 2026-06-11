# capypkg adapter — design and security rationale

**Domain owner:** CapyOS base (`services/capypkg`).
**External upstream:** `CapyAgent` (package format, resolver, Ed25519 signer).
**Roadmap gate:** Etapa 9 alpha — receiving boundary for remote Capy
packages; the kernel never executes installed bytes from this path.

## Why it exists

The CapyOS core must:

1. Stay bootable to a clean shell even when no external services are
   integrated.
2. Receive and verify remote Capy packages installed/updated through
   `capysh`, independently of any graphical UI.
3. Keep the package format/resolver work decoupled in `CapyAgent`
   without leaking that logic into the kernel.

The adapter provides a small, versioned, fail-closed boundary that
covers (1), (2) and (3). It deliberately does **not** parse the full
`.capypkg` container, run resolver heuristics or apply rollouts —
those live in `CapyAgent`.

## File layout

```
include/services/capypkg.h                 public contract
src/services/capypkg/
  internal/capypkg_internal.h              shared between the 5 TUs
  capypkg_state.c                          singleton, init/reset, accessors
  capypkg_manifest.c                       descriptor parsing + SHA-256
  capypkg_repo.c                           repository config + persistence
  capypkg_install.c                        index fetch, install, remove, update
  capypkg_persist.c                        installed-DB + cached-catalog I/O
src/arch/x86_64/kernel_services_capypkg.c  VFS+HTTPS adapter binding +
                                           service poll/start/stop hooks
src/shell/commands/system_control/
  capypkg_commands.c                       9 pkg-* CLI commands
```

Each TU stays well under the 900-line layout cap; the install TU is
the largest. The `internal/capypkg_internal.h` header is strictly
private; callers must use `include/services/capypkg.h`.

## Lifecycle

```
boot -> service manager seeds SYSTEM_SERVICE_CAPYPKG (STOPPED)
shell runtime ready
   -> kernel_service_poll_capypkg
      -> kernel_capypkg_bind_runtime_adapters (idempotent)
         -> capypkg_set_reader/writer/bytes_writer/remover/mkdir
            (VFS through vfs_open/read/write + kernel helpers)
         -> capypkg_set_text_fetcher/bytes_fetcher
            (HTTPS through net/http http_get/http_download)
         -> capypkg_set_bytes_fetcher_progress
            (HTTPS with byte-level progress through http_download_progress)
         -> capypkg_init
            -> seed default repo "stable"
               (capypkg_repo_load best-effort)
            -> capypkg_db_load best-effort
            -> capypkg_catalog_restore best-effort
      -> kernel_update_capypkg_service_status -> READY / DEGRADED
```

`capypkg_set_signature_verifier` is intentionally left NULL by the
kernel binder. Until `CapyAgent`'s Ed25519 verifier is plugged in by
external integration, any install against a `require_signature` repo
fails closed with `CAPYPKG_ERR_SIGNATURE`. The default seeded repo
`stable` has `require_signature = 1`.

## Security boundaries

| Boundary | Where it is enforced |
|---|---|
| HTTPS-only transport | `capypkg_manifest_parse_entry` rejects payload URLs not starting with `https://`; `capypkg_repo_add` rejects non-HTTPS index URLs |
| SHA-256 payload binding | `capypkg_verify_payload` recomputes the digest on the downloaded bytes and compares constant-time enough for non-secret data |
| Signature scope | `verify_signature_if_required` constructs the canonical descriptor `name=N\|version=V\|payload_sha256=H\|payload_url=U\n` and asks the injected verifier to check the hex signature against it. This binding prevents repo-side package-swap attacks because the signature simultaneously commits to identity and bytes |
| Fail-closed signature | If a repo declares `require_signature = 1` and no verifier is plugged in, install refuses with `CAPYPKG_ERR_SIGNATURE` rather than skipping |
| Filesystem scope | `validate_required_fields` accepts `install_root` only when it is exactly `/var/capypkg`, lives strictly under `/var/capypkg/` or under `/opt/`. Bare `starts_with` is not enough: a directory boundary (`'\0'` or `'/'` immediately after the prefix) is required so `/var/capypkgsneak` is rejected. Any `..` path segment in `install_root` is also rejected to block traversal. Defaults to `/var/capypkg/<name>` |
| Name alphabet | `validate_required_fields` restricts `name` to `[a-zA-Z0-9._-]` and rejects dot-only names (`.`, `..`, `...`, …). The `name` is concatenated into the install path and on-disk filename, so the alphabet check forbids path-traversal in the on-disk write |
| Manifest integer overflow | `parse_uint32` rejects digit sequences that would overflow `uint32_t`. Without this, a manifest with `payload_size > UINT32_MAX` could silently wrap to a small value and bypass the `CAPYPKG_PAYLOAD_MAX` quota |
| No code execution | The adapter writes verified bytes through `capypkg_set_bytes_writer` to the VFS and updates metadata. It never loads, maps or executes the payload. Future stages will introduce a sandboxed loader for `/var/capypkg/<name>` contents |
| Quotas | `CAPYPKG_PAYLOAD_MAX` (8 MiB), `CAPYPKG_MAX_INSTALLED` (64), `CAPYPKG_MAX_AVAILABLE` (128), `CAPYPKG_MAX_REPOS` (4), `CAPYPKG_MAX_DEPS` (8) |
| Audit trail | Every package and repository mutation emits a single `[audit] [capypkg] …` entry through the kernel klog ring, alongside the `update_agent` audit convention. Install path: success (`payload-sha256 verified; package installed`), 8 failure variants (`dependency missing or cycle`, `dependency install failed`, `payload fetch failed`, `payload-sha256 mismatch`, `signature verification failed`, `payload write failed`, `installed-table quota exhausted`, `package installed but db persistence failed`). Remove path: success, `payload removal failed; db entry still dropped`, `package removed but db persistence failed`. Repository path: `repository added`, `repository updated`, `repository removed`, and a `but db persistence failed` variant for each. INFO is emitted only when the in-memory mutation also persisted to the db file; WARN is emitted on every failure branch so a forensic reader can reconstruct the exact failure point |
| Cached signed-repo flag | `g_capypkg.any_repo_signed` is recomputed from the full repo table on every `capypkg_repo_add` (update path) and `capypkg_repo_remove`, so the flag never goes stale on signed → unsigned transitions. `capypkg_stats_get` and `signature_required` (fallback path for entries without `source_repo`) read it directly |
| Terminal escape injection | `apply_kv` in `capypkg_manifest.c` and `capypkg_repo_parse` + `capypkg_repo_add` in `capypkg_repo.c` reject any byte outside printable ASCII (0x20-0x7E). The threat is that `shell_print` → `vga_write` mirrors every byte both to the framebuffer (safe — only `\n\b\r` are interpreted) and to COM1 via `outb(0x3F8, ...)`. A terminal emulator attached to that serial port would interpret ANSI escapes, letting a hostile manifest or tampered repos.cfg clear the screen, move the cursor or forge a fake shell prompt above attacker-controlled output. Refusing at parse / API time means hostile bytes never enter the in-memory catalog or the persisted db |

## Manifest format

Line-oriented `key=value`. Multiple package descriptors in one index
are separated by `---` on its own line. Comments (`#`) and blank lines
are tolerated. Unknown keys are tolerated forward-compat so
`CapyAgent` can ship new fields without breaking the alpha runtime.

Required: `name`, `version`, `payload_url`, `payload_sha256`.
Optional: `summary`, `payload_size`, `signature_ed25519`,
`install_root`, `depends`, `repo`.

Field rules enforced at parse time (see
`capypkg_manifest.c::validate_required_fields`):

- `name` must be 1-63 chars from `[a-zA-Z0-9._-]` and is rejected if
  it consists exclusively of dots (`.`, `..`, `...`, …). This
  forbids slash, `@`, whitespace and other characters that could
  escape the on-disk install path `<install_root>/<name>.bin`.
- `version` is treated as opaque text.
- `payload_url` must start with `https://`.
- `payload_sha256` must be exactly 64 hex digits (no shorter, no
  trailing junk).
- `signature_ed25519`, when present, must be exactly 128 hex digits.
  It is required only when the source repository is signed; absence
  on a signed repo causes the install to refuse with
  `CAPYPKG_ERR_SIGNATURE`. The signature is checked against the
  canonical descriptor `name=<N>|version=<V>|payload_sha256=<H>|payload_url=<U>\n`
  (literal `|` separators, trailing newline). CapyAgent must produce
  signatures over exactly this byte string.
- `payload_size`, when present, must parse as a decimal `uint32_t`
  (overflow is rejected) and must be ≤ `CAPYPKG_PAYLOAD_MAX` (8 MiB).
  When set, the downloaded payload length must match exactly.
- `install_root` must be absolute, must be either exactly
  `/var/capypkg`, strictly under `/var/capypkg/`, or under `/opt/`
  (directory boundary enforced), and must not contain any `..` path
  segments. Defaults to `/var/capypkg/<name>` when absent.
- `depends` is a comma-separated list; each entry follows the same
  alphabet rule as `name`. Up to `CAPYPKG_MAX_DEPS` (8) entries.
- `repo` is set by the adapter from the source repository at fetch
  time; manifests should leave it blank.

Unknown keys are tolerated forward-compat so `CapyAgent` can ship
new fields without breaking the alpha runtime.

The same format is reused for the persisted cache
(`/system/capypkg/cache/index.txt`) and the installed DB
(`/system/capypkg/db.idx`), so a single parser implementation covers
all three persistent surfaces.

## Persisted state

| Path | Owner | Content |
|---|---|---|
| `/system/capypkg/repos.cfg` | `capypkg_repo_save` | `name\|index_url\|require_signature\|pinned` per line |
| `/system/capypkg/db.idx` | `capypkg_db_save` | descriptors of installed packages |
| `/system/capypkg/cache/index.txt` | `capypkg_catalog_persist` | last-fetched available catalog |
| `/var/capypkg/<name>/<name>.bin` | `capypkg_install` | verified payload bytes |

All four are written through the bound VFS adapters. Writers fail
soft (the install/save operation reports `CAPYPKG_ERR_STORAGE` but
the in-memory state remains coherent).

## Pluggable seams

The adapter is fully driven by injected function pointers, so the
host test build never touches real VFS or HTTPS. Production wiring is
in `kernel_services_capypkg.c::kernel_capypkg_bind_runtime_adapters`;
the unit tests in `tests/services/test_capypkg.c` plug deterministic
fakes for both transport and storage.

Two optional, additive progress seams surface install progress without
changing the fail-closed contract. Both default to NULL, in which case
the install path behaves exactly as before:

- `capypkg_set_bytes_fetcher_progress` — a progress-aware payload
  fetcher `(url, buf, cap, out_len, cb, cb_ctx)`. When bound,
  `capypkg_install` prefers it over the plain `bytes_fetcher` and hands
  it a callback that receives `(received, total)` byte counts as the
  payload streams in. Production binds it to net/http's
  `http_download_progress`; host tests can leave it NULL and the plain
  fetcher is used unchanged (so the existing fail-closed coverage is
  untouched).
- `capypkg_set_install_observer` — a per-package phase observer
  `(name, phase, cur, total, ctx)`. `capypkg_install` reports
  RESOLVE → DOWNLOAD (cur/total bytes) → VERIFY → STAGE → DONE so a UI
  (the first-boot module wizard) can drive a live status bar. The
  observer is informational only and never gates the install.

Both seams are cleared by `capypkg_reset`, so each host test starts
from a clean slate. The first-boot module sweep that consumes them
(dependency-ordered install waves + per-package retry) is documented
in [`first-boot-wizard.md`](first-boot-wizard.md) §2.1.

## Service supervision

`SYSTEM_SERVICE_CAPYPKG` is supervised by the standard
`service_manager`:

- depends on `SYSTEM_SERVICE_LOGGER`;
- poll interval 3600 s (one hour), restart limit 3;
- declared `STARTUP_MANUAL` so the adapter does not consume cycles
  until something actively uses it;
- member of `SYSTEM_SERVICE_TARGET_FULL` only — core/network/maintenance
  targets do not bring it up. This keeps the maintenance recovery
  shell free of package-manager state.

## CLI surface (`capysh`)

| Command | Purpose |
|---|---|
| `pkg-list [--installed\|--available]` | list installed or catalog entries |
| `pkg-info <name>` | metadata dump |
| `pkg-fetch` | re-download index from all configured repos |
| `pkg-install <name>` | install with SHA-256 + signature verification |
| `pkg-remove <name>` | remove installed package and cached payload |
| `pkg-update [<name>]` | update one package or every installed package |
| `pkg-source-list` | list configured repositories |
| `pkg-source-add <name> <https-url> [--unsigned]` | add a repository (defaults to require_signature=1) |
| `pkg-source-remove <name>` | remove a non-pinned repository |

All commands are tri-lingual (PT/EN/ES) and respect the `--help`
contract used by the rest of `capysh`.

## Test discipline

`tests/services/test_capypkg.c` is the host-side baseline (the
bootstrap-sweep cases compile under `CAPYPKG_BOOTSTRAP_TESTS`).
Coverage includes:

- init idempotency and default repo seeding;
- HTTPS-only enforcement on repo URLs and payload URLs;
- pinned repo protection;
- index fetch population from a fake HTTPS source;
- successful install lifecycle and `CAPYPKG_ERR_ALREADY` on duplicate;
- `CAPYPKG_ERR_DIGEST` on SHA-256 mismatch;
- `CAPYPKG_ERR_SIGNATURE` fail-closed when no verifier is plugged in
  for a `require_signature` repo;
- `CAPYPKG_ERR_SIGNATURE` when the injected verifier rejects;
- `CAPYPKG_ERR_NOT_READY` before init;
- repo serialisation round-trip through the fake VFS;
- `install_root` scope enforcement (only `/var/capypkg` or `/opt/`);
- non-hex `payload_sha256` rejected at parse time;
- dependency chain auto-installed when the dependent is requested;
- `CAPYPKG_ERR_DEPENDENCY` when a declared dep is absent from the
  catalog;
- `capypkg_stats_get` fields after fresh init;
- well-formed `capypkg_state_label` / `capypkg_result_label` strings;
- successful install emits the verified-installed klog audit entry;
- sha256 mismatch emits the mismatch klog audit entry;
- `any_repo_signed` drops back to 0 when the last signed repository
  is updated to unsigned (cached-flag regression);
- `name` field rejects `/`, bare `..`, all-dot names and characters
  outside `[a-zA-Z0-9._-]`;
- `install_root` rejects prefix-bypass (`/var/capypkgsneak`) and
  accepts the lookalike `..hidden` (boundary semantics pinned);
- `install_root` rejects `/../` traversal segments;
- `depends` rejects entries that violate the `name` alphabet at parse
  time (so unsafe deps never reach the install-time lookup);
- `payload_size` rejects values that overflow `uint32_t` so the
  quota check cannot be bypassed by wraparound;
- the parser skips past a malformed descriptor (advances to the next
  `---\n`) so a leading bad entry never silently halts import of the
  subsequent valid ones — DoS-resistance for remote indexes;
- every manifest value is checked to contain only printable ASCII
  (0x20-0x7E) — an ANSI escape inside `summary`, `version`,
  `payload_url` etc. is refused at parse time so it cannot reach
  the serial port via `vga_write`;
- `capypkg_repo_add` refuses control bytes in `name` or `index_url`
  so a piped/scripted caller cannot inject escapes into repos.cfg
  via `pkg-source-add`;
- the install observer reports RESOLVE → DOWNLOAD → VERIFY → STAGE →
  DONE with the final DOWNLOAD sample equal to the payload size, and a
  bound progress-aware fetcher forwards intermediate byte samples
  (`test_install_observer_reports_phases`,
  `test_install_progress_fetcher_forwards_bytes`);
- the first-boot bootstrap sweep performs per-package retry on
  transient errors — it recovers a package that fails twice then
  succeeds, and caps the attempts at three for a permanent failure
  (`test_bootstrap_per_package_retry_recovers`,
  `…_caps_attempts`);
- the dependency-ordered wave planner installs a dependency before its
  dependent even when the catalog lists them in the opposite order,
  pulls a transitive dependency into a CUSTOM selection, and handles an
  unsatisfiable dependency cycle fail-closed without hanging
  (`test_bootstrap_installs_dependency_before_dependent`,
  `…_custom_pulls_transitive_dependency`,
  `…_dependency_cycle_does_not_hang`);
- the kernel trust-anchor bundle's key-type distribution is locked
  (106 RSA + 40 EC), matching the userland trust metadata claim
  (`tests/security/test_tls_trust_anchors.c`).

Adding regression tests for new behaviour is required before changing
manifest parsing, install path or signature semantics.

For fast iteration during adapter development, use the focused target:

```bash
make test-capypkg
```

It links only the 4 capypkg TUs plus `src/security/sha256.c` against
`tests/services/test_capypkg.c` and produces `build/tests/capypkg_tests`.
The full host aggregate (`make test`) still picks up the same source
list through `TEST_SRCS`.

## Future work

- **Ed25519 verifier integration.** `CapyAgent` should publish a
  small adapter that calls `capypkg_set_signature_verifier` once at
  boot, before the first `pkg-install`. The canonical descriptor is
  intentionally simple to make this a 30-line adapter.
- **Streaming download.** The alpha runtime fetches up to 1 MiB into
  a static buffer; a streaming writer that pipes through SHA-256
  incrementally will let the adapter accept up to
  `CAPYPKG_PAYLOAD_MAX` without growing the stack/static footprint.
- **Sandboxed loader.** Activation of installed binaries from
  `/var/capypkg/<name>` is deferred to a future stage with explicit
  sandbox/quota and signature-pinned execution policy.
- **Repo priorities and per-package source pinning.** The alpha
  implementation walks repos in declaration order and accepts the
  first valid descriptor.
