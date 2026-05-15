# CapyOS 0.8.0-alpha.229+20260514

## Entrega

`alpha.229` entrega o primeiro copy/re-encrypt reverso incremental da migracao legacy -> header-managed.

## Antes

- `alpha.228` persistia checkpoint + header alvo Argon2id + manifest no scratch.
- A identidade criptografica do destino ja era duravel e verificavel.
- Nenhum bloco CAPYFS era copiado/recriptografado ainda.
- LBA0 permanecia intocado, corretamente sem commit final.

## Agora

- `include/security/volume_provider.h` adiciona:
  - `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COPY_STEP`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_DONE`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_COMPLETE`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_BUILD_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_READ_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_WRITE_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_VERIFY_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COPY_STEP_CHECKPOINT_FAILED`;
  - campos `blocks_completed` e `blocks_remaining` em `volume_provider_rekey_execution_report`;
  - `volume_provider_rekey_execute_copy_step`.
- `src/security/volume_provider_rekey_execute.c` agora executa uma unidade real de migracao:
  - valida o planner e o scratch de `alpha.228`;
  - parseia checkpoint, header alvo e manifest;
  - verifica CRCs do checkpoint/header staged;
  - deriva chaves PBKDF2 legacy e Argon2id alvo;
  - copia exatamente um bloco em ordem reversa;
  - verifica o plaintext lido pelo dominio criptografico alvo;
  - atualiza checkpoint + manifest somente apos verificacao;
  - regrava e verifica o scratch atualizado.
- O checkpoint agora aceita o estado intermediario `blocks_completed == blocks_total` ainda `IN_PROGRESS`, representando copia concluida sem commit de header.
- A API ainda nao comita LBA0, nao remove fallback legacy e nao declara a migracao completa.

## Testes adicionados

`tests/test_volume_provider_rekey_execute.c` agora cobre:

- um bloco copiado com sucesso e checkpoint avancando de 0 para 1;
- plaintext do bloco alvo igual ao plaintext legacy original;
- recusa por flag incorreta com `WRITES_DISABLED`;
- falha de escrita do bloco alvo;
- falha de verificacao do bloco alvo;
- conclusao do range planejado com status `COPY_STEP_COMPLETE`;
- preservacao de LBA0/commit final fora desta fatia.

## Impacto

- Usuario final: ainda nao ha conversao automatica completa; volumes legacy continuam montando pelo caminho compativel atual ate existir commit seguro.
- Seguranca: a migracao passa a mover dados reais apenas apos validar identidade criptografica alvo e com verificacao de plaintext no destino.
- Recuperabilidade: cada chamada avanca checkpoint+manifest no scratch, reduzindo o trabalho perdido apos interrupcao.
- Estrutura: o executor passa de staging para aplicador incremental, preparando commit final separado e auditavel.

## Proximo passo

`alpha.230` deve implementar commit do header por ultimo, abertura final verificada e politica explicita de rollback/abort para completar a migracao in-place.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
