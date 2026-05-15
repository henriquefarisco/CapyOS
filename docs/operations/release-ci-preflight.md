# CapyOS — Preflight de CI/release F2

## Objetivo

`tools/scripts/release_ci_preflight.py` valida a configuração mínima da esteira
F2 antes de disparar gates destrutivos ou demorados como assinatura/verificação
de release e smoke VMware+E1000.

O preflight não cria chave, não assina artefatos, não liga VM e não executa
`make release-check`. Ele apenas falha cedo quando a CI ainda não tem a chave
pública/fingerprint ou os argumentos VMware obrigatórios.

## Variáveis exigidas

- `RELEASE_PUBLIC_KEY` ou `CAPYOS_RELEASE_PUBLIC_KEY`
  - caminho da chave pública Ed25519 PEM.
- `RELEASE_PUBLIC_KEY_SHA256` ou `CAPYOS_RELEASE_PUBLIC_KEY_SHA256`
  - fingerprint SHA-256 esperado da chave pública.
  - aceita hex64 contínuo ou pares `aa:bb:...`.
- `RELEASE_PUBLIC_KEY_MANIFEST` ou `CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST`
  - manifesto público gerado por `release-public-key-manifest`.
- `SMOKE_X64_VMWARE_ARGS`
  - argumentos do `smoke_x64_vmware.py` para `vmrun` ou `govc`.

## Como obter o fingerprint

O operador deve calcular o fingerprint a partir da chave pública exportada, sem
copiar a chave privada para a CI:

```bash
make release-public-key-fingerprint \
  RELEASE_PUBLIC_KEY=/ci/secrets/release-ed25519.pub.pem
```

A saída padrão é `RELEASE_PUBLIC_KEY_SHA256=<hex64>`.

## Manifesto público

Antes de publicar a release, gere também o manifesto público da chave esperada:

```bash
make release-public-key-manifest \
  RELEASE_PUBLIC_KEY=/ci/secrets/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...>
```

Esse arquivo é um artefato de auditoria e deve acompanhar a chave pública ou a
referência imutável da chave esperada. O preflight valida que esse manifesto
bate com `RELEASE_PUBLIC_KEY` e `RELEASE_PUBLIC_KEY_SHA256`.

## Execução via Makefile

```bash
SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
make release-ci-preflight \
  RELEASE_PUBLIC_KEY=/ci/secrets/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

## Regras de segurança

O preflight rejeita `SMOKE_X64_VMWARE_ARGS` com:

- `--dry-run`
- `--no-artifact-check`
- `--no-poweroff`

Esses flags são úteis para diagnóstico manual, mas não devem aparecer em CI de
release porque podem produzir falso positivo ou deixar VM ligada.

## Providers VMware

### `vmrun`

Exige:

- `--provider vmrun`
- `--vmx /path/to/CapyOS.vmx`
- `vmrun` no `PATH`, salvo quando `--no-tool-check` é usado diretamente.

### `govc`

Exige:

- `--provider govc`
- `--vm-name <nome-da-vm>`
- `--govc-serial-log <datastore-path>`
- `govc` no `PATH`, salvo quando `--no-tool-check` é usado diretamente.
- ambiente `GOVC_URL`, `GOVC_USERNAME`, `GOVC_DATACENTER` e `GOVC_PASSWORD` ou
  `GOVC_PASSWORD_FILE`.

## Posição no pipeline

Ordem recomendada quando a infraestrutura estiver provisionada:

```bash
make release-ci-preflight
make release-check
make verify-release-signature RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-public-materials-check RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-publication-manifest RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make verify-release-publication-manifest RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-publication-contract RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-publication-gate RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-official-provisioning-contract RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-tag-gate RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-official-handoff-manifest RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make verify-release-official-handoff-manifest RELEASE_TAG=... RELEASE_PUBLIC_KEY=... RELEASE_PUBLIC_KEY_SHA256=...
make release-ci-smoke-readiness RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make release-ci-smoke-evidence RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
make verify-release-ci-smoke-evidence RELEASE_TAG=... SMOKE_X64_VMWARE_ARGS='--provider vmrun --vmx build/ci/capyos-smoke.vmx --serial-log build/ci/smoke_x64_vmware.serial.log --summary-log build/ci/smoke_x64_vmware.summary.log'
```

A chave privada de release permanece offline e fora da CI.
