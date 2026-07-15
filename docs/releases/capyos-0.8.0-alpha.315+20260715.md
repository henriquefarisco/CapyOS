# CapyOS 0.8.0-alpha.315+20260715

**Data:** 2026-07-15
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.315+20260715`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** seguranca do instalador, first boot, desktop, updater e integracao CapyAI

## Resumo executivo

A release elimina a selecao automatica destrutiva do maior disco: o instalador
lista apenas alvos elegiveis, exige selecao explicita e o token `ERASE`, e
revalida identidade/geometria/layout antes do wipe. O first boot volta a ser
obrigatorio em volume sem marker autoritativo e o smoke rejeita login direto.

O desktop deixa de reutilizar objetos de smoke que chamavam `desktop_stop()` e
workers persistentes deixam de herdar o principal autenticado. Dispatch legacy
e tipado exige um snapshot sanitizado da sessao. A release coordena CapyUI
`2.24.1` e CapyAI `0.2.0`, mantendo as ABIs existentes.

O updater autentica tamanho e hash antes/depois do cache, mas apply e health
confirmation persistentes continuam recusados com `-60` ate existir boot control
A/B e rollback real. Etapa 8 permanece aberta.

## Mudancas

- Instalador UEFI: politica C99 testavel, minimo de DATA, `PathId`/`MediaId`,
  selecao explicita, confirmacao literal e plano GPT unico.
- First boot: `first-run.done` autoritativo e gravado somente apos a selecao de
  modulos; CAPYCFG oficial nao aceita senha/setup preseedado.
- Build/release: fingerprint de variante invalida kernel/userland ao trocar
  profile/CFLAGS/siblings; ISO de producao rejeita marker de smoke.
- Desktop/CapyAI: logout retorna ao login, primeiro frame protegido, worker sem
  principal herdado e dispatch scoped com snapshot sanitizado.
- CapyAI: TaskPlan v1, grants, checkpoints, audit e ferramentas tipadas de
  arquivo/app/power; capabilities restantes falham fechadas.
- Updater: download autenticado e readback; apply/health persistente continuam
  deliberadamente indisponiveis.
- Supply chain: Actions pinadas por SHA; CodeQL 4.37.0 sincronizado,
  harden-runner 2.20.0 e checkout 7.0.0.

## Validacao

- CapyAI `make release-check`, acceptance e benchmark.
- CapyUI `make validate`, package e lint da desktop-session.
- CapyOS host tests, tests CapyAI/capypkg, layout/version audits, builds full e
  core-only, ISO UEFI, smoke de instalacao e smoke CapyAI grafico.
- GitHub CI/CodeQL/Security e workflows de release acompanhados apos os pushes.

O gate VMware multi-disco com disco guard e o apply A/B persistente continuam
pendentes; nenhum deles e declarado concluido por esta release.

## Compliance de versoes

| Repo | De | Para | ABI |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.314+20260713` | `0.8.0-alpha.315+20260715` | `capyos-base` v3 / package-apply v1 inalterados |
| **CapyUI** | `2.24.0` | `2.24.1` | widget 2.22, desktop-session v1, schema 7 inalterados |
| **CapyAI** | `0.1.0` | `0.2.0` | `capy-ai-core` artifact v0 inalterado |

_Build: `0.8.0-alpha.315+20260715`_
