# CapyOS 0.8.0-alpha.223+20260514

## Entrega

`alpha.223` entrega o preflight seguro/read-only para re-key e migração de volumes cifrados legacy.

## Antes

- Volumes fresh `alpha.222+` já eram header-managed: `CAPYVHDR` no LBA0, CAPYFS em LBA1+, Argon2id com salt per-install e proteção contra downgrade.
- Volumes pre-`alpha.222` continuavam legacy: CAPYFS começava no LBA0 do device cifrado inteiro, com chaves AES-XTS derivadas por PBKDF2 + `g_disk_salt`.
- A migração planejada poderia ser perigosa se implementada como “escrever header no LBA0”, porque isso destruiria o superbloco CAPYFS legacy.

## Agora

- `include/security/volume_provider.h` adiciona status/layout/action/blocker flags e `struct volume_provider_rekey_preflight`.
- `src/security/volume_provider.c::volume_provider_rekey_preflight` classifica volumes header-managed e legacy sem escrever em disco.
- O caminho legacy deriva chaves PBKDF2 com os parâmetros legados fornecidos pelo caller, lê o superbloco CAPYFS descriptografado e valida magic/version/block_size/geometria mínima.
- O preflight reporta as ações obrigatórias para a migração real: reservar LBA0 para `CAPYVHDR`, deslocar CAPYFS para LBA1, recriptografar sob chaves bound ao header e atualizar geometria quando CAPYFS consome todo o raw device.
- Falhas de NULL, block size inválido, I/O, senha errada ou plaintext sem CAPYFS válido limpam `out` e retornam erro.
- `tests/test_volume_provider.c` passa a cobrir 13 funções, incluindo moderno já header-managed, legacy full-device com shrink blocker, legacy partial sem shrink, fail-closed e prova read-only do LBA0.

## Impacto

- Usuário final: volumes existentes continuam montando pelo fallback legacy; nenhuma migração destrutiva é executada ainda.
- Sistema: a próxima entrega (`alpha.224`) tem contrato explícito para implementar o motor transacional de relocation/re-encryption com rollback/abort seguro.

## Validação recomendada fora desta máquina

- `make test`
- `make all64`
