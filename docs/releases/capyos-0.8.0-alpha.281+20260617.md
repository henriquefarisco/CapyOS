# CapyOS 0.8.0-alpha.281+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.281+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Correcao -- smoke task_manager do apps-basic-roundtrip (Etapa 6 / Slice 6.6), pareado com CapyUI 2.22.6

## Resumo executivo

Correcao de um **bug latente de runtime** no smoke `task_manager` do
apps-basic-roundtrip, encontrado numa revisao de correcao dos 5 smokes apos o
fecho in-tree (alpha.280). O build sempre linkou; o bug so se manifestaria
quando o operador rodasse o gate VMware externo.

## O bug (introduzido em alpha.279, herdado pelo gate de 5 apps de alpha.280)

`task_manager_smoke_roundtrip()` exigia `task_manager_count_tasks() >= 1`. Mas no
ponto de smoke **pre-login** a run queue preemptiva esta **vazia**:

- o kernel boota linearmente em `kernel_main` (nao entra no idle loop noreturn do
  `scheduler_start`);
- `task_current() == NULL` e nenhuma task e registrada -- o demo que criaria
  tasks (`capyos_preemptive_demo_run`) e **no-op** sem `CAPYOS_PREEMPTIVE_DEMO`
  (ver `arch/x86_64/preemptive_boot.c`, que documenta "run queue empty");
- o build do gate define `CAPYOS_APPS_ROUNDTRIP_SMOKE`, **nao** o demo.

Logo `count_tasks() == 0` -> o smoke retornava falha -> o latch nunca atingiria
os 5 passes limpos -> o marker `[smoke] apps-basic-roundtrip ready` **nunca
dispararia** -> o gate VMware falharia por timeout. Em alpha.279-280 apenas o
**link** era validado (a CI nao roda o gate; este nao executa runtime), entao o
bug passou.

## O fix

O criterio de roundtrip do `task_manager` e que a sua funcao primaria (a
enumeracao `task_iter` / `process_iter` que sustenta as abas TASKS/PROCESSES)
**rode e termine sem crash**, com contagens sas e estaveis -- **nao** que exista
`>= 1` task. `src/apps/task_manager.c::task_manager_smoke_roundtrip()` (CapyUI)
passa a:

- exercitar as duas enumeracoes;
- exigir contagens **nao-negativas**;
- exigir **estabilidade** entre duas chamadas (pega corrupcao/instabilidade do
  iterador);
- **passar com 0 tasks/processos** (o estado real no boot).

Os outros 4 smokes ja eram **data-independent** e foram revisados como seguros:
`calculator` (aritmetica constante), `file_manager` (helpers puros de path),
`text_editor` (`handle_key` sobre buffer local) e `settings` (validador de
username) -- nenhum depende de estado de kernel no boot.

## Validacao

- `make test` -- verde (0 FAIL).
- `make layout-audit` e `make version-audit` -- sem warnings.
- **`make all64 PROFILE=full CAPYOS_APPS_ROUNDTRIP_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_APPS_ROUNDTRIP_SMOKE -DAPPS_ROUNDTRIP_SMOKE_REQUIRED_APPS=5'`**
  -- compila E LINKA (exit 0, zero warnings).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.280+20260617` | `alpha.281+20260617` | comentario do contrato `task_manager.h` + docs (fix do smoke) |
| **CapyUI** | `2.22.5` | `2.22.6` | `task_manager_smoke_roundtrip` data-independent (run queue vazia no boot) |

Sem mudanca de ABI base do kernel, de `capy-ui-widget` (v2.22) nem da superficie
de `capy-ui-desktop-session` (apenas a logica interna do smoke). Os outros 5
repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.281+20260617`_
