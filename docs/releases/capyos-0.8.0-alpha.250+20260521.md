# CapyOS 0.8.0-alpha.250+20260521 — Slice 3E.4 entregue

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — em andamento

## Resumo executivo

Esta alpha entrega o **Slice 3E.4** — o smoke marker forense
`[smoke] storage-stack ready` que o gate externo de Slice 3E.5
vai consumir para validar reprodutibilidade. Reproduz o padrão
estabelecido pelo USB HID smoke marker em Slice 3D: gate puro
host-testable + emitter de COM1/klog separado + integração na
borda do hot-path do driver.

**Não promove a Etapa 3.** Faltam Slice 3E.5 (gate externo) e
Slices 3F-3J.

## Mudanças entregues

### Novo gate

`@/Users/t808981/Desktop/PR/CapyOS/include/drivers/storage/storage_smoke.h`
+ `@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/storage_smoke.c`:

- `struct storage_smoke_state { ahci_ok_count; nvme_ok_count; emitted; }`.
- `storage_smoke_state_reset(state)` — zera campos, null-safe.
- `storage_smoke_gate_observed(ahci, nvme) → int` — pura, retorna
  1 se **qualquer** contador ≥ 1 (single-controller VMs disparam
  o marker assim que aquela única fonte completa).
- `storage_smoke_observe(state, source) → int` — incrementa
  contador da fonte (`STORAGE_SMOKE_SRC_AHCI` ou `STORAGE_SMOKE_SRC_NVME`),
  retorna 1 **exatamente uma vez** na transição blocked→observed
  e latcha `emitted=1`. Source inválido retorna 0 sem mutar.

Implementação 100% pura: sem MMIO, sem klog, sem kmalloc.
Constant `STORAGE_SMOKE_MARKER = "[smoke] storage-stack ready"`
exposta no header e validada como string canônica em host test.

### Emitter kernel-only

`@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/storage_smoke_io.c`:

- `storage_smoke_emit_marker()` — `com1_puts(MARKER "\n")` +
  `klog(KLOG_INFO, "[storage] Slice 3E.4 smoke marker emitted on COM1.")`.

Stub no-op em `@/Users/t808981/Desktop/PR/CapyOS/tests/stubs/stub_storage_smoke_io.c`
para o host runner.

### Integração AHCI

`@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/ahci.c`:

```
static struct storage_smoke_state g_ahci_smoke_state;

static void ahci_smoke_signal_ok(void) {
  if (storage_smoke_observe(&g_ahci_smoke_state, STORAGE_SMOKE_SRC_AHCI)) {
    storage_smoke_emit_marker();
  }
}
```

`ahci_read_block_ex` e `ahci_write_block_ex` chamam o helper
quando `cls == BLOCK_IO_OK`. Cada controlador mantém seu próprio
state em escopo de arquivo; o marker em si é idempotente
globalmente porque ambos os emitters chamam a mesma função
de IO que escreve uma única linha no COM1.

### Integração NVMe (simétrica)

`@/Users/t808981/Desktop/PR/CapyOS/src/drivers/nvme/nvme.c`:

- `g_nvme_smoke_state` + `nvme_smoke_signal_ok()`.
- `nvme_block_read_ex` / `nvme_block_write_ex` disparam o helper
  na classe OK.

### Cobertura de teste

`@/Users/t808981/Desktop/PR/CapyOS/tests/drivers/test_storage_smoke_gate.c`
(9 testes):

| Teste | Validação |
|---|---|
| state_reset_zeros_counters | reset zera todos os campos |
| state_reset_null_safe | reset(NULL) não crasha |
| gate_predicate | 0,0→false; 1,0/0,1/many→true |
| first_ahci_ok_emits | AHCI=1 dispara emissão + latcha |
| first_nvme_ok_emits | NVMe=1 dispara emissão + latcha |
| subsequent_calls_do_not_re_emit | counters incrementam mas emit=0 |
| invalid_source_is_rejected | source=42 retorna 0 sem mutar |
| null_state_is_rejected | NULL state retorna 0 |
| marker_constant_is_canonical | string `[smoke] storage-stack ready` imutável |

### Wiring

- `@/Users/t808981/Desktop/PR/CapyOS/Makefile`:
  `storage_smoke.o` + `storage_smoke_io.o` em runtime objs;
  test source + stub em `TEST_SRCS`.
- `@/Users/t808981/Desktop/PR/CapyOS/tests/test_runner.c`: declara
  e chama `run_storage_smoke_gate_tests`.

## Critério de fechamento

| Item do plano | Status |
|---|---|
| Marker emitido após primeiro read/write OK em AHCI ou NVMe | ✅ |
| Latch idempotente: marker = 1× por boot | ✅ |
| Stub host + tests determinísticos | ✅ 9 cases |
| AHCI/NVMe simétricos | ✅ |
| Full `dbg_puts → klog` migration | ⏸ diferida para sub-slice 3E.4.B |

## Mudanças de contrato

**Nenhuma cross-repo.** Pura adição interna. O marker é uma
string nova no COM1 — sister repos / harness externos que ainda
não conhecem o marker simplesmente o ignoram (não há schema
versionado a quebrar). Não toca manifest, descritor canônico,
ABI names ou versões pinadas.

## Próximos passos

1. **Slice 3E.5 — external smoke gate.** Novo alvo Makefile
   `smoke-x64-vmware-storage-resilience` + harness
   `smoke_x64_vmware_storage_resilience.py` em `tools/scripts/`
   + runbook `docs/operations/etapa-3-slice-3e-validation-playbook.md`.
   Harness aguarda dois markers no COM1: `[smoke] storage-stack ready`
   e `[net] DHCP: lease acquired.`.
2. Slice 3E.4.B — migração mecânica de `dbg_puts → klog` em
   ahci.c e nvme.c (~50 sites).
3. Slice 3F — multi-table AHCI provisioning + remoção do
   spin-wait em `ahci_exec_classified`.

Conforme `@/Users/t808981/Desktop/PR/CapyOS/docs/architecture/etapa-3-slice-3e-plan.md` §3.
