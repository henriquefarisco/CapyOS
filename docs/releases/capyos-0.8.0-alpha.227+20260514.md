# CapyOS 0.8.0-alpha.227+20260514

## Entrega

`alpha.227` entrega a primeira etapa write-enabled guardada da migracao legacy -> header-managed: gravacao e verificacao do checkpoint no scratch.

## Antes

- `alpha.224` gerava um plano transacional read-only.
- `alpha.225` executava apenas dry-run e recusava writes reais.
- `alpha.226` definia o record persistente de checkpoint, mas ainda nao havia caller que gravasse o record no bloco scratch.

## Agora

- `include/security/volume_provider.h` adiciona:
  - `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_SCRATCH_CHECKPOINT_WRITE`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITTEN`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_WRITE_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_CHECKPOINT_VERIFY_FAILED`;
  - `volume_provider_rekey_execute_checkpoint`.
- Novo `src/security/volume_provider_rekey_execute.c` mantem `volume_provider.c` no limite de 900 linhas e implementa a primeira escrita guardada:
  - aceita somente a flag explicita de checkpoint;
  - recusa qualquer outra flag com `WRITES_DISABLED` antes de senha/planejamento;
  - consome o planner alpha.224;
  - cria checkpoint alpha.226 com `blocks_completed = 0`;
  - grava somente o bloco scratch;
  - le o bloco de volta;
  - parseia e compara semanticamente o checkpoint;
  - nao copia blocos do filesystem;
  - nao recriptografa dados;
  - nao comita header.
- Novo `tests/test_volume_provider_rekey_execute.c` cobre sucesso com preservacao do source block, recusa sem flag, plano bloqueado, no-op moderno, falha de escrita, falha de verificacao e senha errada fail-closed.
- `Makefile` e `tests/test_runner.c` conectam o novo modulo e a nova suite host-side.

## Impacto

- Usuario final: ainda nao ha migracao destrutiva automatica; volumes legacy continuam seguros no fallback atual.
- Sistema: o executor futuro agora tem um ponto persistente real e verificado antes de iniciar copy/re-encrypt reverso.
- Seguranca: a unica escrita permitida e o checkpoint em scratch, com flag explicita, read-back verification e status de falha separado para write/verify.
- Escalabilidade: a separacao do modulo permite crescer o executor por fases sem ultrapassar o limite de tamanho de `volume_provider.c`.

## Proximo passo

`alpha.228` deve implementar copy/re-encrypt reverso usando o checkpoint ja persistido, com atualizacao incremental de progresso e sem commit do header ate a validacao final.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
