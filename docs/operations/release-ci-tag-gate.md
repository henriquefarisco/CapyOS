# CapyOS — Gate público de CI/tag de release

## Objetivo

`tools/scripts/release_ci_tag_gate.py` conecta a esteira pública de aceitação de uma tag de release CapyOS sem acessar chave privada.

O gate valida primeiro o contrato de versão/tag e depois executa, com o mesmo interpretador Python, os verificadores públicos já versionados.

## Execução via Makefile

```bash
make release-ci-tag-gate \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log'"
```

## Etapas internas

1. Contrato de versão/tag
   - confere `VERSION.yaml`, `include/core/version.h`, `README.md` e release note;
   - aceita tag `0.8.0-alpha.N+YYYYMMDD` ou `v0.8.0-alpha.N+YYYYMMDD`;
   - rejeita tag divergente da versão pública.
2. `release_ci_preflight.py`
   - valida chave pública, fingerprint, manifesto da chave e argumentos VMware.
3. `release_ci_publication_contract.py`
   - valida contrato público dos materiais e manifestos antes dos gates.
4. `release_publication_gate.py`
   - valida assinatura, pacote público e manifesto de publicação.

## Regras de segurança

- Não lê chave privada.
- Rejeita `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente.
- Não executa `make` ou `git` internamente.
- Não cria chaves ou artefatos de release.
- Falha fechado na primeira etapa divergente.
- Usa fingerprint SHA-256 pinado para todos os estágios públicos.

## Posição na CI

Este gate deve rodar na esteira de tag depois da geração e assinatura dos artefatos e antes da publicação/aceitação final da tag.

## Provisionamento oficial anterior

Antes de aceitar a tag e executar os gates agregados, a CI oficial pode validar chave pública, fingerprint, manifesto, tag e argumentos VMware com:

```bash
make release-ci-official-provisioning-contract \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```

## Handoff oficial posterior

Depois do gate público de tag, a CI oficial pode materializar o handoff auditável com:

```bash
make release-official-handoff-manifest \
  RELEASE_TAG=0.8.0-alpha.93+20260510 \
  RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem \
  RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...> \
  RELEASE_PUBLIC_KEY_MANIFEST=build/release-public-key.manifest \
  RELEASE_PUBLICATION_MANIFEST=build/release-publication.manifest \
  SMOKE_X64_VMWARE_ARGS="--provider govc --vm-name CapyOS-Release-Smoke --govc-serial-log '[datastore1] CapyOS/serial.log' --serial-log build/ci/smoke_x64_vmware.serial.log"
```
