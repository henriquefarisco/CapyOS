# CapyOS 0.8.0-alpha.228+20260514

## Entrega

`alpha.228` entrega o staging criptografico do header alvo para a migracao legacy -> header-managed.

## Antes

- `alpha.226` definia o checkpoint persistente.
- `alpha.227` gravava e verificava somente o checkpoint no scratch.
- A futura copia/re-encrypt ainda nao tinha uma identidade criptografica de destino persistida: se o sistema perdesse energia depois de iniciar uma copia futura sem header alvo duravel, nao haveria contrato suficiente para retomar ou abortar com seguranca.

## Agora

- `include/security/volume_provider.h` adiciona:
  - `VOLUME_PROVIDER_REKEY_EXEC_FLAG_ALLOW_STAGED_HEADER_WRITE`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITTEN`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_BUILD_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_WRITE_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_STATUS_STAGED_HEADER_VERIFY_FAILED`;
  - `VOLUME_PROVIDER_REKEY_EXEC_PHASE_STAGE_HEADER`;
  - `struct volume_provider_rekey_stage_manifest`;
  - `volume_provider_rekey_stage_manifest_serialize`;
  - `volume_provider_rekey_stage_manifest_parse`;
  - `volume_provider_rekey_execute_stage_header`.
- `src/security/volume_provider_rekey_execute.c` agora persiste em um unico bloco scratch:
  - checkpoint alpha.226 em `offset 0`;
  - header alvo Argon2id em `offset 512`;
  - manifest de staging em `offset 1024`.
- O header alvo usa salt fresco do CSPRNG, Argon2id, `kdf_check_tag` HMAC-SHA256 e CRC32 finalizado.
- O manifest registra offsets, tamanhos, ranges source/target, scratch LBA, total de blocos e CRCs independentes do checkpoint e do header.
- O executor verifica por read-back:
  - parse do checkpoint;
  - parse do header;
  - verificacao do `kdf_check_tag` com as chaves derivadas;
  - parse do manifest;
  - CRCs do checkpoint e do header.
- A API ainda nao copia blocos, nao recriptografa dados e nao comita LBA0.

## Testes adicionados

`tests/test_volume_provider_rekey_execute.c` agora cobre:

- staging bem-sucedido com checkpoint/header/manifest parseaveis;
- header alvo Argon2id com `data_offset_lba = 1`;
- CRCs do manifest batendo com checkpoint/header;
- preservacao do source block;
- recusa por flag incorreta com `WRITES_DISABLED`;
- blocked-by-plan;
- no-op para volume ja header-managed;
- falha de escrita;
- falha de verificacao;
- senha errada fail-closed com `out` zerado;
- roundtrip do manifest e rejeicao fail-closed de CRC/reserved tamper.

## Impacto

- Usuario final: ainda nao ha migracao destrutiva automatica; volumes legacy continuam montando pelo caminho compatível atual.
- Seguranca: a identidade criptografica de destino passa a existir de forma persistente e verificavel antes de qualquer futura copia reversa.
- Recuperabilidade: o scratch agora contem informacao suficiente para uma proxima etapa decidir com mais seguranca entre continuar, abortar ou rollback.
- Estrutura: o executor cresce no modulo dedicado `volume_provider_rekey_execute.c`, mantendo `volume_provider.c` no limite de tamanho.

## Proximo passo

`alpha.229` deve implementar copy/re-encrypt reverso usando o header alvo ja persistido no scratch, atualizando checkpoint de progresso antes de qualquer commit de LBA0.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
