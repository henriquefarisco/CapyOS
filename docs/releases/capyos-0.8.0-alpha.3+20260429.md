# CapyOS 0.8.0-alpha.3+20260429

Data: 2026-04-29
Canal: develop
Base: robustez da trilha `UEFI/GPT/x86_64`

## Destaques

- journal do CAPYFS ganha modo autenticado por volume (HMAC-SHA256 truncado a
  16 bytes, derivado da chave do volume com a superblock como salt)
- nova primitiva reutilizavel `op_budget` em `include/util/op_budget.h` com
  consumo, exaustao, cancelamento externo, reset e razao estavel
- API de privilegios centralizada em `include/auth/privilege.h` cobrindo
  `add-user` e `set-pass` com auditoria `[priv] denied`
- buffer cache passa a expor pacing (`buffer_cache_readahead`,
  `buffer_cache_writeback_pass`, `buffer_cache_dirty_count`) com contadores
  visiveis em `perf-fs`
- navegador ganha estado formal observavel com `html_viewer_state_name`,
  `html_viewer_state_transition_allowed`, ganchos de isolamento, modo estrito
  opt-in que escala transicoes invalidas para `FAILED` e budget de parse
  (`HV_PARSE_NODE_BUDGET = 384`)
- checklist humano de PR/release adicionado em
  `docs/testing/pr-and-release-checklist.md`

## Robustez e seguranca

- `JOURNAL_VERSION_AUTH = 2`, `journal_format_authenticated()`,
  `journal_set_hmac_key()`, `journal_is_authenticated()` e defer-and-verify
  no replay (estagia payload, valida tag antes de aplicar)
- `capyfs_journal_install_root_secret()` recebe a chave por volume injetada
  pelo runtime de mount; replay v2 sem chave configurada e recusado
- `crypt_hmac_sha256` reutilizado para derivar a chave do journal em
  `src/fs/capyfs/capyfs_journal_integration.c`
- `privilege_user_is_admin`, `privilege_session_is_admin`,
  `privilege_active_is_admin`, `privilege_check_admin_or_self`,
  `privilege_log_denied` e `privilege_log_granted` passam a ser o unico
  caminho para checagens de admin no shell

## Performance e cooperacao

- `op_budget_take`, `op_budget_cancel`, `op_budget_exhaust` e
  `op_budget_is_blocked` consolidam o padrao para operacoes longas
- buffer cache ganha read-ahead especulativo, writeback paciado e contagem
  de blocos sujos por dispositivo, com novos campos em
  `buffer_cache_stats` e impressao em `perf-fs`
- novo `tests/test_buffer_cache_pacing.c` cobre clamping de read-ahead,
  budget de writeback e recuperacao apos erro de backend

## Browser

- `html_viewer_state_strict_mode_set/enabled` permite escalar transicoes
  invalidas para `FAILED` em testes/release builds; default permanece
  `warn-only` para compatibilidade
- `hv_parse_budget_take` em `html_push_node` quando `hv_parse_app_get()`
  reporta navegacao real, com `forms_and_response.c` migrado para
  `hv_parse_locked_with_app(app, ...)`
- novos testes em `tests/html_viewer/`: `test_html_viewer_state_machine_helpers`,
  `test_html_viewer_strict_transition_escalates`,
  `test_html_viewer_isolation_hooks` e `test_html_viewer_parse_budget_logs_failure`

## Validacao

```bash
make test
make layout-audit
make version-audit
make all64
```

Versao alinhada: `0.8.0-alpha.3+20260429`
