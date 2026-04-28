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
| M3.5 | Atualizacao deste plano por feature relevante | Parcial | Este documento cria a regra | Incluir verificacao manual no checklist de PR/release |

## M4 - Processos, scheduler e servicos reais

Objetivo: sair de loops cooperativos acoplados ao shell e consolidar tarefas,
jobs e userspace minimo.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M4.1 | Scheduler integrado ao runtime principal | Parcial | `src/kernel/scheduler.c` e testes de task existem | Integrar tick/preempcao real ao fluxo vivo do kernel |
| M4.2 | Processos com isolamento minimo | Parcial | `src/kernel/process.c`, ELF loader e syscalls existem em estagio inicial | Completar address spaces, lifecycle e isolamento de falhas |
| M4.3 | Syscalls basicas completas | Parcial | `sys_open`, `sys_close`, `sys_write` parcial existem; `sys_read`, `getcwd`, `chdir` retornam erro | Implementar syscalls minimas para shell/utilitarios userland |
| M4.4 | `networkd`, `logger` e `update-agent` como jobs reais | Parcial | `service_manager` existe com polling, backoff e targets | Mover cadencia para scheduler/workers em vez de depender de loops principais |
| M4.5 | Task manager com tarefas reais | Parcial | Codigo de task/process existe e app `task_manager` compila | Exibir tabela real e estados observaveis |

## M5 - Performance mensuravel

Objetivo: medir antes de otimizar e impedir regressao de boot, FS, rede e UI.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M5.1 | Metricas de boot por estagio | Implementado | `perf-boot` expoe 6 estagios (`platform-core`, `boot-config`, `storage-probe`, `serial-console`, `input-probe`, `runtime-network`) e total ate login; `make smoke-x64-boot-perf` e `make release-check` passaram em 2026-04-24 | Refinar granularidade depois que houver historico de regressao |
| M5.2 | Comandos de diagnostico de performance | Implementado | `perf-boot`, `perf-net`, `perf-fs` e `perf-mem` foram registrados no CapyCLI e documentados em `docs/reference/cli-reference.md`; `make release-check` passou em 2026-04-24 | Expandir saida conforme novos contadores forem adicionados |
| M5.3 | Baseline de regressao de boot | Implementado | `docs/performance/boot-baseline.json` registra `total_boot_to_login_us=5675879`; `make boot-perf-baseline BOOT_PERF_LOG=build/ci/smoke_x64_boot_perf.boot1.log` validou limite de 20% | Criar baselines separados quando VMware+E1000 e Hyper-V tiverem smokes oficiais estaveis |
| M5.4 | Otimizacao de FS/cache | Parcial | `buffer_cache_stats_get` expoe validos, sujos, pinned, hits, misses, evictions, writebacks e erros; `perf-fs` imprime esses contadores | Adicionar write-back controlado e read-ahead simples apos baseline |
| M5.5 | Operacoes longas sem travar input/UI | Parcial | Browser tem mitigacoes e yield hook de rede | Generalizar budgets e cancelamento para rede/browser/storage |

## M6 - Seguranca base

Objetivo: fortalecer autenticacao, auditoria, privilegios, build e integridade
de dados antes de ampliar apps e internet.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M6.1 | Politica de senha e lockout | Implementado | `auth_policy_validate_password` aplica tamanho minimo; `system_login` chama `auth_policy_check_allowed`, `auth_policy_record_success` e `auth_policy_record_failure`; `user_record_init` e `userdb_set_password` validam a politica; `tests/test_auth_policy.c` cobre senha minima, lockout, unlock e `auth-status`; `make test` passou | Evoluir depois para persistir contadores de lockout entre boots e politicas por perfil |
| M6.2 | Auditoria persistente de eventos sensiveis | Parcial | `klog` persistente existe; `[auth]` cobre login/falha/lockout; `[net]` cobre DHCP; `[update]` cobre stage/arm/disarm/clear/import-sucesso/import-mismatch; `[recovery]` cobre reparo, reset-admin e login de recuperacao; `tests/test_audit_events.c` valida cenarios de update | Expandir privilegios (M6.3); validar persistencia em smoke real com `/var/log/capyos_klog.txt` |
| M6.3 | Privilegios centralizados | Parcial | VFS tem metadados/permissoes iniciais; `update-import-manifest` usa escritor de runtime privilegiado controlado para atualizar `/system/update/latest.ini` sem enfraquecer permissoes de usuario | Centralizar checagens para comandos e paths criticos e substituir elevacoes locais por API unica |
| M6.4 | Build oficial endurecido | Parcial | `make release-check` passou em 2026-04-24 com `TOOLCHAIN64=elf`, `-fstack-protector-strong`, layout estrito, version audit, baseline self-test, ISO UEFI e verificacao SHA-256; build host permanece apenas para desenvolvimento | Completar assinatura dos checksums e smoke VMware+E1000 antes de promover para `Implementado` |
| M6.5 | Integridade autenticada de metadata CAPYFS | Ainda nao iniciado | AES-XTS protege confidencialidade, mas integridade segue pendente | Projetar metadata autenticada e migracao de formato |

## M7 - CAPYFS, recovery e update transacional

Objetivo: transformar persistencia em recuperabilidade real contra queda de
energia, corrupcao e update interrompido.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M7.1 | Journal/WAL de metadata CAPYFS | Implementado | `tests/test_journal.c` valida 5 cenarios: formato e estado limpo, init apos format, commit-replay-verificacao de payload, abort-sem-replay e checkpoint-limpa-replay; bug de durabilidade corrigido em `journal_commit` (superbloco nao era persistido apos avanco do head, causando perda do journal em re-init); todos os cenarios passam em `make test` | Validar semantica de atomicidade em smoke real com shutdown sintetico |
| M7.2 | Replay automatico no mount | Implementado | `mount_capyfs` chama `capyfs_journal_mount_hook(dev, mnt->super.data_start)` apos montar com sucesso; o hook inicializa o journal, formata na primeira montagem pos-upgrade, e replaya entradas pendentes se `journal_needs_replay` retornar verdadeiro; logs `[capyfs-journal]` cobrem todos os caminhos; kernel compila sem erros com `TOOLCHAIN64=host` | Validar semantica de replay em smoke com shutdown sujo sintetico |
| M7.3 | Fsck host com corrupcao sintetica | Implementado | `tests/test_capyfs_check.c` cobre 6 cenarios: volume valido, superbloco corrompido, referencia de bloco de root fora do bitmap, data_start com overflow de layout, bit reservado zerado no bitmap e dirent sem terminador nulo; todos passam em `make test` | Adicionar cenarios de reparo ativo quando capyfs_check ganhar modo de reparo |
| M7.4 | Recovery distinguindo replay, reparo e fallback | Implementado | `capyfs_journal_integration` rastreia `g_journal_recovery_cause` (NONE, WAL_REPLAY, WAL_REPLAY_FAILED, FORMAT); resetado no inicio de cada `capyfs_journal_mount_hook`; `x64_kernel_recovery_status` expoe `journal_recovery_cause`; `recovery-status` imprime `journal-cause=wal-replay/none/first-mount-format`; `tests/test_capyfs_journal_cause.c` valida os 4 codigos de causa; `make test` e `make all64` passaram | Adicionar causa de fsck-repair quando capyfs_check ganhar modo de reparo ativo |
| M7.5 | Update transacional com rollback seguro | Parcial | `update-agent` e boot slots existem; `update-import-manifest` importou e persistiu catalogo local no smoke x64 completo de 2026-04-24 | Adicionar payload verificado, apply atomico e health check de rollback |

## M8 - Internet, navegacao e browser

Objetivo: evoluir o navegador e a internet sem permitir que paginas pesadas ou
rede instavel derrubem o sistema.

| ID | Item | Status | Evidencia | Proximo passo |
|---|---|---|---|---|
| M8.1 | Browser com estado formal e falha controlada | Parcial | `browser-status-roadmap.md` declara Fase 1 fechada, mas sem isolamento | Manter Fase 1 e iniciar isolamento quando processos estiverem prontos |
| M8.2 | Isolamento por processo e watchdog | Ainda nao iniciado | Roadmap do browser lista Fase 2 aberta | Depende de M4: processos, scheduler e kill/restart seguro |
| M8.3 | Render/fetch incremental | Parcial | Ha limites, cancelamento e mitigacoes de carregamento | Implementar pipeline incremental e budgets por etapa |
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
