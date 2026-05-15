# CapyOS 0.8.0-alpha.55+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F2** de publicação segura: o projeto passa a ter
um gate versionado para conferir o pacote público de release antes da publicação
externa, sem acessar chave privada.

A versão alinhada é `0.8.0-alpha.55+20260510`.

## Principais entregas

### Conferência do pacote público

- Novo `tools/scripts/release_public_materials_check.py`.
- Novo alvo `release-public-materials-check` no `Makefile`.
- Validação de sintaxe de `release-artifacts.sha256`.
- Validação de assinatura Ed25519 raw de 64 bytes.
- Validação de chave pública Ed25519 PEM/SPKI e fingerprint pinado.
- Validação do manifesto público da chave esperada.
- Verificação OpenSSL da assinatura sobre o arquivo de checksums.

## Compatibilidade

- Nenhuma chave privada é lida, criada ou versionada.
- O gate é separado de `release-check` para não exigir chave pública oficial em
  validação local comum.
- O gate pode ser usado por operador humano ou CI de publicação.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- parsing do arquivo público de checksums;
- recálculo de SHA-256 dos artefatos listados;
- rejeição de caminhos absolutos, `..`, backslash ou NUL nos artefatos listados;
- checagem do tamanho raw da assinatura Ed25519;
- validação de chave pública Ed25519 DER/SPKI;
- normalização e comparação do fingerprint esperado;
- validação do manifesto público da chave;
- verificação OpenSSL da assinatura sobre `release-artifacts.sha256`;
- target `release-public-materials-check` no `Makefile`;
- documentação operacional em `docs/operations/release-public-materials-check.md`;
- documentação em `docs/security/release-signing.md`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Operador gerar/exportar a chave Ed25519 oficial.
- Publicar a chave pública oficial, fingerprint e manifesto.
- Provisionar VM/serial/credenciais VMware na CI.
- Rodar `release-ci-preflight`, `release-public-materials-check`,
  `verify-release-signature` com pinagem e `smoke-x64-vmware-dhcp` na esteira de
  tag.
