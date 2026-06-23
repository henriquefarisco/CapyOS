# CapyOS 0.8.0-alpha.278+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.278+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 6 / Slice 6.6 -- apps-basic-roundtrip smoke (1o milestone: calculator), modelo in-kernel, pareado com CapyUI 2.22.3

## Resumo executivo

Primeiro milestone do gate apps-basic-roundtrip da Etapa 6. **Descoberta na
implementacao:** os apps basicos do CapyUI sao **funcoes in-kernel** (compiladas
no kernel ELF, chamam o compositor direto), **nao processos ring-3** -- o modelo
de "exit de processo + latch em `process_exit`" do design doc original era
**inviavel**. Esta release entrega o caminho correto (in-kernel) para o
`calculator`, com o wiring cross-repo CapyUI <-> CapyOS compilado + linkado.

## Mudancas

- **CapyUI 2.22.3 (pareado):** `src/apps/calculator.c::calculator_smoke_roundtrip()`
  exercita `calc_eval` de forma headless (sem GUI) e retorna 0 quando os
  resultados batem; novo `src/apps/apps_smoke.c` agrega
  (`apps_smoke_roundtrip_total`/`apps_smoke_roundtrip_run`). Superficie aditiva
  da ABI `capy-ui-desktop-session`.
- **Contrato (`include/apps/apps_smoke.h`, novo):** declaracoes CapyOS-owned
  implementadas pelo CapyUI -- `apps_smoke_roundtrip_total()` /
  `apps_smoke_roundtrip_run(index)`. `include/apps/calculator.h` ganha
  `calculator_smoke_roundtrip()`.
- **Orquestrador (`src/kernel/user_init.c::kernel_boot_run_apps_roundtrip`):**
  in-kernel (apps nao sao processos), gated `CAPYOS_APPS_ROUNDTRIP_SMOKE`, com
  branch em `kernel_main.c`. Roda cada smoke e alimenta o latch
  `apps_roundtrip_smoke` ja existente (**reusado, sem `process_exit`**), que
  emite `[smoke] apps-basic-roundtrip ready` no COM1 apos
  `APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS` (=1) passes limpos.
- **Gate (`make smoke-x64-vmware-apps-basic-roundtrip`):** novo alvo operador.
  `apps_smoke.o` (CapyUI) entra em `APPS_OBJS`.
- **Design doc corrigido** (`etapa-6-apps-roundtrip-orchestration.md`) para o
  modelo in-kernel; readiness/matrix/STATUS/master-plan sincronizados.

## Validacao

- `make test` -- verde (0 FAIL): o latch `apps_roundtrip_smoke` + a suite.
- **`make all64 PROFILE=full CAPYOS_APPS_ROUNDTRIP_SMOKE=1 EXTRA_CFLAGS64=-DCAPYOS_APPS_ROUNDTRIP_SMOKE`**
  -- **compila E LINKA** o wiring cross-repo completo (apps_smoke.c do CapyUI +
  calculator_smoke_roundtrip + o orquestrador + o latch + a branch). Esta e a
  prova de que a integracao esta correta no nivel de build.
- `make layout-audit` e `make version-audit` -- sem warnings.
- **Marker em runtime:** validado pelo gate **VMware externo** (operador), como
  o capybrowse-text. A CI default do CapyOS nao exercita o gate (orquestrador
  gated OFF; sem sibling CapyUI), entao o build default e inalterado.

## Escopo pendente

- Expandir o conjunto aos demais apps (file_manager, text_editor, settings,
  task_manager) -- a logica de cada um e parcialmente separavel; cada um ganha
  `<app>_smoke_roundtrip()` + entra no agregador + `REQUIRED_APPS` cresce.
- Gate VMware externo `make smoke-x64-vmware-apps-basic-roundtrip` (operador).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.277+20260617` | `alpha.278+20260617` | Orquestrador apps-roundtrip + contrato apps_smoke (Slice 6.6) |
| **CapyUI** | `2.22.2` | `2.22.3` | `calculator_smoke_roundtrip` + `apps_smoke` (superficie aditiva de `capy-ui-desktop-session`) |

Sem mudanca de ABI base do kernel nem de `capy-ui-widget` (v2.22 inalterado).
Os outros 5 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.278+20260617`_
