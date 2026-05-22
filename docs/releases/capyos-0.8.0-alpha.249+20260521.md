# CapyOS 0.8.0-alpha.249+20260521 — Slice 3E.3 entregue (escopo reduzido)

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — em andamento

## Resumo executivo

Esta alpha entrega o **Slice 3E.3** com escopo reduzido: a
infraestrutura de multi-slot AHCI via um bitmap allocator puro e
host-testable + auditoria das condições multi-queue NVMe (que já
estavam parcialmente em vigor desde antes). O dispatch concorrente
real (multi-command inflight) depende de um scheduler async no
kernel (Etapa 4) + provisionamento de N command tables (Slice 3F)
e foi explicitamente diferido.

**Não promove a Etapa 3.** Faltam Slices 3E.4, 3E.5, 3F-3J.

## Mudanças entregues

### Novo módulo: AHCI slot allocator

`@/Users/t808981/Desktop/PR/CapyOS/include/drivers/storage/ahci_slot_allocator.h`
+ `@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/ahci_slot_allocator.c`:

API:
- `ahci_slot_allocator_init(alloc, slot_count)` — slot_count em
  `[1, AHCI_MAX_SLOTS=32]`; valores inválidos (0 ou > 32) deixam
  o allocator inutilizável (fail-closed).
- `ahci_slot_alloc(alloc) → int` — devolve o lowest-numbered free
  slot ou `-1` se cheio / NULL.
- `ahci_slot_release(alloc, slot) → int` — rejeita slot fora de
  range, NULL, ou double-release.
- `ahci_slot_inflight_count(alloc) → uint8_t` — diagnóstico
  (popcount dos slots em uso).
- `ahci_slot_is_free(alloc, slot) → int` — accessor.
- `ahci_slot_allocator_reset(alloc)` — marca todos os slots livres;
  chamado após COMRESET para alinhar o allocator com o estado pós-reset
  do controlador (que descarta todos os comandos in-flight).

Implementação 100% pura: sem MMIO, sem klog, sem kmalloc.
Suitable for the host runner.

### Integração no runtime AHCI

`@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/ahci.c`:

- `struct ahci_port_ctx` ganha `slot_alloc`.
- `ahci_setup_port` chama `ahci_slot_allocator_init(..., AHCI_RUNTIME_SLOT_COUNT)`.
  `AHCI_RUNTIME_SLOT_COUNT = 1` enquanto a runtime aloca apenas
  uma command table por porta — multi-table provisioning é Slice 3F.
- `ahci_exec_classified` agora:
  1. `slot = ahci_slot_alloc(&ctx->slot_alloc)` — retorna
     `BLOCK_IO_ERR_TRANSIENT` se cheio (caller pode retentar).
  2. Usa `cmd_list[slot]` para o header e `1u << slot` na
     máscara `mmio_write32(&port->ci, ...)`.
  3. Libera no completion limpo e no aborted path
     (`ahci_slot_release`).
  4. **Não libera no TIMEOUT path**: o controlador ainda considera
     o slot ocupado; quando o `block_device` retry loop chama
     `ops->reset` (`ahci_port_comreset`), o allocator é reinicializado
     via `ahci_slot_allocator_reset`. Isso evita inconsistência
     entre allocator e estado do controlador.

### NVMe — auditoria

- `NVME_QUEUE_DEPTH = 64` desde antes (já configurado em
  `src/drivers/nvme/nvme.c`). Admin SQ/CQ e I/O SQ/CQ todos com
  64 entradas.
- `dev->next_cid++` em `uint16_t` produz CID rolling wrap-safe
  (CID=0 é válido per NVMe 1.4 §4.6.1).
- Sem mudança de código necessária — só documentação para tornar
  explícito que o requisito do plano foi atendido sem trabalho
  adicional.

### Cobertura de teste

`@/Users/t808981/Desktop/PR/CapyOS/tests/drivers/test_ahci_slot_allocator.c`
(11 testes):

| Teste | Validação |
|---|---|
| init_typical | slot_count=4 → mask=0x0F, inflight=0 |
| init_full_32 | slot_count=32 → mask=0xFFFFFFFF |
| init_invalid_disables_allocator | slot_count=0 e >32 desabilitam |
| alloc_lowest_first | sequência 0,1,2; inflight progressivo |
| alloc_until_full_then_fail | 4 allocs em 4 slots, 5° = -1 |
| release_reuses_slot | release(s0) → próximo alloc devolve s0 |
| release_invalid_inputs | slot negativo, OOB, NULL, nunca alocado |
| double_release_is_rejected | release segundo do mesmo slot = -1 |
| is_free_accessor | reflete o estado correto pré/pós-alloc |
| reset_marks_all_free | reset() devolve inflight a 0 |
| inflight_count_full_32 | 32 allocs → inflight=32, 33° = -1 |

### Wiring

- `@/Users/t808981/Desktop/PR/CapyOS/Makefile`:
  `ahci_slot_allocator.o` em runtime objs; fontes em `TEST_SRCS`.
- `@/Users/t808981/Desktop/PR/CapyOS/tests/test_runner.c`: declara
  e chama `run_ahci_slot_allocator_tests`.

## Critério de fechamento

| Item do plano original | Status |
|---|---|
| Tests host validam slot management | ✅ 11 cases |
| NVMe queue depth = 64 + CID rolling | ✅ auditado |
| AHCI passa pelo allocator no hot path | ✅ |
| COMRESET reseta o allocator | ✅ |
| Concurrent inflight real | ⏸ diferido para Slice 3F |
| Sem regressão em `smoke-x64-cli-nvme` | ⏳ validação externa pendente |

## Mudanças de contrato

**Nenhuma cross-repo.** Pura adição interna ao AHCI.
- Não toca `block_device_ops`, manifest, descritor canônico,
  quotas, install_root, alfabeto, marker de ativação, ABI names
  ou versões pinadas.
- O comportamento observável da runtime AHCI permanece idêntico:
  ainda usa slot 0, ainda serializa, ainda emite os mesmos
  bit-patterns no doorbell. Diferença é apenas que o slot agora
  vem do allocator (que devolve 0 quando configurado para 1 slot).

## Próximos passos

1. **Slice 3E.4 — klog estruturado + smoke marker.** Substituir
   `dbg_puts(port 0xE9)` em `ahci.c`/`nvme.c` por
   `klog(KLOG_INFO|WARN|ERR, ...)` com prefixos canônicos
   `[ahci]`/`[nvme]`. Emitir `[smoke] storage-stack ready` após
   o primeiro read/write bem-sucedido em cada controlador.
2. Slice 3E.5 — gate externo `smoke-x64-vmware-storage-resilience`.
3. Slice 3F — provisionamento de N command tables + remoção do
   spin-wait em `ahci_exec_classified` (requer scheduler async
   da Etapa 4).
4. Slices 3G-3J conforme plano.

Conforme `@/Users/t808981/Desktop/PR/CapyOS/docs/architecture/etapa-3-slice-3e-plan.md` §3.
