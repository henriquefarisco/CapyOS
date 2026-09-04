# CapyOS 0.10.0-alpha.1+20260903

Versão: `0.10.0-alpha.1+20260903`. Primeira entrega de desenvolvimento da Etapa 9.

- índice oficial `capyos-modules-index-v2` resolvido antes da publicação,
  vinculado a `capyos-base-v3`, SHA-256 e assinatura Ed25519;
- CapyAgent 0.1.0 com component-index ABI v2 e resolver SemVer host-testável;
- `pkgd`, comando unificado `pkg`, instalação/atualização atômica com rollback
  e persistência de metadados;
- CapyUI 2.25.0 com Software Center usando backend injetado;
- SDK inicial em `sdk/include/capyos`, sample e guia de build.

Esta alpha não é uma release pública. Os gates locais de build, QEMU e VMware
da Etapa 9 passaram; a promoção depende da integração e publicação da branch.
