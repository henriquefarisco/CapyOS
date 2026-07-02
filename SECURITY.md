# Security Policy

CapyOS is an experimental (alpha) operating system. It is **not** intended for
production use and ships with no warranty (see `LICENSE` and `LAWFUL_USE.md`).

## Supported versions

Only the latest `0.8.0-alpha.*` build on the `main` branch receives security
attention. Older alphas are not maintained.

| Version | Supported |
|---|---|
| latest `0.8.0-alpha.*` (main) | ✅ |
| any older alpha | ❌ |

## Reporting a vulnerability

Please report suspected vulnerabilities **privately**, not via public issues:

- Preferred: open a [GitHub private security advisory](https://github.com/henriquefarisco/CapyOS/security/advisories/new).
- Alternatively, contact the maintainer through the address on their GitHub profile.

Include, where possible: affected version/commit, a description, reproduction
steps or a proof of concept, and the impact you observed. Please allow a
reasonable period for triage and a fix before any public disclosure.

## Scope notes

- Cryptography lives in `src/security/` (first-party) and `third_party/bearssl`
  (vendored TLS). Vendored code is excluded from first-party code scanning; TLS
  cipher-suite selection never enables legacy DES/3DES suites.
- The package installer (`services/capypkg`) is HTTPS-only, SHA-256 verified,
  and fail-closed on signature checks without a pinned trust anchor.
