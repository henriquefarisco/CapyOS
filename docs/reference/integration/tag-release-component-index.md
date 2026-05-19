# Tag release component index

**Status:** alpha integration reference for future Etapas 8-9 runtime work.
**Trust model:** GitHub release tags plus sha256 and ABI compatibility metadata. Signed indexes and certificates are intentionally deferred.

## Purpose

The component index will let a future CapyOS installer/package adapter install
or update optional modules independently from the base system while keeping the
base bootable with only the core and default shell.

The installer/runtime boundary is defined in `modular-installation-architecture.md`.

## Future install modes

- **Basic:** install only CapyOS core and default shell.
- **Recommended:** install official modules compatible with the running CapyOS ABI.
- **Custom:** allow user-selected modules or future custom sources, with permissions and dependencies shown before staging.

## Minimum descriptor fields

Each component entry must declare:

- component id;
- display name;
- kind (`agent`, `browser-core`, `codec`, `ui`, `lang-runtime`, `benchmark`, `app`);
- channel (`stable`, `testing`, `experimental`, `custom`);
- GitHub release tag, such as `v0.1.0`;
- release artifact name or URL;
- sha256 of the release artifact;
- required ABI names and minimum versions;
- dependencies;
- permissions;
- rollback/staging compatibility class.

An example descriptor lives in `CapyAgent/docs/component-index-example.md`.

## Future alpha verification flow

1. Fetch the index from a known tag/release URL.
2. Parse descriptors.
3. Reject malformed tags, missing artifact names, invalid sha256 strings and incompatible ABI requirements.
4. Resolve dependencies.
5. Download selected release artifacts from tag URLs.
6. Compute sha256 locally and compare with the index.
7. Stage artifacts.
8. Activate only after staging succeeds; keep base shell fallback.

## Current limitation

This flow is not active runtime behavior while Etapa 3 is active. Once the
Etapas 8-9 adapter exists, the alpha flow should detect accidental artifact
mismatch and prevent incompatible module installation. It does not protect
against a compromised GitHub account, mutable release assets plus modified
index, or malicious custom source.

Before this becomes an official user-facing update trust chain, add at least one of:

- signed component index;
- signed artifacts;
- protected release root key and rotation policy;
- explicit vendor trust store for custom sources.

## Ownership

CapyAgent owns the host-testable descriptor and compatibility logic. CapyOS owns network fetch, local sha256 calculation, staging, activation, rollback and user prompts.

## ABI naming

Use the ABI names registered in `modular-installation-architecture.md` rather
than repository names when declaring compatibility.
