# CapyOS 0.8.0-alpha.56+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de auditabilidade de publicação: o projeto
passa a gerar um manifesto público determinístico dos materiais que acompanham a
release, sem acessar chave privada.

A versão alinhada é `0.8.0-alpha.56+20260510`.

## Principais entregas

### Manifesto público de publicação

- Novo `tools/scripts/release_publication_manifest.py`.
- Novo alvo `release-publication-manifest` no `Makefile`.
- Novo artefato padrão `build/release-publication.manifest`.
- Validação de checksums, artefatos, assinatura, chave pública, fingerprint e
  manifesto da chave antes da escrita.
- Manifesto determinístico, sem timestamp e sem chave privada.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O manifesto de publicação é separado do manifesto da chave pública.
- O artefato pode ser publicado junto da release para auditoria humana/CI.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing do arquivo público de checksums;
- rejeição de caminhos absolutos, `..`, backslash ou NUL nos artefatos listados;
- recálculo SHA-256 dos artefatos listados;
- checagem do tamanho raw da assinatura Ed25519;
- validação de chave pública Ed25519 DER/SPKI;
- normalização, comparação e registro do fingerprint esperado;
- validação do manifesto público da chave;
- verificação OpenSSL da assinatura sobre `release-artifacts.sha256`;
- escrita atômica do manifesto de publicação;
- target `release-publication-manifest` no `Makefile`;
- documentação operacional em `docs/operations/release-publication-manifest.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifestos públicos.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `release-public-materials-check`,
  `release-publication-manifest`, `verify-release-signature` com pinagem e
  `smoke-x64-vmware-dhcp` na esteira de tag.
