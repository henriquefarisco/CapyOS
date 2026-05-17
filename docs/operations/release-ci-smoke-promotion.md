# CapyOS — Promoção pública pós-smoke VMware F2

> **Runbook completo da Etapa 2:** [`etapa-2-external-validation-playbook.md`](etapa-2-external-validation-playbook.md) (Fase E).

## Objetivo

`tools/scripts/release_ci_smoke_promotion.py` gera ou verifica um manifesto público de promoção pós-smoke VMware oficial.

O gate consome `release-publication.manifest`, `release-official-handoff.manifest`, `release-smoke-acceptance.manifest`, `release-smoke-evidence.manifest` e `SMOKE_X64_VMWARE_ARGS`. Ele não cria chave, não assina artefatos, não liga VM, não chama `make`, não chama `git`, não chama OpenSSL e não acessa chave privada.

## Execução via Makefile

```bash
SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
make release-ci-smoke-promotion RELEASE_TAG=0.8.0-alpha.237+20260514 SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

Para conferir um manifesto de promoção já publicado:

```bash
make verify-release-ci-smoke-promotion RELEASE_TAG=0.8.0-alpha.237+20260514 SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

## Entradas conferidas

- `RELEASE_TAG` ou tag equivalente do provedor de CI.
- `VERSION.yaml`.
- `include/core/version.h`.
- `README.md`.
- release note da versão pública.
- `release-publication.manifest`.
- `release-official-handoff.manifest`.
- `release-smoke-evidence.manifest`.
- `release-smoke-acceptance.manifest`.
- `SMOKE_X64_VMWARE_ARGS`.

## Regras de segurança

- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente.
- Recalcula a expectativa do manifesto de aceitação antes de promover.
- Rejeita divergência entre publicação, handoff, aceitação e estado esperado.
- Rejeita marcadores de chave privada nos manifestos públicos.
- Registra `public_release_promotable=yes`, `private_key_included=no`, `vm_powered_on_by_gate=no`, `make_executed=no` e `git_executed=no`.
- Gera manifesto determinístico com hashes SHA-256 dos manifestos encadeados.

## Posição no pipeline

Ordem recomendada quando a infraestrutura oficial estiver provisionada:

```bash
make smoke-marker-policy-selftest
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
