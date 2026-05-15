# CapyOS 0.8.0-alpha.10+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Auth/F6** focado em confiabilidade,
seguranca e consistencia de bootstrap/recovery. Depois do alinhamento UI/CLI em
alpha.9, o reset do administrador em recovery e o provisionamento automatico de
first-boot foram aproximados do mesmo fluxo robusto de criacao de usuarios.

A versao alinhada e `0.8.0-alpha.10+20260509`.

## Principais entregas

### Recovery `reset-admin` robusto

- `recovery_storage_reset_admin()` agora garante `/etc/users.db` antes de buscar
  ou gravar a conta admin.
- As operacoes sensiveis rodam em sessao VFS de sistema e restauram a sessao
  anterior no encerramento.
- Homes existentes ou novas sao preparadas por `user_home_prepare()`.
- `struct user_record` local e limpo no encerramento.

### First-boot admin sem literais de ID

- O provisionamento automatico reserva UID/GID via `userdb_next_ids()`.
- Os ultimos literais `1000` do caminho admin foram removidos em favor dos macros
  `USER_UID_FIRST_REGULAR` e `USER_GID_FIRST_REGULAR`.
- A home do novo admin e preparada pelo helper comum antes de gravar o registro.
- Registros temporarios `admin` e `verify_admin` sao limpos antes de descartar a
  senha do instalador.

## Arquivos de maior impacto

- `src/shell/commands/system_control/service_helpers.c`
- `src/shell/commands/system_control/internal/system_control_internal.h`
- `src/arch/x86_64/kernel_shell_runtime.c`
- `include/core/version.h`
- `VERSION.yaml`
- `docs/plans/STATUS.md`
- `docs/plans/active/capyos-master-plan.md`

## Compatibilidade

- O comando de recovery permanece `recovery-storage-repair reset-admin <senha>`.
- A conta padrao continua `admin` quando o instalador nao fornece outro nome.
- Homes continuam em `/home/<usuario>`.
- A base de usuarios continua `/etc/users.db`.

## Validacao

Nesta sessao, a validacao foi feita por revisao estatica de codigo e
documentacao. Nao foram executados `make`, `git`, scripts de build, scripts de
teste ou automacao Python de validacao.

Pontos revisados estaticamente:

- `recovery_storage_reset_admin()` restaura a sessao ativa no bloco `done`.
- `recovery_storage_reset_admin()` chama `userdb_ensure()` e usa
  `user_home_prepare()` para admin existente ou novo.
- O first-boot admin usa `userdb_next_ids()` e nao chama mais
  `user_home_prepare(..., 1000, 1000)` nem `user_record_init(..., 1000, 1000, ...)`.
- Os scripts temporarios usados para aplicar o patch foram removidos.
- `VERSION.yaml`, `include/core/version.h`, `README.md`, release note, STATUS e
  master plan declaram `0.8.0-alpha.10+20260509`.

## Status do pacote

- ✅ Recovery admin alinhado ao fluxo robusto.
- ✅ First-boot admin sem literais de UID/GID regular.
- ✅ Documentacao e manifesto de versao alinhados para fechamento manual.

## Proximos passos

- Operador pode executar comandos `git` manualmente para registrar o patch.
- Validacoes executaveis ficam para operador/CI, conforme as restricoes desta
  sessao.
