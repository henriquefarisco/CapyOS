# CapyOS 0.8.0-alpha.248+20260521 — Slice 3E.2.B entregue

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — em andamento

## Resumo executivo

Esta alpha entrega o **Slice 3E.2.B** — recoverable retry policy
+ reset escalation. Antes desta entrega, o `block_device` apenas
propagava 0/-1 do driver para o caller; um TIMEOUT virava um erro
permanente sem chance de recuperação. Agora o layer block aplica
um orçamento de retries específico por classe de erro e escalona
um reset (COMRESET no AHCI, Controller Level Reset no NVMe)
exatamente uma vez quando um TIMEOUT é observado.

**Não promove a Etapa 3.** Faltam Slices 3E.3, 3E.4, 3E.5.

## Mudanças entregues

### `block_device_ops` estendido

Em `@/Users/t808981/Desktop/PR/CapyOS/include/fs/block.h`:

- Três novos campos opcionais:
  - `read_block_ex(ctx, blk, buf) → enum block_io_error_class`.
  - `write_block_ex(ctx, blk, buf) → enum block_io_error_class`.
  - `reset(ctx) → int` (0 device usable / -1 give up).
- Novos públicos:
  - `block_device_read_ex(dev, blk, buf) → class`.
  - `block_device_write_ex(dev, blk, buf) → class`.
- ABI legacy preservada: drivers que não opted-in (`ramdisk`,
  `efi_block`, `ata_pio`, `crypt`, `offset_wrapper`, `chunk_wrapper`,
  todos os tests de fs/security/boot) continuam funcionando sem
  modificação — designated initializers garantem zero-init dos
  novos campos.

### Retry policy unificada

Em `@/Users/t808981/Desktop/PR/CapyOS/src/fs/storage/block_device.c`:

```
BLOCK_IO_OK:           0 retries
BLOCK_IO_ERR_TRANSIENT: up to 3 retries
BLOCK_IO_ERR_TIMEOUT:   1 retry, preceded by ops->reset
BLOCK_IO_ERR_PERMANENT: 0 retries
BLOCK_IO_ERR_DEVICE_GONE: 0 retries
```

Invariantes:
- Reset emitido no máximo uma vez por chamada.
- Se `ops->reset` for NULL ou retornar -1, o TIMEOUT é surfaceado
  como PERMANENT (não loopa eternamente esperando recovery).
- Fast-path legacy: se o driver não expôs `read_block_ex`,
  `block_device_read` dispara direto `read_block` sem custo de
  loop nem de classifier.

### AHCI integration

Em `@/Users/t808981/Desktop/PR/CapyOS/src/drivers/storage/ahci.c`:

- `ahci_exec` refatorado em `ahci_exec_classified` que retorna
  `enum block_io_error_class`. Wrapper legacy `ahci_exec(...) →ahci int`
  preservado para os call-sites internos (identify) que ainda
  usam ABI 0/-1.
- Novas `ahci_read_block_ex` / `ahci_write_block_ex` retornam a
  classe diretamente para o block layer.
- Novo `ahci_port_comreset` implementa COMRESET seguindo
  AHCI 1.3.1 §10.4.2:
  1. Stop port (PxCMD.ST=0, PxCMD.FRE=0).
  2. PxSCTL.DET=1 (start COMRESET).
  3. Spin ≥ 1 ms.
  4. PxSCTL.DET=0 (release).
  5. Wait PxSSTS.DET=3 (device present + PHY ready).
  6. Clear PxSERR.
  7. Restart port.
  Retorna 0 se device usable, -1 se device não voltou.
- Hook `ahci_reset` exposto em `block_device_ops::reset`.

### NVMe integration

Em `@/Users/t808981/Desktop/PR/CapyOS/src/drivers/nvme/nvme.c`:

- `nvme_io_cmd` refatorado em `nvme_io_cmd_classified`. Wrapper
  legacy preservado.
- `nvme_block_read_ex` / `nvme_block_write_ex` constroem SQE via
  `nvme_build_rw_cmd` (do Slice 3E.1) e retornam a classe.
- Novo `nvme_controller_reset` implementa Controller Level Reset
  (NVMe 1.4 §7.3.1):
  1. CC.EN=0.
  2. Wait CSTS.RDY=0.
  3. CC.EN=1.
  4. Wait CSTS.RDY=1.
  5. Re-prime `admin_cq_phase`, `io_cq_phase`, tails.
  Estratégia heavy-handed mas adequada: TIMEOUT já indica
  controller wedged; tentar Abort command-level requer rastrear
  CIDs in-flight (diferido para Slice 3E.3 com multi-queue).
- Hook `nvme_reset_op` em `block_device_ops::reset`.

### Cobertura de teste

`@/Users/t808981/Desktop/PR/CapyOS/tests/fs/test_block_retry.c`
(12 testes) usa um mock device cuja resposta é uma sequência
programável de classes, permitindo auditar deterministicamente
budget e ordem de reset:

| Teste | Cenário validado |
|---|---|
| immediate_success | OK na primeira → retorna OK, 1 dispatch, 0 reset |
| transient_recovers | TRANSIENT,TRANSIENT,OK → OK, 3 dispatches, 0 reset |
| transient_exhausts | 4×TRANSIENT → TRANSIENT, 4 dispatches |
| permanent_no_retry | PERMANENT → PERMANENT, 1 dispatch |
| device_gone_no_retry | DEVICE_GONE → DEVICE_GONE, 0 reset |
| timeout_resets_and_retries | TIMEOUT,OK → OK, 1 reset, 2 reads |
| timeout_no_reset_op | TIMEOUT sem `ops->reset` → PERMANENT |
| timeout_reset_fails | TIMEOUT + reset=-1 → PERMANENT |
| timeout_then_timeout | TIMEOUT,TIMEOUT → TIMEOUT, 1 reset (não loopa) |
| write_path_same_policy | TRANSIENT,OK em write → OK |
| invalid_input_permanent | NULL/OOB → PERMANENT, 0 dispatch |
| legacy_driver_no_overhead | Driver sem `read_block_ex` → 1 dispatch |

### Wiring

- `@/Users/t808981/Desktop/PR/CapyOS/Makefile`:
  `tests/fs/test_block_retry.c` + `src/fs/storage/block_device.c`
  em `TEST_SRCS`.
- `@/Users/t808981/Desktop/PR/CapyOS/tests/test_runner.c`: declara
  e chama `run_block_retry_tests()`.

## Mudanças de contrato

**Nenhuma cross-repo.** Pura extensão interna do `block_device_ops`,
aditiva. Todos os drivers existentes (incluindo testes que
declaram seus próprios mock ops com designated initializers)
continuam funcionando sem modificação. Nenhuma sister repo
afetada.

## Atualizações de documentação

- `docs/architecture/etapa-3-slice-3e-plan.md` §3.Slice 3E.2.B
  marcado como entregue.
- `docs/plans/STATUS.md`: bullet de Slice 3E.2.B; próximo bloco
  Slice 3E.3.
- `docs/plans/active/capyos-master-plan.md`: §20 atualizado.
- `docs/reference/integration/compatibility-matrix.md`: CapyOS
  row bumped para alpha.248.

## Próximos passos

1. **Slice 3E.3 — multi-slot AHCI + multi-queue NVMe.** Permitir
   múltiplos comandos inflight (slot bitmap no AHCI, CID rolling
   no NVMe). Habilita Abort granular no NVMe (alternativa ao CLR
   pesado).
2. Slice 3E.4 — klog estruturado + smoke marker `[smoke]
   storage-stack ready`.
3. Slice 3E.5 — gate externo `smoke-x64-vmware-storage-resilience`.

Conforme `docs/architecture/etapa-3-slice-3e-plan.md` §3.
