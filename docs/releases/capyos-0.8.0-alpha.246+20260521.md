# CapyOS 0.8.0-alpha.246+20260521 — Slice 3E.1 entregue

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — em andamento; Slice 3D fechado em alpha.245, Slice 3E em curso

## Resumo executivo

Esta alpha entrega o **Slice 3E.1** — extração host-testável dos
AHCI/NVMe command builders. A motivação: o storage stack original
combinava lógica de protocolo (encoding de FIS, command header,
PRDT, SQE) com I/O real (MMIO doorbell, busy-wait em registers).
Isso tornava impossível testar o encoding sem rodar VM completa.
Agora a lógica de encoding vive em TUs pequenos e puros que o
host runner exercita diretamente.

**Não promove a Etapa 3** — Slices 3E.2 (error class + retry),
3E.3 (multi-slot/queue), 3E.4 (klog), 3E.5 (gate externo), 3F-3J
continuam pendentes.

## Mudanças entregues

### Novos módulos

- `include/drivers/storage/ahci_commands.h` — protocol-level
  encoders + structs (`ahci_cmd_header`, `ahci_prdt_entry`,
  `ahci_cmd_table`) + constantes (`AHCI_FIS_TYPE_REG_H2D`,
  `AHCI_H2D_FIS_LEN_DW`, `AHCI_CMD_HEADER_FLAG_WRITE`,
  `AHCI_PRDT_FLAG_INTERRUPT`, `AHCI_PRDT_MAX_BYTES`).
- `src/drivers/storage/ahci_commands.c` — 3 builders puros:
  - `ahci_build_h2d_fis(cfis, command, lba, sector_count)` — zera
    os 64 bytes e codifica o H2D Register FIS (LBA48).
  - `ahci_build_command_header(header, ctba, fis_len_dw, prdt_len, write)` —
    flags com FIS-length + Write bit, prdtl, prdbc=0, CTBA/CTBAU
    splitting 64-bit, reserved=0.
  - `ahci_build_prdt_entry(entry, buffer, byte_count, ioc)` —
    dba/dbau, dbc_i com byte_count-1 e I-bit opcional.
  - Validações: NULL pointers, fis_len_dw > 31, byte_count zero,
    odd ou acima de `AHCI_PRDT_MAX_BYTES`.
- `include/drivers/nvme/nvme_commands.h` — 5 builder prototypes +
  constantes (`NVME_IDENTIFY_CNS_CONTROLLER`,
  `NVME_IDENTIFY_CNS_NAMESPACE`, `NVME_CREATE_QUEUE_PC_BIT`).
- `src/drivers/nvme/nvme_commands.c` — 5 builders puros para
  SQE: Identify Controller, Identify Namespace, Create CQ,
  Create SQ, R/W (read/write). Cada um zera o SQE, popula os
  campos necessários e valida entrada (nsid==0 reserved,
  block_count>65536 inválido per spec, opcode permitido apenas
  NVME_CMD_READ/WRITE para o builder R/W).

### Refatoração drivers

- `src/drivers/storage/ahci.c`:
  - Removida função estática local `ahci_build_h2d_fis` (movida).
  - Removidas structs `ahci_cmd_header`, `ahci_prdt_entry`,
    `ahci_cmd_table` (movidas para header público).
  - Adicionado `#include "drivers/storage/ahci_commands.h"`.
  - Em `ahci_exec`, o setup inline de command header + FIS +
    PRDT foi substituído por 3 chamadas aos builders.
  - Bit-pattern resultante é byte-idêntico ao código antigo.
- `src/drivers/nvme/nvme.c`:
  - Adicionado `#include "drivers/nvme/nvme_commands.h"`.
  - `nvme_identify`, `nvme_identify_ns`, `nvme_create_io_cq`,
    `nvme_create_io_sq`, `nvme_read_blocks`, `nvme_write_blocks`
    agora chamam os builders. Setup inline removido.
  - Bit-pattern do SQE preservado.

### Tests host

- `tests/drivers/test_ahci_commands.c` (10 testes):
  - Rejeição de NULL em todos os 3 builders.
  - H2D FIS: basic read, LBA48 high bits, máscara LBA48 todos-1.
  - Command header: rejeita fis_len > 31; read path; write flag.
  - PRDT entry: rejeita byte_count zero/ímpar/acima do max;
    byte_count - 1 encoding; I-bit ON/OFF.
- `tests/drivers/test_nvme_commands.c` (10 testes):
  - Identify Ctrl: rejeita inválido + layout (opcode, nsid=0,
    prp1, CNS=01).
  - Identify NS: rejeita nsid=0 + layout (CNS=00).
  - Create CQ: rejeita inválido + layout (QSIZE-1, QID, PC bit).
  - Create SQ: layout (CQID high half + PC bit).
  - R/W: rejeita opcode/nsid/count inválido; layout READ; max
    count=65536 com NLB=0xFFFF.

### Wiring

- `Makefile`: adicionado `ahci_commands.o`, `nvme_commands.o` nos
  runtime objects; adicionados as duas fontes de teste em
  `TEST_SRCS`.
- `tests/test_runner.c`: 2 novos `extern int run_*_tests(void)` +
  2 chamadas em `main`.

## Evidências internas

- 20 novos host tests totais.
- Bit-pattern equivalência verificada por inspeção: cada builder
  reproduz exatamente os bytes que o setup inline antigo
  produzia.
- ABI runtime preservada: as funções públicas exportadas
  (`nvme_read_blocks`, `nvme_write_blocks`, `nvme_init`,
  `ahci_init`, `ahci_get_block_device`) não mudaram de assinatura
  nem de comportamento.

## Mudanças de contrato

**Nenhuma.** Pura refatoração interna ao CapyOS core. Não toca:

- Manifest format line-oriented do `capypkg`.
- Descritor canônico Ed25519.
- Quotas em `include/services/capypkg.h`.
- Escopo do `install_root`.
- Alfabeto de `name`/`depends`.
- Layout do marker de ativação.
- Nomes ABI canônicos.
- Versões pinadas das sister repos.

## Atualizações de documentação

- `docs/architecture/etapa-3-slice-3e-plan.md` §3.Slice 3E.1
  marcado como entregue com lista detalhada.
- `docs/plans/STATUS.md`: bullet de Slice 3E.1; próximo bloco
  apontando para Slice 3E.2.
- `docs/plans/active/capyos-master-plan.md`: §20 atualizado.
- `docs/reference/integration/compatibility-matrix.md`: CapyOS
  row bumped para alpha.246.

## Próximos passos

1. **Slice 3E.2 — error classification + recoverable retry
   policy.** Criar `enum block_io_error_class` em
   `include/drivers/storage/block_error.h` (OK / TRANSIENT /
   PERMANENT / TIMEOUT / DEVICE_GONE). Traduzir AHCI TFD bits
   + NVMe SC/SCT para essas classes. `block_device_ops` ganha
   `read_block_ex` opcional. Retry policy 3 tentativas para
   TRANSIENT, 0 para PERMANENT, 1 com COMRESET/Abort para
   TIMEOUT.
2. Slice 3E.3 — multi-slot AHCI / multi-queue NVMe.
3. Slice 3E.4 — klog estruturado + smoke marker storage.
4. Slice 3E.5 — gate externo `smoke-x64-vmware-storage-resilience`.

Conforme `docs/architecture/etapa-3-slice-3e-plan.md` §3.
