# CapyOS 0.8.0-alpha.58+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de orquestração pública: o projeto passa a ter
um gate agregado que executa as verificações públicas de assinatura, materiais e
manifesto de publicação sem acessar chave privada.

A versão alinhada é `0.8.0-alpha.58+20260510`.

## Principais entregas

### Gate público agregado de publicação

- Novo `tools/scripts/release_publication_gate.py`.
- Novo alvo `release-publication-gate` no `Makefile`.
- Nova documentação operacional em `docs/operations/release-publication-gate.md`.
- Encadeamento de `verify_release_signature.py`.
- Encadeamento de `release_public_materials_check.py`.
- Encadeamento de `verify_release_publication_manifest.py`.
- Rejeição fail-closed de ambiente com variável de chave privada definida.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O gate usa o mesmo interpretador Python para chamar os scripts versionados.
- O gate não executa `make` ou `git` internamente.
- O operador ainda precisa provisionar chave pública oficial e infraestrutura
  VMware para fechar os gates externos de F2.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente;
- exigência de chave pública e fingerprint SHA-256 esperado;
- normalização de fingerprint hex64 ou `aa:bb:...`;
- ordem das etapas internas do gate;
- passagem explícita de `checksums`, `signature`, chave pública, fingerprint,
  manifesto da chave e manifesto de publicação;
- ausência de chamadas `make` ou `git` no gate;
- target `release-publication-gate` no `Makefile`;
- documentação operacional e de assinatura de release;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifestos públicos.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `release-publication-gate`,
  `verify-release-signature` com pinagem e `smoke-x64-vmware-dhcp` na esteira de
  tag.
