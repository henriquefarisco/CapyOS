# CapyOS 0.8.0-alpha.252+20260521 — Audit fix pós-Slice 3E.5

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — scaffolded; aguarda execução externa do gate `smoke-x64-vmware-storage-resilience` com esta build

## Resumo executivo

Após o scaffolding completo de Slice 3E (alphas 246-251), uma
**revisão crítica** dos 6 sub-slices identificou dois bugs
**críticos** antes da execução externa do gate. Esta alpha
entrega as correções com tests de regressão.

Nenhum bug afeta o gate externo de Slice 3E.5 funcionalmente (o
`markers_in_order` tolera repetição e o NVMe reset escalation é
um caminho raro), mas ambos violam contratos documentados e
seriam armadilhas em produção real.

## Bugs corrigidos

### BUG #1 — Storage smoke marker double-emission em VMs dual-storage

**Severidade:** crítica (contract violation, false positive em harness).

**Sintoma:** Em uma VM com AHCI **e** NVMe ambos presentes, o
marker `[smoke] storage-stack ready` era emitido **duas vezes**
no COM1 — uma quando AHCI completava a primeira operação OK,
outra quando NVMe completava a primeira OK.

**Causa raíz:** Cada driver carregava sua própria
`storage_smoke_state` em escopo de arquivo (file-static):

```c
// alpha.250 (broken)
// ahci.c
static struct storage_smoke_state g_ahci_smoke_state;
// nvme.c
static struct storage_smoke_state g_nvme_smoke_state;
```

Os latches `emitted=1` eram **independentes**. Cada um podia
transicionar blocked→observed independentemente.

**Violação:** `include/drivers/storage/storage_smoke.h:9-21` documenta:

> The state is **latched so subsequent transitions never re-emit**.

E o playbook (`docs/operations/etapa-3-slice-3e-validation-playbook.md` §1):

> O kernel emite `[smoke] storage-stack ready` em COM1 **exatamente uma vez** por boot.

**Por que o gate ainda passava:** `tools/scripts/smoke_x64_vmware.py`
usa `markers_in_order` (em `smoke_marker_policy.py`), que aceita
markers repetidos. Mas parsers downstream que assumem 1×
quebram silenciosamente.

**Fix:** Adicionado um **latch global single-source-of-truth** em
`@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/storage_smoke.c`:

```c
static struct storage_smoke_state g_global_state;

void storage_smoke_global_reset(void) {
    storage_smoke_state_reset(&g_global_state);
}

int storage_smoke_try_latch_global(enum storage_smoke_source source) {
    return storage_smoke_observe(&g_global_state, source);
}
```

Drivers consomem o latch global via:

```c
// ahci.c
static void ahci_smoke_signal_ok(void) {
  if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI)) {
    storage_smoke_emit_marker();
  }
}
// nvme.c — simétrico com STORAGE_SMOKE_SRC_NVME
```

Os antigos `g_ahci_smoke_state` e `g_nvme_smoke_state` foram
**removidos** dos drivers. Agora um único estado serve toda a
storage stack.

### BUG #2 — NVMe Controller Level Reset não recriava I/O queues

**Severidade:** crítica (recovery path effectively non-functional).

**Sintoma:** Após qualquer TIMEOUT em NVMe, o reset escalation
disparado por `block_device_ops.reset` toggava CC.EN=0 → CC.EN=1
mas não reemitia `Create I/O Submission Queue` / `Create I/O
Completion Queue`. O retry subsequente pendurava (controller sem
mapping de I/O queue 1), gerava segundo TIMEOUT, e a retry loop
queimava o budget surfacing PERMANENT.

**Causa raíz:** O comentário no código alpha.248 dizia
literalmente:

> we keep the same static buffers (`g_admin_sq`, `g_admin_cq`, `g_io_sq`, `g_io_cq`)
> and only need to re-init the phase tracker.

Confundia **DRAM buffers** (que sobrevivem ao CC.EN toggle)
com **registração no controlador** (que não sobrevive).

**Violação de spec:** NVMe Base Spec 2.0 §3.5.4 e NVMe 1.4 §7.3.1:

> The Controller Reset clears the Controller Configuration register
> and resets the controller to an idle state. ... Pending commands,
> including queues to-be-created, are discarded. The host shall
> re-initialize the controller after Controller Reset.

**Fix:** Em
`@/Users/t808981/Desktop/PR/CapyOS/src/drivers/nvme/nvme.c::nvme_controller_reset`,
após o re-prime dos phase trackers:

```c
/* alpha.252 audit fix BUG #2: recreate I/O queues. The controller
 * discarded them when CC.EN dropped; without these the next I/O
 * command would pend forever and burn the retry budget. */
if (nvme_create_io_cq(dev, 1) != 0) {
  dbg_puts("[nvme] controller reset: failed to recreate I/O CQ\n");
  return -1;
}
if (nvme_create_io_sq(dev, 1, 1) != 0) {
  dbg_puts("[nvme] controller reset: failed to recreate I/O SQ\n");
  return -1;
}
```

Admin queues sobrevivem porque o registrador AQA (Admin Queue
Attributes) é preservado através do CC.EN toggle; só toccaríamos
AQA explicitamente em uma reset mais agressivo (NSSR). O caminho
admin permanece usável para emitir os próprios comandos Create
I/O Queue.

## Cobertura de teste

### Novos host tests (BUG #1)

`@/Users/t808981/Desktop/PR/CapyOS/tests/drivers/test_storage_smoke_gate.c`
ganha 4 cases:

| Test | Validação |
|---|---|
| `global_latch_ahci_then_nvme` | AHCI primeiro latcha; NVMe e AHCI subsequentes NÃO re-emitem |
| `global_latch_nvme_then_ahci` | NVMe primeiro latcha; AHCI subsequente NÃO re-emite |
| `global_latch_reset_isolates_tests` | `storage_smoke_global_reset` permite fresh latch |
| `global_latch_rejects_invalid_source` | Source inválida não consome o latch; AHCI válida depois ainda latcha |

Total no arquivo: **13 cases** (9 originais + 4 novos).

### BUG #2

Sem unit test direto — requer NVMe MMIO mock fora do escopo
desta correção. Coberto por:

1. Inspeção estática (a função agora chama explicitamente
   `nvme_create_io_cq` + `nvme_create_io_sq`).
2. Execução externa do gate `smoke-x64-vmware-storage-resilience`
   em VMware oficial, que exercerá o caminho real.

Follow-up sugerido: extrair `nvme_controller_reset` em sub-passos
puros para permitir host testing com MMIO mock (Slice 3F ou
posterior).

## Arquivos tocados

| Arquivo | Mudança |
|---|---|
| `include/drivers/storage/storage_smoke.h` | +API `storage_smoke_try_latch_global` + `storage_smoke_global_reset` |
| `src/drivers/storage/storage_smoke.c` | +`g_global_state` + 2 funções novas |
| `src/drivers/storage/ahci.c` | −`g_ahci_smoke_state`, helper usa latch global |
| `src/drivers/nvme/nvme.c` | −`g_nvme_smoke_state`, helper usa latch global; `nvme_controller_reset` reemite Create I/O CQ/SQ |
| `tests/drivers/test_storage_smoke_gate.c` | +4 cases de regressão |

**Não tocados (intencionalmente):**
- `tests/stubs/stub_storage_smoke_io.c` — stub continua válido.
- `Makefile` — wiring de teste já incluía `storage_smoke.c`.
- `docs/operations/etapa-3-slice-3e-validation-playbook.md` — contrato continua o mesmo do ponto de vista do operador externo.

## Mudanças de contrato

**Nenhuma cross-repo.** Pura correção interna:

- API pública nova (`storage_smoke_try_latch_global`,
  `storage_smoke_global_reset`) é aditiva.
- Comportamento observável externamente (marker no COM1) **fica
  estritamente melhor**: 1× exato em vez de potencialmente 2×.
- O contrato de `block_device_ops.reset` continua o mesmo
  (retorna 0 se device usable, -1 caso contrário).

## Próximos passos

### Imediato

Operador externo executa em VMware oficial **com a build alpha.252**:

```bash
make smoke-x64-vmware-storage-resilience \
  SMOKE_X64_VMWARE_ARGS="--vmx /path/to/capyos-vmware.vmx \
                         --serial-log /path/to/serial.log \
                         --timeout 240"
```

Com a correção do BUG #1, o operador deve ver **exatamente um**
`[smoke] storage-stack ready` no COM1, mesmo em VMs dual-storage.

Após aprovação, decidir fechamento formal da Etapa 3 ou
continuação com Slices 3F-3J.

### Médio prazo

1. **Slice 3F** — Device manager unificado (DMA API + IRQ
   routing + ownership); pré-requisito para multi-table AHCI
   dispatch concorrente.
2. **Sub-slice 3E.4.B** — Migração mecânica de `dbg_puts → klog`
   em ahci.c e nvme.c (~50 sites).
3. **Sub-slice 3E.5.B** — Extrair `nvme_controller_reset` em
   passos puros para permitir unit test do BUG #2 fix.

Conforme `@/Users/t808981/Desktop/PR/CapyOS/docs/architecture/etapa-3-slice-3e-plan.md`.

## Documentação operacional adicional

Como follow-up do audit, foi criado o documento canônico
`@/Users/t808981/Desktop/PR/CapyOS/docs/architecture/smoke-marker-pattern.md`
que formaliza o pattern de smoke markers no projeto:

- **9 seções:** definição, invariantes obrigatórios, anti-pattern
  documentado (BUG #1), regras de escopo de latch (per-state vs
  global), template completo para adicionar um novo marker,
  checklist de revisão para PRs, manifest dos markers ativos,
  histórico de evolução e referências cruzadas.
- **Objetivo:** prevenir reincidência da classe de bug do
  BUG #1 (cross-driver latch leakage) em smoke markers futuros
  (e.g., Slice 3F device manager, Slice 3G fallback policy).
- **Audiência:** autores de drivers que adicionam markers, e
  revisores que validam PRs com markers novos.

Este documento não exige novo alpha bump porque é docs-only e
não muda código ou contrato runtime.
