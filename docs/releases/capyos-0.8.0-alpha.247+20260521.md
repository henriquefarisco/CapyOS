# CapyOS 0.8.0-alpha.247+20260521 — Slice 3E.2.A entregue

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — em andamento; Slice 3D fechado em alpha.245, Slice 3E em curso

## Resumo executivo

Esta alpha entrega o **Slice 3E.2.A** — unified block-I/O error
classifier compartilhado entre AHCI e NVMe. Antes desta entrega,
cada driver tinha sua própria política ad-hoc para distinguir erro
recuperável de erro permanente; o caller só recebia `0` ou `-1` e
não conseguia tomar decisões diferenciadas. Agora os dois drivers
classificam o desfecho em uma taxonomia comum de cinco classes
e anotam os logs com `class=<name>`, preparando o terreno para o
Slice 3E.2.B (retry policy + COMRESET/Abort) sem mudar a ABI atual.

**Não promove a Etapa 3.** Faltam Slices 3E.2.B, 3E.3, 3E.4, 3E.5
e demais sub-stages do Slice 3F-3J.

## Mudanças entregues

### Novo módulo

- `include/drivers/storage/block_error.h`:
  - `enum block_io_error_class { BLOCK_IO_OK, BLOCK_IO_ERR_TRANSIENT,
    BLOCK_IO_ERR_PERMANENT, BLOCK_IO_ERR_TIMEOUT, BLOCK_IO_ERR_DEVICE_GONE }`.
  - `block_io_classify_ahci(pxis, pxtfd, timed_out, port_present)`.
  - `block_io_classify_nvme(status, timed_out)`.
  - `block_io_error_class_name(cls)` — lowercase short name para klog.
  - `block_io_should_retry(cls)` — política rápida (TRANSIENT e
    TIMEOUT respondem true; o caller bound de retry é responsável
    por respeitar o orçamento).

- `src/drivers/storage/block_error.c` — implementação pura, sem
  MMIO, sem kmalloc, sem klog. Apropriada tanto para o host runner
  quanto para o runtime.

### Decisão de classificação

**AHCI (ATA-ACS3 §7.16, AHCI 1.3.1 §5.4):**

1. `!port_present` → `DEVICE_GONE` (precedência máxima — não vale
   a pena retornar nada mais útil).
2. `timed_out` → `TIMEOUT`.
3. `IS.TFES=0 ∧ TFD.STS.ERR=0` → `OK`.
4. ERROR byte com `UNC|IDNF|AMNF` → `PERMANENT` (dado
   irrecuperável ou endereço inválido).
5. ERROR byte com `ABRT` → `PERMANENT` (controlador recusou o
   comando; tipicamente comando malformado para o device).
6. Caso contrário (ICRC, NM, MCR, MC, TFES com ERROR=0) → `TRANSIENT`.

**NVMe (NVMe 1.4 §4.6.1.2):**

1. `timed_out` → `TIMEOUT`.
2. `SC=0 ∧ SCT=0` → `OK`.
3. `SCT=3` (Path Related Status) → `DEVICE_GONE`.
4. `SCT=2` (Media and Data Integrity) → `PERMANENT`.
5. `DNR=1` → `PERMANENT` (controlador disse explicitamente para
   não retentar).
6. Caso contrário (Generic/Command-Specific sem DNR) → `TRANSIENT`.

### Integração runtime

- `src/drivers/storage/ahci.c::ahci_exec` — 3 sites de
  classificação:
  - Completion bem-sucedida observada via `(ci & slot)==0`:
    chama classifier para validar e logar; se != OK, retorna -1
    com `class=<name>` no log.
  - Erro early (IS.TFES ou TFD.ERR sinalizado durante o spin):
    classifier produz a categoria para o log antes do return -1.
  - Timeout pós-spin: classifier com `timed_out=1` gera log
    `class=timeout`.
- `src/drivers/nvme/nvme.c::nvme_admin_cmd` e `nvme_io_cmd` — 4
  sites de classificação (completion + timeout em cada uma).

A ABI runtime exposta (`ahci_init`, `nvme_init`, `block_device.*`)
não muda. Os logs `dbg_puts` agora carregam `class=<name>` o que
permite que gates externos e ferramentas forenses (Slice 3E.5)
distingam falha permanente de transiente sem ter que decodificar
bytes brutos do TFD ou do CQE.

### Test coverage

- `tests/drivers/test_block_error.c` (15 testes):
  - **AHCI (8 testes):** clean OK; device-gone precede UNC;
    timeout; UNC permanent; ABRT permanent; ICRC transient; TFES
    sem ERROR byte transient; IDNF permanent.
  - **NVMe (6 testes):** clean OK; timeout; SCT=3 path-related →
    device-gone; SCT=2 media → permanent; DNR=1 permanent; default
    transient.
  - **Helpers (1 teste):** nome de cada classe + política de
    retry para todas as 5 classes.

### Wiring

- `Makefile`: `block_error.o` em runtime objs;
  `tests/drivers/test_block_error.c` + `src/drivers/storage/block_error.c`
  em `TEST_SRCS`.
- `tests/test_runner.c`: declaração + chamada de
  `run_block_error_tests()`.

## Evidências internas

- 15 novos host tests cobrem todas as 5 classes para AHCI e NVMe,
  incluindo casos de precedência (device-gone sobre UNC) e edge
  cases (TFES com ERROR=0).
- Bit-pattern dos comandos enviados ao hardware é o mesmo: o
  classifier só inspeciona os bytes de completion / timeout.
- Retorno legacy `int` (0 sucesso / -1 falha) preservado em todas
  as funções públicas de AHCI e NVMe.

## Mudanças de contrato

**Nenhuma.** Pura adição interna. Não toca contratos com sister
repos, manifest format, descritor canônico, quotas, install_root,
alfabeto, marker de ativação, nomes ABI canônicos ou versões
pinadas.

## Atualizações de documentação

- `docs/architecture/etapa-3-slice-3e-plan.md` §3 dividido em
  Slice 3E.2.A (entregue) e Slice 3E.2.B (pendente).
- `docs/plans/STATUS.md`: bullet de Slice 3E.2.A; próximo bloco
  apontando para Slice 3E.2.B.
- `docs/plans/active/capyos-master-plan.md`: §20 atualizado.
- `docs/reference/integration/compatibility-matrix.md`: CapyOS
  row bumped para alpha.247.

## Próximos passos

1. **Slice 3E.2.B — recoverable retry & reset escalation.**
   `block_device_ops` ganha `read_block_ex`/`write_block_ex`
   opcional que retorna o `enum block_io_error_class`. Caller
   loop em `block_device_read`/`block_device_write` consome a
   classe e aplica orçamento 3 / 1 / 0 / 0 / 0. AHCI emite
   COMRESET em TIMEOUT antes do retry único; NVMe emite Abort,
   caindo para Controller Level Reset na segunda falha.
2. Slice 3E.3 — multi-slot AHCI / multi-queue NVMe.
3. Slice 3E.4 — klog estruturado + smoke marker `[smoke]
   storage-stack ready`.
4. Slice 3E.5 — gate externo `smoke-x64-vmware-storage-resilience`.

Conforme `docs/architecture/etapa-3-slice-3e-plan.md` §3.
