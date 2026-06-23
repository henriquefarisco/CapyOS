# CapyOS 0.8.0-alpha.279+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.279+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 6 / Slice 6.6 -- apps-basic-roundtrip expandido ao 2o app (task_manager), pareado com CapyUI 2.22.4

## Resumo executivo

Segundo milestone do gate apps-basic-roundtrip da Etapa 6: o conjunto de smoke
roundtrips in-kernel cresce do `calculator` para o `task_manager`. Mantem-se o
modelo correto (apps sao funcoes in-kernel, nao processos ring-3) entregue em
alpha.278. Esta release tambem **endurece** o orquestrador contra drift entre o
`total()` reportado pelo CapyUI (runtime) e o `REQUIRED_APPS` do CapyOS
(compile-time), eliminando uma classe de falso-positivo do latch count-to-N.

## Mudancas

- **CapyUI 2.22.4 (pareado):** `src/apps/task_manager.c::task_manager_smoke_roundtrip()`
  exercita a enumeracao `task_iter` / `process_iter` de forma headless (sem
  janela/compositor) -- a mesma que alimenta as abas TASKS/PROCESSES -- e retorna
  0 num snapshot sao (`>= 1` task viva, process count nao-negativo). O agregador
  `src/apps/apps_smoke.c` passa a `apps_smoke_roundtrip_total()==2` e
  `apps_smoke_roundtrip_run(1)->task_manager_smoke_roundtrip()`. Superficie
  aditiva da ABI `capy-ui-desktop-session`.
- **Contrato (`include/apps/task_manager.h`):** ganha a declaracao
  `task_manager_smoke_roundtrip()`, junto da `calculator_smoke_roundtrip()`
  ja existente, sob o contrato `include/apps/apps_smoke.h`.
- **Orquestrador (`src/kernel/user_init.c::kernel_boot_run_apps_roundtrip`):**
  nova **guarda** `total() == APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS`. O latch puro
  dispara no N-esimo exit limpo (N = `REQUIRED_APPS`, compile-time); como o
  numero real de apps vem de `total()` (CapyUI, runtime), um descasamento faria
  o marker disparar cedo (`REQUIRED < total` -> falso-positivo: um app posterior
  falhando ja nao retrataria) ou nunca (`REQUIRED > total`). A guarda recusa
  rodar em drift (o gate falha por timeout, sem marker) em vez de falso-positivo;
  com igualdade, o marker so dispara se **todos** os `total()` apps saem limpos.
- **Gate (`make smoke-x64-vmware-apps-basic-roundtrip`):** `REQUIRED_APPS`
  sobe de 1 para 2 (`-DAPPS_ROUNDTRIP_SMOKE_REQUIRED_APPS=2`), casando com
  `apps_smoke_roundtrip_total()`.

## Validacao

- `make test` -- verde (0 FAIL): o latch `apps_roundtrip_smoke` + a suite.
- `make layout-audit` e `make version-audit` -- sem warnings.
- **`make all64 PROFILE=full CAPYOS_APPS_ROUNDTRIP_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_APPS_ROUNDTRIP_SMOKE -DAPPS_ROUNDTRIP_SMOKE_REQUIRED_APPS=2'`**
  -- **compila E LINKA** o wiring cross-repo completo (`task_manager_smoke_roundtrip`
  + `apps_smoke.c` do CapyUI + `calculator_smoke_roundtrip` + o orquestrador com
  a guarda + o latch). Build clean (apos `make clean`), exit 0, zero warnings.
- **Marker em runtime:** validado pelo gate **VMware externo** (operador), como
  o capybrowse-text. A CI default do CapyOS nao exercita o gate (orquestrador
  gated OFF; sem sibling CapyUI), entao o build default e inalterado.

## Escopo pendente

- Expandir o conjunto aos demais apps: `file_manager`, `text_editor`, `settings`
  -- estes sao mais acoplados ao GUI (mutam estado + invalidam a janela), entao
  exigem extracao headless cuidadosa; cada um ganha `<app>_smoke_roundtrip()`,
  entra no agregador e `REQUIRED_APPS` cresce em lockstep.
- Gate VMware externo `make smoke-x64-vmware-apps-basic-roundtrip` (operador).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.278+20260617` | `alpha.279+20260617` | task_manager no apps-roundtrip + guarda de drift do orquestrador (Slice 6.6) |
| **CapyUI** | `2.22.3` | `2.22.4` | `task_manager_smoke_roundtrip` + `apps_smoke` total()=2 (superficie aditiva de `capy-ui-desktop-session`) |

Sem mudanca de ABI base do kernel nem de `capy-ui-widget` (v2.22 inalterado).
Os outros 5 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.279+20260617`_
