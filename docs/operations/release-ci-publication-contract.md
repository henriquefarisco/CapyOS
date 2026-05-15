# CapyOS — Contrato público de CI para publicação

## Objetivo

`tools/scripts/release_ci_publication_contract.py` valida o contrato público que a
CI deve satisfazer antes de executar os gates de publicação de uma tag.

O contrato não assina, não liga VM, não chama `make`, não chama `git` e não lê
chave privada. Ele verifica a estrutura pública esperada para a esteira F2.

## Execução via Makefile

```bash
make release-ci-publication-contract \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```

## Entradas conferidas

- `build/release-artifacts.sha256`
- `build/release-artifacts.sha256.sig`
- chave pública Ed25519 exportada
- `build/release-public-key.manifest`
- `build/release-publication.manifest`
- `RELEASE_PUBLIC_KEY_SHA256`
- `SMOKE_X64_VMWARE_ARGS`

## Regras de segurança

- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente.
- Exige fingerprint SHA-256 pinado.
- Rejeita manifesto público com chave privada indicada.
- Rejeita checksums e manifestos malformados.
- Rejeita `--dry-run`, `--no-artifact-check`, `--no-poweroff` e
  `--no-tool-check` nos argumentos VMware de CI.
- Para `govc`, exige `GOVC_URL`, `GOVC_USERNAME`, `GOVC_DATACENTER` e
  `GOVC_PASSWORD` ou `GOVC_PASSWORD_FILE`.

## Diferença para os gates criptográficos

Este contrato valida coerência estrutural e operacional para CI. A verificação
criptográfica completa continua nos gates públicos:

- `verify-release-signature`
- `release-public-materials-check`
- `verify-release-publication-manifest`
- `release-publication-gate`

## Gate de tag

Na esteira real de tag, `release-ci-tag-gate` executa este contrato entre o preflight público de CI e o gate público agregado de publicação.

```bash
make release-ci-tag-gate \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```
