# CapyOS 0.8.0-alpha.9+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Auth/F6** focado em confiabilidade,
seguranca e compatibilidade do ciclo de vida de usuarios. Depois da correcao da
criacao via Settings em alpha.8, o backend `userdb` e o caminho CLI `add-user`
foram alinhados ao fluxo robusto de IDs, home e contexto VFS de sistema; o
recovery passou a reutilizar os mesmos macros de UID/GID regulares.

A versao alinhada e `0.8.0-alpha.9+20260509`.

## Principais entregas

### IDs regulares centralizados

- `include/auth/user.h` agora declara `USER_UID_FIRST_REGULAR` e
  `USER_GID_FIRST_REGULAR` como base `1000`.
- `userdb_next_ids()` garante `/etc/users.db` antes de iterar usuarios.
- Bancos vazios passam a reservar UID/GID regulares a partir de `1000`, evitando
  criacao acidental de contas comuns com UID/GID `0`.

### CLI `add-user` alinhado ao fluxo robusto

- `add-user` chama `userdb_ensure()` antes de procurar duplicados e reservar IDs.
- As operacoes de banco (`ensure/find/next_ids/add`) rodam em sessao VFS de
  sistema, como no Settings UI.
- A criacao de `/home/<user>` usa `user_home_prepare()` em vez de helper local
  duplicado.
- Preferencias iniciais sao gravadas no mesmo contexto privilegiado.
- `struct user_record` e limpo no encerramento do fluxo.

### Recovery sem literais duplicados

- O reset do administrador de recovery passa a usar os mesmos macros de UID/GID
  regular em vez de duplicar `1000`.

## Arquivos de maior impacto

- `include/auth/user.h`
- `src/auth/user.c`
- `src/shell/commands/user_manage.c`
- `src/shell/commands/system_control/service_helpers.c`
- `include/core/version.h`
- `VERSION.yaml`
- `docs/plans/STATUS.md`
- `docs/plans/active/capyos-master-plan.md`

## Compatibilidade

- O comando publico permanece `add-user <username> <password> [role]`.
- A politica de roles continua aceitando `user` e `admin`.
- A base permanece em `/etc/users.db`.
- Homes continuam em `/home/<user>`.

## Validacao

Nesta sessao, a validacao foi feita por revisao estatica de codigo e
documentacao. Nao foram executados `make`, `git`, scripts de build, scripts de
teste ou automacao Python de validacao.

Pontos revisados estaticamente:

- `userdb_next_ids()` nao retorna mais UID/GID zero para banco vazio.
- `userdb_next_ids()` chama `userdb_ensure()` antes de `iterate_users()`.
- `add-user` restaura a sessao ativa no bloco `done` e limpa o registro local.
- `add-user` nao usa mais helper local duplicado de VFS para criar homes.
- `VERSION.yaml`, `include/core/version.h`, `README.md`, release note, STATUS e
  master plan declaram `0.8.0-alpha.9+20260509`.

## Status do pacote

- ✅ Backend de IDs regulares consolidado.
- ✅ Backend/CLI/UI alinhados no fluxo robusto de criacao de usuarios.
- ✅ Documentacao e manifesto de versao alinhados para fechamento manual.

## Proximos passos

- Operador pode executar comandos `git` manualmente para registrar o patch.
- Validacoes executaveis ficam para operador/CI, conforme as restricoes desta
  sessao.
