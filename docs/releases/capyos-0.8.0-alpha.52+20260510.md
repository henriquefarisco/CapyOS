# CapyOS 0.8.0-alpha.52+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de preparação da chave pública oficial: o
projeto passa a ter um helper versionado para emitir o fingerprint SHA-256
publicável da chave pública Ed25519 de release, sem tocar na chave privada.

A versão alinhada é `0.8.0-alpha.52+20260510`.

## Principais entregas

### Fingerprint publicável da chave pública

- Novo `tools/scripts/release_public_key_fingerprint.py`.
- Novo alvo `release-public-key-fingerprint` no `Makefile`.
- Validação de chave pública Ed25519 PEM via OpenSSL DER/SPKI.
- Emissão padrão em formato `RELEASE_PUBLIC_KEY_SHA256=<hex64>` para CI.
- Formatos opcionais `hex`, `colon` e `env` para uso manual ou automação.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- Nenhuma chave pública oficial é fabricada por este patch.
- O helper apenas deriva fingerprint a partir da chave pública já exportada pelo
  operador.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- validação de arquivo de chave pública não vazio;
- conversão OpenSSL para DER/SPKI;
- checagem do prefixo Ed25519 esperado;
- cálculo SHA-256 sobre DER/SPKI;
- formatos de saída `env`, `hex` e `colon`;
- target `release-public-key-fingerprint` no `Makefile`;
- documentação em `docs/security/release-signing.md` e
  `docs/operations/release-ci-preflight.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial e o fingerprint derivado pelo helper.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `verify-release-signature` com pinagem e
  `smoke-x64-vmware-dhcp` na esteira de tag.
