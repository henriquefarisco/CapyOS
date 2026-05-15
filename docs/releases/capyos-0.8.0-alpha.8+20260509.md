# CapyOS 0.8.0-alpha.8+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Settings/CapyUI** focado na
correcao da criacao de usuarios pela UI. O fluxo `Add user...` agora acompanha
a semantica segura do CLI: somente sessoes admin podem iniciar o fluxo, o
username e validado com a politica restrita `[A-Za-z0-9_-]`, e as operacoes de
sistema que tocam `/etc/users.db`, `/home/<user>` e preferencias iniciais sao
executadas sem herdar as permissoes VFS do usuario ativo.

A versao alinhada e `0.8.0-alpha.8+20260509`.

## Principais entregas

### Criacao de usuario via Settings corrigida

- `start_user_creation()` exige admin antes de abrir o prompt de username.
- `on_username_submit()` revalida admin, valida o nome como o CLI e consulta o
  banco de usuarios em sessao VFS de sistema.
- `on_password_submit()` revalida admin, troca para sessao VFS de sistema antes
  de `userdb_ensure()`, `userdb_find()`, `userdb_next_ids()`, `userdb_add()` e
  gravacao de preferencias iniciais.
- A criacao de `/home/<user>` foi centralizada em `user_home_prepare()` em vez de
  duplicar helpers locais no Settings.
- Falhas ao abrir prompts agora retornam status localizado na propria UI.

### Seguranca e consistencia

- A politica de permissao da UI passa a usar `privilege_user_is_admin()` e
  `privilege_log_denied()` como o restante da camada de auth.
- A UI nao depende mais do UID/GID do usuario ativo para escrever arquivos de
  sistema quando a acao ja foi autorizada por papel admin.
- O registro sensivel em memoria (`struct user_record`) e limpo no encerramento
  do callback de senha.

## Arquivos de maior impacto

- `src/apps/settings.c`
- `include/core/version.h`
- `VERSION.yaml`
- `README.md`
- `docs/plans/STATUS.md`
- `docs/plans/active/capyos-master-plan.md`

## Compatibilidade

- Mantem o fluxo visual `username -> password` existente.
- Mantem `inline_prompt_show()` e `inline_prompt_show_secret()` sem alteracao de
  contrato.
- Mantem a base de usuarios em `/etc/users.db` e o layout de home em
  `/home/<user>`.
- Mantem o papel padrao `user` para contas criadas pela UI.

## Validacao

Nesta sessao de fechamento, a validacao foi feita por revisao estatica de codigo
e documentacao. Nao foram executados `make`, `git`, scripts de build, scripts de
teste ou automacao Python de validacao.

Pontos revisados estaticamente:

- `VERSION.yaml` declara `current=0.8.0-alpha.8` e
  `extended=0.8.0-alpha.8+20260509`.
- `include/core/version.h` declara `CAPYOS_VERSION_FULL` alinhado.
- `README.md` declara `Versao de referencia: 0.8.0-alpha.8`.
- Esta release note existe em `docs/releases/` e declara o extended version no
  cabecalho.
- `docs/releases/README.md`, `docs/plans/STATUS.md`, o indice de screenshots e
  o master plan foram atualizados para refletir o fechamento do patch.

## Status do pacote

- ✅ Criacao de usuario via Settings corrigida por revisao estatica.
- ✅ Versao e documentacao alinhadas para fechamento manual do patch.
- ✅ O patch pode ser revisado e registrado manualmente pelo operador.

## Proximos passos

- Operador pode executar os comandos `git` manualmente para registrar o patch.
- A validacao executavel (`make version-audit`, build e smokes) fica para o
  operador/CI, conforme as restricoes desta sessao.
