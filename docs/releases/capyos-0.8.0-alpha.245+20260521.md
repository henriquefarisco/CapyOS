# CapyOS 0.8.0-alpha.245+20260521 — Slice 3D fechado

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 3 (Driver framework + USB HID + storage estável) — em andamento; Slice 3D fechado, Slice 3E pendente

## Resumo executivo

Esta alpha fecha o **Slice 3D** da Etapa 3 — a onda completa de
suporte a entrada USB HID via xHCI com interrupt polling. O gate
externo `make smoke-x64-vmware-usb-hid-keyboard` foi validado em
VMware + UEFI + E1000 com teclado USB HID real anexado, marker
`[smoke] usb-hid-keyboard ready` observado no COM1. Junto com o
fechamento do slice principal, foram entregues 3 follow-ups
arquiteturais (§14.1-§14.3), 5 audit fixes (§15.1-§15.5) e 1
bug crítico descoberto em audit (slot reuse collision em
port-cycle). 25 novos host tests fortalecem a regressão. Plano da
próxima slice (3E — storage hardening + recoverable I/O errors)
foi rascunhado.

**Não promove a Etapa 3** — Slices 3E (storage), 3F (device
manager unificado), 3G (USB mass storage), 3H (VirtIO), 3I
(VMware SVGA) continuam pendentes.

## Mudanças entregues

### Scaffolding do gate externo (`[smoke] usb-hid-keyboard ready`)

- `include/drivers/usb/usb_hid_smoke.h` + `src/drivers/usb/usb_hid_smoke.c`:
  gate puro host-testável com latch idempotente
  (`USB_HID_KEYBOARD_SMOKE_GATE_VERSION = 1`). Marker emite quando
  `kbd_configured_count >= 1 AND kbd_chars_received >= 1`.
- `src/drivers/usb/usb_hid_smoke_io.c`: backend kernel que emite
  o marker via COM1 + klog mirror.
- `tests/stubs/stub_usb_hid_smoke_io.c`: stub host-side.
- `tests/drivers/test_usb_hid_smoke_gate.c`: 6 testes cobrindo
  predicate, latch idempotente, version constant, recovery após
  reset.
- `Makefile`: alvo `smoke-x64-vmware-usb-hid-keyboard`.
- `docs/operations/etapa-3-external-validation-playbook.md`: runbook
  com diagrama ASCII completo do pipeline xHCI → shell stdin.

### Follow-ups §14.1-§14.3

- **§14.1 USB no input runtime backend dispatch.**
  `src/arch/x86_64/input_runtime.c` expandiu `order[5]` para
  incluir USB como backend primário em VMware, seguido por
  PS/2/Hyper-V/EFI/COM1 fallback. EFI fallback agora rejeita
  builds < 2025-01-01.
- **§14.2 Event ring multi-endpoint.** `xhci_event_pump` em
  `src/drivers/usb/xhci.c` drena `evt_ring` e despacha por
  slot/endpoint, permitindo múltiplos endpoints concorrentes em
  vez do polling single-endpoint anterior. 5 novos testes em
  `test_xhci_address_device.c`.
- **§14.3 Hot-unplug teardown.** Nova API `xhci_release_slot`
  emite Disable Slot ao controlador, libera EP0 ring + interrupt
  ring + device context, zera DCBAA entry e limpa pending event
  latches. 6 novos testes.

### Audit fixes §15.1-§15.5

- **§15.1 Hotplug periódico armado.** Nova API `xhci_port_ack_csc`
  faz RW1C correto de CSC preservando outros change bits
  (xHCI 1.2 §5.4.8). `usb_hotplug_check` reescrito para iterar
  portas, ack'ar CSC e re-enumerar UMA vez por tick;
  `kernel_work_usb_poll` agora chama `usb_hotplug_check()` antes
  de `usb_poll_all()`. 2 novos testes.
- **§15.2 Slot leak em Address Device.** `xhci_address_device`
  agora delega à `xhci_release_slot` na branch de falha,
  recuperando ownership do slot no controlador.
- **§15.3 LEDs de teclado dirigidos.** Nova API
  `usb_hid_send_led_report` em `usb_core.h` despacha HID
  SET_REPORT (Output) via control transfer.
  `usb_hid_handle_keyboard_report` detecta Caps/Num/Scroll Lock
  presses, toggle latched led_state, dispatch SET_REPORT. Caps
  Lock afeta tradução de letras (Shift inverte). 2 novos testes.
- **§15.4 Ctrl combinations.** `usb_hid_handle_keyboard_report`
  detecta modifiers bit 0 (Left Ctrl) + bit 4 (Right Ctrl) e
  traduz alpha keys para control codes (Ctrl+A → 0x01,
  Ctrl+Z → 0x1A). Compatível com Shift simultâneo. 2 novos testes.
- **§15.5 PED wait pós-reset.** `xhci_port_reset` agora
  busy-waits por PED após observar PRC, bounded em 10000
  iterações; retorna -3 explícito se PED nunca asserted
  (controlador não-compliant).

### Bug crítico audit-discovered (Bug W)

- **Slot reuse collision em port-cycle.** Quando CCS=0/CSC=1
  acontecia sem teardown explícito, o slot ficava lingering e
  Enable Slot subsequente podia colidir. Nova função
  `usb_release_stale_slots` em `src/drivers/usb/usb_core.c`
  chamada no INÍCIO de cada `usb_enumerate_devices`, percorre
  ports e libera slots cujo CCS=0 ou CSC=1.

## Evidências internas

- 25 novos host tests:
  - 6 em `tests/drivers/test_usb_hid_smoke_gate.c`.
  - 5 (event pump) + 6 (release slot) + 2 (port ack CSC) em
    `tests/drivers/test_xhci_address_device.c`.
  - 2 (Ctrl combinations) + 2 (Caps/LED) em
    `tests/drivers/test_usb_hid_init.c`.
  - 4 stubs auxiliares em `tests/stubs/`.
- Validação interna por inspeção estática: cross-reference de
  callers, headers, Makefile wiring e docs. Conforme a política
  de execução local, gates externos (`make test`, `make all64`,
  `make smoke-x64-vmware-usb-hid-keyboard`, `make release-check`)
  foram executados pelo operador externo.

## Mudanças de contrato

**Nenhuma.** Esta alpha é puramente interna ao CapyOS core. Não
toca:

- Manifest format line-oriented do `capypkg`.
- Descritor canônico Ed25519.
- Quotas em `include/services/capypkg.h`.
- Escopo do `install_root`.
- Alfabeto de `name`/`depends`.
- Layout do marker de ativação.
- Nomes ABI canônicos.
- Versões pinadas das sister repos em `compatibility-matrix.md`.

ABI pública preservada — todos os novos símbolos
(`xhci_release_slot`, `xhci_port_ack_csc`, `usb_hid_send_led_report`)
são aditivos.

## Atualizações de documentação

- `docs/architecture/etapa-3-driver-foundation-plan.md`: §13-§15
  atualizados marcando follow-ups e audit issues como endereçados.
- `docs/architecture/etapa-3-slice-3e-plan.md` (**novo**): plano
  subordinado de Slice 3E rascunhado com 5 sub-slices, riscos,
  estimativa e checklist de pré-requisitos.
- `docs/operations/etapa-3-external-validation-playbook.md`:
  diagrama ASCII do pipeline xHCI → shell + marker contract.
- `docs/plans/STATUS.md`: header atualizado, Slice 3D marcado como
  fechado, próximo bloco apontando para Slice 3E.
- `docs/plans/active/capyos-master-plan.md`: §6 e §20 atualizados.
- `docs/reference/integration/compatibility-matrix.md`: CapyOS row
  bumped para alpha.245.

## Próximos passos

1. **Slice 3E.1 — host-testable extraction de AHCI/NVMe command
   builders.** Extrair `ahci_build_h2d_fis`, `ahci_build_command_header`,
   `ahci_build_prdt_entry` para `src/drivers/storage/ahci_commands.c`;
   `nvme_build_*_cmd` para `src/drivers/nvme/nvme_commands.c`. Sem
   mudança funcional, apenas tornar host-testable.
2. Slice 3E.2 — error classification + recoverable retry policy
   unificada entre AHCI/NVMe/ATA-PIO.
3. Slice 3E.3 — multi-slot AHCI / multi-queue NVMe.
4. Slice 3E.4 — klog estruturado + smoke marker storage.
5. Slice 3E.5 — gate externo `smoke-x64-vmware-storage-resilience`.

Conforme `docs/architecture/etapa-3-slice-3e-plan.md` §3.
