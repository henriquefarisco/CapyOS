# CapyOS 0.8.0-alpha.230+20260514

## Entrega

`alpha.230` entrega o commit final guardado da migracao legacy -> header-managed.

## Antes

- `alpha.228` persistia checkpoint + header alvo Argon2id + manifest no scratch.
- `alpha.229` copiava/recriptografava blocos em ordem reversa e atualizava checkpoint+manifest.
- LBA0 continuava preservado, entao o boot ainda via o volume como legacy.
- A migracao ainda nao tinha abertura final verificada pelo caminho header-managed.

## Agora

- `include/security/volume_provider.h` adiciona:
  - `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_COMMIT_HEADER`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITTEN`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_BUILD_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_WRITE_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_VERIFY_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_OPEN_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_COMMIT_HEADER_CHECKPOINT_FAILED`;
  - `volume_provider_rekey_execute_commit_header`.
- `src/security/volume_provider_rekey_commit.c` executa a etapa final guardada:
  - valida plano, scratch, checkpoint, manifest e header staged;
  - exige `blocks_completed == blocks_total` ainda em `IN_PROGRESS`;
  - rejeita commit se fases `COMMIT_HEADER`/`VERIFY_OPEN` ja aparecerem antes da hora;
  - deriva/verifica chaves do header Argon2id staged;
  - grava LBA0 por ultimo;
  - verifica read-back byte a byte;
  - abre o volume via `volume_provider_open` pelo caminho header-managed;
  - le e valida o superbloco CAPYFS no dominio novo;
  - so entao marca o checkpoint como `COMPLETED` no scratch.
- `Makefile` liga `src/security/volume_provider_rekey_commit.c` no kernel x86_64 e nos testes host-side.
- `tests/test_runner.c` chama a nova suite.

## Testes adicionados

`tests/test_volume_provider_rekey_commit.c` cobre:

- sucesso do commit final com LBA0 header-managed;
- abertura final via `volume_provider_open`;
- leitura do superbloco CAPYFS no dominio Argon2id novo;
- checkpoint final `COMPLETED`;
- recusa por flag incorreta com `WRITES_DISABLED`;
- rejeicao de copia incompleta;
- falha de escrita de LBA0;
- falha de verificacao de LBA0.

## Impacto

- Usuario final: a conversao legacy -> header-managed agora tem todas as primitivas write-enabled essenciais, incluindo commit final seguro; ainda falta o orquestrador operacional para expor isso como fluxo automatico de migracao.
- Seguranca: LBA0 so e escrito apos copia completa e validada, com header Argon2id autenticado e abertura final verificada sem fallback legacy.
- Recuperabilidade: o checkpoint so vira `COMPLETED` depois da verificacao final; interrupcoes antes disso continuam distinguiveis pelo scratch.
- Estrutura: o commit foi isolado em modulo proprio para manter `volume_provider.c`, `volume_provider_rekey_execute.c` e os testes existentes dentro do limite de tamanho.

## Proximo passo

`alpha.231` deve implementar politica operacional de rollback/abort, limpeza de scratch e retomada pos-interrupcao, fechando a migracao como fluxo automatizavel.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
