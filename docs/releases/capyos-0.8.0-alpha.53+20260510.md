# CapyOS 0.8.0-alpha.53+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de publicação auditável da chave de release:
o projeto passa a gerar um manifesto público determinístico da chave Ed25519
esperada e do fingerprint SHA-256 usado pela CI.

A versão alinhada é `0.8.0-alpha.53+20260510`.

## Principais entregas

### Manifesto público da chave esperada

- Novo `tools/scripts/release_public_key_manifest.py`.
- Novo alvo `release-public-key-manifest` no `Makefile`.
- Novo artefato padrão `build/release-public-key.manifest`.
- Validação de chave pública Ed25519 PEM via OpenSSL DER/SPKI.
- Validação do fingerprint esperado antes de gravar o manifesto.
- Manifesto determinístico sem timestamp e sem chave privada.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- Nenhuma chave pública oficial é fabricada por este patch.
- O manifesto é gerado somente a partir da chave pública exportada pelo operador
  e do fingerprint esperado.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- validação de arquivo de chave pública não vazio;
- normalização do fingerprint SHA-256 esperado;
- conversão OpenSSL para DER/SPKI;
- checagem do prefixo Ed25519 esperado;
- rejeição de fingerprint divergente;
- escrita atômica do manifesto com `--force` opcional;
- target `release-public-key-manifest` no `Makefile`;
- documentação em `docs/security/release-signing.md` e
  `docs/operations/release-ci-preflight.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifesto.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `verify-release-signature` com pinagem e
  `smoke-x64-vmware-dhcp` na esteira de tag.
