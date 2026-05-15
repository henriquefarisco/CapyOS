# CapyOS 0.8.0-alpha.225+20260514

## Entrega

`alpha.225` entrega o executor transacional guardado/dry-run para a migracao de volumes cifrados legacy para o layout header-managed.

## Antes

- `alpha.223` classificava volumes e bloqueava a escrita destrutiva ingenua de header sobre CAPYFS LBA0.
- `alpha.224` produzia um plano deterministico com ranges source/target, scratch, copia reversa e estimativas de I/O.
- Ainda faltava uma API de execucao que validasse o plano e expusesse a ordem transacional sem permitir writes prematuros.

## Agora

- `include/security/volume_provider.h` adiciona flags/status/fases de execucao e `struct volume_provider_rekey_execution_report`.
- `src/security/volume_provider.c::volume_provider_rekey_execute` consome o planner e retorna:
  - `ALREADY_HEADER_MANAGED` para volumes ja modernos;
  - `BLOCKED_BY_PLAN` quando shrink/scratch ainda impedem migracao;
  - `WRITES_DISABLED` antes de derivar senha/planejar para qualquer chamada sem `VOLUME_PROVIDER_REKEY_EXEC_FLAG_DRY_RUN` puro ou com flags desconhecidas;
  - `DRY_RUN_READY` com fases validate_plan, checkpoint_scratch, copy_reverse, commit_header e verify_open quando o layout esta pronto.
- Novo `tests/test_volume_provider_execute.c` cobre dry-run read-only, recusa de writes reais, flags desconhecidas, blocked-by-plan, no-op moderno e fail-closed em senha errada.
- `tests/test_runner.c` e `Makefile` conectam o novo teste ao host runner.

## Impacto

- Usuario final: nenhuma migracao destrutiva e executada; volumes legacy continuam montando via fallback.
- Sistema: existe agora uma fronteira explicita entre plano seguro e writes reais, impedindo callers futuros de aplicar migracao sem dry-run/contrato.
- Segurança: o executor falha fechado, zera `out` em erro herdado do planner e recusa writes ate haver executor write-enabled com checkpoint persistente auditado.
- Escalabilidade: o relatorio publica fases, proximo source/target LBA e estimativas de I/O para permitir batching/checkpoint no executor write-enabled futuro.

## Proximo passo

`alpha.226` deve entregar o contrato persistente de checkpoint; o executor write-enabled fica para a fatia seguinte, com gravacao do checkpoint, rollback/abort seguro e commit do header somente no final.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
