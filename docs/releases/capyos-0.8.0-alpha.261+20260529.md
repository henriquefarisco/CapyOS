# CapyOS 0.8.0-alpha.261+20260529

**Data:** 2026-05-29  
**Canal:** alpha (experimental)  
**Versao:** `0.8.0-alpha.261+20260529`  
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)  
**Tipo:** bug fix de UX (provisionamento de pastas do usuario) + compliance de versoes cross-repo

> **Tag a ser criada manualmente pelo operador.** Esta nota descreve o
> estado preparado no tree; o git HEAD anterior era a tag
> `v0.8.0-alpha.258+20260523` e esta release dobra tambem o trabalho
> in-tree nao-taggeado de `alpha.259` (stack Hyper-V slices 1+2) e
> `alpha.260` (3E.4.C / 3E.5.B / Etapa 4 Fase D+E / P1 hardening).

## Resumo executivo

Corrige o bug em que as **pastas padrao de cada usuario** (`Desktop`,
`Documents`, `Personal`, `Professional`) nao apareciam no desktop nem no
file manager para o usuario primario criado durante a instalacao.

A logica de provisionamento (`user_home_prepare`, em `src/auth/user_home.c`)
ja existia e ja era chamada pelo comando `add-user` (`user_manage.c`) e
pelo recovery (`service_helpers.c`), mas o **first-boot wizard**
(`src/config/first_boot/program.c`) criava o usuario admin direto via
`user_record_init` + `userdb_add` **sem** chamar `user_home_prepare`. O
resultado: o usuario de instalacao terminava com home vazio, entao a
CapyUI `desktop.c` (que renderiza `<home>/Desktop` no wallpaper) e a
`file_manager.c` (que abre em `<home>`) nao tinham pastas para mostrar.

## Mudancas entregues

### Fix — provisionamento das pastas no first-boot wizard

- `src/config/first_boot/program.c`:
  - novo `#include "auth/user_home.h"`;
  - chamada `user_home_prepare(admin_home, admin_uid, admin_gid)` no ponto
    em que o home do admin e confirmado (logo apos `vfs_set_metadata` do
    home e antes do `config_sync_root_device()`).
- Cobre **tanto** o admin recem-criado **quanto** um admin pre-existente
  (re-run do wizard) — `user_home_prepare` e idempotente via
  `ensure_directory_with_metadata`.
- Falha e **nao-fatal**: emite o warning ja existente
  `SYS_UI_ADMIN_HOME_PERM_WARNING` e segue o fluxo (a conta continua
  utilizavel), em vez de abortar a instalacao.
- Pastas provisionadas (owner = usuario, modo `0700`): `Desktop`,
  `Documents`, `Personal`, `Professional` (lista em `src/auth/user_home.c`).

### Display (ja correto, sem mudanca)

- CapyUI `src/desktop/desktop.c` ja monta `<home>/Desktop` e o passa a
  `desktop_icons_init` — o comentario do proprio arquivo cita
  `user_home_prepare` como a fonte das pastas.
- CapyUI `src/apps/file_manager.c::fm_initial_path` ja abre em
  `user->home`, listando as 4 pastas.

### Cobertura host

- `tests/auth/test_user_home.c` ja valida que `user_home_prepare` cria as
  4 pastas (sem nova regressao introduzida).

## Compliance de versoes (cross-repo)

- **CapyOS** `0.8.0-alpha.260 -> 0.8.0-alpha.261+20260529`: `VERSION.yaml`
  (current/extended/current_summary + history), `include/core/version.h`,
  `STATUS.md`, `capyos-master-plan.md`, `README.md`,
  `compatibility-matrix.md`, addendum no `compatibility-audit-2026-05-23.md`.
- **CapyUI** `2.13.1 -> 2.19.0` (ABI `capy-ui-widget` v2.13 -> v2.19;
  display-list **schema 7 inalterado**, os minors 2.14-2.19 sao aditivos
  no estado dos advanced widgets): propagado na matriz, STATUS, pins e
  `.windsurf`.
- **Pins do CapyOS core** nos 6 sisters (`docs/compatibility.md` + READMEs
  + guias): `alpha.260 -> alpha.261`.
- Demais sisters sem mudanca de versao: CapyAgent/CapyBrowser/CapyCodecs
  `0.0.6`, CapyLang `0.1.7`, CapyBenchmark `0.0.6`.

## Mudancas de contrato

**Nenhuma.** Adapter `services/capypkg`, activation gate
`kernel/module_gate.c`, install profile schema, line-oriented manifest
format, Ed25519 descriptor scope, quotas `CAPYPKG_*` e `install_root`
scope **intactos**. O fix e interno ao CapyOS core (1 TU) e o bump do
CapyUI e aditivo (schema de display-list inalterado).

## Evidencias / validacao

Validacao **nao executada** nesta maquina (politica review/edit-only do
workspace). Gates recomendados para o operador/CI antes de promover a tag:

- `make test` — host tests (inclui `run_user_home_tests`).
- `make layout-audit` e `make version-audit`.
- `make all64 PROFILE=full` (com `../CapyUI` 2.19.0 como sibling).
- `make iso-uefi` + `make smoke-x64-vmware-etapa-4`.
- Smoke manual: instalar via wizard, logar como o usuario criado e
  confirmar que `Desktop`/`Documents`/`Personal`/`Professional` aparecem
  no file manager e que `<home>/Desktop` e renderizado no desktop.
- `make release-check`.

## Proximos passos

1. Rodar os gates externos acima fora desta maquina.
2. Criar manualmente a tag `v0.8.0-alpha.261+20260529`.
3. Etapa 4 Fase F (validacao externa em VMware) continua sendo o gate de
   fechamento da etapa ativa.
4. P0 do signer Ed25519 do CapyAgent permanece em aberto (Etapa 9).
