# CapyOS 0.8.0-alpha.4+20260429

Data: 2026-04-29
Canal: develop
Base: robustez da trilha `UEFI/GPT/x86_64`

## Destaques

- adocao profunda do `op_budget` no navegador: cada `html_viewer_app`
  carrega um `nav_op_budget` que centraliza cancelamento cooperativo;
  cada budget de fase (parse/render/external resource) propaga
  exaustao via `op_budget_exhaust`, e `html_viewer_cancel_navigation`
  trip-a o budget para que loops ainda em execucao parem na proxima
  consulta sem precisar checar flags ad-hoc
- eventos de auditoria estruturados com prefixo `[audit]`:
  `[audit] [browser] strict-mode violation` no escalonamento estrito do
  navegador e `[audit] [update] payload-sha256 ...` em todas as decisoes
  da verificacao do payload
- `[capyfs-journal]` agora emite estado autenticacao em formato chave=valor
  estavel (`auth=on key=derived-from-super replay=enabled`,
  `auth=on key=missing replay=refused`, `auth=off legacy-v1=on`,
  `format=first-mount`) facilitando log-mining
- M6.4 ganhou primeira fase de assinatura criptografica:
  `update_agent_apply_boot_slot_verified()` exige um digest SHA-256 hex
  fornecido pelo chamador, recusa mismatch (`-31`), digest ausente (`-30`)
  e digest mal-formado (`-32`); fallback transparente para o caminho
  legacy quando `payload_sha256` nao esta declarado no manifesto
- M7.2 ganhou round-trip sintetico em
  `tests/test_capyfs_journal_cause.c::test_dirty_shutdown_roundtrip_replays_only_committed`
  validando que apos crash apenas TX comitadas chegam ao disco e que um
  segundo mount apos replay reporta `cause=NONE`
- `capyfs_journal_release_slot()` adicionado para liberar slots da tabela
  de mounts (4 entradas), permitindo que o teste de round-trip e futuros
  unmount cleanup nao gastem capacidade

## Browser e cooperacao

- `include/apps/html_viewer.h` agrega `struct op_budget nav_op_budget` no
  `html_viewer_app`
- `src/apps/html_viewer/navigation_budget.c` ganha
  `hv_nav_budget_reset/blocked/cancel/propagate_exhaust/reason`; cada
  per-phase `take()` consulta `op_budget_is_blocked` antes de decidir
- `tests/html_viewer/resource_cases.inc::test_html_viewer_nav_budget_cancellation_aborts_phases`
  cobre arm/cancel/refuse-all-phases/re-arm

## Seguranca

- `[audit] [browser] strict-mode violation -> nav=FAILED` agora e emitido
  no `klog` quando o modo estrito do navegador escalona uma transicao
  proibida; coberto por
  `tests/html_viewer/navigation_cases.inc::test_html_viewer_strict_transition_escalates`
- `[capyfs-journal] auth=on/off` em
  `src/fs/capyfs/capyfs_journal_integration.c` documenta o estado de
  autenticacao do journal em todas as transicoes de mount
- M6.4: `payload_sha256=hex64` em manifestos e propagado para
  `system_update_status::staged_payload_sha256`;
  `update_agent_staged_requires_payload_verification()` indica se a
  proxima ativacao exige verificacao; `update_agent_apply_boot_slot_verified()`
  e o caminho seguro a ser usado em release builds

## Modularidade

- `src/services/update_agent.c` (943 linhas com a feature M6.4) foi
  dividido em `src/services/update_agent.c` (808 linhas) e
  `src/services/update_agent_transact.c` (181 linhas) com fronteira em
  `src/services/internal/update_agent_internal.h`; cada TU permanece bem
  abaixo do limite de 900 linhas; layout audit estrito passa sem warnings

## Testes adicionados

- `tests/html_viewer/resource_cases.inc::test_html_viewer_nav_budget_cancellation_aborts_phases`
- `tests/html_viewer/navigation_cases.inc::test_html_viewer_strict_transition_escalates` (atualizado para verificar `[audit]`)
- `tests/test_capyfs_journal_cause.c::test_dirty_shutdown_roundtrip_replays_only_committed`
- `tests/test_update_transact.c::test_apply_verified_legacy_path_no_digest`
- `tests/test_update_transact.c::test_apply_verified_matching_digest`
- `tests/test_update_transact.c::test_apply_verified_mismatched_digest_refuses`
- `tests/test_update_transact.c::test_apply_verified_missing_digest_refuses`
- `tests/test_update_transact.c::test_apply_verified_malformed_digest_refuses`

## Validacao

```bash
make test
make layout-audit
make version-audit
make all64
```

Versao alinhada: `0.8.0-alpha.4+20260429`
