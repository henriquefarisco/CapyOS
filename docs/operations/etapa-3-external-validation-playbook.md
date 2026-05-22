# Etapa 3 — External validation playbook

**Scope:** orchestrates the single external gate that promotes Slice 3D
(SET_CONFIGURATION + HID boot protocol + Configure Endpoint + interrupt
transfer + HID polling) of the CapyOS Etapa 3 to "validated", unblocking
Slice 3E (storage hardening + device manager).

**Authority:**

- `docs/plans/active/capyos-master-plan.md` §6 (Etapa 3 contract).
- `docs/architecture/etapa-3-driver-foundation-plan.md` §6 (Slice 3D
  contract) and §9.6 (Próximo gate ativo).
- `docs/plans/STATUS.md` (current alpha + bloqueador).

**Local execution policy reminder:** this machine is review/edit only.
Every gate below MUST be executed externally (operator workstation or
release CI), never on the local CapyOS workspace machine.

## Pipeline being validated (visão de conjunto)

O gate `smoke-x64-vmware-usb-hid-keyboard` testemunha o caminho
completo do hardware USB até o shell stdin. Cada caixa abaixo é uma
camada que precisa estar operacional para o marker emitir:

```
+---------------------------------------------------------------+
|                       VMware USB HID keyboard                  |
+---------------------------------------------------------------+
                              | (operator types a key)
                              v
+---------------------------------------------------------------+
| xHCI controller writes Transfer Event to evt_ring (HW)         |
+---------------------------------------------------------------+
                              |
                              v
+---------------------------------------------------------------+
| SYSTEM_WORK_USB_POLL  (kernel_work_usb_poll, interval=1 tick)  |
|   -> usb_poll_all                                              |
|        -> per device:                                          |
|             -> xhci_poll_interrupt                             |
|                  -> xhci_event_pump (drains evt_ring)          |
|                       -> xhci_dispatch_event                   |
|                            -> intr_pending[slot] latched       |
|                  -> copy report from intr_buffer to caller     |
|                  -> requeue NORMAL TRB + ring doorbell         |
+---------------------------------------------------------------+
                              |
                              v
+---------------------------------------------------------------+
| usb_hid_handle_keyboard_report                                 |
|   -> diff vs prev_keys (suppress repeats)                      |
|   -> translate HID usage to ASCII (Shift + Ctrl support)       |
|   -> kbd_buffer_push(c)                                        |
|        -> kbd_chars_received++                                 |
|        -> usb_hid_keyboard_smoke_observe                       |
|             -> first transition: emit marker once via COM1     |
|                + klog mirror "[usb-hid] Slice 3D smoke marker  |
|                  emitted on COM1."                             |
+---------------------------------------------------------------+
                              |
                              v
+---------------------------------------------------------------+
| (consumer side, shell loop in another invocation)              |
|                                                                |
| x64_input_poll_char (input runtime dispatcher)                 |
|   -> iterates order[]: PS/2 -> Hyper-V -> USB -> EFI -> COM1   |
|   -> case X64_INPUT_BACKEND_USB:                               |
|        -> usb_hid_keyboard_poll                                |
|             -> kbd_buffer_pop                                  |
|        -> forward char to caller                               |
+---------------------------------------------------------------+
                              |
                              v
+---------------------------------------------------------------+
|                  Shell stdin (visible on screen)                |
+---------------------------------------------------------------+
```

**Marker fail-closed contract:** o marker `[smoke] usb-hid-keyboard
ready` só é emitido quando ambas as condições estão simultaneamente
verdadeiras:

1. `g_hid.kbd_configured_count >= 1` — pelo menos um HID keyboard
   alcançou `USB_DEV_CONFIGURED` (SET_CONFIGURATION + HID
   SET_PROTOCOL(BOOT) + Configure Endpoint OK).
2. `g_hid.kbd_chars_received >= 1` — pelo menos um caractere ASCII
   foi bufferizado por `kbd_buffer_push` a partir de um interrupt
   transfer real.

Latch idempotente em `struct usb_hid_keyboard_smoke_state`: marker
emite exatamente uma vez por boot.

## 0. Pre-requisitos

1. Aceite operacional da Etapa 2 já registrado em 2026-05-18.
2. Workspace na revisão CapyOS `0.8.0-alpha.245+20260521` ou superior
   com o scaffolding desta playbook aplicado:
   - `include/drivers/usb/usb_hid_smoke.h`
   - `src/drivers/usb/usb_hid_smoke.c`
   - `src/drivers/usb/usb_hid_smoke_io.c`
   - `tests/stubs/stub_usb_hid_smoke_io.c`
   - `tests/drivers/test_usb_hid_smoke_gate.c`
   - alvo `smoke-x64-vmware-usb-hid-keyboard` no `Makefile`
3. Hardware/VMware:
   - VMware Workstation/ESXi com a VM oficial UEFI + E1000.
   - Controlador USB **xHCI 1.0+** habilitado na VM
     (em `.vmx`: `usb_xhci.present = "TRUE"`; remover qualquer
     `usb.present = "TRUE"` que force UHCI).
   - Teclado USB HID conectado e exposto à VM
     (`Edit virtual machine settings → USB Controller → USB compatibility
     = USB 3.1`; em seguida no menu **VM → Removable Devices** marcar
     `Connect (Disconnect from host)` para o teclado HID).
   - Serial COM1 redirecionado para arquivo/named pipe que o harness
     `tools/scripts/smoke_x64_vmware.py` consiga ler.

## 1. Gate ordering (estritamente sequencial)

Execute na ordem abaixo. Pare imediatamente no primeiro fail.

### 1.1 Host gates (build/lint)

```
make check-toolchain
make layout-audit
make test
```

`make test` deve incluir a saída `[tests] usb_hid_smoke_gate OK`
(suite nova introduzida por este scaffolding). Se ela faltar ou
falhar, **não prossiga** — o marker emitido pelo kernel não tem
contrato verificável.

### 1.2 Build gates

```
make all64
make iso-uefi
make manifest64
make release-check
```

`release-check` agrega `test + all64 + iso-uefi` e gera os artefatos
de release em `build/`. Esta playbook não publica tag; consulte
`/publish-release-tag` workflow para a etapa de publicação.

### 1.3 External smoke gate (Slice 3D)

```
make smoke-x64-vmware-usb-hid-keyboard \
  SMOKE_X64_VMWARE_ARGS="--vmx <path-to-vmx> \
                         --serial-log <path-to-com1.log> \
                         --boot-timeout 90 \
                         --idle-timeout 120"
```

Após o boot completar e o desktop ativar, o operador deve
**digitar pelo menos uma letra ASCII** no foco da janela do
guest. O harness escuta COM1 até ver, na ordem:

1. `[net] DHCP: lease acquired.`
2. `[smoke] usb-hid-keyboard ready`

O segundo marker é emitido pelo kernel via
`src/drivers/usb/usb_hid_smoke_io.c` exatamente uma vez, e só
depois que:

- pelo menos um device USB HID alcançou `USB_DEV_CONFIGURED`
  (SET_CONFIGURATION + HID SET_PROTOCOL(BOOT) + Configure
  Endpoint OK), e
- `usb_hid_handle_keyboard_report` bufferizou ao menos um
  caractere ASCII real vindo do interrupt transfer.

O latch idempotente (`struct usb_hid_keyboard_smoke_state`) garante
que múltiplas teclas não re-emitem o marker. A ausência total do
marker indica regressão num dos passos do Slice 3D — consulte a
seção 3 abaixo para triagem.

## 2. Critérios de aceite externos

O operador só pode declarar Slice 3D validado quando TODOS os
critérios abaixo estiverem satisfeitos no mesmo ciclo de boot:

- [ ] `make smoke-x64-vmware-usb-hid-keyboard` saiu com código 0.
- [ ] Log COM1 contém, na ordem, `[net] DHCP: lease acquired.` e
      `[smoke] usb-hid-keyboard ready`.
- [ ] `dmesg`/klog do guest mostra, antes do marker de smoke:
      `[usb] XHCI addressed slot N`, `[boot] USB HID class initialised.`,
      `[usb-hid] HID keyboard found.` e finalmente
      `[usb-hid] Slice 3D smoke marker emitted on COM1.`
      (mirror forense da emissão).
- [ ] Hotplug: desconectar o teclado USB, reconectar e digitar mais
      uma letra — o klog do guest mostra novos reports keyboard
      sendo bufferizados (não é mais exigido novo marker; o latch é
      one-shot e o caminho continua íntegro).
- [ ] **(novo, §14.1)** Ao digitar uma letra ASCII no shell
      gráfico após o marker aparecer, a letra é renderizada no
      terminal corretamente. Isso confirma o caminho completo
      `xHCI interrupt transfer → usb_hid_handle_keyboard_report
      → kbd_buffer_push → usb_hid_keyboard_poll →
      x64_input_poll_char (X64_INPUT_BACKEND_USB) → shell stdin`,
      encerrando o critério Etapa 3 §6 "teclado USB funcional
      fora do EFI ConIn".
- [ ] **(novo, §14.1)** O backend ativo reportado por
      `x64_input_primary_backend_name` (visível em
      `dmesg`/diagnostics) é `usb` quando rodando em VMware sem
      PS/2/Hyper-V. Em ambientes com PS/2 habilitado, USB fica
      em terceira posição (PS/2 → Hyper-V → USB → EFI) — o
      teste de aceite só exige que USB esteja registrado e
      drene quando os backends acima não produzirem.
- [ ] `release-check` continua verde com o mesmo `RELEASE_TAG`.

## 3. Triagem de falha

| Sintoma | Provável causa | Onde olhar |
|---|---|---|
| `[net] DHCP: lease acquired.` aparece, marker não | XHCI inicializou mas keyboard não chegou a `USB_DEV_CONFIGURED` | `klog`: WARN `[usb] XHCI HID interrupt endpoint config failed` |
| Marker nunca aparece e não há `[usb] XHCI ...` | Controlador xHCI não detectado pelo PCIe scan | `src/drivers/usb/xhci.c::xhci_find`, BAR0, `prog_if=0x30` |
| `[usb-hid] HID keyboard found.` aparece mas marker não | Interrupt transfer não chega; Configure Endpoint pode ter falhado em silêncio | `src/drivers/usb/xhci.c::xhci_configure_interrupt_endpoint` retorno e `xhci_poll_interrupt` cycle bit |
| `[boot] USB HID class initialised.` ausente | Nenhum HID em `USB_DEV_CONFIGURED` ao fim do bring-up | `src/arch/x86_64/kernel_services.c::kernel_work_usb_bringup` e WARNs precedentes em `usb_enumerate_devices` |
| Klog mostra `[boot] USB HID class init skipped (no HID device).` | `SYSTEM_WORK_USB_POLL` ficou desabilitado por design (sem HID) | Confirmar que a VM realmente expõe o teclado USB antes do bring-up tick |
| Marker emitido duas vezes | Regressão do latch | `usb_hid_keyboard_smoke_observe` em `src/drivers/usb/usb_hid_smoke.c` |

## 4. Pós-validação

Quando o gate fechar:

1. Atualizar `docs/architecture/etapa-3-driver-foundation-plan.md`
   §6.3 marcando o checkbox `Em VMware com USB HID keyboard, digitar
   uma letra entrega o caractere correto via usb_hid_keyboard_poll`.
2. Atualizar `docs/plans/STATUS.md` mudando a linha da Etapa 3 para
   indicar que Slice 3D foi validado externamente e o próximo bloco
   é Slice 3E (storage hardening + device manager unificado).
3. Adicionar entry em `VERSION.yaml` history com o `RELEASE_TAG` e o
   evidence path do COM1 log.
4. Iniciar o planejamento de Slice 3E em
   `docs/architecture/etapa-3-driver-foundation-plan.md` §10.

Não promover sister-repo contracts neste passo: a Etapa 3 não cruza
a fronteira de nenhuma repo apartada
(`docs/reference/integration/compatibility-matrix.md` confirma
Etapas 4 e 6 como a próxima fronteira de cross-repo).
