# CapyOS 0.8.0-alpha.318+20260727

**Data:** 2026-07-27
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.318+20260727`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** apply A/B persistente habilitado (staging autorizado + arm + rollback)

## Resumo executivo

Fecha o lifecycle A/B em producao. O slot inativo passa a receber o payload
verificado, o arm publica exatamente uma tentativa duravel vinculada a lease e a
geracao produzida pelo staging, o boot seguinte consome a tentativa no UEFI e o
runtime confirma saude ou reporta o rollback que o loader ja aplicou.
`update-prepare`, `update-stage`, `update-arm on`, `update-apply` e
`update-rollback-check` deixam de retornar `-60` incondicional; a partir daqui
`-60` significa exatamente uma coisa: a transicao persistente nao pode ser
provada (sem provider com flush duravel, geracao/lease obsoleta, payload nao
verificado ou commit indeterminado).

A evidencia externa do ciclo completo com payload assinado e reboot continua
sendo um gate de operador. Esta release entrega e prova a implementacao em host,
nao o aceite oficial da Etapa 8.

## Mudancas

- `boot_slot_store_arm` sai da declaracao e passa a existir: exige binding ativo
  provider-backed, lease exata, geracao exata e ausencia de staging em curso
  antes de delegar a transicao ao manager. Um commit indeterminado vira
  `BOOT_SLOT_STORE_ERR_COMMIT_UNKNOWN` e invalida a capability pelo resto do
  boot; qualquer outra recusa vira `STALE` sem tocar o disco.
- Novo planner puro `x64_storage_boot_provider_plan_stage`: escolhe o slot
  inativo a partir do snapshot, exige que o slot confirmado seja o ativo e
  saudavel, recusa attempt pendente/rollback armado, valida versao ASCII
  imprimivel, digest nao nulo e o teto de capacidade do slot alvo.
- Novas pontes de runtime `x64_storage_runtime_stage_boot_payload_sha256`,
  `_arm_boot_slot` e `_boot_rollback_check`, todas condicionadas ao provider
  registrado. O staging usa o mesmo snapshot para planejar e para autorizar, de
  modo que planner e autorizacao geracional descrevem o mesmo estado.
- O updater grava o payload que acabou de verificar: `update_agent_payload_load_verified`
  entrega o buffer ja re-hasheado contra o manifesto assinado, e o apply o
  encaminha ao store em vez de reabrir o cache num segundo passe nao verificado.
- `update-apply` faz stage e arm como dois passos explicitos. Um slot gravado
  mas nao armado e uma recusa (`-60`), nunca um sucesso: o loader continuaria
  bootando o slot confirmado.
- `update-rollback-check` passa a observar o resultado duravel: `2` quando o
  boot atual e o rollback aplicado pelo loader (e nesse caso desarma o staged
  update), `1` quando a tentativa ainda espera confirmacao, `0` quando nao ha
  nada pendente.
- `update-stage` volta a promover o catalogo verificado para `staged.ini`
  (`-49` quando o cache nao pode ser re-hasheado, `-5` sem catalogo mais novo) e
  `update-arm on` volta a armar a ativacao (`-10` sem staged update). Nenhum dos
  dois grava boot slot: so `update-apply` toca o disco.
- `update-prepare-explain` deixa de reportar o gate sintetico `persistence`
  depois de todos os gates criptograficos passarem.
- `kernel_main` valida o token de tentativa pelo mesmo validador puro que os
  host tests exercitam (`x64_storage_boot_attempt_from_handoff`), eliminando a
  duplicacao da regra de handoff v10 entre kernel e policy.
- `src/boot/boot_slot.c` foi dividido: as transicoes de lifecycle (arm,
  activate, select, confirm, rollback) passam para
  `src/boot/boot_slot_lifecycle.c` e `bs_commit_locked` vira o unico ponto de
  mutacao compartilhado pelo header interno. Kernel, loader UEFI e host tests
  compilam a nova unidade.

## Correcoes de regressao herdadas do trabalho in-tree alpha.317

- `make layout-audit` estava vermelho: `src/boot/boot_slot.c` tinha 923 linhas
  contra o limite de 900. A divisao acima resolve.
- `make test` estava vermelho: `tests/auth/test_audit_events.c` nao declarava
  `/system/update/payload.bin` no fixture, e `update_agent_clear_stage` reporta
  `-13` quando nao consegue remover o cache. O fixture agora espelha o remover
  de producao, que trata arquivo ausente como removido.

## Postura fail-closed

`-60` continua sendo a unica recusa de transicao persistente e agora e
especifico: sem provider raw 512 com read/write/flush duravel, sem binding
ESP/BOOT/DATA estrito, com lease/geracao divergente, com payload nao verificado
ou com resultado de commit indeterminado. `update-apply` sem digest
(`update_agent_apply_boot_slot`) permanece recusado por definicao — o payload so
alcanca o slot inativo pelo caminho verificado.

## Validacao

- `make test` — exit `0` ("Todos os testes passaram"), incluindo o lifecycle A/B
  ponta a ponta no harness provider-backed (stage, arm de tentativa unica,
  select que consome a tentativa, confirmacao duravel e rollback de tentativa
  nao confirmada), o planner puro de staging e os caminhos de sucesso/recusa do
  updater.
- `make layout-audit` — exit `0`, zero warnings (inclui a divisao de
  `boot_slot.c`, que estava em 923/900 linhas).
- `make version-audit` — exit `0`.
- `make all64` e `make iso-uefi` com o toolchain host padrao — exit `0`, zero
  warnings, ISO gerada.
- `make release-check` (forca `TOOLCHAIN64=elf` internamente) — exit `0`,
  incluindo suite host, layout/version audits, selftests, kernel ELF, loader
  UEFI, manifest, ISO e os cinco checksums de release verificados. ISO final
  `9ae5f60fc6a05be6c2cdc6a22508787709490f61ea5b72d8d711d4bcbd008e47`;
  `capyos64.bin` `78ea80bcd7ff05115d5c8d1188d96aa4c631e4a9037a6af9c21e3f3b96711841`.
  Nota operacional: o cross-toolchain `x86_64-elf-*` esta instalado em
  `~/cross/bin` no host WSL mas nao entra no PATH de shells nao-interativos;
  exporte-o antes de rodar o gate.

## Limites conhecidos

- O gate externo do ciclo completo (payload assinado publicado, apply, reboot,
  confirmacao e rollback em VMware UEFI/E1000) nao foi executado nesta sessao.
  `release-check` prova build, testes e checksums; nao prova update assinado.
- Os gates de instalador QEMU/VMware nao foram reexecutados apos este rebuild de
  ISO. O caminho do instalador nao foi tocado por esta release, mas os artefatos
  mudaram; recomenda-se rerun antes de promover.
- `make modules-index` nao foi reexecutado: nenhuma saida de publisher nem o
  inventario externo mudaram, entao o agregado do `alpha.317` continua valendo.
- O indice de modulos pinado em `alpha.318` ainda nao tem asset publicado, logo
  `make smoke-x64-iso-modules-net` permanece bloqueado por pre-condicao externa.
- Discos anteriores ao `alpha.317` com GUIDs duplicados seguem em modo legado
  validado apenas para mount e nunca recebem capability de update.
- `tests/services/test_update_transact.c` esta a 887/900 linhas; a proxima fatia
  de testes do updater deve dividir a unidade antes de crescer.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.317+20260720` | `0.8.0-alpha.318+20260727` | `capyos-base` v3 e handoff interno v10 inalterados; nenhuma ABI externa mudou |

CapyUI `2.24.1`, CapyAI `0.2.1`, CapyBrowser `0.6.7`, CapyAgent `0.0.10`,
CapyCodecs `0.0.12`, CapyLang `0.1.12` e CapyBenchmark `0.0.11` permanecem
inalterados.

_Build: `0.8.0-alpha.318+20260727`_
