# CapyOS 0.8.0-alpha.231+20260514

## Entrega

`alpha.231` entrega recovery operacional para a migracao legacy -> header-managed.

## Antes

- `alpha.228` preparava checkpoint, header alvo Argon2id e manifest no scratch.
- `alpha.229` copiava/recriptografava blocos em ordem reversa.
- `alpha.230` comitava LBA0 por ultimo e marcava checkpoint `COMPLETED` apos abertura header-managed verificada.
- Ainda faltavam abort/rollback operacional antes do commit e limpeza verificada do scratch apos commit.

## Agora

- `include/security/volume_provider.h` adiciona:
  - `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_ROLLBACK_STEP`;
  - `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_CLEANUP_SCRATCH`;
  - statuses `ROLLBACK_*` e `CLEANUP_SCRATCH_*`;
  - `volume_provider_rekey_execute_rollback_step`;
  - `volume_provider_rekey_execute_cleanup_scratch`.
- `src/security/volume_provider_rekey_recovery.c` implementa rollback/abort guardado:
  - aceita somente flag explicita;
  - valida plano, scratch, checkpoint, manifest e header staged;
  - rejeita rollback se `COMMIT_HEADER`/`VERIFY_OPEN` ja apareceram no checkpoint;
  - valida header staged Argon2id e offset alvo;
  - restaura exatamente um bloco por chamada do dominio header-managed alvo para o dominio legacy;
  - verifica plaintext restaurado por read-back no dominio legacy;
  - decrementa checkpoint+manifest de forma verificada;
  - zera e verifica o scratch quando o abort chega a zero blocos migrados.
- `volume_provider_rekey_execute_cleanup_scratch` implementa limpeza pos-commit:
  - exige flag explicita;
  - confirma que LBA0 e header-managed;
  - abre o volume por `volume_provider_open`;
  - valida superbloco CAPYFS;
  - calcula o scratch pela geometria migrada;
  - rejeita scratch estranho ou incompleto;
  - zera e verifica o scratch.
- `Makefile` liga `volume_provider_rekey_recovery.c` no kernel x86_64 e nos testes host-side.
- `tests/test_runner.c` chama a nova suite.

## Testes adicionados

`tests/test_volume_provider_rekey_recovery.c` cobre:

- recusa de rollback por flag incorreta;
- recusa de cleanup por flag incorreta;
- rollback parcial com decremento de checkpoint;
- abort completo com scratch zerado e abertura legacy ainda funcional;
- cleanup pos-commit com scratch zerado;
- falha de write durante cleanup com status dedicado.

## Impacto

- Usuario final: a migracao cifrada agora tem caminho de recuperacao antes e depois do commit, reduzindo risco operacional de interrupcoes e aborts manuais.
- Seguranca: recovery continua fail-closed, sem downgrade apos header-managed, sem logs de segredos, com chaves e buffers limpos via wipe volatil.
- Performance/escalabilidade: rollback opera em um bloco por chamada, preservando latencia previsivel e retomada incremental em volumes grandes.
- Estrutura: recovery foi isolado em modulo proprio para manter os executores anteriores pequenos e auditaveis.

## Proximo passo

`alpha.232` deve implementar o orquestrador automatico da migracao completa: detectar estado atual, escolher proximo passo seguro, retomar apos interrupcao e acionar cleanup/rollback conforme politica.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
