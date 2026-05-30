# CapyOS 0.8.0-alpha.260+20260525

**Data:** 2026-05-25  
**Canal:** alpha (experimental)  
**Versao:** `0.8.0-alpha.260+20260525`  
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)  
**Tipo:** pure-internal CapyOS release (nenhum contrato sister tocado)

## Resumo executivo

Batch de **hardening + cleanup** sobre `alpha.259`, sem impacto no caminho de
boot VMware oficial. Fecha follow-ups não-bloqueantes da Etapa 3 e completa as
fatias de código + host tests das fases C/D/E da Etapa 4, deixando a etapa
pendente apenas da validação externa (Fase F) em VMware. Todas as ABIs públicas
novas são aditivas; nenhuma linha da `compatibility-matrix.md` muda.

## Mudanças entregues

### Frente 1 — Sub-slice 3E.4.C concluída (migração `dbg_*` → `klog`)

- Migração mecânica de `dbg_puts`/`dbg_hex32`/`dbg_putc` para
  `klog`/`klog_hex` em **11 TUs adicionais (~158 sites)**: `format_mount.c`,
  `filesystem_helpers.c`, `public_mount_api.c`, `mount_initialize.c`,
  `crypt_aes_xts.c` (batch LF) e `ramdisk.c`, `buffer_cache.c`,
  `offset_wrapper.c`, `chunk_wrapper.c`, `efi_block.c`, `storage_runtime.c`
  (batch CRLF, normalizado via `perl -i -pe 's/\r$//g'`).
- Helpers locais `dbg_putc`/`dbg_puts`/`dbg_hex32` e variantes `_serial`
  removidos de dois headers internos (`capyfs_runtime_internal.h`,
  `kernel_volume_runtime_internal.h`). Utilitários puros big-endian
  preservados (`dbg_be32_local`, `crypt_be32`, `ramdisk_be32`).
- Efeito: traces de produção vão para o klog ring (recuperável via
  `klog_dump`, persistido pelo logger service) em vez do port `0xE9`
  (QEMU-only). 6 arquivos migraram de CRLF para LF como efeito colateral.

### Frente 2 — Sub-slice 3E.5.B (`nvme_controller_reset` host-testável)

- Novo `include/drivers/nvme/nvme_reset.h` + `src/drivers/nvme/nvme_reset.c`
  expõem a lógica não-MMIO em símbolos puros: `nvme_reset_reprime_queue_state`,
  predicados CSTS (`nvme_reset_csts_rdy_cleared`/`_set`) e o planner
  `nvme_reset_next_admin_action` (ordem `CREATE_IO_CQ → CREATE_IO_SQ → DONE`).
- `nvme_controller_reset` refatorado para dirigir o planner em loop,
  travando o fix da BUG #2 (alpha.252) via teste host. Novo
  `tests/drivers/test_nvme_controller_reset.c` com **13 casos**.
- Extensão: predicado `nvme_reset_csts_fatal` (CSTS.CFS) checado em **4
  pontos** do reset path (dentro e após cada spin loop) para bail early com
  log forense quando o controlador entra em fatal status; **5 host tests**.

### Frente 3 — Etapa 4 Fase E (`thread-crash-survives`)

- Novo `include/kernel/thread_crash_smoke.h` + `src/kernel/thread_crash_smoke.c`
  (latch puro) + `thread_crash_smoke_io.c` (emissão COM1). Latch: uma saída de
  processo com `exit_code >= 128` (encoding POSIX death-by-signal usado por
  `process_exit(128 + vector)`) seguida de N ticks de scheduler.
- Hook em `process_exit` + `scheduler_tick`, gated por
  `CAPYOS_THREAD_CRASH_SURVIVES_SMOKE` (custo zero em builds de produção).
  **10 host tests** + novo target `make smoke-x64-vmware-thread-crash-survives`.

### Frente 4 — Etapa 4 Fase D follow-up (cursor erase escopado a overlap)

- Na composição parcial, o compositor apaga a área do cursor **apenas quando
  algum dirty rect intersecta o sprite**, eliminando flicker breve em hosts com
  framebuffer lento (Hyper-V Gen2). `compositor_render_cursor` ganhou erase do
  retângulo antigo no MOVE mesmo após full-present, preservando "sem rastro".
- Novo campo aditivo `cursor_erases_partial` em `struct compositor_stats` como
  evidência host-side; novo teste `test_compositor_cursor_erase_only_on_overlap`.

### Frente 5 — Hardening da revisão regressiva (P1)

- **P1-A:** novo work item `SYSTEM_WORK_STORAGE_HYPERV_RETRY` (self-disable em
  non-Hyper-V + backoff exponencial).
- **P1-C:** `vmbus_transport_drain_simp` com cap de 256 iterações e continue
  apenas em retorno positivo (antes podia pinar a thread se `g_simp_page` fosse
  desanexada mid-drain).
- **P1-E:** removido o par hardcoded 800×600 em `mouse_ps2_init` que
  sobrescrevia `mouse_set_bounds` prévio.
- **P1-F:** guard defense-in-depth `i < USB_MAX_DEVICES` em `usb_poll_all`.
- P1-B (login_runtime polls 12×) classificado como design intencional; P1-D
  (`vmbus_mouse` experimental) permanece gated por flag.

### Auditorias e fatias adicionais

- **Driver safety audit (xHCI HSE):** `XHCI_STS_HSE` (Host System Error) passou
  a ser checado no topo dos 4 spin loops de `xhci_reset`/`start`/`stop` com logs
  forenses, com precedência HCH-vs-HSE corrigida por code review.
- **Slice 3F (extração inicial):** novo `include/drivers/storage/ahci_dispatch.h`
  + `ahci_dispatch.c` com 5 símbolos puros (classifier de tick, fan-in de
  completions, popcount, gate de admissão, first-slot) — **31 host tests** — e
  o accessor `ahci_slot_inflight_mask` no allocator — **9 host tests**.
- **Gate agregado Etapa 4:** novo target `make smoke-x64-vmware-etapa-4`
  consolida os 5 markers (DHCP → gui-session → scheduler-fairness →
  compositor-damage-track → thread-crash-survives) em um único boot.

## Estado da Etapa 4

Com esta release, as fases **A-E** da Etapa 4 (CapyDisplay 2D + scheduler/
multithread) estão atendidas em **código + host tests**; resta apenas a **Fase F
— validação externa** em VMware oficial. Runbook:
[`docs/operations/etapa-4-external-validation-playbook.md`](../operations/etapa-4-external-validation-playbook.md).

## Mudanças de contrato

**Nenhuma.** Tudo dentro do CapyOS core. ABIs públicas novas
(`drivers/nvme/nvme_reset.h`, `drivers/storage/ahci_dispatch.h`,
`kernel/thread_crash_smoke.h`) são aditivas; `struct compositor_stats` ganhou
campo aditivo `cursor_erases_partial`. Adapter `services/capypkg`, activation
gate `kernel/module_gate.c`, install profile schema e os sister repos
(`CapyUI` `2.13.1`, `CapyAgent`, `CapyBrowser`, `CapyCodecs`, `CapyLang`,
`CapyBenchmark`) permanecem intocados.

## Evidências internas

Validação local (host-only, neste Mac):

- `make test` — **OK** (suíte host completa passa).
- `make test-capypkg` — **OK**.
- `make layout-audit` — **OK** sem warnings.
- `make version-audit` — **OK** (`current`/`extended` alinhados com `VERSION.yaml`).

Validação externa **pendente** (toolchain cruzado `x86_64-*-gcc` ausente nesta
máquina):

- `make all64 TOOLCHAIN64=elf` / `make iso-uefi TOOLCHAIN64=elf` — link + ISO.
- `make release-check` — gate agregado.
- `make smoke-x64-vmware-etapa-4` — Fase F da Etapa 4 (5 markers em um boot).
- `make smoke-x64-vmware-usb-hid-keyboard` / `make smoke-x64-vmware-storage-resilience`
  — regressões da Etapa 3.

## Próximos passos

1. **Validação externa** dos gates acima para ratificar a release e fechar a
   Fase F da Etapa 4 (`etapa-transition` para abrir a Etapa 5 — TLS userland real).
2. **Slices 3F-3J** seguem como follow-ups oportunísticos não-bloqueantes.
3. **Track Hyper-V** (Slice 3 StorVSC data plane) permanece laboratorial,
   sem alterar a plataforma oficial.
