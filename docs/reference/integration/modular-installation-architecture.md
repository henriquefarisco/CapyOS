# Modular installation architecture

**Status:** structural contract for future installer/package integration.
**Current roadmap gate:** documentation and external-repo hygiene only while Etapa 3 is active.
**Runtime integration gate:** Etapa 8 for installer/update UX and Etapa 9 for package manager, SDK and stable ABI.

## Goal

CapyOS must remain bootable and usable as a clean base system before any optional component is installed. External projects may evolve independently, but they must enter CapyOS only through versioned contracts, compatibility checks, staging and rollback.

This document is the installation boundary for:

- `CapyAgent` package/component planning;
- `CapyBrowser` browser core;
- `CapyCodecs` media codecs;
- `CapyUI` portable widget/display-list model;
- `CapyLang` language runtime;
- `CapyBenchmark` benchmark harness.

## Non-goals for the active stage

While Etapa 3 is active, this contract does not authorize:

- importing external repo code into the CapyOS runtime;
- replacing in-tree GUI loaders or package manager code;
- adding network downloads to the installer;
- treating optional components as required for boot/login/shell;
- bypassing the sequential roadmap.

## Core install invariant

A successful Basic install must contain only the base system required for:

1. UEFI/GPT x86_64 boot;
2. encrypted `DATA` volume open/install;
3. CAPYFS mount and persistence;
4. login/session startup;
5. default shell/desktop fallback;
6. update/recovery primitives owned by the base system.

No optional external component may be required to reach the base shell or recover a failed install.

## Installation profiles

| Profile | Purpose | Allowed components |
|---|---|---|
| Basic | Minimal reliable CapyOS core | Base system only |
| Recommended | Default user-facing experience when the matching roadmap stage is active | Official components compatible with the installed CapyOS ABI |
| Custom | User-selected or future custom sources | Explicitly selected compatible components with shown permissions and dependencies |

Basic is the fallback for every failure path.

## Ownership boundaries

| Area | External owner | CapyOS base owner |
|---|---|---|
| Component descriptors and dependency planning | `CapyAgent` | fetch, hash verification, staging, activation, rollback, UI prompts |
| Package manifest and resolver | `CapyAgent` | real filesystem mutation, permissions, journal, boot-slot recovery |
| Browser parsing/layout core | `CapyBrowser` | DNS/TCP/TLS, windows, cache/cookies, sandbox and user-facing errors |
| Media decode cores | `CapyCodecs` | file/network IO, allocator adapter, renderer/audio backend, sandbox policy |
| Widget/display-list model | `CapyUI` | compositor, input devices, fonts, surfaces, shell integration |
| Language runtime core | `CapyLang` | host ABI, sandbox, process/resource policy, filesystem/network grants |
| Benchmarks and baselines | `CapyBenchmark` | clocks, platform identity, release gate orchestration |

## Component lifecycle

Future install/update flows must follow this order:

1. **Discover** a component index from an approved source.
2. **Parse** descriptors with fail-closed validation.
3. **Filter** by CapyOS ABI, component ABI and architecture.
4. **Plan** dependencies with a deterministic dry-run plan.
5. **Present** permissions, dependencies and install mode to the user.
6. **Fetch** artifacts through CapyOS networking/update policy.
7. **Verify** sha256 in the alpha flow; add signatures before official user-facing release.
8. **Stage** artifacts outside active runtime paths.
9. **Activate** only after staging succeeds completely.
10. **Record** activation state for rollback and diagnostics.
11. **Fallback** to Basic core when activation fails.

## Required descriptor fields

Every installable component must declare:

- stable component id;
- display name;
- kind;
- channel;
- release tag;
- artifact name or URL;
- lowercase sha256;
- required CapyOS ABI names and minimum versions;
- required component ABI names and minimum versions;
- dependencies;
- permissions;
- rollback/staging compatibility class.

## ABI naming convention

Use explicit ABI names instead of repository names when possible:

| ABI name | Owner | Purpose |
|---|---|---|
| `capyos-base` | CapyOS | base install/runtime compatibility |
| `capyos-package-apply` | CapyOS | package application and staging adapter |
| `capy-agent-component-index` | CapyAgent | component descriptor and planning model |
| `capy-codec-image` | CapyCodecs | image decode API and pixel contract |
| `capy-browser-core` | CapyBrowser | browser parse/layout contract |
| `capy-ui-widget` | CapyUI | widget/display-list contract |
| `capy-lang-host` | CapyLang/CapyOS | bytecode host ABI boundary |
| `capy-benchmark-report` | CapyBenchmark | report/baseline contract |

ABI version bumps must be additive until the relevant integration stage explicitly allows a breaking migration.

## Filesystem layout after future activation

The exact paths are future-stage work, but the base policy is:

- staged artifacts live outside active runtime paths until verified;
- active component metadata is separate from payload files;
- rollback metadata is owned by CapyOS, not by external components;
- components cannot write outside their declared prefix;
- the base shell remains available without reading optional component payloads.

## Security rules

- Fail closed on malformed descriptors, incompatible ABI, unknown critical fields or hash mismatch.
- Do not log passwords, salts, raw hashes, user secrets or raw private component keys.
- Treat unsigned GitHub tag-release artifacts as alpha-only.
- Add signed index/artifact verification before official user-facing update/install chains.
- Optional components receive permissions only through CapyOS policy, never by direct syscalls or kernel pointers.

## Integration gates

- Etapa 8 may integrate installer/update UX and staging UI.
- Etapa 9 may integrate package manager, SDK and stable ABI.
- Earlier stages may only update contracts, host-testable external code and documentation.

Recommended external/manual gates when runtime integration eventually occurs:

- package/index logic: `make test`;
- source-layout or adapter placement: `make layout-audit`;
- runtime object/link changes: `make all64`;
- installer path changes: `make iso-uefi` and `make smoke-x64-iso`;
- release/update/package promotion: `make release-check`.
