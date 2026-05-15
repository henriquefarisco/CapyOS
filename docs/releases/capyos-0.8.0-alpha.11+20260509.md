# CapyOS 0.8.0-alpha.11+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Update/F5** focado em seguranca,
confiabilidade e compatibilidade do fluxo local de update. Antes de qualquer
fetch remoto/assinatura Ed25519, o `update-agent` agora trata o manifesto local e
o staged como objetos de seguranca: nao aceita downgrade, nao promove update sem
`payload_sha256` e nao aplica staged hashado pelo caminho direto.

A versao alinhada e `0.8.0-alpha.11+20260509`.

## Principais entregas

### Manifest gate anti-downgrade

- `update_agent_poll()` compara semanticamente a versao candidata com a versao
  atual do sistema.
- Catalogos locais com versao menor que a atual entram em estado degradado.
- Staged manifests com versao menor que a atual entram em estado degradado.
- `update-import-manifest` recusa manifestos que nao sejam mais novos que o
  sistema atual.

### `payload_sha256` obrigatorio para updates novos

- Catalogo local so marca `update_available` quando o manifesto novo declara
  `payload_sha256` hex64 valido.
- Staged manifest so marca `stage_ready` quando possui `payload_sha256` hex64
  valido.
- Import manual recusa manifestos sem `payload_sha256` ou com hash malformado.

### Apply protegido

- `update_agent_apply_boot_slot()` recusa aplicar staged hashado sem verifier e
  registra evento `[audit] [update]`.
- `update_agent_apply_boot_slot_verified()` propaga falhas de poll antes de
  ativar boot slot.
- O caminho interno compartilhado aplica boot slot apenas depois do estado staged
  validado.

## Arquivos de maior impacto

- `src/services/update_agent.c`
- `src/services/update_agent_transact.c`
- `include/services/update_agent.h`
- `tests/test_update_agent.c`
- `tests/test_update_transact.c`
- `docs/reference/cli-reference.md`
- `VERSION.yaml`
- `include/core/version.h`

## Compatibilidade

- O formato do manifesto continua baseado em `available_version=`, `channel=`,
  `branch=`, `source=`, `published_at=` e `payload_sha256=`.
- Manifestos sem `payload_sha256` continuam legiveis, mas nao sao promovidos a
  update/staged valido quando representam uma versao nova.
- `update-check`, `update-import-manifest`, `update-stage`, `update-arm` e
  `update-clear` continuam existindo; agora retornam summaries mais especificos
  para hash ausente/malformado e downgrade.

## Validacao

Nesta sessao, a validacao foi feita por revisao estatica de codigo e
documentacao. Nao foram executados `make`, `git`, scripts de build, scripts de
teste ou automacao Python de validacao.

Pontos revisados estaticamente:

- `update_agent_poll()` so marca `update_available` se a versao for maior que a
  atual e `payload_sha256` for hex64 valido.
- `update_agent_poll()` so marca `stage_ready` se staged for nao-downgrade e
  possuir `payload_sha256` hex64 valido.
- `update_agent_import_manifest_path()` rejeita manifestos nao novos e manifestos
  sem `payload_sha256` valido antes de persistir o cache.
- `update_agent_apply_boot_slot()` retorna `-33` quando o staged hashado exige
  verifier.
- `update_agent_apply_boot_slot_verified()` retorna o erro de poll quando o staged
  esta degradado antes de tocar no boot slot.
- Regressões planejadas foram ajustadas em `tests/test_update_agent.c` e
  `tests/test_update_transact.c`, mas nao foram executadas nesta sessao.

## Status do pacote

- ✅ Protecao anti-downgrade no fluxo local de update.
- ✅ `payload_sha256` obrigatorio para updates novos/staged.
- ✅ Apply direto bloqueado para staged hashado.
- ✅ Documentacao e manifesto de versao alinhados para fechamento manual.

## Proximos passos

- Implementar fetch remoto assinado e verificador Ed25519 completo na sequencia
  F5/F2.
- Operador pode executar comandos `git` manualmente para registrar o patch.
- Validacoes executaveis ficam para operador/CI, conforme as restricoes desta
  sessao.
