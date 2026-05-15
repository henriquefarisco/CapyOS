# CapyOS 0.8.0-alpha.60+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de gate público de CI/tag: o projeto passa a ter uma entrada única para conectar contrato de versão/tag, preflight de CI, contrato público de publicação e gate público agregado sem chave privada.

A versão alinhada é `0.8.0-alpha.60+20260510`.

## Principais entregas

- Novo `tools/scripts/release_ci_tag_gate.py`.
- Novo alvo `release-ci-tag-gate` no `Makefile`.
- Nova documentação operacional em `docs/operations/release-ci-tag-gate.md`.
- Validação de tag pública contra `VERSION.yaml`, `include/core/version.h`, `README.md` e release note.
- Orquestração de `release_ci_preflight.py`, `release_ci_publication_contract.py` e `release_publication_gate.py`.
- Rejeição de ambiente com variável de chave privada definida.

## Segurança e compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O gate não executa `make` ou `git` internamente.
- A execução real em CI ainda depende da chave pública oficial, fingerprint publicado e infraestrutura VMware provisionada.
- Tags aceitas seguem `0.8.0-alpha.N+YYYYMMDD` ou `v0.8.0-alpha.N+YYYYMMDD`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, scripts de build, scripts de teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de `RELEASE_PRIVATE_KEY` e `CAPYOS_RELEASE_PRIVATE_KEY` no ambiente;
- validação de tag contra versão pública;
- alinhamento entre `VERSION.yaml`, `include/core/version.h`, `README.md` e release note;
- passagem de chave pública, fingerprint e manifesto para o preflight;
- passagem de checksums, assinatura, chave e manifesto para o contrato público;
- passagem de checksums, assinatura, artifact root e manifesto para o gate agregado de publicação;
- target `release-ci-tag-gate` no `Makefile`;
- documentação operacional e de assinatura de release;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifestos públicos.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-tag-gate` e `smoke-x64-vmware-dhcp` na esteira real de tag.
