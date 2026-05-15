# CapyOS — Contrato oficial de provisionamento CI/release

## Objetivo

`tools/scripts/release_ci_official_provisioning_contract.py` valida a prontidão pública oficial da esteira F2 antes de aceitar uma tag ou rodar o smoke VMware real.

O contrato não cria chave, não assina artefatos, não liga VM, não chama `make`, não chama `git`, não chama OpenSSL e não lê chave privada. Ele verifica apenas materiais públicos e configuração de CI.

## Execução via Makefile

```bash
make release-ci-official-provisioning-contract \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

## Entradas conferidas

- `RELEASE_TAG` ou tag equivalente do provedor de CI.
- `VERSION.yaml`.
- `include/core/version.h`.
- `README.md`.
- release note da versão pública.
- chave pública Ed25519 PEM/SPKI.
- `RELEASE_PUBLIC_KEY_SHA256`.
- `release-public-key.manifest`.
- `SMOKE_X64_VMWARE_ARGS`.

## Regras de segurança

- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente.
- Rejeita arquivo de chave pública com marcador de chave privada.
- Decodifica PEM/SPKI Ed25519 sem depender de OpenSSL.
- Exige fingerprint SHA-256 pinado e coerente com a chave pública oficial.
- Exige manifesto público da chave com `private_key_included=no`.
- Rejeita tag divergente da versão estendida em `VERSION.yaml`.
- Rejeita `--dry-run`, `--no-artifact-check`, `--no-poweroff`, `--no-tool-check` e `--gui` no smoke oficial.
- Exige `--serial-log` relativo em `build/ci/*.log`.
- Para `govc`, exige `GOVC_URL`, `GOVC_USERNAME`, `GOVC_DATACENTER` e `GOVC_PASSWORD` ou `GOVC_PASSWORD_FILE`.

## Posição no pipeline

Ordem recomendada quando a infraestrutura oficial estiver provisionada:

```bash
make release-ci-official-provisioning-contract RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-tag-gate RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-official-handoff-manifest RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make verify-release-official-handoff-manifest RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-smoke-readiness RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make release-ci-smoke-evidence RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make verify-release-ci-smoke-evidence RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make release-ci-smoke-acceptance RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make verify-release-ci-smoke-acceptance RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make release-ci-smoke-promotion RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make verify-release-ci-smoke-promotion RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
```

A chave privada oficial permanece offline e fora da CI.
