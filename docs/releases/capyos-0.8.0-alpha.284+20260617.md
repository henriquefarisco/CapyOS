# CapyOS 0.8.0-alpha.284+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.284+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Infra de CI -- gate de runtime QEMU (recomendacao #1)

## Resumo executivo

Implementa a **recomendacao #1** da avaliacao geral: fecha a lacuna entre
"CI verde" e "funciona em runtime". A CI passa a **executar** o marker de boot do
apps-basic-roundtrip sob QEMU+OVMF, nao so compilar/linkar. Esta release tambem
produz a **primeira validacao de runtime** do roundtrip dos 5 apps (antes so o
link era exercitado em alpha.278-281). Sem mudanca de runtime/ABI; 6 irmaos sem
bump.

## Mudancas

- **`tools/scripts/smoke_x64_qemu_marker.py`** (novo): driver generico de smoke
  QEMU+OVMF -- provisiona o disco a partir dos artefatos, boota e espera um
  `--marker` no log serial (com `--fail-marker`, `--timeout`, tail no erro).
  Marker-only (sem servidor HTTP / sem NIC), reusando o harness
  `smoke_x64_session`/`smoke_x64_common` do driver capybrowse. Serve markers
  in-kernel que disparam pre-login.
- **`make smoke-x64-qemu-apps-basic-roundtrip`** (novo alvo): espelho QEMU do
  alvo VMware -- build gatado (`PROFILE=full CAPYOS_APPS_ROUNDTRIP_SMOKE=1`,
  `REQUIRED_APPS=5`) -> `iso-uefi` -> `manifest64` -> driver com o marker
  `[smoke] apps-basic-roundtrip ready` (timeout 300s; TCG).
- **`.github/workflows/ci.yml`**: novo job **`qemu-apps-roundtrip`** (reusa o
  setup qemu+ovmf+sibling do job `qemu-smoke`). **Advisory** no rollout
  (`continue-on-error: true`) ate a estabilidade no TCG ser observada; depois e
  promovido a check obrigatorio removendo o `continue-on-error`.

## Por que importa

Os dois bugs latentes recentes (`task_manager` exigindo >=1 task, alpha.281; o
`p_vaddr` do ELF loader, alpha.282) passaram na CI link-only. Um gate de runtime
QEMU teria pego o primeiro automaticamente. Como o marker so dispara quando os
**5** apps saem limpos (latch `REQUIRED_APPS=5` + guarda `total()==REQUIRED_APPS`
do orquestrador), o gate e estruturalmente discriminante: um app quebrado ->
sem marker -> timeout -> falha.

VMware + UEFI + E1000 continua sendo o **aceite oficial**; o QEMU e o feedback
rapido que bloqueia merge (nao substitui o VMware).

## Validacao

- **No host (QEMU+OVMF):** `make smoke-x64-qemu-apps-basic-roundtrip` -- o kernel
  gatado bootou e o orquestrador emitiu `[smoke] apps-basic-roundtrip ready`
  (`VALIDATE_RC=0`). **Primeira prova de runtime do roundtrip dos 5 apps**;
  confirma retroativamente o Slice 6.6 e o fix do task_manager (alpha.281).
- `make version-audit` -- alinhado em `0.8.0-alpha.284` (rodado pelo
  `make bump-alpha` que cortou esta release).
- `make all64` default nao afetado (mudancas sao scripts + Makefile + CI).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.283+20260617` | `0.8.0-alpha.284+20260617` | gate de runtime QEMU na CI (#1) |

Sem mudanca de ABI. Os 6 repos irmaos permanecem inalterados (CapyUI segue 2.22.6).

_Build: `0.8.0-alpha.284+20260617`_
