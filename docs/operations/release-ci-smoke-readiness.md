# CapyOS — Prontidão oficial do smoke VMware F2

## Objetivo

`tools/scripts/release_ci_smoke_readiness.py` valida a prontidão pública do smoke VMware oficial antes de ligar a VM real.

O gate consome o manifesto oficial de handoff CI/release e `SMOKE_X64_VMWARE_ARGS`. Ele não cria chave, não assina artefatos, não liga VM, não chama `make`, não chama `git`, não chama OpenSSL e não acessa chave privada.

## Execução via Makefile

```bash
make release-ci-smoke-readiness \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

Por padrão o alvo lê `build/release-official-handoff.manifest`. Para trocar o caminho, use `RELEASE_CI_SMOKE_READINESS_ARGS` com `--handoff-manifest`.

## Entradas conferidas

- `RELEASE_TAG` ou tag equivalente do provedor de CI.
- `VERSION.yaml`.
- `include/core/version.h`.
- `README.md`.
- release note da versão pública.
- `release-official-handoff.manifest`.
- `SMOKE_X64_VMWARE_ARGS`.

## Regras de segurança

- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente.
- Rejeita marcadores de chave privada dentro do manifesto de handoff.
- Exige `format=capyos-release-official-handoff-manifest-v1`.
- Exige `private_key_included=no`, `vm_powered_on=no`, `make_executed=no` e `git_executed=no`.
- Exige `signature_algorithm=Ed25519`, `checksum_algorithm=SHA-256` e `signature_size=64`.
- Exige logs relativos em `build/ci/*.log`.
- Exige os gates obrigatórios: `release-ci-official-provisioning-contract`, `release-ci-tag-gate`, `release-publication-gate` e `smoke-x64-vmware-mouse-events`.
- Reusa o contrato oficial do smoke para rejeitar flags de diagnóstico incompatíveis com CI oficial.

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

A chave privada oficial permanece offline e fora da CI pública.
