# CapyOS — Manifesto oficial de handoff CI/release

## Objetivo

`tools/scripts/release_official_handoff_manifest.py` gera e verifica o manifesto público de handoff oficial da esteira F2.

O manifesto consolida, em formato chave/valor determinístico, os materiais públicos necessários para o operador/CI assumir a etapa externa real: tag, versão, checksums, assinatura, chave pública oficial, manifesto público da chave, manifesto público de publicação e contrato VMware.

O script não cria chave, não assina, não liga VM, não chama `make`, não chama `git`, não chama OpenSSL e não acessa chave privada.

## Execução via Makefile

```bash
make release-official-handoff-manifest \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  RELEASE_PUBLICATION_MANIFEST=build/release-publication.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

Para conferir um manifesto já publicado contra os materiais públicos:

```bash
make verify-release-official-handoff-manifest \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  RELEASE_PUBLICATION_MANIFEST=build/release-publication.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

## Campos principais

- `format=capyos-release-official-handoff-manifest-v1`.
- `release_tag` e versão pública estendida.
- `checksums_file` e `checksums_sha256`.
- `signature_file`, `signature_sha256` e `signature_size`.
- `public_key_file`, `public_key_sha256` e `expected_public_key_sha256`.
- `public_key_manifest_file` e `public_key_manifest_sha256`.
- `publication_manifest_file` e `publication_manifest_sha256`.
- `artifact_count` e lista `artifact.N.path`/`artifact.N.sha256`.
- `smoke_provider`, `smoke_serial_log`, `smoke_summary_log` e `smoke_vm_identifier_configured`.
- `required_gate.N` com a ordem pública esperada para o handoff.

## Regras de segurança

- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY`.
- Reusa o contrato oficial de provisionamento para validar tag, chave pública, fingerprint, manifesto da chave e smoke VMware.
- Confere o manifesto público de publicação contra checksums, assinatura, chave pública e manifesto da chave.
- Registra `private_key_included=no`.
- Registra `vm_powered_on=no`.
- Registra `make_executed=no` e `git_executed=no`.
- Falha fechado se o modo `--verify` encontrar qualquer diferença no manifesto existente.

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
