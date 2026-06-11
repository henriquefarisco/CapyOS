# Compatibility audit 2026-06-11

**CapyOS core:** `0.8.0-alpha.265+20260611`
**Escopo:** handoff cross-repo para desbloquear Etapa 6 / Slice 6.4.

## Versoes coordenadas

| Repositorio | Versao anterior | Versao atual | Contrato |
|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.264+20260607` | `0.8.0-alpha.265+20260611` | `capyos-base` v3 + `capyos-package-apply` v1 |
| `CapyBrowser` | `0.5.0` | `0.6.0` | `capy-browser-core` v1 text subset publicado |

Sem bump nos demais repositorios: CapyUI `2.22.0`, CapyCodecs `0.0.7`,
CapyLang `0.1.8`, CapyAgent `0.0.7` e CapyBenchmark `0.0.7`.

## Handoff CapyBrowser 0.6.0

`CapyBrowser v0.6.0` e o handoff explicito para a Etapa 6 / Slice 6.4:

- `make package STAGE=text` gera o pacote `org.capyos.browser.text`;
- `depends=` fica vazio por design, porque o modo texto nao deve esperar
  codecs de imagem da Etapa 7;
- o payload publicado e o `capy-browser-core` textual: URL
  parse/normalize/origin, HTML-to-text, links e modelo deterministico de
  erros/warnings;
- CapyOS continua dono de DNS/TCP/TLS/HTTP, filesystem, sandbox, janela,
  input, render, lifecycle e gates externos.

O pacote grafico `org.capyos.browser.core` permanece Etapa 7-gated e declara
`org.capyos.codecs.image-basic` quando imagem estiver habilitada.

## Estado CapyOS alpha.265

O bloqueio anterior da Slice 6.4 era "publicacao do core externo". Esse
bloqueio foi removido. A Slice 6.4 nao esta marcada como concluida: o proximo
trabalho e implementar o adapter CapyOS-side, reusar render/scroll existente e
passar o smoke `make smoke-x64-vmware-capybrowse-text`.

## Validacao

- CapyBrowser: `make validate`, `make package STAGE=text`,
  `make package STAGE=core`.
- CapyOS: `make version-audit`, `make layout-audit`, `make test` e, antes de
  promover runtime, `make smoke-x64-vmware-capybrowse-text`.

## Arquivos sincronizados

- `CapyBrowser/VERSION`
- `CapyBrowser/Makefile`
- `CapyBrowser/docs/compatibility.md`
- `CapyBrowser/docs/roadmap.md`
- `CapyOS/VERSION.yaml`
- `CapyOS/docs/reference/integration/compatibility-matrix.md`
- `CapyOS/docs/reference/integration/browser-core-integration-contract.md`
- `CapyOS/docs/reference/integration/external-core-repositories.md`
- `CapyOS/docs/architecture/etapa-6-desktop-apps-readiness.md`
- `CapyOS/docs/plans/active/capyos-master-plan.md`
