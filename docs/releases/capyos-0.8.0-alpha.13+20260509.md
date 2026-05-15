# CapyOS 0.8.0-alpha.13+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Release/F2** focado em assinatura
operacional de artefatos. O projeto agora possui tooling versionado para assinar
e verificar `build/release-artifacts.sha256` com Ed25519 via OpenSSL, sem manter
chave privada no repositorio.

A versão alinhada é `0.8.0-alpha.13+20260509`.

## Principais entregas

### Tooling de assinatura

- `tools/scripts/sign_release.py` assina `build/release-artifacts.sha256` com
  Ed25519 raw via `openssl pkeyutl -sign -rawin`.
- O script recusa chave privada legível por grupo/outros por padrão.
- Exporta chave pública PEM opcional com `--public-key-out`.
- Faz auto-verificação quando a chave pública é exportada.

### Tooling de verificação

- `tools/scripts/verify_release_signature.py` valida que a chave pública é
  Ed25519/SPKI.
- Verifica a assinatura sobre os bytes exatos do arquivo de checksums.
- Suporta `CAPYOS_RELEASE_PUBLIC_KEY` para integração futura com CI.

### Makefile

- Novo alvo opcional `sign-release-checksums`.
- Novo alvo opcional `verify-release-signature`.
- Variáveis operacionais:
  - `RELEASE_PRIVATE_KEY`
  - `RELEASE_PUBLIC_KEY`
  - `RELEASE_SIGNATURE`

### Documentação operacional

- Novo guia `docs/security/release-signing.md` com:
  - geração de chave offline;
  - permissões recomendadas;
  - assinatura/verificação;
  - publicação;
  - rotação;
  - compatibilidade futura com F5/update-fetch.

## Compatibilidade

- `release-check` não passa a depender da chave offline nesta etapa.
- `verify-release-checksums` continua validando SHA-256 como antes.
- A assinatura Ed25519 é adicionada como etapa operacional explícita, adequada
  para operador/CI que possua a chave privada fora do workspace.

## Validacao

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- scripts recusam entradas ausentes/vazias;
- assinatura/verificação usam OpenSSL Ed25519 raw sobre o arquivo de checksums;
- chave privada exige permissão segura por padrão;
- chave pública é validada pelo prefixo SPKI Ed25519 esperado;
- alvos Makefile são opcionais e não quebram `release-check` sem chave offline;
- documentação foi ligada ao índice principal e ao checklist de release.

## Próximos passos

- Operador gerar chave offline oficial fora do repositório.
- Publicar chave pública esperada para verificação humana/CI.
- Integrar `verify-release-signature` à CI quando o segredo de release estiver
  provisionado de forma segura.
- Continuar F5 com `update-fetch` remoto assinado.
