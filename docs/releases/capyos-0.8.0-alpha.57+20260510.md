# CapyOS 0.8.0-alpha.57+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de verificação pós-publicação: o projeto passa
a conferir o manifesto público de publicação contra os materiais reais da release,
sem acessar chave privada.

A versão alinhada é `0.8.0-alpha.57+20260510`.

## Principais entregas

### Verificador do manifesto público de publicação

- Novo `tools/scripts/verify_release_publication_manifest.py`.
- Novo alvo `verify-release-publication-manifest` no `Makefile`.
- Validação estrita dos campos do manifesto de publicação.
- Comparação de hashes dos materiais públicos listados no manifesto.
- Revalidação do manifesto público da chave.
- Revalidação dos artefatos listados em `release-artifacts.sha256`.
- Verificação Ed25519 da assinatura sobre o arquivo de checksums.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O verificador pode usar um fingerprint externo pinado via
  `RELEASE_PUBLIC_KEY_SHA256`.
- O verificador usa por padrão o diretório do manifesto para materiais públicos e
  o diretório atual como raiz dos artefatos listados.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing estrito do manifesto público de publicação;
- rejeição de campos ausentes, duplicados, desconhecidos ou malformados;
- validação de `artifact_count` e sequência `artifact.N`;
- rejeição de nomes inseguros para materiais e artefatos;
- comparação de SHA-256 dos materiais públicos;
- revalidação dos checksums e dos artefatos listados;
- validação Ed25519 DER/SPKI da chave pública;
- comparação com fingerprint externo pinado;
- revalidação do manifesto público da chave;
- verificação OpenSSL da assinatura sobre `release-artifacts.sha256`;
- target `verify-release-publication-manifest` no `Makefile`;
- documentação operacional e de assinatura de release;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifestos públicos.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `release-public-materials-check`,
  `release-publication-manifest`, `verify-release-publication-manifest`,
  `verify-release-signature` com pinagem e `smoke-x64-vmware-dhcp` na esteira de
  tag.
