# CapyOS - Plano Mestre de Robustez com Rastreamento

Data de referencia: 2026-04-23
Horizonte: 12 meses
Plataforma primaria: `VMware + UEFI + E1000`
Marco inicial: release alpha robusta

Este documento e o plano vivo de robustez do CapyOS. Ele consolida o plano de
execucao, performance, seguranca, rede, navegacao, clean code e organizacao do
codigo em uma matriz rastreavel.

## Regras de status

Cada item deve usar exatamente um destes estados:

- `Ainda nao iniciado`
- `Parcial`
- `Implementado`
- `Bloqueado`
- `Substituido`

Regras de atualizacao:

- `Implementado` exige evidencia minima em teste, smoke, build, arquivo ou
  release note.
- `Parcial` deve registrar o que ja existe e o que falta no campo `Proximo
  passo`.
- `Bloqueado` deve registrar o motivo e o desbloqueio esperado.
- Toda mudanca relevante em robustez, rede, seguranca, performance, browser,
  processos ou organizacao deve atualizar este documento.

## Comandos de validacao

Validacao minima para alteracoes comuns:

```bash
make test
make layout-audit
make all64
```

Validacao de release robusta:

```bash
make all64 TOOLCHAIN64=elf
make iso-uefi
make boot-perf-baseline-selftest
make smoke-x64-boot-perf
make boot-perf-baseline BOOT_PERF_LOG=build/ci/smoke_x64_boot_perf.boot1.log
make smoke-x64-cli
make smoke-x64-iso
```

Observacao inicial:

- `make test`, `make layout-audit`, `python3 tools/scripts/check_deps.py` e
  `make all64` passaram em auditoria local de 2026-04-23.
- Em 2026-04-23, apos iniciar M2.1, `make test`, `make all64` e
  `make layout-audit` passaram com o default de nova instalacao ajustado para
  `network_mode=dhcp`.
- Em 2026-04-23, apos iniciar M2.3/M2.4, `make test`, `make all64` e
  `make layout-audit` passaram com retry DHCP em `networkd` e diagnostico de
  lease/erro exposto em comandos de rede.
- Em 2026-04-23, M2.2 passou a registrar no `klog` sucesso ou falha do DHCP no
  bootstrap; `make test`, `make all64` e `make layout-audit` passaram.
- Em 2026-04-23, `make layout-audit` virou gate estrito de clean code e
  anti-monolitos, com `make layout-audit-report` preservado para relatorio
  informativo; `make layout-audit`, `make layout-audit-report` e `make test`
  passaram.
- `make all64 TOOLCHAIN64=elf` falhava em 2026-04-23 porque BearSSL inclui
  headers libc padrao; isso foi corrigido com shims freestanding em
  `include/string.h` e `include/stdlib.h`.
- Em 2026-04-23, `make all64 TOOLCHAIN64=elf` passou com
  `-fstack-protector-strong`; `make all64` tambem passou apos troca de
  toolchain, validando limpeza automatica dos artefatos x64 ao alternar entre
  `host` e `elf`.
- Em 2026-04-23, `make release-check` passou executando `check-toolchain`
  com `TOOLCHAIN64=elf`, `make test`, `make layout-audit`,
  `make all64 TOOLCHAIN64=elf` e `make iso-uefi TOOLCHAIN64=elf`.
- Em 2026-04-23, `make release-check` passou tambem gerando
  `build/release-artifacts.sha256` e validando `capyos64.bin`,
  `uefi_loader.efi`, `manifest.bin`, `boot_config.bin` e a ISO UEFI com
  `sha256sum -c`.
- Em 2026-04-23, `make version-audit` passou validando alinhamento entre
  `VERSION.yaml`, `include/core/version.h`, `README.md`, screenshots
  versionados e release note.
- Em 2026-04-24, a versao alpha foi elevada para `0.8.0-alpha.2+20260424`
  com release note `docs/releases/capyos-0.8.0-alpha.2+20260424.md`; `make
  version-audit` passou.
- Em 2026-04-23, o `dns_cache` passou a aplicar TTL em segundos com expiracao
  por `dns_cache_tick`; `make test` cobre preservacao antes do TTL, expiracao
  apos TTL e contadores de expiracao; `make release-check` passou depois da
  mudanca.
- Em 2026-04-23, o cache HTTP do `html_viewer` recebeu limite total de memoria,
  estatisticas basicas, eviccao por pressao, rejeicao de `no-store` e teste de
  budget; `make release-check` passou depois da mudanca.
- Em 2026-04-23, `about:network` e `about:memory` foram adicionados ao
  navegador para expor metricas de DNS cache, HTTP cache e heap; `make
  release-check` passou.
- Em 2026-04-24, os comandos `perf-boot`, `perf-net`, `perf-fs` e `perf-mem`
  foram adicionados ao CapyCLI; o buffer cache passou a expor contadores de
  hits, misses, evictions, writebacks e erros; `make release-check` passou.
- Em 2026-04-24, o smoke `smoke_x64_cli.py` passou a executar `perf-boot` em
  boots com shell e `tools/scripts/check_boot_perf_baseline.py` passou a
  validar regressao percentual contra baseline JSON; `make
  boot-perf-baseline-selftest` e `make release-check` passaram.
- Em 2026-04-24, `boot_metrics_mark_login_ready()` passou a ser acionado quando
  a sessao shell fica pronta, `perf-boot` passou a exibir 6 estagios principais,
  `make smoke-x64-boot-perf` gerou `build/ci/smoke_x64_boot_perf.boot1.log` e
  `docs/performance/boot-baseline.json` foi gravado com
  `total_boot_to_login_us=5675879` e limite de regressao de 20%; `make
  release-check` passou depois da instrumentacao.
- Em 2026-04-24, `update-import-manifest` passou a gravar o catalogo local via
  escritor de runtime privilegiado controlado, preservando permissoes VFS para
  usuarios comuns; `make smoke-x64-cli SMOKE_X64_CLI_ARGS='--step-timeout 30
  --require-shell'` validou importacao de manifesto, persistencia em dois boots,
  usuario comum com desktop autostart, reboot e poweroff.
- Em 2026-04-24, o harness de smoke x64 passou a tratar desktop autostart como
  sessao valida e sair para CLI quando `--require-shell` e usado; tambem deixou
  de enviar comandos depois de reboot/poweroff. `python3 -m py_compile
  tools/scripts/smoke_x64_helpers.py tools/scripts/smoke_x64_flow.py
  tools/scripts/smoke_x64_cli.py tools/scripts/smoke_x64_boot.py
  tools/scripts/smoke_x64_iso_install.py` passou.
- Em 2026-04-24, `make release-check` passou novamente apos os ajustes de
  update/smoke, incluindo `make test`, `make layout-audit`, `make version-audit`,
  `make boot-perf-baseline-selftest`, `make all64 TOOLCHAIN64=elf`,
  `make iso-uefi TOOLCHAIN64=elf` e verificacao SHA-256 dos artefatos.
- Em 2026-04-28, M7.1 fechado: `tests/test_journal.c` valida commit/replay/abort/
  checkpoint do WAL; bug de durabilidade corrigido em `journal_commit` — superbloco
  nao era persistido apos avanco do head, fazendo re-init ignorar entradas
  comprometidas; suite completa passa.
- Em 2026-04-28, M7.3 fechado: `tests/test_capyfs_check.c` ganhou tres cenarios
  de corrupcao sintetica adicionais (overflow de layout, bit reservado zerado no
  bitmap de blocos e dirent sem terminador nulo); suite completa passa.
- Em 2026-04-28, M7.2 implementado: `mount_capyfs` agora chama
  `capyfs_journal_mount_hook` ao fim do mount com sucesso, ativando replay
  automatico apos shutdown sujo; `capyfs_journal_integration.c` ja continha o
  hook completo (init, format na primeira montagem, replay condicional via
  `journal_needs_replay`); kernel compila sem erros em `feature/robustness-continuation`.
- Em 2026-04-28, M0.5 implementado: `docs/reference/driver-support-matrix.md` criado com
  tabelas por categoria (Virtualizadores, Rede, Armazenamento, Input, Sistema) cobrindo todos
  os drivers no codigo; status Suportado/Laboratorio/Experimental/Fora-de-suporte alinhados
  com decisoes do README.md.
- Em 2026-04-28, M7.4 implementado: `capyfs_journal_integration` rastreia causa de recovery
  (NONE, WAL_REPLAY, WAL_REPLAY_FAILED, FORMAT) e reseta a cada mount_hook; campo
  `journal_recovery_cause` adicionado a `x64_kernel_recovery_status`; `recovery-status`
  imprime `journal-cause=` para distinguir tipo de recovery; `tests/test_capyfs_journal_cause.c`
  cobre 4 causas e helper de label; suite passa.
- Em 2026-04-28, M3.4 implementado: `audit_source_layout.py` ganhou
  `check_internal_boundary` que detecta includes cruzados de headers em pastas
  `internal/` entre modulos distintos; includes relativos (`../internal/`) excluidos
  por construcao; tree atual sem violacoes; `make layout-audit` passa em modo estrito.
- Em 2026-04-28, M6.2 expandiu auditoria persistente para eventos de update e
  recovery: `update_agent` emite `[update]` no klog para stage, arm, disarm,
  clear e import (sucesso e falha por repositorio); comandos de recovery emitem
  `[recovery]` para reparo de base, reset de admin e login de recuperacao;
  `tests/test_audit_events.c` cobre seis cenarios de audit via klog_flush;
  bug pre-existente de build de testes (`mkstemp` oculto pelo shim stdlib.h) foi
  corrigido em `tests/test_grub_cfg_builder.c`; `make layout-audit` e suite
  completa de testes passaram em `feature/robustness-continuation`.
- Em 2026-04-28, M7.5 foi implementado com integracao transacional entre
  `update_agent` e boot slots: `update_agent_apply_boot_slot()` ativa o slot
  inativo com versao staged, `update_agent_confirm_health()` desarma rollback
  apos health check e `update_agent_check_rollback()` reverte slot pendente e
  limpa stage; `tests/test_update_transact.c` cobre pre-condicao, apply,
  confirmacao de saude, rollback e no-op saudavel; `make test`, `make
  layout-audit`, `make all64` e commit `b0855a4` validaram a entrega.
- Em 2026-04-28, M8.3 iniciou budget por etapa no browser: estado explicito de
  budget externo por navegacao, modulo `navigation_budget.c`, degradacao em
  `safe_mode` quando CSS/imagens excedem `HV_EXTERNAL_FETCH_LIMIT` e log
  persistente `[browser]` via `klog`; `tests/html_viewer/resource_cases.inc`
  cobre esgotamento de budget e `make test` passou.
- Em 2026-04-28, M8.3 foi estendido para budget de paint/layout: cada frame
  zera `render_nodes_visited`, para em `HV_RENDER_NODE_BUDGET`, ativa
  `safe_mode`, preserva motivo de falha e registra `[browser] render node
  budget exhausted` no `klog`; a suite `html_viewer` cobre o esgotamento.
- Em 2026-04-24, M6.1 passou a aplicar politica minima de senha e lockout no
  login real: `auth_policy` valida tamanho minimo, bloqueia apos falhas
  consecutivas configuradas, registra sucesso/falha e e coberto por
  `tests/test_auth_policy.c`; `make test` validou a mudanca.
- Em 2026-04-23, `docs/plans/README.md` passou a classificar planos como
  `Ativo`, `Historico`, `Experimental` ou `Substituido`, com link no indice
  principal de documentacao; inspecao por script confirmou que todos os
  `docs/plans/*.md` estao classificados e `make layout-audit` passou.
- O build padrao ainda usa `x86_64-linux-gnu-*` e desativa stack protector,
  conforme aviso do `Makefile`; ele fica documentado como build rapido de
  desenvolvimento, enquanto `TOOLCHAIN64=elf` e a trilha endurecida.
- Em 2026-04-28, M6.5 saiu de `Ainda nao iniciado` para `Parcial`: o journal
  do CAPYFS ganhou modo autenticado (`JOURNAL_VERSION_AUTH = 2`) com tag
  HMAC-SHA256 truncada em 16 bytes embutida no bloco COMMIT, key por volume
  injetada via `journal_set_hmac_key()`/`journal_format_authenticated()`,
  defer-and-verify no replay para que dados forjados nao sejam aplicados,
  recusa explicita de replay em volume v2 sem chave configurada e cinco
  testes novos em `tests/test_journal.c` cobrindo formato, replay com chave
  correta, replay com chave errada, replay sem chave e detecao de tamper de
  payload; falta integrar a derivacao de chave por volume em
  `capyfs_journal_integration.c`.
- Em 2026-04-28, M5.4 avancou de `Parcial` para `Implementado` em sua
  primitiva: `buffer_cache_readahead()`, `buffer_cache_writeback_pass()` e
  `buffer_cache_dirty_count()` foram adicionados a `include/fs/buffer.h` e
  `src/fs/cache/buffer_cache.c`, com contadores `readaheads` e
  `writeback_passes` expostos via `perf-fs`; consumidores reais (FS,
  scheduler tick) ainda devem chamar essas APIs em proxima trilha.
- Em 2026-04-28, M5.5 ganhou primitiva generica de orcamento/cancelamento
  em `include/util/op_budget.h` + `src/util/op_budget.c`, cobrindo
  consumo, exaustao, cancelamento externo, reset e razao estavel; coberto
  por `tests/test_op_budget.c`. A integracao em rede/storage segue como
  proximo passo.
- Em 2026-04-28, M6.3 passou a oferecer API centralizada
  `include/auth/privilege.h` + `src/auth/privilege.c` com
  `privilege_user_is_admin`, `privilege_session_is_admin`,
  `privilege_active_is_admin`, `privilege_check_admin_or_self`,
  `privilege_log_denied` e `privilege_log_granted`; `add-user` e `set-pass`
  em `user_manage.c` migraram para essa API e emitem `[priv] denied`
  consistente; `tests/test_privilege.c` cobre os casos principais.
- Em 2026-04-28, M8.1 ganhou estado formal observavel:
  `html_viewer_state_name`, `html_viewer_state_transition_allowed` e
  ganchos de isolamento `html_viewer_set_isolation_ops`/heartbeat/on_fatal
  (placeholder ate processos reais existirem); `set_state` agora emite
  `[browser] suspicious state transition` no klog quando uma transicao
  invalida acontece; coberto por `test_html_viewer_state_machine_helpers`
  e `test_html_viewer_isolation_hooks` em
  `tests/html_viewer/navigation_cases.inc`.
- Em 2026-04-28, M8.3 foi estendido para a fase de parse: novo
  `HV_PARSE_NODE_BUDGET` aplicado por `hv_parse_budget_take()` dentro de
  `html_push_node` quando `hv_parse_app_get()` esta ativo; `forms_and_response.c`
  agora usa `hv_parse_locked_with_app(app, ...)` para o caminho real, enquanto
  paginas internas (about:home, error pages) ficam fora do orcamento via
  `hv_parse_locked` legado; `tests/html_viewer/resource_cases.inc` ganhou
  `test_html_viewer_parse_budget_logs_failure` validando flag, safe_mode,
  motivo e log `[browser] parse node budget exhausted`.
- Em 2026-04-28, M3.5 ganhou checklist humano em
  `docs/testing/pr-and-release-checklist.md`, indexado em
  `docs/README.md`, cobrindo gates automaticos, atualizacao do plano de
  robustez, performance, seguranca, browser, FS e documentacao.
- Em 2026-04-29, M6.3 foi promovido para `Implementado` apos auditoria
  confirmar que todos os call sites de privilegio admin no shell ja
  passam pela API centralizada (`user_manage.c` migrou e nenhum outro
  comando faz `role == "admin"`); usos remanescentes de `"admin"` em
  `recovery_storage.c` e `service_helpers.c` referem-se ao USUARIO admin
  (operacoes), nao a checagem de privilegio.
- Em 2026-04-29, M6.5 ganhou integracao de chave por volume:
  `capyfs_journal_install_root_secret()`/`clear_root_secret()`/
  `root_secret_installed()` foram adicionados ao header e `capyfs_journal_mount_hook`
  agora aceita superblock e deriva chave HMAC por volume com
  `HMAC-SHA256(root_secret, super_bytes)`; o runtime de mount em
  `kernel_volume_runtime` instala o segredo derivado da chave do volume
  ANTES de `mount_root_capyfs` para que a primeira montagem ja crie
  journal autenticado; `tests/test_capyfs_journal_cause.c` ganhou
  `test_root_secret_install_clear` e `test_authenticated_mount_with_super`.
- Em 2026-04-29, M5.4 ganhou cobertura de testes em
  `tests/test_buffer_cache_pacing.c` validando read-ahead com clamp por
  block_count, refusal de block_size errado, budget de writeback respeitando
  `max_blocks` (3 entao 100), parada na falha do backend e recuperacao
  apos retorno do backend; suite registrada em `test_runner.c`.
- Em 2026-04-29, M8.1 ganhou modo estrito opt-in:
  `html_viewer_state_strict_mode_set/enabled` em `include/apps/html_viewer.h`;
  quando ativo, transicoes invalidas em `set_state` sao escaladas para
  `HTML_VIEWER_NAV_FAILED` com motivo `Suspicious browser state transition
  (strict mode).` e disparam `on_fatal` no isolation hook; permissive mode
  permanece o default; coberto por
  `test_html_viewer_strict_transition_escalates`.
- Em 2026-04-29, versao alpha promovida para `0.8.0-alpha.3+20260429`:
  `VERSION.yaml`, `include/core/version.h`, `README.md`,
  `docs/screenshots/0.8.0-alpha.3/`, `docs/releases/capyos-0.8.0-alpha.3+20260429.md`,
  `docs/screenshots/README.md` e `docs/releases/README.md` alinhados;
  `make version-audit` passou.
- Em 2026-04-29 (segunda janela do mesmo dia), M5.5 ganhou adocao profunda:
  `struct op_budget nav_op_budget` adicionado ao `html_viewer_app` em
  `include/apps/html_viewer.h`; `src/apps/html_viewer/navigation_budget.c`
  ganhou `hv_nav_budget_reset/blocked/cancel/propagate_exhaust/reason` e
  passou a forwardar exaustao das per-phase budgets (parse/render/external)
  via `hv_nav_budget_propagate_exhaust`; `hv_*_budget_take` consultam
  `op_budget_is_blocked` antes de admitir trabalho;
  `html_viewer_cancel_navigation` faz `hv_nav_budget_cancel` antes de bumpar
  o `active_navigation_id`; coberto por
  `tests/html_viewer/resource_cases.inc::test_html_viewer_nav_budget_cancellation_aborts_phases`.
- Em 2026-04-29, M6.2 ganhou eventos `[audit]` estruturados: o modo estrito
  do navegador emite `[audit] [browser] strict-mode violation -> nav=FAILED`
  no `klog` e o teste `test_html_viewer_strict_transition_escalates`
  verifica isso; `[capyfs-journal]` agora imprime estado de autenticacao em
  pares chave=valor (`auth=on/off`, `key=derived-from-super/missing/none`,
  `replay=enabled/refused`, `format=first-mount`) em
  `src/fs/capyfs/capyfs_journal_integration.c`.
- Em 2026-04-29, M6.4 ganhou primeira fase de assinatura criptografica:
  `update_agent_apply_boot_slot_verified()` (em `update_agent_transact.c`)
  exige um digest SHA-256 hex e refuse mismatch (`-31`), digest ausente
  (`-30`) e digest mal-formado (`-32`); `payload_sha256=hex64` no manifesto
  popula `system_update_status::staged_payload_sha256`; eventos
  `[audit] [update] payload-sha256 ...` em todos os caminhos; coberto por 5
  cenarios em `tests/test_update_transact.c`.
- Em 2026-04-29, M7.2 ganhou round-trip sintetico em
  `tests/test_capyfs_journal_cause.c::test_dirty_shutdown_roundtrip_replays_only_committed`
  validando que apos crash apenas TX comitadas chegam ao disco (TX abortado
  permanece com payload pristine), e que um segundo mount pos-replay
  reporta `cause=NONE`; `capyfs_journal_release_slot()` adicionado para
  liberar slots da tabela de mounts (4 entradas).
- Em 2026-04-29, `src/services/update_agent.c` foi dividido em
  `src/services/update_agent.c` (808 linhas) + novo
  `src/services/update_agent_transact.c` (181 linhas) com fronteira
  privada em `src/services/internal/update_agent_internal.h`; layout
  audit estrito passa sem warnings.
- Em 2026-04-29, versao alpha promovida para `0.8.0-alpha.4+20260429`:
  `VERSION.yaml`, `include/core/version.h`, `README.md`,
  `docs/screenshots/0.8.0-alpha.4/`, `docs/releases/capyos-0.8.0-alpha.4+20260429.md`,
  `docs/screenshots/README.md` e `docs/releases/README.md` alinhados;
  `make version-audit` passou.

## M0 - Verdade oficial e governanca tecnica

Objetivo: unificar a fonte de verdade do projeto, a matriz suportada e os
gates de entrega.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M0.1 | Documento vivo de robustez | Parcial | Este arquivo foi criado como fonte de rastreamento | Manter atualizado em cada entrega relevante |
| M0.2 | Plataforma primaria oficial `VMware + UEFI + E1000` | Implementado | `README.md` declara VMware+E1000 como caminho oficial; `docs/plans/README.md` reclassificou `platform-hardening-plan.md` de `Ativo` para `Historico` (era trilha Hyper-V/QEMU); `docs/setup/hyper-v.md` ja continha aviso de nao-suporte; `docs/reference/driver-support-matrix.md` consolida status por driver | Manter consistente ao adicionar novos planos ou promover plataformas |
| M0.3 | Classificar planos antigos como ativo, historico, substituido ou experimental | Implementado | `docs/plans/README.md` classifica todos os planos atuais como `Ativo`, `Historico`, `Experimental` ou `Substituido`; inspecao por script confirmou cobertura de todos os `docs/plans/*.md`; `make layout-audit` passou | Manter a tabela atualizada quando planos forem criados, arquivados ou substituidos |
| M0.4 | Gates obrigatorios de merge/release | Implementado | `make release-check` orquestra `check-toolchain TOOLCHAIN64=elf`, `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`, `make all64 TOOLCHAIN64=elf`, `make iso-uefi TOOLCHAIN64=elf` e verificacao SHA-256; passou em 2026-04-24 | Estender gate futuro com smoke VMware+E1000 e assinatura de artefatos |
| M0.5 | Matriz de suporte de drivers e virtualizadores | Implementado | `docs/reference/driver-support-matrix.md` criado com tabelas por categoria (Rede, Armazenamento, Input, Sistema/Plataforma, Virtualizadores); status Suportado/Laboratorio/Experimental/Fora-de-suporte definidos e aplicados a todos os drivers presentes no codigo; alinhado com decisoes do `README.md` | Atualizar a tabela quando novos drivers forem adicionados ou promovidos de status |

## M1 - Release robusta e build reproduzivel

Objetivo: tornar a trilha `x86_64-elf-*` a base oficial de release, com
artefatos verificaveis e hardening ativo.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M1.1 | Build `TOOLCHAIN64=elf` funcional | Implementado | `include/string.h` e `include/stdlib.h` fornecem shims freestanding para BearSSL/GCC; `make all64 TOOLCHAIN64=elf` passou em 2026-04-23 | Manter `TOOLCHAIN64=elf` como gate obrigatorio de release |
| M1.2 | Stack protector ativo no build oficial | Implementado | `make all64 TOOLCHAIN64=elf` compilou com `-fstack-protector-strong` e incluiu `src/util/stack_protector.c`; build host segue avisando que desativa stack protector | Adicionar verificacao automatica futura para impedir release sem `TOOLCHAIN64=elf` |
| M1.3 | Build host apenas para desenvolvimento rapido | Implementado | `README.md` documenta `TOOLCHAIN64=elf` como build oficial endurecido e `make all64` como build rapido; `release-check` usa explicitamente `TOOLCHAIN64=elf`; `prepare-x64-toolchain` limpa artefatos ao alternar `host`/`elf` | Manter a separacao em release notes e CI |
| M1.4 | Artefatos com checksums e manifestos verificaveis | Implementado | `release-checksums` gera `build/release-artifacts.sha256` para kernel, loader UEFI, manifest, boot config e ISO; `verify-release-checksums` validou tudo via `sha256sum -c`; `make release-check` passou em 2026-04-24 | Adicionar assinatura criptografica dos checksums em M6 |
| M1.5 | Versao unica entre `VERSION.yaml`, headers e release notes | Implementado | `tools/scripts/audit_version_manifest.py` valida `VERSION.yaml`, `include/core/version.h`, `README.md`, screenshots versionados e release note; `make version-audit` passou em 2026-04-23 e entrou no `release-check` | Manter o auditor atualizado quando novos canais ou formatos de release forem adicionados |

## M2 - Rede no boot com DHCP automatico

Objetivo: novas instalacoes devem obter DHCP e IP automaticamente no boot,
mantendo fallback estatico e diagnostico claro.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M2.1 | DHCP padrao em instalacao nova | Parcial | `system_network_mode_or_default(NULL)` agora retorna `dhcp`; first-boot e fallback x64 gravam `network_mode=dhcp`; `make test`, `make all64` e `make layout-audit` passaram | Validar em smoke ISO/VMware+E1000 que nova instalacao obtem lease sem comando manual |
| M2.2 | DHCP executado durante bootstrap de rede | Parcial | `network_bootstrap_apply_settings()` chama `net_stack_dhcp_acquire()` quando o modo e `dhcp`, imprime resultado no console e registra sucesso/falha no `klog`; `make test`, `make all64` e `make layout-audit` passaram | Validar no smoke ISO/VMware+E1000 que o log persistente captura lease real e fallback |
| M2.3 | Retry DHCP em background pelo `networkd` | Parcial | `networkd` tenta DHCP com backoff quando o modo e `dhcp`, o driver esta pronto e ainda nao ha lease; `make test`, `make all64` e `make layout-audit` passaram | Validar comportamento de lease em smoke ISO/VMware+E1000 e ajustar timeout/backoff se necessario |
| M2.4 | Diagnostico de lease em `net-status` | Parcial | `net_stack_status` expoe `dhcp_lease_acquired`, `dhcp_attempts` e `dhcp_last_error`; `net-status` e `net-dump-runtime` imprimem diagnostico DHCP; `make test`, `make all64` e `make layout-audit` passaram | Validar saida em sucesso/falha real de DHCP no smoke oficial |
| M2.5 | Smoke de boot com DHCP, DNS e fetch | Parcial | `make smoke-x64-cli SMOKE_X64_CLI_ARGS='--step-timeout 30 --require-shell'` passou em QEMU/UEFI validando boot, persistencia, `network_mode=dhcp`, DNS/fetch opcionais e comandos de rede; ainda nao e VMware+E1000 obrigatorio | Tornar DHCP automatico criterio obrigatorio no smoke oficial VMware+E1000 |

## M3 - Clean code, anti-monolitos e organizacao

Objetivo: impedir crescimento desorganizado e manter modulos segregados por
responsabilidade.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M3.1 | Regra de tamanho maximo para novos arquivos | Implementado | `tools/scripts/audit_source_layout.py` aplica limite de 900 linhas para C/runtime/testes com excecao documentada para dados estaticos; `make layout-audit` passou em modo estrito | Revisar o limite por release quando o codigo crescer |
| M3.2 | Auditoria automatica de monolitos | Implementado | `make layout-audit` executa `audit_source_layout.py --strict` e falha com monolitos, headers internos ambiguos ou mistura suspeita; `make layout-audit-report` gera relatorio informativo; `make release-check` inclui o gate e passou | Manter excecoes documentadas e revisar limites por release |
| M3.3 | Separacao por responsabilidade em rede/browser/shell | Implementado | Todos os 9 modulos monolito migrados para TUs em `develop` (html_viewer, shell_main, input_runtime, http, kernel_volume_runtime, capyfs/runtime, system_control, system_info, uefi_loader); cada modulo tem `internal/<mod>_internal.h` como fronteira; `make layout-audit` passa em modo estrito | Manter padrao em novos modulos; M3.4 (auditoria de includes) continua pendente |
| M3.4 | Politica de includes e fronteiras internas | Implementado | `tools/scripts/audit_source_layout.py` detecta includes cruzados de headers `internal/` entre modulos distintos; inclui relativos (`../internal/`) por design (nao podem cruzar fronteiras); tree atual sem violacoes; `make layout-audit` passa em modo estrito | Manter a regra ativa em cada PR; estender com verificacao de `include/` publicos que exponham simbolos internos se necessario |
| M3.5 | Atualizacao deste plano por feature relevante | Implementado | `docs/testing/pr-and-release-checklist.md` formaliza o checklist humano que cobre atualizacao da matriz, gates automaticos, performance, seguranca, browser e FS; indexado em `docs/README.md` | Revisar o checklist a cada release e adicionar itens novos quando aparecerem dominios |

## M4 - Processos, scheduler e servicos reais

Objetivo: sair de loops cooperativos acoplados ao shell e consolidar tarefas,
jobs e userspace minimo.

A entrega "tudo + telemetria" para M4.1 ate M4.5 esta organizada em fases
incrementais. A fase 0 (observabilidade) ja esta concluida: introduz APIs
publicas para iterar `task` e `process` sem expor as tabelas privadas, expoe
snapshots seguros (`task_stats`, `process_stats`), publica o comando shell
`perf-task` e estende o `task_manager` com abas Services/Tasks/Processes. As
fases seguintes (preempcao real, isolamento, syscalls, userland minimo,
service-runner como task, telemetria CPU%/RSS e smoke QEMU) usam essa base.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M4.1 | Scheduler integrado ao runtime principal | Parcial | `src/kernel/scheduler.c` e testes de task existem; `task_iter`/`process_iter` publicos em `include/kernel/task_iter.h` e `include/kernel/process_iter.h` permitem observar a tabela de tasks sem expor o storage privado; M4 fase 2 ratificou a seam IRQ-safe do `context_switch` (cli no topo + popfq restaurando IF do task resumido) e adicionou `tests/test_context_switch.c` (30 asserts) que cobre layout invariants do `struct task_context` (locked C/asm contract), `scheduler_pick_next` por prioridade/cooperative, `scheduler_tick` em modo cooperativo (sem context_switch, mas com despertar de sleepers e reap de zombies) e o seam `schedule()` invocando `context_switch` com os ponteiros corretos via `scheduler_block_current`/`scheduler_sleep_current` | Mover tick/preempcao real para o fluxo vivo do kernel (M4 fase 8: flip para preemptivo com smoke QEMU automatico) |
| M4.2 | Processos com isolamento minimo | Parcial | `src/kernel/process.c`, ELF loader e syscalls existem em estagio inicial; `process_iter` ja entrega snapshots (`process_stats`) com PID/PPID/UID/GID/estado/nome/rss_pages para observabilidade do task manager e do shell; M4 fase 4 introduziu o classificador puro `arch_fault_classify` (`include/arch/x86_64/fault_classify.h` + `src/arch/x86_64/fault_classify.c`) que decide panic-vs-kill por CPL+vector, com NMI/#DF/#MC sempre fatal e reservados sempre panic; `x64_exception_dispatch` agora invoca o classifier e chama `process_exit(128+vector)` quando a falta vier de Ring 3 com `process_current() != NULL`; `tests/test_fault_classify.c` (42 asserts) trava CPL boundary, kernel panic, plataforma fatal, kill table e enum contract | Aterrar a primeira falta real assim que a fase 5 entregar um binario user; estender o classifier para devolver `ARCH_FAULT_RECOVERABLE` em #PF demand-paging (M4 fase 7) |
| M4.3 | Syscalls basicas completas | Parcial | `sys_open`, `sys_close`, `sys_write` parcial existem; `sys_read`, `getcwd`, `chdir` retornam erro; M4 fase 3 lockou o contrato MSR/GDT do SYSCALL/SYSRET via `include/arch/x86_64/syscall_msr.h` (mirrored em `syscall_entry.S` e em `gdt_init`), instalou os descritores user-mode no GDT (slots 0x18 user data + 0x20 user code) que antes faltavam, e adicionou `tests/test_syscall_msr.c` (36 asserts) cobrindo enderecos MSR (Intel SDM), STAR layout, SFMASK=IF\|DF, selectors RPL=3, e access bytes do GDT (DPL=3, L bit em user code); M4 fase 3.5 fechou a fundacao: `include/arch/x86_64/cpu_local.h` + `src/arch/x86_64/cpu/cpu_local.c` declaram a per-CPU area (kernel_rsp em 0x00, user_rsp_scratch em 0x08) acessada por `%gs:offset` e gravam IA32_GS_BASE no boot; `syscall_entry.S` agora referencia os offsets simbolicos (em vez de `0x00`/`0x08` hardcoded); `src/arch/x86_64/cpu/user_mode_entry.S` expoe `enter_user_mode(rip, rsp)` que monta IRET frame `[SS=0x1B, RSP, RFLAGS=0x202, CS=0x23, RIP]` + `iretq`; `src/arch/x86_64/process_user_mode.c` valida proc/main_thread/rip/rsp antes de delegar, retornando `enum process_enter_user_mode_result`; boot wiring em `kernel_main.c` reserva 16 KiB de syscall kernel stack e chama `cpu_local_init` antes de `syscall_init`; `tests/test_cpu_local.c` (12 asserts) trava offsets, MSR addresses e contrato init; `tests/test_enter_user_mode.c` (7 asserts) usa setjmp/longjmp para validar wrapper sem violar `noreturn` | Phase 5a (capylibc skeleton) DONE 2026-04-29: split de `include/kernel/syscall_numbers.h` (asm-friendly) consumido por `kernel/syscall.h` e por `userland/lib/capylibc/*.S`; `userland/include/capylibc/capylibc.h` expoe API publica (capy_exit/getpid/getppid/write/read/yield/sleep/time); `userland/lib/capylibc/syscall_stubs.S` com 8 stubs SYSCALL e `crt0.S` com `_start` que chama main + SYS_EXIT; novo target `make capylibc` (CAPYLIBC_BUILD_DIR overridable); `tests/test_capylibc_abi.c` (60 asserts) trava todos os 41 SYS_*, layout de struct syscall_frame e SysV register ABI. Phase 5b (primeiro user binary `hello`) DONE 2026-04-29: `userland/bin/hello/main.c` (capy_write 16 bytes em fd=1, return 0); regra Makefile `make hello-elf` linkando ELF estatico; `tests/test_hello_program.c` (7 asserts) usa #include da main.c renomeada via #define para travar comportamento end-to-end. Phase 5c (embed + spawn helper) DONE 2026-04-29: refactor de `elf_loader.c` (elf_load_from_file delega para `elf_load_into_process`); `include/kernel/embedded_hello.h` + `src/kernel/embedded_hello.c` declaram os simbolos do objcopy `_binary_hello_elf_start/_end/_size` + helpers `embedded_hello_data/_size`; novo target `make hello-blob` faz objcopy do hello.elf para `.rodata` blob com `cd` para preservar prefixo de simbolo; `include/kernel/user_init.h` + `src/kernel/user_init.c` expoem `kernel_spawn_embedded_hello(struct process **out)` retornando `enum kernel_spawn_result {OK=0, BAD_ELF=-1, NO_PROCESS=-2, LOAD_FAILED=-3}`; `embedded_hello.o`, `user_init.o`, e `HELLO_BLOB_OBJ` adicionados a `CAPYOS64_OBJS`; `tests/test_user_init.c` (15 asserts) trava enum contract, bad-ELF short-circuit, happy path com (data,size) corretos e nome="hello", load failure, NULL out_proc. Phase 5d (kernel_main wiring) DONE 2026-04-29: `user_init.c` ganha `kernel_boot_run_embedded_hello()` (compose spawn + process_enter_user_mode noreturn); `kernel_main.c` inclui `kernel/user_init.h` e adiciona bloco gated por `#ifdef CAPYOS_BOOT_RUN_HELLO` apos `syscall_init` que loga + chama o helper + loga warning se o spawn voltar (caso de erro); production builds nao definem o macro entao kernel shell continua reachable; `make -n all64 CFLAGS64+=' -DCAPYOS_BOOT_RUN_HELLO'` resolve a nova dep chain; CRLF de kernel_main.c preservado. Smoke contract documentado no progress note (build com flag, capturar serial/debug, pass se "[user_init] CAPYOS_BOOT_RUN_HELLO defined;" + "hello, capyland" aparecem e "panic" nao aparece). Phase 5e (QEMU smoke harness) DONE 2026-04-29: `tools/scripts/smoke_x64_hello_user.py` (270 linhas) reusa o QEMU lifecycle existente (make_qemu_cmd, provision_disk, etc.) para bootar com `-debugcon` + polling com timeout 30s; pass se ambos os success markers (`[user_init] CAPYOS_BOOT_RUN_HELLO defined;` + `hello, capyland`) aparecem e nenhum failure marker (`panic`, `hello spawn returned`); novo target `make smoke-x64-hello-user` faz `make clean && make all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'` antes de rodar; `EXTRA_CFLAGS64` adicionado ao Makefile como ponto canonico de macro injection. Phase 6 (process lifecycle: process_destroy + cleanup) DONE 2026-04-29: `process_destroy(p)` publico em `include/kernel/process.h` + `src/kernel/process.c` (NULL/UNUSED no-op, mata main_thread via task_kill, libera address_space, zera fds, state=UNUSED, struct nao e zerada para preservar legibilidade pos-mortem); `process_wait` refatorado para usar process_destroy (single audit point); `user_init.c::kernel_spawn_embedded_hello` chama process_destroy no path KERNEL_SPAWN_LOAD_FAILED (resolve TODO da fase 5c); `tests/test_process_destroy.c` (17 asserts) cobre NULL, UNUSED, EMBRYO->UNUSED com fd cleanup verification, ZOMBIE->UNUSED, idempotencia, slot reuse com pid fresh + name fresh + AS fresh. Deferred phase 6.5+: address-space copy real no process_fork, argv/envp packing no exec, re-parent de filhos, COW marking para fork. Phase 7a (recoverable user #PF seam) DONE 2026-04-29: `arch_fault_classify` agora retorna `ARCH_FAULT_RECOVERABLE` para user-mode #PF (vector 14) com error code P=0, RSVD=0, PK=0 (precedencia documentada em `include/arch/x86_64/fault_classify.h`); `pf_error_code_is_recoverable()` privado em `fault_classify.c` documenta os bits do error code (Intel SDM Vol 3 Sec 4.7); `x64_exception_dispatch` consulta `vmm_handle_page_fault(cr2, error_code)` na RECOVERABLE com escalate logado para KILL_PROCESS se a VMM recusar; `vmm_handle_page_fault` mantem stub retornando -1 (corpo real fica para 7b sem tocar dispatcher/classifier); `tests/test_fault_classify.c` cresceu para **50/50 asserts** com nova secao PF error-code matrix (test_pf_recoverable cobre P=0 com W=0/W=1/I=1; test_pf_kill cobre P=1, P=1+W=1 (CoW deferred), RSVD=1, PK=1; test_pf_kernel_always_panics cobre kernel-mode #PF). Deferred phase 7b: corpo real de `vmm_handle_page_fault` (zero-page fill, lazy mmap, sbrk grow), registry de anonymous regions, RSS wiring. Deferred phase 7c (apos CoW): rule (5) extendida para P=1+W=1 em paginas CoW. Phase 5f (segfault smoke for phase 4 kill path) DONE 2026-04-29: `Makefile` ganha `EXTRA_USERLAND_CFLAGS` (espelha `EXTRA_CFLAGS64`); `userland/bin/hello/main.c` ganha `#ifdef CAPYOS_HELLO_FAULT` branch que escreve "before-fault\n" e dereferencia NULL; `tools/scripts/smoke_x64_hello_segfault.py` (~250 linhas) reusa todo o harness do 5e com markers de sucesso `[user_init] CAPYOS_BOOT_RUN_HELLO defined;`, `before-fault`, `[x64] User-mode fault, killing offending process`, `Page Fault` e markers de falha `panic` + `hello, capyland` (regression guard: se aparecer significa que CAPYOS_HELLO_FAULT nao propagou); `make smoke-x64-hello-segfault` faz `make clean && make all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO' EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_FAULT' && make iso-uefi manifest64 && python3 ...`. Valida end-to-end o caminho phase 4 kill + phase 7a RECOVERABLE->KILL escalate (vmm_handle_page_fault ainda stub retornando -1). `[test_hello_program]` segue 7/7 no host (HELLO_FAULT desligado em host builds). Phase 6.5 (process-tree linkage) DONE 2026-04-29: `src/kernel/process.c` ganha helpers privados `process_link_child()` e `process_unlink_child()` como **unica writer surface** para `parent`/`children`/`next_sibling`; insercao LIFO (head insertion, ordem nao contratual); `process_create` substitui `p->parent = current_proc` por `process_link_child(p, current_proc)`; `process_fork` faz `process_unlink_child(child)` + `process_link_child(child, parent)` para sobrescrever o link automatico de `process_create`; `process_destroy` ganha tree detach em DUAS etapas ANTES de qualquer payload teardown: (1) walk em `p->children` orfanando cada um (parent=NULL, ppid=0, next_sibling=NULL — POSIX re-parent para init/PID 1 fica para quando init existir, sera one-line swap), (2) `process_unlink_child(p)` para detach do parent. `include/kernel/process.h` documenta a disciplina nos 3 fields da struct. `tests/test_process_destroy.c` cresceu de 17/17 para **40/40** (5 novas funcoes, 23 asserts: test_fork_links_child, test_fork_lifo_ordering, test_destroy_unlinks_from_parent, test_destroy_orphans_children, test_orphan_destroy_safe). `Todos os testes passaram.` + layout-audit Warnings:none. Unblock: future fork() consumers (shell job control, service-runner worker spawning, eventual init), process tree mode no task_manager (ppid ja exposto via process_iter), POSIX re-parent quando init landar. Deferred phase 6.5+: real address-space copy em process_fork, argv/envp packing em process_exec. Deferred phase 6.6: process_kill nao chama process_destroy (so seta ZOMBIE + mata thread, AS + FDs freed apenas via process_wait/process_destroy direto). Phase 6.6 (zombie reaping correctness) DONE 2026-04-29: `process_kill(pid, signal)` agora grava `exit_code = 128 + (signal & 0x7F)` (POSIX-ish WTERMSIG, signals > 127 clampados a 7 bits) e **auto-reaps inline** se `parent == NULL` (orfao/root). Parented children continuam ZOMBIE para o `wait()` do parent. Novo `size_t process_reap_orphans(void)` em `include/kernel/process.h` faz sweep O(PROCESS_MAX) destruindo apenas slots ZOMBIE+parent==NULL e retorna a contagem; idempotente e safe sem zumbis. `service_runner_step()` invoca `process_reap_orphans()` ao final de cada tick e contabiliza em `g_runner_orphans_reaped_total` (novo campo `orphans_reaped_total` em `struct service_runner_stats`). `tests/test_process_destroy.c` cresceu 40/40 -> **61/61** (9 funcoes, 21 asserts cobrindo kill-orphan-reaps, kill-parented-stays-zombie, signal-clamping, unknown-pid, reap-empty, reap-skips-alive, reap-skips-parented, reap-destroys-orphans, reap-idempotent). Fecha o leak loophole do phase 5f (boot-time embedded hello segfault sem parent). Phase 7b (demand paging body) DONE 2026-04-29: novo `struct vmm_anon_region` + campos `rss_pages`/`anon_regions` em `struct vmm_address_space` (`include/memory/vmm.h`); novo arquivo `src/memory/vmm_regions.c` (host-friendly, sem inline asm) com `vmm_register_anon_region` (rejeita NULL/zero/overflow/overlap), `vmm_clear_anon_regions`, `vmm_anon_region_find`, `vmm_address_space_rss`, `vmm_current_address_space`. `src/memory/vmm.c::vmm_handle_page_fault` ganha body real: lookup current AS, find region, pmm_alloc_page, zero-fill via identity-map, vmm_map_page com flags do region, retorna 0 (escalate to kill em qualquer falha; pmm_free_page on map fail). RSS counter mantido em vmm_map_page (transition not-present->present+user bumps por 1) e vmm_unmap_page (le PTE antes de clear; decrementa se era user+present, com `> 0` guard). `src/kernel/process_iter.c` substitui `stats->rss_pages = 0` por `vmm_address_space_rss(p->address_space)`. `src/kernel/elf_loader.c::elf_load_into_process` registra 240 paginas BELOW a regiao eager de 16 paginas como anonima (stack expansion ate 1 MiB total sem comprometer); errors swallowed. Novos tests em `tests/test_vmm_anon_regions.c` (NEW, **33/33**: empty AS, register basic, register rejeita bad inputs/overlap, find across multiple, clear idempotent, NULL-safe, RSS per-AS). `test_process_iter.c` ganha assertion confirmando que rss_pages reflete `vmm_address_space->rss_pages` (bump 42, snapshot retorna 42). Makefile: `vmm_regions.o` em CAPYOS64_OBJS + `test_vmm_anon_regions.c` + `vmm_regions.c` em TEST_SRCS. layout-audit Warnings:none. .S cpp scan limpo. Limitacoes deferidas: page-fault flow real precisa QEMU smoke (deferido para par com smoke-x64-hello-segfault), heap-grow/sbrk integration aguarda syscall sbrk, CoW para phase 7c. Proximo: phase 7c (CoW classifier extension apos vmm_clone_address_space) ou phase 8 (preemptive scheduler flip + smoke QEMU automatico) |
| M4.4 | `networkd`, `logger` e `update-agent` como jobs reais | Parcial | `service_manager` existe com polling, backoff e targets; M4 fase 1 introduziu `service-runner`, kernel task observavel via `task_iter` cuja iteracao orquestra `service_manager_poll_due` + `work_queue_poll_due`; `kernel_service_poll` agora delega para `service_runner_step` mantendo a mesma cadencia cooperativa | Flip do scheduler para preemptivo na M4 fase 8 (e remocao do delegate cooperativo) para que o runner rode pelo proprio dispatch loop |
| M4.5 | Task manager com tarefas reais | Parcial | App `task_manager` agora suporta tres views (Services/Tasks/Processes) com tab strip, iteracao via `task_iter`/`process_iter`, contagem por view, selecao/scroll por view e Restart limitado a Services; comando `perf-task` no shell publica os mesmos snapshots; testes host `test_task_iter`, `test_task_stats` e `test_process_iter` (42 asserts) cobrem iteradores, ordenacao estavel, snapshots e labels | Cobrir kill-by-row em Tasks/Processes apos M4 fase 8 (fault isolation) e ligar a coluna CPU%/RSS a telemetria da fase 7 |

**Update 2026-04-30 (M4 fase 7c + 8a-8f.5 + 9-11 entregues):** todos
os 5 itens M4.1-M4.5 transitaram de "Parcial" para **Implementado** no
fechamento da fase 8 (preemptive scheduler) + fase 7c (CoW) + bloco
de consolidacao 9-11. Status detalhado por sub-fase em
`docs/plans/historical/m4-finalization-progress.md` e em
`docs/plans/STATUS.md`. A suite host adicionou 110+ asserts cobrindo
decisao CoW (`vmm_cow`), refcount de paginas (`pmm_refcount`), TSS
layout (`tss_layout`), arch sched hook, synthetic IRET frame builder
(`user_task_init`) e quantum/preemptive tick (`context_switch`). A
suite QEMU adicionou 4 smokes preemptivos (`smoke-x64-preemptive`,
`smoke-x64-preemptive-demo`, `smoke-x64-preemptive-user`,
`smoke-x64-preemptive-user-2task`) + agregador
`smoke-x64-preemptive-all`, provando end-to-end que duas tarefas em
ring 3 alternam sob a tick de 100 Hz do APIC. As "Proximo passo"
columns abaixo continuam refletindo o estado pre-2026-04-30 para
preservar a trilha historica.

## M5 - Performance mensuravel

Objetivo: medir antes de otimizar e impedir regressao de boot, FS, rede e UI.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M5.1 | Metricas de boot por estagio | Implementado | `perf-boot` expoe 6 estagios (`platform-core`, `boot-config`, `storage-probe`, `serial-console`, `input-probe`, `runtime-network`) e total ate login; `make smoke-x64-boot-perf` e `make release-check` passaram em 2026-04-24 | Refinar granularidade depois que houver historico de regressao |
| M5.2 | Comandos de diagnostico de performance | Implementado | `perf-boot`, `perf-net`, `perf-fs` e `perf-mem` foram registrados no CapyCLI e documentados em `docs/reference/cli-reference.md`; `make release-check` passou em 2026-04-24 | Expandir saida conforme novos contadores forem adicionados |
| M5.3 | Baseline de regressao de boot | Implementado | `docs/performance/boot-baseline.json` registra `total_boot_to_login_us=5675879`; `make boot-perf-baseline BOOT_PERF_LOG=build/ci/smoke_x64_boot_perf.boot1.log` validou limite de 20% | Criar baselines separados quando VMware+E1000 e Hyper-V tiverem smokes oficiais estaveis |
| M5.4 | Otimizacao de FS/cache | Implementado | `buffer_cache_stats_get` expoe validos, sujos, pinned, hits, misses, evictions, writebacks, erros, `readaheads` e `writeback_passes`; `perf-fs` imprime esses contadores; APIs `buffer_cache_readahead`, `buffer_cache_writeback_pass` e `buffer_cache_dirty_count` expostas em `include/fs/buffer.h`; `tests/test_buffer_cache_pacing.c` cobre clamp de read-ahead, mismatch de block_size, budget de writeback, parada em erro do backend e recuperacao | Conectar consumidores reais (capyfs/runtime, scheduler tick) quando M4 entregar workers; adicionar microbenchmark de regressao no smoke quando o harness oficial estiver pronto |
| M5.5 | Operacoes longas sem travar input/UI | Implementado | `include/util/op_budget.h` + `src/util/op_budget.c` introduzem primitiva generica `op_budget` com consumo, exaustao, cancelamento externo, reset e razao estavel; `tests/test_op_budget.c` cobre os caminhos. Adocao profunda no navegador: `html_viewer_app` agrega `nav_op_budget`; `hv_nav_budget_reset/blocked/cancel/propagate_exhaust` em `src/apps/html_viewer/navigation_budget.c` unificam cancelamento e propagacao de exaustao; `hv_*_budget_take` consultam `op_budget_is_blocked` e `hv_*_budget_mark_exhausted` chamam `propagate_exhaust`; `html_viewer_cancel_navigation` aciona `hv_nav_budget_cancel` antes de bumpar `active_navigation_id`; coberto por `test_html_viewer_nav_budget_cancellation_aborts_phases` (arm/cancel/refuse-all-phases/re-arm) | Estender adocao para network stack (yield em loops longos de TCP/HTTP) e storage scans quando M4 entregar workers cooperativos |

## M6 - Seguranca base

Objetivo: fortalecer autenticacao, auditoria, privilegios, build e integridade
de dados antes de ampliar apps e internet.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M6.1 | Politica de senha e lockout | Implementado | `auth_policy_validate_password` aplica tamanho minimo; `system_login` chama `auth_policy_check_allowed`, `auth_policy_record_success` e `auth_policy_record_failure`; `user_record_init` e `userdb_set_password` validam a politica; `tests/test_auth_policy.c` cobre senha minima, lockout, unlock e `auth-status`; `make test` passou | Evoluir depois para persistir contadores de lockout entre boots e politicas por perfil |
| M6.2 | Auditoria persistente de eventos sensiveis | Implementado | `klog` persistente cobre `[auth]` (login/falha/lockout), `[net]` (DHCP), `[update]` (stage/arm/disarm/clear/import + payload-sha256 com prefixo `[audit]`), `[recovery]` (reparo, reset-admin, login de recuperacao), `[priv]` (denied/granted via privilege API), `[browser]` strict mode com prefixo `[audit]` e `[capyfs-journal]` em formato chave=valor (`auth=on/off`, `key=derived-from-super/missing/none`, `replay=enabled/refused`, `format=first-mount`); `tests/test_audit_events.c`, `test_html_viewer_strict_transition_escalates`, e os cenarios de `test_update_transact.c` validam os eventos | Validar persistencia em smoke real com `/var/log/capyos_klog.txt` quando smoke VMware+E1000 estiver estavel |
| M6.3 | Privilegios centralizados | Implementado | `include/auth/privilege.h` + `src/auth/privilege.c` definem API unica (`privilege_user_is_admin`, `privilege_session_is_admin`, `privilege_active_is_admin`, `privilege_check_admin_or_self`, `privilege_log_denied`, `privilege_log_granted`); `add-user` e `set-pass` em `user_manage.c` usam essa API e emitem `[priv] denied`; auditoria 2026-04-29 confirmou que nenhum outro comando de shell faz checagem `role == "admin"` (usos remanescentes de `"admin"` em `recovery_storage.c` e `service_helpers.c` operam sobre o usuario admin, nao sobre privilegio); `tests/test_privilege.c` cobre admin/usuario/admin-or-self/sessao ativa | Estender quando novos dominios de privilegio surgirem (ex.: capabilities por servico) |
| M6.4 | Build oficial endurecido | Parcial | `make release-check` passou em 2026-04-24 com `TOOLCHAIN64=elf`, `-fstack-protector-strong`, layout estrito, version audit, baseline self-test, ISO UEFI e verificacao SHA-256; primeira fase de assinatura criptografica entregue: `update_agent_apply_boot_slot_verified()` em `src/services/update_agent_transact.c` exige digest SHA-256 hex do payload, recusa mismatch (`-31`), digest ausente (`-30`) e digest mal-formado (`-32`), com fallback transparente quando o manifesto nao declara `payload_sha256`; eventos `[audit] [update] payload-sha256 ...` em todos os caminhos; `update_agent_staged_requires_payload_verification()` indica quando a verificacao e obrigatoria; 5 cenarios em `tests/test_update_transact.c` cobrem matching/mismatch/missing/malformed/legacy | Completar assinatura ponta-a-ponta dos checksums dos artefatos de release e smoke VMware+E1000 antes de promover para `Implementado` |
| M6.5 | Integridade autenticada de metadata CAPYFS | Implementado | Journal v2 autenticado em `src/fs/journal/journal.c` (`JOURNAL_VERSION_AUTH = 2`, tag HMAC-SHA256 truncado em 16 bytes embutida no bloco COMMIT, defer-and-verify no replay, recusa de volumes v2 sem chave); integracao por volume em `src/fs/capyfs/capyfs_journal_integration.c` (`capyfs_journal_install_root_secret`, `capyfs_journal_clear_root_secret`, `capyfs_journal_root_secret_installed` e `capyfs_journal_mount_hook(dev, data_start, super_bytes, super_len)` derivando chave com `HMAC-SHA256(root_secret, super_bytes)`); runtime de mount em `arch/x86_64/kernel_volume_runtime/{public_mount_api,mount_initialize}.c` instala o segredo a partir da chave do volume ANTES de `mount_root_capyfs` para que a primeira montagem ja crie journal autenticado; `tests/test_journal.c` cobre formato, replay com chave correta, recusa por chave errada/ausente e tamper; `tests/test_capyfs_journal_cause.c` cobre `install/clear/installed` e mount autenticado com superblock | Mover a chave armazenada para fora do bloco do journal e estender HMAC para metadata fora-do-journal (bitmap, inode table, superblock) em ciclo futuro |

## M7 - CAPYFS, recovery e update transacional

Objetivo: transformar persistencia em recuperabilidade real contra queda de
energia, corrupcao e update interrompido.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M7.1 | Journal/WAL de metadata CAPYFS | Implementado | `tests/test_journal.c` valida 5 cenarios: formato e estado limpo, init apos format, commit-replay-verificacao de payload, abort-sem-replay e checkpoint-limpa-replay; bug de durabilidade corrigido em `journal_commit` (superbloco nao era persistido apos avanco do head, causando perda do journal em re-init); novo round-trip sintetico em `tests/test_capyfs_journal_cause.c::test_dirty_shutdown_roundtrip_replays_only_committed` cobre o efeito ponta-a-ponta no mem-backed device (TX1+TX2 commit + TX3 abort, dirty shutdown, replay aplica somente as committed, segundo mount reporta cause=NONE); todos os cenarios passam em `make test` | Validar em smoke real VMware+E1000 quando o harness oficial estiver estavel |
| M7.2 | Replay automatico no mount | Implementado | `mount_capyfs` chama `capyfs_journal_mount_hook(dev, super_bytes, super_len)` apos montar com sucesso; o hook inicializa o journal, formata na primeira montagem pos-upgrade, e replaya entradas pendentes se `journal_needs_replay` retornar verdadeiro; logs `[capyfs-journal] auth=on/off ...` em pares chave=valor cobrem todos os caminhos; round-trip sintetico de dirty shutdown em `tests/test_capyfs_journal_cause.c::test_dirty_shutdown_roundtrip_replays_only_committed` confirma que apos crash apenas TX comitadas chegam ao disco e que o segundo mount reporta `cause=NONE`; `capyfs_journal_release_slot()` adicionado para liberar slots da tabela de mounts (4 entradas) | Validar smoke real VMware+E1000 com shutdown sujo quando o harness oficial estiver estavel |
| M7.3 | Fsck host com corrupcao sintetica | Implementado | `tests/test_capyfs_check.c` cobre 6 cenarios: volume valido, superbloco corrompido, referencia de bloco de root fora do bitmap, data_start com overflow de layout, bit reservado zerado no bitmap e dirent sem terminador nulo; todos passam em `make test` | Adicionar cenarios de reparo ativo quando capyfs_check ganhar modo de reparo |
| M7.4 | Recovery distinguindo replay, reparo e fallback | Implementado | `capyfs_journal_integration` rastreia `g_journal_recovery_cause` (NONE, WAL_REPLAY, WAL_REPLAY_FAILED, FORMAT); resetado no inicio de cada `capyfs_journal_mount_hook`; `x64_kernel_recovery_status` expoe `journal_recovery_cause`; `recovery-status` imprime `journal-cause=wal-replay/none/first-mount-format`; `tests/test_capyfs_journal_cause.c` valida os 4 codigos de causa; `make test` e `make all64` passaram | Adicionar causa de fsck-repair quando capyfs_check ganhar modo de reparo ativo |
| M7.5 | Update transacional com rollback seguro | Implementado | `update_agent_apply_boot_slot()`, `update_agent_confirm_health()` e `update_agent_check_rollback()` integram stage/update com `boot_slot_stage`, `boot_slot_activate`, `boot_slot_confirm_health` e `boot_slot_rollback`; `update_agent_apply_boot_slot_verified()` (em `update_agent_transact.c`) acrescenta gate criptografico de SHA-256 hex sobre o payload, com retorno -30/-31/-32 e eventos `[audit] [update] payload-sha256 ...`; `tests/test_update_transact.c` cobre apply sem stage, apply com slot inativo, confirmacao de saude, rollback pendente, no-op saudavel e 5 cenarios de payload-sha256 (legacy/match/mismatch/missing/malformed); `make test`, `make layout-audit`, `make all64` e commit `b0855a4` passaram | Smoke de interrupcao real em trilha futura de release |

## M8 - Internet, navegacao e browser

Objetivo: evoluir o navegador e a internet sem permitir que paginas pesadas ou
rede instavel derrubem o sistema.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M8.1 | Browser com estado formal e falha controlada | Implementado | `html_viewer_state_name`, `html_viewer_state_transition_allowed`, `html_viewer_set_isolation_ops` (heartbeat + on_fatal) formalizam o ciclo de vida; `set_state` registra `[browser] suspicious state transition` no klog quando uma transicao invalida e detectada; modo estrito opt-in (`html_viewer_state_strict_mode_set/enabled`) escala transicoes invalidas para `HTML_VIEWER_NAV_FAILED` com motivo estavel e emite `[audit] [browser] strict-mode violation -> nav=FAILED` para log-mining; permissive mode permanece o default; `tests/html_viewer/navigation_cases.inc` cobre transicoes validas/invalidas, modo estrito (`test_html_viewer_strict_transition_escalates` valida o evento `[audit]`) e ganchos de isolamento | Conectar isolation ops ao supervisor real e habilitar strict mode globalmente em release builds quando M4 entregar processos |
| M8.2 | Isolamento por processo e watchdog | Ainda nao iniciado | Roadmap do browser lista Fase 2 aberta | Depende de M4: processos, scheduler e kill/restart seguro |
| M8.3 | Render/fetch incremental | Parcial | `navigation_budget.c` centraliza budgets por navegacao/frame; CSS/imagens de rede consomem `external_fetch_attempts`, paint consome `render_nodes_visited` e parse consome `parse_nodes_visited` (`HV_PARSE_NODE_BUDGET = 384`) via `hv_parse_budget_take`; o `nav_op_budget` no `html_viewer_app` agrega cancelamento (Esc, supervisor) e exaustao de qualquer fase em um unico canal cooperativo, com `hv_*_budget_take` consultando `op_budget_is_blocked` antes de admitir trabalho e `hv_*_budget_mark_exhausted` propagando ao `nav_op_budget`; esgotamento ativa `safe_mode`, preserva motivo em `last_error_reason` e registra `[browser] parse|external resource|render node budget exhausted` no `klog`; `tests/html_viewer/resource_cases.inc` cobre os quatro caminhos, incluindo `test_html_viewer_nav_budget_cancellation_aborts_phases` | Fatiar fetch/parse/render em etapas cooperativas com tasks reais quando o supervisor de processos (M4) existir |
| M8.4 | DNS cache com TTL e HTTP cache | Implementado | `src/net/services/dns_cache.c` aplica TTL em segundos; `tests/test_dns_cache.c` valida expiracao; `src/apps/html_viewer/common.c` limita cache HTTP a `HV_HTTP_CACHE_TOTAL_MAX`, coleta estatisticas e rejeita `no-store`; `tests/html_viewer/resource_cases.inc` valida budget e eviccao; `make release-check` passou em 2026-04-23 | Evoluir para cache persistente ou ETag/If-None-Match somente apos metricas de uso real |
| M8.5 | Telemetria de browser e rede | Implementado | `about:network` expoe DNS cache e HTTP cache; `about:memory` expoe heap e memoria do cache HTTP; `tests/html_viewer/resource_cases.inc` valida as paginas; `make release-check` passou em 2026-04-23 | Expandir com latencia por request e counters por etapa quando o pipeline incremental estiver pronto |
| M8.6 | JavaScript robusto e sandboxed | Ainda nao iniciado | Roadmap declara JS robusto como fase futura | Iniciar somente apos isolamento e budgets confiaveis |

## Criterios de aceite da primeira release robusta

- Build oficial `TOOLCHAIN64=elf` passa.
- Stack protector fica ativo na trilha de release.
- ISO UEFI e disco provisionado sao gerados com manifestos e checksums.
- Nova instalacao usa `network_mode=dhcp` por padrao.
- Boot em VMware+E1000 tenta DHCP automaticamente e nao trava em caso de falha.
- `net-status` mostra modo, driver, ready, IP, gateway, DNS e erro DHCP.
- `make test`, `make layout-audit`, `make all64`, `make iso-uefi` passam.
- Smoke oficial valida boot, login, persistencia, DHCP, DNS e `net-fetch`.
- Este documento e atualizado com evidencias antes do fechamento da release.
