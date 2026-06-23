# CapyOS 0.8.0-alpha.280+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.280+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 6 / Slice 6.6 -- apps-basic-roundtrip COMPLETO (5 apps), pareado com CapyUI 2.22.5

## Resumo executivo

Conclui (no nivel de codigo/build) o gate apps-basic-roundtrip da Etapa 6: o
conjunto de smoke roundtrips in-kernel passa a cobrir **os 5 apps basicos do
desktop**. Adiciona `file_manager`, `text_editor` e `settings` aos ja entregues
`calculator` (alpha.278) e `task_manager` (alpha.279), mantendo o modelo correto
in-kernel e a guarda de drift do orquestrador.

## Mudancas

- **CapyUI 2.22.5 (pareado):** tres novas funcoes headless (sem janela/compositor),
  superficie aditiva da ABI `capy-ui-desktop-session`:
  - `src/apps/file_manager.c::file_manager_smoke_roundtrip()` -- exercita os
    helpers puros de path (`fm_join_path`/`fm_path_equal`/`fm_path_inside_or_same`/
    `fm_basename`) que sustentam navegacao, move e drag-drop. Deterministico, sem
    VFS (o conteudo do FS nao e garantido no ponto de smoke pre-login).
  - `src/apps/text_editor.c::text_editor_smoke_roundtrip()` -- exercita
    `text_editor_handle_key` (insert / backspace / quebra de linha) sobre o
    singleton in-kernel `g_editor` (reusado, sem alocar 256 KiB extra;
    `text_editor_open` re-zera ao abrir, entao deixa-lo sujo aqui e seguro).
  - `src/apps/settings_actions.c::settings_smoke_roundtrip()` -- valida a politica
    de username (`settings_validate_username`: charset + comprimento, puro). As
    acoes `apply_*` persistem em `/system/config.ini` e mutam estado global, entao
    ficaram **de fora** do smoke de proposito.
  - `src/apps/apps_smoke.c`: `apps_smoke_roundtrip_total()` passa a 5; `run(2..4)`
    despacha file_manager/text_editor/settings.
- **Contrato (`include/apps/{file_manager,text_editor,settings}.h`):** cada um
  ganha a declaracao `<app>_smoke_roundtrip()`.
- **Gate (`make smoke-x64-vmware-apps-basic-roundtrip`):** `REQUIRED_APPS` sobe
  de 2 para 5, casando com `apps_smoke_roundtrip_total()`. A guarda
  `total() == APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS` do orquestrador (alpha.279)
  garante que o marker so dispara se **os 5** apps saem limpos.

## Validacao

- `make test` -- verde (0 FAIL).
- `make layout-audit` e `make version-audit` -- sem warnings.
- **`make all64 PROFILE=full CAPYOS_APPS_ROUNDTRIP_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_APPS_ROUNDTRIP_SMOKE -DAPPS_ROUNDTRIP_SMOKE_REQUIRED_APPS=5'`**
  -- **compila E LINKA** o wiring cross-repo completo: os 5 `<app>_smoke_roundtrip`
  + o agregador `apps_smoke.c` + o orquestrador (com a guarda) + o latch. Build
  clean (apos `make clean`), exit 0, zero warnings.
- **Marker em runtime:** validado pelo gate **VMware externo** (operador). A CI
  default do CapyOS nao exercita o gate (orquestrador gated OFF; sem sibling
  CapyUI), entao o build default e inalterado.

## Escopo pendente

- **Slice 6.6 (apps-basic-roundtrip) completo no nivel de codigo/build.** Resta o
  gate VMware externo `make smoke-x64-vmware-apps-basic-roundtrip` (operador) para
  o fecho do criterio na Etapa 6.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.279+20260617` | `alpha.280+20260617` | apps-roundtrip completo (5 apps): +file_manager/text_editor/settings (Slice 6.6) |
| **CapyUI** | `2.22.4` | `2.22.5` | +`file_manager`/`text_editor`/`settings` `_smoke_roundtrip`, total()=5 (superficie aditiva de `capy-ui-desktop-session`) |

Sem mudanca de ABI base do kernel nem de `capy-ui-widget` (v2.22 inalterado).
Os outros 5 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.280+20260617`_
