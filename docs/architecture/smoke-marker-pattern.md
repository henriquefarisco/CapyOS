# Smoke marker pattern — arquitetura

**Audiência:** autores de drivers e gates externos no CapyOS.
**Origem:** Slice 3D (USB HID, alpha.245), Slice 3E.4 (storage, alpha.250) e audit fix do alpha.252.
**Última revisão:** 2026-05-22.

Este documento descreve o pattern canônico para criar smoke
markers forenses que sirvam como gates externos
(`make smoke-x64-vmware-*`). Foi formalizado após o BUG #1 do
alpha.252, em que a falta de uma regra clara sobre o escopo do
latch levou a double-emission do marker de storage em VMs
dual-storage.

## 1. O que é um smoke marker

Uma string canônica emitida no console serial (COM1) **exatamente
uma vez por boot**, na primeira vez em que um subsistema atinge
seu critério de prontidão observável. O marker é a interface entre
o kernel e um harness externo (`tools/scripts/smoke_x64_vmware.py`)
que valida automação de boot em VMware oficial.

Exemplos atuais:

| Marker | Subsistema | Critério de prontidão | Alpha |
|---|---|---|---|
| `[net] DHCP: lease acquired.` | rede | Lease DHCP obtido em interface E1000 | base |
| `[smoke] usb-hid-keyboard ready` | USB HID | Teclado USB configurado + 1 caractere recebido | 245 (Slice 3D) |
| `[smoke] gui-session ready` | desktop | Sessão gráfica inicializada | base |
| `[smoke] mouse-events ready` | input mouse | 1 evento de mouse processado | base |
| `[smoke] storage-stack ready` | storage | 1 read/write OK em AHCI **ou** NVMe | 250 (Slice 3E.4) + 252 (audit) |
| `[smoke] scheduler-fairness ready` | scheduler | 3 task IDs despachados 2× cada | Etapa 4 Fase C |
| `[smoke] compositor-damage-track ready` | compositor | 2 frames parciais com dirty rects | Etapa 4 Fase D |
| `[smoke] thread-crash-survives ready` | scheduler/processo | Processo morto por fault (exit ≥ 128) + N ticks de scheduler depois | Etapa 4 Fase E |
| `[smoke] tls-handshake ready` | TLS userland | `tls_smoke` (ring-3) faz GET HTTPS válido OK **e** GET de cert inválido recusado, então sai 0 | Etapa 5 Slice 5.6 |

## 2. Invariantes obrigatórios

Todo smoke marker DEVE atender:

1. **Idempotência:** emitido no máximo uma vez por boot. Marker
   ambíguo (sem latch) é um bug de contrato.
2. **Determinismo:** o critério de prontidão é puro e testável em
   host runner sem MMIO real.
3. **Forensia dupla:** emitido em **COM1** (capturado pelo harness)
   **e** registrado via `klog(KLOG_INFO, ...)` (recuperável via
   `/var/log/capyos_klog.txt` mesmo sem serial capture).
4. **Separação:** lógica de gate (pura, host-testable) fica em
   `*_smoke.h/.c`; emissão I/O (COM1 + klog) fica em
   `*_smoke_io.c` (kernel-only, com stub host em `tests/stubs/`).
5. **Escopo do latch:** o latch deve cobrir todo o caminho de
   emissão. Se múltiplos TUs podem disparar a emissão, o latch
   **deve** ser global (file-static em uma TU compartilhada),
   nunca per-TU.

## 3. Anti-pattern documentado — BUG #1 do alpha.252

Em alpha.250, o storage smoke marker foi entregue com latches
**per-driver**:

```c
// ahci.c (alpha.250 — BROKEN)
static struct storage_smoke_state g_ahci_smoke_state;
// nvme.c (alpha.250 — BROKEN)
static struct storage_smoke_state g_nvme_smoke_state;
```

Em VMs com AHCI **e** NVMe ambos presentes, cada driver flipava
seu próprio latch independentemente. Resultado: o marker era
emitido **duas vezes** no COM1, violando o invariante #1
("idempotência").

O gate passava por sorte — `markers_in_order` aceita repetição.
Mas:

- Parsers downstream que assumem 1× quebram silenciosamente.
- Métricas de boot timing ficam ruidosas.
- Telemetria de produção fica não-confiável.

**Lição:** se o contrato é "1 marker por subsistema", o estado
de latch é uma propriedade do **subsistema**, não dos drivers
que compõem o subsistema. Logo, o estado vive na TU dona do
subsistema, não nos drivers individuais.

### Fix canônico (alpha.252)

```c
// src/drivers/storage/storage_smoke.c
static struct storage_smoke_state g_global_state;

int storage_smoke_try_latch_global(enum storage_smoke_source source) {
    return storage_smoke_observe(&g_global_state, source);
}

void storage_smoke_global_reset(void) {
    storage_smoke_state_reset(&g_global_state);
}
```

Drivers consomem o latch global:

```c
// ahci.c (alpha.252 — FIXED)
static void ahci_smoke_signal_ok(void) {
  if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI)) {
    storage_smoke_emit_marker();
  }
}
// nvme.c — simétrico com STORAGE_SMOKE_SRC_NVME
```

## 4. Quando per-state vs global?

| Cenário | Escopo do latch | Exemplo |
|---|---|---|
| Subsistema com 1 instância única no kernel | per-state em global único | USB HID (`g_hid.smoke`) |
| Subsistema com N instâncias do mesmo driver | per-state na TU do driver | (não aplicável atualmente) |
| Subsistema com múltiplos drivers irmãos | **global em TU compartilhada** | storage (AHCI + NVMe) |

A regra prática: o latch fica onde está a unidade de "prontidão"
do subsistema, do ponto de vista do harness externo. Se o
harness vê "storage" como uma coisa só, o latch é único para
"storage", não por driver.

## 5. Template para um novo smoke marker

Para adicionar `[smoke] FOO ready`:

### 5.1 Header `include/drivers/foo/foo_smoke.h`

```c
#ifndef DRIVERS_FOO_FOO_SMOKE_H
#define DRIVERS_FOO_FOO_SMOKE_H

#include <stdint.h>

#define FOO_SMOKE_GATE_VERSION 1
#define FOO_SMOKE_MARKER "[smoke] foo ready"

struct foo_smoke_state {
    /* counters that compose the readiness predicate */
    uint8_t emitted;
};

void foo_smoke_state_reset(struct foo_smoke_state *state);
int  foo_smoke_gate_observed(/* counters */);
int  foo_smoke_observe(struct foo_smoke_state *state, /* event */);

/* Process-wide single-emission latch (Invariant #5). */
int  foo_smoke_try_latch_global(/* event */);
void foo_smoke_global_reset(void);

/* Emitter — kernel only; stub in tests/stubs/. */
void foo_smoke_emit_marker(void);

#endif
```

### 5.2 Pure logic `src/drivers/foo/foo_smoke.c`

Implementação pura. Sem MMIO, sem kmalloc, sem klog. O global
state vive aqui.

### 5.3 I/O wrapper `src/drivers/foo/foo_smoke_io.c`

```c
#include "drivers/foo/foo_smoke.h"
#include "drivers/serial/serial_com1.h"
#include "kernel/log/klog.h"

void foo_smoke_emit_marker(void) {
    com1_puts(FOO_SMOKE_MARKER "\n");
    klog(KLOG_INFO, "[foo] smoke marker emitted on COM1.");
}
```

### 5.4 Host stub `tests/stubs/stub_foo_smoke_io.c`

```c
#include "drivers/foo/foo_smoke.h"
void foo_smoke_emit_marker(void) { /* no-op for host tests */ }
```

### 5.5 Host tests `tests/drivers/test_foo_smoke_gate.c`

Cobertura mínima obrigatória:

- Reset zera todos os campos.
- Predicate é puro.
- Primeira observação dispara emissão e latcha.
- Observações subsequentes NÃO re-emitem.
- NULL state retorna 0.
- Marker constant é canônica.
- **Latch global single-emission entre fontes** (regressão BUG #1).
- **Reset isolation entre tests** (via `foo_smoke_global_reset`).

### 5.6 Driver integration

Driver chama `foo_smoke_try_latch_global` no primeiro evento de
sucesso. Se retorna 1, chama `foo_smoke_emit_marker`. Nunca
mantém estado local de "já emitiu"; sempre delega ao latch
global.

### 5.7 Gate externo no Makefile

```make
smoke-x64-vmware-foo: all64 iso-uefi manifest64
	@echo "Executando smoke test VMware+E1000 foo..."
	python3 tools/scripts/smoke_x64_vmware.py \
		--marker "[net] DHCP: lease acquired." \
		--marker "$(FOO_SMOKE_MARKER)" \
		$(SMOKE_X64_VMWARE_ARGS)
```

### 5.8 Runbook em `docs/operations/`

Sempre inclui:

1. Contrato de prontidão (quando o marker dispara).
2. Pipeline observado (ASCII art do caminho event → emissão).
3. Pré-requisitos (build, VM config, captura serial).
4. Comandos de execução (vmrun + govc).
5. Critérios de aceite (markers em ordem + sem panic + exit 0).
6. Triagem por sintoma (tabela de modos de falha).
7. Protocolo pós-aprovação.
8. Local execution policy reafirmada.

## 6. Checklist de revisão

Quando revisar um PR que adiciona ou modifica um smoke marker:

- [ ] Existe `*_smoke.h` em `include/drivers/*/`?
- [ ] Existe `*_smoke.c` puro (sem MMIO/klog) em `src/drivers/*/`?
- [ ] Existe `*_smoke_io.c` em `src/drivers/*/` (kernel-only emitter)?
- [ ] Existe `stub_*_smoke_io.c` em `tests/stubs/`?
- [ ] Latch é **global** se o subsistema tem múltiplos drivers?
- [ ] Tests cobrem regressão "cross-source latch" (Invariant #5)?
- [ ] Tests cobrem reset isolation entre runs?
- [ ] Marker constant é validada por host test?
- [ ] Makefile target adiciona `--marker` na ordem correta (deps DHCP-first)?
- [ ] Runbook em `docs/operations/` segue o template §5.8?
- [ ] Marker aparece em `klog` além de COM1 (forensia dupla)?

## 7. Manifest dos smoke markers ativos

Mantido sincronizado com `tools/scripts/smoke_x64_vmware.py` e
com os alvos `smoke-x64-vmware-*` em `Makefile`.

| Marker | Header | Pure logic | Emitter | Gate Makefile | Runbook |
|---|---|---|---|---|---|
| `[net] DHCP: lease acquired.` | embutido no stack TCP/IP | `src/net/dhcp/*` | direct `com1_puts` | base | `docs/operations/vmware-e1000-validation-playbook.md` |
| `[smoke] usb-hid-keyboard ready` | `include/drivers/usb/usb_hid_smoke.h` | `src/drivers/usb/usb_hid_smoke.c` | `src/drivers/usb/usb_hid_smoke_io.c` | `smoke-x64-vmware-usb-hid-keyboard` | `docs/operations/etapa-3-external-validation-playbook.md` |
| `[smoke] storage-stack ready` | `include/drivers/storage/storage_smoke.h` | `src/drivers/storage/storage_smoke.c` | `src/drivers/storage/storage_smoke_io.c` | `smoke-x64-vmware-storage-resilience` | `docs/operations/etapa-3-slice-3e-validation-playbook.md` |
| `[smoke] thread-crash-survives ready` | `include/kernel/thread_crash_smoke.h` | `src/kernel/thread_crash_smoke.c` | `src/kernel/thread_crash_smoke_io.c` | `smoke-x64-vmware-thread-crash-survives` | `docs/operations/etapa-4-external-validation-playbook.md` |
| `[smoke] tls-handshake ready` | `include/kernel/tls_handshake_smoke.h` | `src/kernel/tls_handshake_smoke.c` | `src/kernel/tls_handshake_smoke_io.c` | `smoke-x64-vmware-tls-handshake` | `docs/operations/etapa-5-external-validation-playbook.md` |

## 8. Histórico de evolução

- **alpha.245 (Slice 3D):** primeiro smoke marker forense
  (`[smoke] usb-hid-keyboard ready`). Pattern original com
  state per-TU (válido porque USB HID tem instância única).
- **alpha.250 (Slice 3E.4):** segundo smoke marker
  (`[smoke] storage-stack ready`). Reproduziu o pattern de
  Slice 3D incorretamente: usou state per-driver em AHCI e
  NVMe, criando o BUG #1.
- **alpha.251 (Slice 3E.5):** gate externo plumed (não
  detectou BUG #1 porque `markers_in_order` tolera repetição).
- **alpha.252 (audit fix):** BUG #1 corrigido movendo o latch
  para global em `storage_smoke.c`. Este documento foi criado
  para formalizar o pattern e prevenir reincidência.
- **Etapa 4 Fases C/D/E:** três latches kernel-side seguindo o
  pattern (`scheduler-fairness`, `compositor-damage-track`,
  `thread-crash-survives`). A Fase E introduziu o idioma de
  **alimentar o latch pelo exit code do processo** em
  `process_exit` (exit ≥ 128 = morte por fault).
- **Etapa 5 Slice 5.6 (`[smoke] tls-handshake ready`):** primeiro
  marker que valida um caminho **userland** (handshake TLS real).
  Reusa o idioma exit-code da Fase E ao contrário: o programa
  ring-3 `tls_smoke` sai **0** só quando o GET HTTPS válido OK e o
  GET de cert inválido falhou fechado, e o latch em `process_exit`
  emite no COM1. Necessário porque `capy_write(1,...)` de ring-3
  cai na porta 0xE9 (debug-console), que o VMware não captura — só
  o COM1 kernel-side chega ao harness.

## 9. Referências cruzadas

- `docs/operations/etapa-3-external-validation-playbook.md` —
  runbook do gate USB HID.
- `docs/operations/etapa-3-slice-3e-validation-playbook.md` —
  runbook do gate storage.
- `docs/operations/etapa-4-external-validation-playbook.md` —
  runbook dos gates scheduler/compositor/thread-crash (Fases C/D/E).
- `docs/operations/etapa-5-external-validation-playbook.md` —
  runbook do gate tls-handshake (Slice 5.6).
- `docs/architecture/etapa-3-slice-3e-plan.md` §3.Audit fix —
  registro técnico do BUG #1.
- `tools/scripts/smoke_marker_policy.py` —
  utilitários `markers_in_order` / `unique_markers` /
  `first_failure_marker` consumidos pelos harnesses.
