# CapyOS — Gate público agregado de publicação

## Objetivo

`tools/scripts/release_publication_gate.py` executa uma conferência pública
agregada antes ou depois da publicação externa da release, sem acessar chave
privada.

O gate encadeia os verificadores públicos já versionados e falha fechado no
primeiro erro.

## Execução via Makefile

```bash
make release-publication-gate \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest
```

## Etapas internas

O gate executa, nessa ordem:

1. `verify_release_signature.py`
   - valida assinatura Ed25519 sobre `release-artifacts.sha256`.
2. `release_public_materials_check.py`
   - confere checksums, artefatos, assinatura, chave pública, fingerprint e
     manifesto da chave.
3. `verify_release_publication_manifest.py`
   - confere `release-publication.manifest` contra os materiais reais.

## Regras de segurança

- Não lê chave privada.
- Rejeita o ambiente se `RELEASE_PRIVATE_KEY` ou `CAPYOS_RELEASE_PRIVATE_KEY`
  estiver definido.
- Exige fingerprint SHA-256 esperado da chave pública.
- Não executa `make` ou `git` internamente.
- Usa o mesmo interpretador Python para chamar os scripts versionados.


## Contrato CI anterior ao gate

Antes do gate agregado, a CI pode validar o contrato público de ambiente:

```bash
make release-ci-publication-contract \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```


## Gate CI/tag

Para aceitar uma tag pública, use o gate de CI/tag como agregador final dos contratos públicos antes da publicação externa:

```bash
make release-ci-tag-gate \
  RELEASE_TAG=0.8.0-alpha.63+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```
