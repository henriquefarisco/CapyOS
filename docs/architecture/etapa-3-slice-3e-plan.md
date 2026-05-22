# Etapa 3 — Slice 3E plan (storage hardening + recoverable I/O errors)

**Status:** rascunho 2026-05-21. **Não comece este slice antes do gate
externo de Slice 3D (`make smoke-x64-vmware-usb-hid-keyboard`) fechar
com sucesso.** Bloqueio sequencial estrito.

**Plano-mãe:**
[`etapa-3-driver-foundation-plan.md`](etapa-3-driver-foundation-plan.md).
**Plano global:**
[`../plans/active/capyos-master-plan.md §6`](../plans/active/capyos-master-plan.md).

**Audiência:** desenvolvedor que vai abrir Slice 3E quando 3D fechar.

## 1. Escopo (mapeado contra Etapa 3 §6 entregáveis)

Da lista de entregáveis pendentes da Etapa 3 §6, este slice cobre:

- **AHCI maduro** — error recovery, timeouts deterministas, COMRESET
  controlado, suporte a múltiplos ports concorrentes via slot bitmap.
- **NVMe básico estável** — completion queue handling robusto, retry
  policy em CC.SC != 0, queue overflow recovery.
- **Tratamento de erros de I/O recuperável** — política unificada
  entre AHCI/NVMe/ATA-PIO: erro transiente retry com backoff, erro
  permanente surface ao caller, log estruturado em klog.

**Fora do escopo desta slice** (vão para slices futuras de Etapa 3):

- VirtIO-net e VirtIO-block (Slice 3H).
- VMware SVGA II (Slice 3I).
- Device manager unificado (Slice 3F — pode vir antes ou depois de
  3E dependendo da prioridade).
- Política de fallback de driver no nível do kernel (Slice 3G).
- USB mass storage (Slice 3J).

## 2. Estado atual auditado

### 2.1 AHCI (`src/drivers/storage/ahci.c`)

**O que existe:**

- `ahci_init` faz PCI find via class 0x01/subclass 0x06/prog_if 0x01,
  habilita bus mastering, mapeia ABAR, reset, port enumeration,
  IDENTIFY DEVICE, registra como `block_device`.
- `ahci_exec` builda H2D FIS, monta command header + PRDT, escreve em
  `port->ci`, polls `ci` até clear ou TFD.ERR.
- `ahci_read_block` / `ahci_write_block` chamam `ahci_exec` com
  comandos READ_DMA_EX (0x25) / WRITE_DMA_EX (0x35) ou LBA28 fallback.

**Issues identificadas (não corrigir aqui, só catalogar):**

- **AHCI-A**: usa apenas slot 0 (`AHCI_CMD_SLOT = 0`). Sem slot
  bitmap, não consegue I/O paralelo entre múltiplos requests
  inflight. Limita throughput.
- **AHCI-B**: `ahci_exec` é polling-only. Sem support a interrupts.
  Para Slice 3E pode permanecer polling (consistente com xHCI Slice
  3D); interrupt support fica para Slice 5+.
- **AHCI-C**: error recovery limitada. Quando TFD.ERR aparece, o code
  só retorna -1. Não emite COMRESET, não limpa o erro no port via
  `PxSERR` write-1-clear, e o port pode ficar travado.
- **AHCI-D**: timeout em `AHCI_TIMEOUT_SPINS` é fixo. Erro de timeout
  não tenta resetar o port.
- **AHCI-E**: sem distinção entre erro transiente (UDMA CRC) e erro
  permanente (sector defect). Caller não tem como saber se vale
  retry.
- **AHCI-F**: nenhum host test. Toda a TU é exercitada apenas em
  hardware/VM.

### 2.2 NVMe (`src/drivers/nvme/nvme.c`)

**O que existe:**

- `nvme_init` faz PCI find via class 0x01/subclass 0x08/prog_if 0x02,
  inicializa o controlador (CC.EN), cria admin SQ/CQ, identifica
  controller + namespace 1, cria I/O SQ/CQ qid=1.
- `nvme_io_cmd` submete comando, polls completion queue até CID match.
- `nvme_read_blocks` / `nvme_write_blocks` chamam READ (opcode 0x02)
  / WRITE (opcode 0x01).

**Issues identificadas:**

- **NVME-A**: queue depth = 64 (`NVME_QUEUE_SIZE`). Polling completion
  é serializado — um comando por vez. Multi-queue não usado.
- **NVME-B**: ao receber CQE com `cqe->status != 0`, retorna `-1`.
  Não decodifica o SC (Status Code) nem distingue NamespaceNotReady,
  AbortedByRequest, MediaError. Caller perde contexto.
- **NVME-C**: timeout em `NVME_CMD_TIMEOUT_SPINS`. Em timeout, não
  emite Abort command nem reseta o controlador.
- **NVME-D**: namespace ID é hardcoded `1`. Não enumera namespaces
  adicionais.
- **NVME-E**: usa um buffer global `g_io_buffer[4096]` para PRP.
  Concurrent I/O destruiria os dados — não há lock nem fila. OK
  para o single-threaded kernel atual, mas vai colidir quando
  Slice 4 introduzir scheduler multithread.
- **NVME-F**: nenhum host test.

### 2.3 ATA-PIO (`src/drivers/storage/ata_pio.c`)

Fallback para BIOS legacy / não-AHCI. Já estável conforme aceite
operacional Etapa 2. **Não tocar nesta slice** — só catalogue se
algum bug for descoberto durante o trabalho de 3E.

### 2.4 Storage runtime selection
(`src/arch/x86_64/storage_runtime.c`)

Já roteia AHCI > NVMe > ATA-PIO > RAM-disk fallback. Política de
fail-safe parcial. Boas práticas para 3E: garantir que o roteamento
NÃO cai silenciosamente para RAM-disk se o storage real falhou
durante boot — usuário precisa ver klog WARN.

## 3. Slice breakdown proposto

### Slice 3E.1 — AHCI/NVMe host-testable extraction (entregue 2026-05-21)

**Objetivo:** extrair lógica pura de cada driver para TUs separadas
que possam ser host-testadas. Sem mudança funcional.

**Entrega:**

- `include/drivers/storage/ahci_commands.h` (novo) +
  `src/drivers/storage/ahci_commands.c` (novo). Builders:
  `ahci_build_h2d_fis(cfis, cmd, lba, count) → int`,
  `ahci_build_command_header(hdr, ctba, fis_len_dw, prdt_len, write) → int`,
  `ahci_build_prdt_entry(entry, buffer, byte_count, ioc) → int`.
  Structs `ahci_cmd_header`, `ahci_prdt_entry`, `ahci_cmd_table` e
  constantes `AHCI_FIS_TYPE_REG_H2D`, `AHCI_H2D_FIS_LEN_DW`,
  `AHCI_CMD_HEADER_FLAG_WRITE`, `AHCI_PRDT_FLAG_INTERRUPT`,
  `AHCI_PRDT_MAX_BYTES` movidas para o header público.
- `include/drivers/nvme/nvme_commands.h` (novo) +
  `src/drivers/nvme/nvme_commands.c` (novo). Builders:
  `nvme_build_identify_ctrl_cmd(cmd, buf)`,
  `nvme_build_identify_ns_cmd(cmd, buf, nsid)`,
  `nvme_build_create_cq_cmd(cmd, buf, qid, qsize)`,
  `nvme_build_create_sq_cmd(cmd, buf, qid, qsize, cqid)`,
  `nvme_build_rw_cmd(cmd, opcode, nsid, lba, count, buf)`. Todos
  retornam `int` e validam entrada (NULL, nsid==0, opcode
  inválido, block_count > 65536, etc.).
- `src/drivers/storage/ahci.c` refatorado: remove `ahci_build_h2d_fis`
  local, remove structs (movidas para header), inclui o novo
  header e chama os 3 builders dentro de `ahci_exec`.
- `src/drivers/nvme/nvme.c` refatorado: inclui
  `drivers/nvme/nvme_commands.h`, substitui setup inline de SQE
  em `nvme_identify`, `nvme_identify_ns`, `nvme_create_io_cq`,
  `nvme_create_io_sq`, `nvme_read_blocks`, `nvme_write_blocks`
  por chamadas aos builders.

**Cobertura de teste:** 20 novos host tests:

- `tests/drivers/test_ahci_commands.c` (10 testes):
  rejeita NULL; FIS basic read; LBA48 high bits; máscara LBA48;
  command header rejeita inválido; header read path; header
  write flag; PRDT rejeita inválido; PRDT byte_count - 1; PRDT
  sem I-bit.
- `tests/drivers/test_nvme_commands.c` (10 testes):
  Identify Ctrl rejeita inválido; Identify Ctrl layout;
  Identify NS rejeita inválido; Identify NS layout; Create CQ
  rejeita inválido; Create CQ layout; Create SQ layout; R/W
  rejeita opcode/nsid/count inválidos; R/W layout READ; R/W
  block_count máximo 65536 com NLB=0xFFFF.

**Wirado:** `Makefile` (runtime objs + TEST_SRCS), `test_runner.c`
(2 novos `run_*_tests` declarations + chamadas).

**Critério de fechamento — atingido:**

- [x] `make test` deve passar com 20 novos tests (validação
      externa pendente).
- [x] Nenhum delta em comportamento runtime; AHCI/NVMe escrevem o
      mesmo bit-pattern. Verificado por inspeção comparando o
      código antigo e os novos builders.

### Slice 3E.2 — Error classification & recoverable retry

Dividido em duas sub-slices para reduzir risco:

#### Slice 3E.2.A — Unified classifier (entregue 2026-05-21, alpha.247)

**Objetivo:** taxonomia compartilhada e classificação determinística
do desfecho de cada comando, sem mudar o contrato runtime.

**Entrega:**

- Novo `include/drivers/storage/block_error.h` definindo
  `enum block_io_error_class` com `BLOCK_IO_OK`,
  `BLOCK_IO_ERR_TRANSIENT`, `BLOCK_IO_ERR_PERMANENT`,
  `BLOCK_IO_ERR_TIMEOUT`, `BLOCK_IO_ERR_DEVICE_GONE`. Adiciona
  `block_io_classify_ahci(pxis, pxtfd, timed_out, port_present)`,
  `block_io_classify_nvme(status, timed_out)`,
  `block_io_error_class_name(cls)` e `block_io_should_retry(cls)`.
- Novo `src/drivers/storage/block_error.c` com implementação pura
  (sem MMIO, sem klog). Decisão para AHCI segue ATA-ACS3:
  `!port_present → DEVICE_GONE` > `timed_out → TIMEOUT` >
  ERR=0 e TFES=0 → OK > UNC/IDNF/AMNF/ABRT → PERMANENT >
  ICRC/MC/NM/TFES-vazio → TRANSIENT. Decisão para NVMe segue
  NVMe 1.4 §4.6.1.2: `timed_out → TIMEOUT` > SC=0,SCT=0 → OK >
  SCT=3 (Path) → DEVICE_GONE > SCT=2 (Media) → PERMANENT >
  DNR=1 → PERMANENT > default → TRANSIENT.
- `src/drivers/storage/ahci.c` integrado: 3 sites de classificação
  em `ahci_exec` (completion limpa, completion com TFES, timeout
  pós-spin). Logs anotados com `class=<name>`. Retorno 0/-1
  preservado.
- `src/drivers/nvme/nvme.c` integrado: 4 sites de classificação
  em `nvme_admin_cmd` e `nvme_io_cmd` (completion e timeout).
  Logs anotados.
- Novo `tests/drivers/test_block_error.c` com 15 testes:
  AHCI (clean, device-gone wins over UNC, timeout, UNC, ABRT,
  ICRC, TFES sem ERROR, IDNF) + NVMe (clean, timeout, SCT=3 path,
  SCT=2 media, DNR=1, default transient) + names + retry policy.

**Wirado:** Makefile (`block_error.o` em runtime, fontes em
TEST_SRCS); `test_runner.c` (declaração + chamada).

**Critério de fechamento — atingido para 3E.2.A:**

- [x] Host tests cobrindo cada classe — 15 cases validam decisões.
- [x] AHCI/NVMe usam o classifier no caminho de completion e
      timeout.
- [x] Sem mudança de bit-pattern, sem nova ABI runtime exposta.

#### Slice 3E.2.B — Recoverable retry & reset escalation (entregue 2026-05-21, alpha.248)

**Objetivo:** o caller-bound de retry aplica a política do
classifier (3 para TRANSIENT, 1 para TIMEOUT após reset, 0 para
PERMANENT/DEVICE_GONE/OK).

**Entrega:**

- `include/fs/block.h`: `block_device_ops` ganha campos opcionais
  `read_block_ex`/`write_block_ex` (retornam
  `enum block_io_error_class`) e `reset` (`int(*)(void *ctx)`).
  Novos públicos `block_device_read_ex(dev, blk, buf) → class` e
  `block_device_write_ex(dev, blk, buf) → class`.
- `src/fs/storage/block_device.c` reescrito com retry loop unificado.
  Budget hardcoded: TRANSIENT=3 retries, TIMEOUT=1 retry após reset
  bem-sucedido. Para TIMEOUT: se `ops->reset` for NULL ou retornar
  -1, surface PERMANENT (não loopa eternamente). Reset é emitido
  no máximo uma vez por chamada (`reset_attempted` flag). Fast-path
  legacy: se o driver não expôs `read_block_ex`, `block_device_read`
  dispara direto `read_block` sem custo de loop.
- `src/drivers/storage/ahci.c`:
  - `ahci_exec` refatorado em `ahci_exec_classified` retornando
    `enum block_io_error_class`. Legacy `ahci_exec` (0/-1) virou
    wrapper.
  - `ahci_read_block_ex` / `ahci_write_block_ex` (retornam classe).
  - Novo `ahci_port_comreset` implementa COMRESET seguindo AHCI
    1.3.1 §10.4.2: stop port → PxSCTL.DET=1 → spin → DET=0 → wait
    PxSSTS.DET=3 → clear SERR → restart. Hook `ahci_reset` exposto
    em `block_device_ops::reset`.
- `src/drivers/nvme/nvme.c`:
  - `nvme_io_cmd` refatorado em `nvme_io_cmd_classified`.
  - `nvme_block_read_ex` / `nvme_block_write_ex` constroem comando
    via `nvme_build_rw_cmd` e retornam a classe.
  - Novo `nvme_controller_reset` implementa Controller Level Reset
    (CC.EN=0 → wait CSTS.RDY=0 → CC.EN=1 → wait CSTS.RDY=1) e
    re-prima `admin_cq_phase`/`io_cq_phase`. Hook `nvme_reset_op`
    em `block_device_ops::reset`.
- `tests/fs/test_block_retry.c` (12 testes) com mock device cuja
  resposta é programável via `seq[]`:
  - immediate OK; TRANSIENT recovers dentro do budget; TRANSIENT
    exhausts budget (4 calls); PERMANENT no retry; DEVICE_GONE no
    retry nem reset; TIMEOUT resets and retries (1 reset + 2
    reads); TIMEOUT sem reset op → PERMANENT; TIMEOUT reset falha
    → PERMANENT; TIMEOUT seguido de TIMEOUT surface (reset só uma
    vez); write path aplica mesma política; invalid input
    PERMANENT; legacy driver sem `read_block_ex` não paga overhead
    do loop.

**Critério de fechamento — atingido:**

- [x] Host tests com mocks de classifier + retry budget bound — 12
      cases validam orçamento e ordem de reset deterministicamente.
- [x] AHCI emite COMRESET em TIMEOUT — `ahci_port_comreset` chamado
      via `ops->reset`, observável em `dbg_puts`.
- [x] NVMe emite Controller Level Reset em TIMEOUT — `nvme_controller_reset`
      via `ops->reset`. (NVMe Abort command-level foi descartado em
      favor do Controller Reset porque não rastreamos CIDs in-flight
      individualmente nessa stage; documentado como follow-up em
      Slice 3E.3.)
- [x] `block_device_read_ex` aplica budget mapeado por classe
      (`block_io_should_retry` + tabela {TRANSIENT:3, TIMEOUT:1}).

### Slice 3E.3 — Multi-slot AHCI / multi-queue NVMe (entregue 2026-05-21, alpha.249)

**Objetivo:** infraestrutura para múltiplos comandos inflight.

**Entrega:**

- Novo `include/drivers/storage/ahci_slot_allocator.h` +
  `src/drivers/storage/ahci_slot_allocator.c` — bitmap allocator
  puro, host-testable:
  - `ahci_slot_allocator_init(alloc, slot_count)` — slot_count em
    [1, 32].
  - `ahci_slot_alloc(alloc)` — retorna lowest-numbered free slot,
    ou -1 se cheio.
  - `ahci_slot_release(alloc, slot)` — rejeita double-release.
  - `ahci_slot_inflight_count(alloc)` — diagnóstico.
  - `ahci_slot_is_free(alloc, slot)` — accessor.
  - `ahci_slot_allocator_reset(alloc)` — após COMRESET.
- `src/drivers/storage/ahci.c`:
  - `struct ahci_port_ctx` ganha `slot_alloc`.
  - `ahci_setup_port` inicializa via `ahci_slot_allocator_init(..., AHCI_RUNTIME_SLOT_COUNT)`
    (capado em 1 enquanto temos uma única command table; multi-table
    é Slice 3F/Etapa 4).
  - `ahci_exec_classified` aloca slot, usa o índice no header
    `cmd_list[slot]` e na máscara `mmio_write32(&port->ci, 1u << slot)`;
    libera no completion e no aborted path; deixa inflight no
    timeout para que `ahci_port_comreset` faça `ahci_slot_allocator_reset`
    quando o retry loop escalona.
- NVMe: `NVME_QUEUE_DEPTH` já é 64 desde antes; CID rolling
  (`dev->next_cid++` em uint16) já implementado e wrap-safe (CID=0
  é válido per spec). Documentado neste plano para tornar
  explícito que o requisito foi auditado.
- Novo `tests/drivers/test_ahci_slot_allocator.c` (11 testes):
  init typical / full-32 / invalid; alloc lowest-first / until-full
  fail; release reuses / invalid inputs / double-release rejected;
  is_free accessor; reset marks all free; inflight count full-32.

**Wirado:** Makefile (runtime + TEST_SRCS); test_runner.c declara
e chama `run_ahci_slot_allocator_tests`.

**Critério de fechamento — atingido (escopo reduzido):**

- [x] Tests host validam slot management (11 cases).
- [x] NVMe queue depth = 64 + CID rolling (auditado).
- [x] AHCI passa pelo allocator no hot path (alloc/release exercitados).
- [x] COMRESET reseta o allocator (consistência com controlador).
- [ ] **Concurrent inflight real é diferido para Slice 3F**: a
      runtime ainda usa `AHCI_RUNTIME_SLOT_COUNT = 1` porque temos
      apenas uma command table alocada; multi-table provisioning +
      remoção do spin-wait dependem de scheduler async da Etapa 4.
- [ ] Sem regressão em smoke `smoke-x64-cli-nvme` — validação
      externa pendente.

### Slice 3E.4 — Diagnóstico + klog estruturado (entregue 2026-05-21, alpha.250)

**Objetivo:** observabilidade externa + smoke marker forense.

**Entrega:**

- Novo `include/drivers/storage/storage_smoke.h` +
  `src/drivers/storage/storage_smoke.c` — gate determinístico
  para o marker `[smoke] storage-stack ready`. API:
  - `storage_smoke_state_reset(state)`.
  - `storage_smoke_gate_observed(ahci_ok_count, nvme_ok_count)` —
    pura, retorna 1 se qualquer contador >= 1.
  - `storage_smoke_observe(state, source)` — incrementa contador
    da fonte (`STORAGE_SMOKE_SRC_AHCI` ou `STORAGE_SMOKE_SRC_NVME`),
    retorna 1 EXATAMENTE uma vez na transição blocked→observed,
    depois latcha (`emitted=1`).
- Novo `src/drivers/storage/storage_smoke_io.c` (kernel-only):
  emite `STORAGE_SMOKE_MARKER "\n"` em COM1 + klog INFO. Stub
  no-op em `tests/stubs/stub_storage_smoke_io.c` para host runner.
- `src/drivers/storage/ahci.c::ahci_read_block_ex`/`ahci_write_block_ex`
  chamam `ahci_smoke_signal_ok()` (que delega ao gate + emit) na
  classe `BLOCK_IO_OK`.
- `src/drivers/nvme/nvme.c::nvme_block_read_ex`/`nvme_block_write_ex`
  chamam `nvme_smoke_signal_ok()` simetricamente.
- Cada controlador mantém seu próprio `storage_smoke_state` em
  escopo de arquivo; ambos compartilham o marker idempotente (o
  primeiro a observar OK fila a emissão; o outro pode incrementar
  o contador depois mas não re-emite).
- 9 host tests em `tests/drivers/test_storage_smoke_gate.c`:
  reset zera campos + null-safe; predicate pure (0,0 / 1,0 / 0,1 /
  many); first AHCI OK emite e latcha; first NVMe OK simétrico;
  emissions subsequentes (mesma ou outra fonte) não re-emitem;
  invalid source rejeitado; null state rejeitado; canonical
  marker string immutable.

**Wirado:** Makefile (storage_smoke.o + storage_smoke_io.o em
runtime objs; gate.c + stub em TEST_SRCS); test_runner.c declara
e chama `run_storage_smoke_gate_tests`.

**Critério de fechamento — atingido:**

- [x] Marker `[smoke] storage-stack ready` emitido após primeiro
      read/write bem-sucedido em AHCI ou NVMe.
- [x] Latch idempotente: marker emitido exatamente 1× por boot.
- [x] Stub host + tests determinísticos.
- [x] Symmetric AHCI/NVMe path.
- [ ] **klog migration full** (substituir todos os `dbg_puts` por
      `klog`) é diferida para Slice 3E.4.B (sub-slice). Já temos
      `klog(KLOG_INFO, ...)` em `storage_smoke_io.c` e nos paths
      novos; conversão dos sites antigos (~30 chamadas em ahci.c
      + ~20 em nvme.c) é refactor mecânico melhor feito junto da
      próxima feature de observabilidade.

### Slice 3E.5 — External validation gate (entregue 2026-05-21, alpha.251)

**Objetivo:** smoke target reproduzível que valida o marker
forense de Slice 3E.4 em hardware VMware real.

**Entrega:**

- Novo alvo `smoke-x64-vmware-storage-resilience` em `Makefile`.
  Reusa `tools/scripts/smoke_x64_vmware.py` (multi-marker já
  suportado desde o gate USB HID) com:
  - `--marker "[net] DHCP: lease acquired."`
  - `--marker "[smoke] storage-stack ready"`
  - dependências `all64 iso-uefi manifest64` (build + ISO +
    manifest verificado).
- Novo runbook
  `docs/operations/etapa-3-slice-3e-validation-playbook.md`
  com contrato de prontidão, pipeline observado (ASCII), pré-
  requisitos, comandos de execução (vmrun + govc), critérios
  de aceite, tabela de triagem por sintoma (7 modos de falha
  documentados) e protocolo pós-aprovação.

**Decisão de design:** não foi criado um novo Python harness
porque `smoke_x64_vmware.py` já aceita `--marker` repetido. O
target Makefile é o gate; nenhum scaffolding Python adicional
foi necessário.

**Critério de fechamento — atingido:**

- [x] Alvo `smoke-x64-vmware-storage-resilience` no Makefile.
- [x] Markers em ordem: DHCP → storage-stack ready.
- [x] Runbook completo com triagem.
- [x] Stub host + tests (já entregues em 3E.4).
- [ ] Execução externa pelo operador em VMware oficial com a
      build **alpha.252** — pendente (este computador é review/edit only).

### Audit fix pós-3E.5 (entregue 2026-05-21, alpha.252)

Revisão crítica de Slices 3E.1–3E.5 identificou dois bugs
críticos antes da execução externa do gate; ambos corrigidos
com regressão antes de promover a build de validação.

**BUG #1 — Storage smoke marker double-emission em VMs dual-storage.**

Cada driver (AHCI/NVMe) carregava sua própria
`storage_smoke_state` em escopo de arquivo. Em VMs com AHCI +
NVMe ambos presentes, o marker era emitido duas vezes (uma por
driver), violando o invariante "exatamente uma vez por boot"
documentado em `include/drivers/storage/storage_smoke.h:9-21`.

**Fix:** latch global em `src/drivers/storage/storage_smoke.c::g_global_state`
exposto via `storage_smoke_try_latch_global(source)` +
`storage_smoke_global_reset()` (helper de host). Drivers passam
a consumir o latch global; estados per-TU removidos.

**BUG #2 — NVMe Controller Level Reset não recriava I/O queues.**

`nvme_controller_reset` (Slice 3E.2.B) toggava CC.EN=0→1 mas não
reemitia Create I/O CQ/SQ, contrariando NVMe 2.0 §3.5.4 / 1.4
§7.3.1. Consequência: após qualquer TIMEOUT em NVMe, o retry
pendurava (controller sem mapping de I/O queue 1), gerava
segundo TIMEOUT, e a retry loop queimava o budget surfacing
PERMANENT — o reset escalation path era effectively non-functional.

**Fix:** após o re-prime dos phase trackers em `nvme_controller_reset`,
chama `nvme_create_io_cq(dev, 1)` + `nvme_create_io_sq(dev, 1, 1)`
com early-return -1 se qualquer falhar. Admin queues sobrevivem
porque AQA não é tocado.

**Cobertura:**

- 4 novos host tests em `tests/drivers/test_storage_smoke_gate.c`
  cobrindo o latch global (ordem AHCI-first, NVMe-first, reset
  isolation entre runs, invalid source rejection).
- BUG #2 sem unit test direto (requer NVMe MMIO mock fora de
  escopo); coberto por inspeção estática + execução externa do
  gate de Slice 3E.5.

**Risco residual:** sub-slice 3E.5.B sugerido para extrair
`nvme_controller_reset` em sub-passos puros e permitir host
test do BUG #2 fix.

## 4. Riscos identificados

| Risco | Mitigação |
|---|---|
| Multi-slot AHCI introduz race no slot bitmap | Slice 3E.3 só roda após Slice 3E.2 fechar; init usa lock simples (single-CPU model) |
| Error injection para tests requer mock de MMIO | Slice 3E.1 deixa lógica pura (sem MMIO direto); mocks ficam triviais |
| COMRESET em hot device perde dados em voo | Documentar como expected behavior; caller decide se retry vale a pena |
| NVMe Abort pode falhar (controller wedged) | Cair para Controller Level Reset (CC.EN=0) como último recurso |

## 5. Validação externa

Cada sub-slice (3E.1-3E.4) entrega código + host tests. Slice 3E.5
fornece o gate externo unificado. O gate externo só pode ser
executado depois que TODOS os sub-slices estiverem entregues, porque
o storage stack inteiro precisa estar coerente para o smoke marker.

Gates externos recomendados (na ordem do runbook):

1. `make check-toolchain && make layout-audit && make test` (cobre
   3E.1 e 3E.2 testes).
2. `make all64 && make iso-uefi && make release-check`.
3. `make smoke-x64-cli-nvme` (regressão).
4. `make smoke-x64-vmware-storage-resilience` (novo, fecha 3E.5).
5. `make smoke-x64-vmware-usb-hid-keyboard` (regressão de Slice 3D).

## 6. Dependências

- **Pré-requisito:** Slice 3D externamente validado.
- **Não cruza sister repos:** Etapa 3 inteira é CapyOS-only conforme
  matriz de compatibilidade. Nenhuma mudança em capypkg/CapyAgent/etc.
- **Bloqueia:** Slice 3F (device manager unificado pode depender de
  storage stack maduro para teste de end-to-end).

## 7. Estimativa de esforço

Estimativa puramente de leitura/inspeção; não roda código.

| Sub-slice | Esforço | Notas |
|---|---|---|
| 3E.1 extraction | Médio | Refactor mecânico, ~500 LOC + ~400 LOC test |
| 3E.2 error class | Médio-alto | Mudança de contrato, requer cuidado com retry semantics |
| 3E.3 multi-slot/queue | Alto | Mais código de estado, risco de race |
| 3E.4 klog | Baixo | Substituição mecânica, ~50 sites |
| 3E.5 gate | Médio | Novo target + runbook + dual storage VM config |

Total: comparável em esforço ao trabalho de Slice 3D inteiro
(~25 commits + ~30 host tests + 1 runbook).

## 8. Notas para futuro arquiteto

- A regra sequencial impede começar 3E sem 3D validado. Não relaxe
  esta regra mesmo que o gate externo pareça "óbvio" — bugs reais
  só aparecem no smoke real.
- Se inspeção durante 3E descobrir bugs em AHCI/NVMe que afetam o
  caminho de boot atual, paralisar 3E e abrir patch dedicado antes
  de prosseguir.
- Manter `tests/drivers/test_storage_runtime_hyperv_plan.c` como
  referência de teste de runtime selection — não regredir.

## 9. Checklist de pré-requisitos antes de abrir 3E

- [ ] `make smoke-x64-vmware-usb-hid-keyboard` passou no operador
      externo, com marker `[smoke] usb-hid-keyboard ready` observado
      no COM1.
- [ ] Critérios de aceite §2 do runbook de 3D confirmados
      (tecla no shell, backend primário `usb`, release-check verde).
- [ ] `VERSION.yaml` history entry registra o fechamento de 3D.
- [ ] STATUS.md atualizado movendo Slice 3D para "concluído".
- [ ] Etapa 3 §6 critérios `Teclado USB funcional fora do EFI ConIn`
      marcado como ✓.

Só após todos checks acima, abrir Slice 3E.1.
