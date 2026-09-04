# CapyOS package SDK

The public package SDK targets `capyos-base-v3`. Headers live in
`sdk/include/capyos/`; applications must include only this tree, never kernel
internal headers. Run `make sdk-check` before publishing.

## Compatibility policy

- `CAPYOS_SDK_ABI_VERSION` changes only when an existing public layout or
  behavior becomes incompatible. Additive fields use `struct_size` negotiation.
- A manifest must declare `provides_abi`, `abi_version`, `core_abi_min`,
  `core_abi_max`, and `known_good`. The publisher selects only a signed,
  known-good candidate whose core range contains version 3.
- The OS consumes the signed index addressed by `capyos-base-v3`; it does not
  pin each package version at first boot.
- Package bytes are data-only today. Runtime execution remains disabled until
  a later sandboxed-loader stage, so the sample documents the future stable
  entry boundary without weakening the current no-execution policy.

## Sample

`sdk/samples/hello-package` demonstrates the versioned entry point and a
publisher manifest. Its URL, digest and `known_good=0` are deliberate
placeholders and must be replaced and signed before promotion.

```sh
make sdk-check
```
