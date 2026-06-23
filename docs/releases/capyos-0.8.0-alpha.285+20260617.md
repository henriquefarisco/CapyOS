# CapyOS 0.8.0-alpha.285+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.285+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Infra de operador -- gate VMware da Etapa 6 em um comando (recomendacao #3)

## Resumo executivo

Implementa a **recomendacao #3** da avaliacao geral, **fechando as 4 entregas**
(#2 automacao de release / #4 cadencia de auditoria em alpha.283; #1 gate de
runtime QEMU em alpha.284; #3 agora). Reduz o atrito do operador que fecha a
Etapa 6: um comando roda os dois gates externos pendentes, com pre-voo QEMU
local para nao desperdicar tempo de VMware. Sem mudanca de runtime/ABI; 6 irmaos
sem bump.

## Mudancas

- **`make etapa-6-vmware-gates SMOKE_X64_VMWARE_ARGS=...`** (novo alvo): roda, em
  sequencia, (1) o **pre-voo QEMU local** (gratis) dos dois markers
  (`smoke-x64-qemu-apps-basic-roundtrip` + `smoke-x64-qemu-capybrowse-text`),
  para que um build quebrado falhe localmente **antes** de gastar tempo de
  VMware; (2) os **gates VMware oficiais**
  (`smoke-x64-vmware-apps-basic-roundtrip` + `smoke-x64-vmware-capybrowse-text`).
  Banners por etapa + `OK` final; aborta no primeiro que falhar.
- **`make etapa-6-qemu-preflight`** (novo alvo auxiliar): so o pre-voo QEMU.
- **`ETAPA6_SKIP_QEMU_PREFLIGHT=1`**: pula o pre-voo (hosts sem QEMU).
- **`docs/operations/etapa-6-external-validation-playbook.md`**: nova secao 0
  (atalho um-comando), apontando que o `apps-basic-roundtrip` ja roda
  continuamente como gate de runtime na CI (job `qemu-apps-roundtrip`,
  alpha.284), entao o build chega ao VMware pre-validado.

## Por que importa

O gate VMware so pode ser rodado pelo operador (esta workspace e review/edit).
Antes eram duas invocacoes separadas; agora e uma, com o pre-voo QEMU acoplado
de-risca o tempo caro de VMware. VMware + UEFI + E1000 segue como **aceite
oficial**; o QEMU e o feedback rapido.

## Validacao

- `make -n etapa-6-vmware-gates ...` -- o orquestrador parseia e sequencia
  corretamente (skip-preflight -> VMware apps-roundtrip -> VMware capybrowse-text),
  sem erros de regra. Os gates subjacentes sao pre-existentes e provados (o
  `apps-basic-roundtrip` validado em runtime sob QEMU em alpha.284).
- `make version-audit` -- alinhado em `0.8.0-alpha.285` (via `make bump-alpha`).
- `make all64` default nao afetado (apenas Makefile + doc).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.284+20260617` | `0.8.0-alpha.285+20260617` | orquestrador one-command do gate VMware da Etapa 6 (#3) |

Sem mudanca de ABI. Os 6 repos irmaos permanecem inalterados (CapyUI segue 2.22.6).

_Build: `0.8.0-alpha.285+20260617`_
