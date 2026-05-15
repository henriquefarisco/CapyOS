# CapyOS 0.8.0-alpha.226+20260514

## Entrega

`alpha.226` entrega o contrato persistente de checkpoint para migracao legacy -> header-managed.

## Antes

- `alpha.224` calculava o plano transacional read-only.
- `alpha.225` validava o plano em dry-run e recusava writes reais.
- Ainda faltava um record persistente auditavel para o executor futuro gravar no scratch antes da copia real e usar apos interrupcao.

## Agora

- `include/security/volume_provider.h` adiciona constantes de checkpoint, `struct volume_provider_rekey_checkpoint` e APIs puras:
  - `volume_provider_rekey_checkpoint_init`;
  - `volume_provider_rekey_checkpoint_serialize`;
  - `volume_provider_rekey_checkpoint_parse`.
- `src/security/volume_provider.c` implementa formato de 128 bytes:
  - campos little-endian;
  - CRC32 no offset 124 contra corrupcao acidental;
  - reserved-zero em `[88..124)` para expansao futura;
  - flags `IN_PROGRESS` e `COMPLETED`;
  - validacao semantica de plano `READY`, copia reversa, source/target/scratch, blockers zero, fases permitidas por progresso e `blocks_completed`.
- `tests/test_volume_provider_execute.c` cobre roundtrip, checkpoint completo, tamper de CRC, reserved-zero fail-closed, fases invalidas e rejeicao de plano/progresso/range invalido.

## Impacto

- Usuario final: nenhuma migracao destrutiva e executada; volumes legacy continuam montando via fallback.
- Sistema: o executor write-enabled futuro ganha contrato persistente para decidir resume, rollback ou abort apos power loss.
- Seguranca: checkpoints corrompidos, incompletos, com reserved bytes nao-zero ou progresso incoerente falham fechados e zeram o output.
- Escalabilidade: progresso em blocos e proximo source/target LBA permitem batching/checkpoint sem depender de estado em memoria.

## Proximo passo

`alpha.227` deve implementar o executor write-enabled que grava este checkpoint no scratch, aplica copy/re-encrypt reverso, faz rollback/abort seguro e comita o header somente no final.

## Validacao recomendada fora desta maquina

- `make test`
- `make layout-audit`
- `make all64`
