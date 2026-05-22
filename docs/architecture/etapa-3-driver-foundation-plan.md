# Etapa 3 — Driver foundation plan (XHCI + USB HID + storage)

**Status:** Etapa 3 **concluída** em 2026-05-21 (alpha.253) após aprovação externa do gate `make smoke-x64-vmware-storage-resilience`. Etapa 4 abriu na mesma data (CapyDisplay 2D + scheduler/multithread). Este documento permanece como referência histórica e operacional dos sub-slices entregues; também documenta os follow-ups não-bloqueantes (3F-3J + 3E.4.C + 3E.5.B) que ficam como bug fixes oportunísticos.

**Escopo:** detalha a primeira onda de trabalho da Etapa 3 nova (Driver framework + entrada USB HID + storage estável), conforme `docs/plans/active/capyos-master-plan.md` §6.

**Audiência:** desenvolvedor que precisa entender o que foi entregue na Etapa 3 ou que vai tocar follow-ups (Slices 3F-3J, sub-slices 3E.4.C e 3E.5.B).

## Implementation log

| Slice | Estado | Notas |
|---|---|---|
| 3A | Código entregue 2026-05-15, validação externa própria pendente | bug de state machine corrigido; 6 testes host-side adicionados |
| 3B | Código entregue 2026-05-18, validação externa própria pendente | XHCI Address Device + enumeração até ADDRESSED + testes host-side de contexto/ring |
| 3C | Código entregue 2026-05-18, validação externa própria pendente | control transfer GET_DESCRIPTOR + descriptor parsing; device permanece ADDRESSED enriquecido até Configure Endpoint |
| 3D | Código entregue 2026-05-18, scaffolding de gate entregue 2026-05-21, validação externa pendente | SET_CONFIGURATION + HID boot protocol + Configure Endpoint + interrupt transfer + HID polling real + marker `[smoke] usb-hid-keyboard ready` |

## 1. Estado atual auditado (2026-05-18)

### 1.1 XHCI

- `src/drivers/usb/xhci.c` entrega `xhci_find`, `xhci_init`, `xhci_reset`, `xhci_start`, `xhci_stop`, `xhci_port_reset`, `xhci_port_get_status`, `xhci_enable_slot`, `xhci_address_device`, `xhci_control_transfer`, `xhci_configure_interrupt_endpoint` e `xhci_poll_interrupt`.
- `xhci_init` aloca DCBAA, command ring (256 TRBs, link no fim), event ring (256 TRBs), ERST formal de segmento único, programa `DCBAAP`, `CRCR`, `CONFIG.MaxSlotsEn`, `ERSTSZ`, `ERSTBA` e `ERDP`.
- `xhci_enable_slot` emite Enable Slot TRB, faz polling no event ring completo de 256 entradas respeitando cycle bit, consome eventos não-command até a Command Completion, zera `slot_id` em falha e retorna `slot_id` válido em sucesso.
- `xhci_address_device` monta Input Context 32/64 bytes, Device Context, transfer ring de EP0, emite Address Device TRB, atualiza `DCBAA[slot_id]` e mantém contexto/EP0 ring em ownership do controlador.
- `xhci_control_transfer` usa o transfer ring persistente de EP0 para Setup/Data/Status TRBs, toca doorbell do slot com target EP0 e aguarda Transfer Event matching slot+endpoint.
- `xhci_configure_interrupt_endpoint` monta Input Context de Configure Endpoint para interrupt IN, cria transfer ring por slot, pré-arma Normal TRB e toca doorbell do endpoint.
- `xhci_poll_interrupt` consome Transfer Event matching slot+endpoint, copia report para o chamador, rearma Normal TRB e toca doorbell do endpoint.
- `xhci_find_keyboard` e `xhci_keyboard_poll` permanecem como API legada não usada pelo caminho USB core/HID.
- ERST formal inicial presente; IRQ-driven ainda ausente.
- Interrupter **não habilitado** (`XHCI_CMD_INTE` nunca é setado).

### 1.2 USB core

- `src/drivers/usb/usb_core.c`.
- `usb_core_init` chama `xhci_find` + `xhci_init` + `xhci_start`. OK.
- `usb_enumerate_devices` detecta `PORTSC.CCS`, reseta porta, chama `xhci_enable_slot` + `xhci_address_device`, grava `slot_id`, avança para `USB_DEV_ADDRESSED`, lê Device/Configuration descriptors via GET_DESCRIPTOR, emite SET_CONFIGURATION, emite HID SET_PROTOCOL(BOOT) e tenta Configure Endpoint para HID interrupt IN; falhas ficam em `USB_DEV_ERROR` para Enable/Address e permanecem `ADDRESSED` quando descriptor/configuração de endpoint falha.
- Estados definidos (`include/drivers/usb/usb_core.h`): `DISCONNECTED, ATTACHED, ADDRESSED, CONFIGURED, ERROR`; `CONFIGURED` é alcançado no Slice 3D após SET_CONFIGURATION + HID SET_PROTOCOL(BOOT) + Configure Endpoint bem-sucedidos.
- `src/drivers/usb/usb_descriptors.c` contém helpers host-testáveis para montar GET_DESCRIPTOR setup packet e parsear Device/Configuration/Interface/Endpoint descriptors, incluindo `configuration_value` e `interface_number` para requests de configuração/HID.
- `usb_poll_all` faz polling não bloqueante de endpoints interrupt configurados e despacha reports para `usb_hid`.
- Hotplug: `usb_hotplug_check` re-chama enumerate quando `PORTSC.CSC` set. Lógica básica OK.

### 1.3 USB HID

- `src/drivers/usb/usb_hid.c`.
- Tabelas scancode→ASCII (boot protocol US layout) e shift variant prontas.
- `usb_hid_init` aceita `USB_DEV_ADDRESSED` ou `USB_DEV_CONFIGURED` quando `class_code` já foi populado, preservando o contrato entregue no Slice 3A.
- `usb_hid_handle_keyboard_report` traduz boot keyboard reports para o ring buffer ASCII.
- `usb_hid_handle_mouse_report` publica o último report de mouse; `usb_hid_mouse_poll` entrega deltas/botões.

### 1.4 Storage (contexto, fora do primeiro slice)

- `src/drivers/storage/ahci.c` (599 LOC): substancial, sem TODOs `grep`-able. Cobertura provável: identify, R/W via FIS.
- `src/drivers/nvme/nvme.c` (538 LOC): substancial. Avaliação detalhada fica para slice 4 (fora desta primeira onda).

### 1.5 Tests

- `tests/drivers/test_usb_hid_init.c` cobre contrato 3A de descoberta HID sobre dispositivos ADDRESSED/CONFIGURED e contrato 3D de handlers keyboard/mouse.
- `tests/drivers/test_xhci_address_device.c` cobre contrato 3B de speed/MPS, layout do Input Context 32/64 bytes, avanço/wrap do command/event ring, toggle de cycle bit do event ring, skip de eventos não-command, layout/wrap de control transfer EP0, SET_CONFIGURATION, HID SET_PROTOCOL(BOOT), Configure Endpoint, Normal TRB e polling interrupt.
- `tests/drivers/test_usb_descriptor_parse.c` cobre GET_DESCRIPTOR setup packet, device/config parsing, keyboard/mouse HID, descriptor truncado, overflow de endpoints e interface HID em dispositivo composto.
- `tests/stubs/stub_usb_core.c` fornece tabela USB controlável para testes HID.

## 2. Estratégia de slicing

Quebra a primeira parte da Etapa 3 em 4 slices verticais auditáveis. Cada slice fecha código + testes host-side + documentação + critérios de validação externa.

| Slice | Tema | Risco | Saída | Smoke externo |
|---|---|---|---|---|
| 3A | Bug fix de regressão: state machine USB | baixo | `usb_hid_init` deixa de ser broken por contrato; estado intermediário ADDRESSED é alcançado mesmo com stubs | regressão local via `make test` |
| 3B | XHCI device enumeration real | médio | `xhci_address_device` real + Input Context; enumerate progride para ADDRESSED | smoke VMware com XHCI |
| 3C | Control transfer + descriptor parsing | médio | GET_DESCRIPTOR sobre EP0; parsing de device/config/interface/endpoint descriptors; estado ADDRESSED enriquecido com class/endpoints | smoke USB HID descriptor |
| 3D | SET_CONFIGURATION + HID boot protocol + Configure Endpoint + interrupt transfer + HID polling real | alto | Device passa para CONFIGURED; transfer ring por endpoint interrupt; `usb_poll_all` lê e dispara handlers HID | `smoke-x64-vmware-usb-hid-keyboard` |

Slices subsequentes (3E, 3F, …) cobrem AHCI/NVMe hardening, device manager unificado, política de fallback. Ficam fora deste documento.

## 3. Slice 3A — Bug fix do state machine USB (lowest hanging fruit)

**Objetivo:** corrigir a regressão silenciosa que impede `usb_hid_init` de descobrir devices mesmo se a enumeração estivesse perfeita.

### 3A.1 Alvos de código entregues

- `@/Volumes/CapyOS/src/drivers/usb/usb_hid.c:75` — relaxar gate para aceitar `USB_DEV_ADDRESSED` também, e marcar requisito de `class_code` populado.
- Documentar contrato em `@/Volumes/CapyOS/include/drivers/usb/usb_core.h:16-22` explicitando que ADDRESSED é pré-requisito mínimo de descoberta, CONFIGURED é pré-requisito de polling efetivo.

### 3A.2 Tests host-side criados

Arquivo `tests/drivers/test_usb_hid_init.c`:

1. `test_usb_hid_init_skips_attached_only` — device com `state=ATTACHED` mas `class_code=0` deve ser pulado.
2. `test_usb_hid_init_finds_addressed_kbd` — device com `state=ADDRESSED`, `class_code=HID`, `subclass=BOOT`, `protocol=KBD` deve ser registrado como keyboard mesmo sem endpoints.
3. `test_usb_hid_init_finds_addressed_mouse` — análogo para mouse.
4. `test_usb_hid_keyboard_poll_returns_zero_without_transfer` — sem transfer ring real, poll retorna 0 sem crash.

Stub correspondente: `tests/stubs/stub_usb_core.c` com `usb_get_device_count`/`usb_get_device` controláveis.

### 3A.3 Critérios de aceite

- [x] `usb_hid_init` retorna 0 quando há ao menos um HID em estado ADDRESSED com class_code/subclass/protocol corretos.
- [x] `usb_hid_keyboard_poll` é seguro chamar mesmo sem transfer ring (`return 0; *out_char = 0`).
- [ ] Nenhum teste host-side existente regrediu em validação externa.

### 3A.4 Validação externa

- `make test` ainda sem comparação com hardware real.
- `make all64` continua compilando.
- `make smoke-x64-iso TOOLCHAIN64=host` continua passando.

### 3A.5 LOC estimadas

- Código: ~10-15 linhas em `usb_hid.c` + ~5 linhas em `usb_core.h`.
- Testes: ~180-220 LOC (`test_usb_hid_init.c` + `stub_usb_core.c`).
- Total: 1 commit pequeno, baixo risco.

## 4. Slice 3B — XHCI device enumeration real

**Objetivo:** implementar `xhci_address_device` de verdade, evoluindo enumerate para ADDRESSED.

### 4.1 Alvos de código entregues

- `@/Volumes/CapyOS/src/drivers/usb/xhci.c`:
  - Suporte a Input Context 32/64 bytes dependendo de `HCCPARAMS1.CSZ`.
  - Suporte a Device Context e transfer ring de EP0 com ownership persistido no controlador.
  - `xhci_address_device` aloca Input Context, popula Slot Context (`root_hub_port_number=port+1`, `speed=PORTSC.PortSpeed`), popula EP0 Context (EP Type=Control Bidirectional, MaxPacketSize conservador por speed, Avg TRB Length=8, TR Dequeue Pointer = transfer ring para EP0), liga Input Context A0+A1, emite Address Device TRB e faz polling até Command Completion com CC=1.
  - Tabela DCBAA em `slot_id` aponta para Device Context recém-alocado em sucesso e é zerada em falha.
  - Bug de polling/avanço de ring corrigido: command/event ring respeitam 256 TRBs e command ring faz wrap antes do Link TRB.
- `@/Volumes/CapyOS/src/drivers/usb/usb_core.c`:
  - `usb_enumerate_devices`: para cada porta conectada, depois de `xhci_port_reset`, chama `xhci_enable_slot` + `xhci_address_device`, grava `slot_id`, avança `state=USB_DEV_ADDRESSED` em sucesso e registra `USB_DEV_ERROR` em falha.

### 4.2 Tests host-side criados

Arquivo `tests/drivers/test_xhci_address_device.c`:

1. `test_input_context_a0_a1_set` — verifica que Input Control Context tem A0 e A1 setados após chamada de address.
2. `test_slot_context_root_hub_port_indexing` — root_hub_port_number é 1-indexed (verifica para porta 0 → valor 1).
3. `test_ep0_max_packet_size_per_speed` — Low Speed=8, Full Speed=8, High Speed=64, Super Speed=512.
4. `test_command_completion_polls_event_ring_full_size` — assegura que o índice circular usa 256, não 64, valida wrap/toggle de cycle bit e pula eventos não-command até a conclusão.

Cobertura atual: `tests/drivers/test_xhci_address_device.c` exercita helpers puros e rings em memória; não há MMIO real nesta máquina.

### 4.3 Critérios de aceite

- [x] `usb_enumerate_devices` chama `xhci_enable_slot` + `xhci_address_device` e registra `state=ADDRESSED` em sucesso.
- [x] Falha de Enable Slot/Address Device não derruba kernel; loga WARN e registra `state=ERROR`.
- [x] Command/event ring não usa mais `% 64`; testes host-side cobrem avanço além do índice 63, wrap no Link TRB, toggle de cycle bit do event ring e consumo de eventos não-command.
- [ ] Em VMware com USB HID virtual, `usb_enumerate_devices` retorna ≥1 com `state=ADDRESSED` e `slot_id` válido.
- [ ] Logs do kernel mostram `XHCI addressed slot N`.

### 4.4 Validação externa

- `make smoke-x64-vmware-usb-hid-keyboard` (novo, ainda parcial neste slice — apenas testa enumeração até ADDRESSED).
- `make test` + `make all64` + `make release-check` passam.

### 4.5 LOC estimadas

- Código produtivo: ~200-280 LOC em `xhci.c` (Input Context structs, alloc helpers, address_device), ~20 LOC em `usb_core.c`.
- Testes: ~250-300 LOC.
- Total: commit médio, risco médio (lógica TRB tem cantos sutis).

## 5. Slice 3C — Control transfer + descriptor parsing

**Objetivo:** ler descriptors via control transfer e popular `usb_device_info` com class/subclass/protocol/endpoints suficientes para descoberta HID, mantendo o device em `USB_DEV_ADDRESSED` até o Slice 3D implementar Configure Endpoint.

### 5.1 Alvos de código entregues

- `src/drivers/usb/xhci.c`:
  - `xhci_control_transfer(slot_id, setup, buf, len, dir)` monta Setup Stage TRB + Data Stage TRB + Status Stage TRB no transfer ring persistente de EP0, toca doorbell do slot com target EP0 e faz polling até Transfer Event matching slot+EP0.
  - `ep0_ring_idx[]` e `ep0_ring_cycle[]` preservam o estado por slot e cobrem wrap antes do Link TRB.
- `src/drivers/usb/usb_descriptors.c`:
  - `usb_build_get_descriptor_request` monta SETUP packet GET_DESCRIPTOR.
  - `usb_parse_device_descriptor` extrai campos de 18 bytes com endian explícito.
  - `usb_parse_configuration_descriptor` parseia Configuration/Interface/Endpoint descriptors, identifica HID boot keyboard/mouse, clipa endpoints em `USB_MAX_ENDPOINTS` e rejeita malformados sem publicar estado parcial.
- `src/drivers/usb/usb_core.c`:
  - `usb_get_descriptor(slot_id, type, index, buf, len)` monta GET_DESCRIPTOR e chama `xhci_control_transfer`.
  - Após Address Device: lê Device Descriptor, lê cabeçalho de Configuration Descriptor para `wTotalLength`, lê o descriptor completo até 256 bytes, popula `usb_device_info.descriptor`, `class_code/subclass/protocol`, `endpoint_count/endpoints[]`, `is_keyboard/is_mouse`.
  - O estado permanece `USB_DEV_ADDRESSED`; `USB_DEV_CONFIGURED` fica reservado para 3D após SET_CONFIGURATION, HID boot protocol e Configure Endpoint real.

### 5.2 Tests host-side

Novo `tests/drivers/test_usb_descriptor_parse.c`:

1. `test_parse_device_descriptor_minimal` — buffer 18 bytes válido → extrai MaxPacketSize0, Vendor/Product e número de configurações.
2. `test_build_get_descriptor_request` — valida bmRequestType/bRequest/wValue/wIndex/wLength e rejeita saída nula/tamanho zero.
3. `test_parse_config_descriptor_keyboard` — config descriptor canônico de HID keyboard → identifica interface HID, subclass=BOOT, protocol=KBD, endpoint interrupt IN.
4. `test_parse_config_descriptor_mouse` — análogo mouse.
5. `test_parse_truncated_descriptor_rejected` — buffer truncado retorna erro sem crash e sem publicar estado parcial.
6. `test_parse_too_many_endpoints_clipped` — config descriptor com >USB_MAX_ENDPOINTS endpoints é clipado, sem overflow.
7. `test_parse_composite_prefers_hid_interface_endpoints` — dispositivo composto descarta endpoint da interface anterior e mantém endpoint HID.

Cobertura adicionada em `tests/drivers/test_xhci_address_device.c`:

1. `test_control_transfer_queues_get_descriptor_trbs` — valida Setup/Data/Status TRBs para GET_DESCRIPTOR IN e doorbell do slot.
2. `test_control_transfer_wraps_ep0_ring` — valida wrap do transfer ring de EP0 e toggle de cycle bit.

### 5.3 Critérios de aceite

- [x] GET_DESCRIPTOR setup packet e parsing de descriptors são host-testáveis e rejeitam inputs malformados sem crash/estado parcial.
- [x] `xhci_control_transfer` enfileira Setup/Data/Status TRBs no EP0 ring e aguarda Transfer Event matching slot+EP0.
- [x] `usb_enumerate_devices` tenta popular `usb_device_info` com Device/Configuration descriptors após Address Device.
- [ ] Em VMware com USB HID virtual, `usb_enumerate_devices` retorna ≥1 device em `state=ADDRESSED`, com `class_code=3`, `subclass=1`, `protocol=1`, ≥1 endpoint interrupt IN.
- [ ] `usb_hid_init` encontra o keyboard a partir do device ADDRESSED enriquecido.

### 5.4 Validação externa

- `make test` para a suíte host-side.
- `make all64` para confirmar link/runtime x86_64.
- `make smoke-x64-vmware-usb-hid-keyboard` deve chegar até descriptor parsing e descoberta HID, ainda sem receber caractere real.

### 5.5 LOC estimadas

- Código produtivo: ~220-320 LOC.
- Testes: ~320-430 LOC.

## 6. Slice 3D — Configure Endpoint + interrupt transfer + HID polling real

**Objetivo:** emitir Configure Endpoint para endpoints interrupt HID, receber input reports via interrupt endpoint e entregar caracteres reais.

### 6.1 Alvos de código entregues

- `src/drivers/usb/xhci.c`:
  - `xhci_build_configure_endpoint_input_context` monta Input Context para interrupt IN com DCI correto, endpoint type Interrupt IN, MaxPacketSize, interval, transfer ring e dequeue cycle.
  - `xhci_configure_interrupt_endpoint` aloca transfer ring e buffer persistentes por slot, pré-arma Normal TRB com IOC, emite Configure Endpoint command e toca doorbell do endpoint após sucesso.
  - `xhci_poll_interrupt` faz polling não bloqueante do event ring, aceita Transfer Event matching slot+endpoint, copia o report, rearma Normal TRB e toca doorbell do endpoint.
  - `xhci_endpoint_dci` e `xhci_build_normal_trb` expõem helpers puros host-testáveis.
- `src/drivers/usb/usb_core.c`:
  - Após descriptor parsing, HID keyboard/mouse com interrupt IN endpoint emite SET_CONFIGURATION, HID SET_PROTOCOL(BOOT), passa por Configure Endpoint e avança para `USB_DEV_CONFIGURED`.
  - `usb_poll_all` percorre devices `CONFIGURED`, chama `xhci_poll_interrupt` e despacha reports para `usb_hid_handle_keyboard_report`/`usb_hid_handle_mouse_report`.
- `src/drivers/usb/usb_hid.c`:
  - `usb_hid_handle_keyboard_report` traduz boot keyboard report para o ring buffer ASCII com supressão de repetição.
  - `usb_hid_handle_mouse_report` publica o último report; `usb_hid_mouse_poll` entrega deltas/botões reais quando disponíveis.

### 6.2 Tests host-side entregues

`tests/drivers/test_usb_hid_init.c`:

1. `test_keyboard_report_handler_buffers_ascii` — buffer com keys[0]=4 (USB HID 'a') sem shift gera ASCII 'a' no ring buffer.
2. `test_mouse_report_handler_surfaces_delta` — report de mouse entrega botões/deltas via `usb_hid_mouse_poll`.

`tests/drivers/test_xhci_address_device.c`:

3. `test_endpoint_dci_mapping` — valida mapeamento USB endpoint address → xHCI DCI.
4. `test_build_configure_endpoint_input_context_interrupt_in` — valida Input Context de Configure Endpoint para interrupt IN.
5. `test_build_normal_trb_layout` — Normal TRB tem param=buf, status.length=N, control.type=NORMAL, IOC=1.
6. `test_control_transfer_set_configuration_no_data` — valida SET_CONFIGURATION sem data stage, com status IN e doorbell EP0.
7. `test_control_transfer_preserves_unmatched_transfer_event` — garante que control transfer não consome Transfer Event que pertence a outro slot.
8. `test_control_transfer_hid_set_protocol_no_data` — valida HID SET_PROTOCOL(BOOT) sem data stage e com interface correta no setup packet.
9. `test_configure_interrupt_endpoint_queues_command_and_primes_ring` — valida Configure Endpoint command, ring/buffer persistentes, Normal TRB inicial e doorbell.
10. `test_poll_interrupt_copies_report_and_rearms` — valida cópia do report, consumo do Transfer Event, rearme de Normal TRB e doorbell.
11. `test_poll_interrupt_preserves_unmatched_transfer_event` — garante que polling de outro slot não consome Transfer Event que pertence a outro endpoint/dispositivo.

### 6.3 Critérios de aceite

- [x] Código emite SET_CONFIGURATION, coloca HID em boot protocol, enfileira Configure Endpoint para HID interrupt IN e avança device para `USB_DEV_CONFIGURED` após sucesso.
- [x] `usb_poll_all` entrega reports para handlers HID sem alocação no caminho de polling.
- [x] Host tests cobrem DCI, SET_CONFIGURATION, HID SET_PROTOCOL, Configure Endpoint context, Normal TRB, preservação de eventos não correspondentes, polling interrupt e handlers HID.
- [ ] Em VMware com USB HID keyboard, digitar uma letra entrega o caractere correto via `usb_hid_keyboard_poll`.
- [ ] Mouse virtual entrega deltas e botões.
- [ ] Hotplug: remover e re-plugar keyboard continua funcionando.
- [ ] Performance: poll-loop ≤2% CPU em idle.

### 6.4 Validação externa

- `make smoke-x64-vmware-usb-hid-keyboard` completo: ISO boot → digita 5 caracteres via teclado virtual VMware → verifica que stdout do shell recebeu exatamente esses 5 caracteres.
- `make all64`, `make release-check`, `make smoke-x64-iso` regridem sem erros.

### 6.5 LOC estimadas

- Código produtivo: ~250-350 LOC (transfer ring é a parte mais densa).
- Testes: ~300-400 LOC.
- Risco: alto — interrupt transfer + cycle bit + IOC tem cantos sutis.

## 7. Dependências horizontais e contratos

- **Não introduzir alocação dinâmica no caminho de polling** — buffers de report devem ser pré-alocados em `xhci_init` ou primeira chamada de `address_device`. Política do projeto: kernel evita malloc em fast paths.
- **Constant-time não aplica** (não é cripto).
- **Sensitive buffer wipe não aplica** (sem secrets).
- **Layout audit:** novos arquivos vão para `src/drivers/usb/` e `include/drivers/usb/` (já existem); testes para `tests/drivers/test_usb_*.c` ou `tests/drivers/test_xhci_*.c`. Limite 900 LOC/arquivo: `xhci.c` permanece abaixo do limite, mas avaliar split em `xhci_init.c` + `xhci_device.c` + `xhci_transfer.c` se passar de 700.
- **Makefile:** novos testes precisam ser registrados no `TEST_SRCS` do Makefile como parte do `make test`.

## 8. Riscos identificados

| Risco | Mitigação |
|---|---|
| VMware XHCI difere de spec real em cantos | Implementar contra spec xHCI 1.2 estrita; smoke em VMware como gate, mas tests host-side com TRBs canônicos |
| Event ring sem ERST formal pode falhar em VMware | Slice 3B já programa ERST formal (`ERSTBA`, `ERSTSZ=1`, `ERDP`) antes de qualquer Address Device |
| Transfer ring + interrupter sem IRQ pode estourar CPU | Aceitar polling para Etapa 3; IRQ-driven fica para Etapa 4 (CapyDisplay+scheduler) ou backlog |
| Cycle bit bugs causam loops infinitos | Polling com timeout iter máximo + log de timeout; nunca loop infinito |
| Endpoint indexing confuso (USB vs xHCI) | Helper `xhci_endpoint_dci(addr)` = `(addr & 0x0F) * 2 + ((addr & 0x80) ? 1 : 0)`, testado |

## 9. Sequência de execução recomendada

1. **Pré-requisito externo atendido:** aceite operacional da Etapa 2 informado em 2026-05-18 (gates `smoke-x64-vmware-mouse-events` + `release-check` em VMware+UEFI+E1000 fora desta máquina).
2. Slice **3A** (bug fix state machine) — código já entregue; manter como base de regressão.
3. Slice **3B** (XHCI address device) — código entregue; manter como base de regressão.
4. Slice **3C** (descriptor parsing) — código entregue; manter como base de regressão.
5. Slice **3D** (SET_CONFIGURATION + HID boot protocol + Configure Endpoint + interrupt transfer + HID polling) — código entregue; manter como base de regressão.
6. Próximo gate ativo: smoke externo `smoke-x64-vmware-usb-hid-keyboard` deve passar antes de liberar a próxima onda da Etapa 3 (storage hardening + device manager unificado).

## 10. Itens fora deste plano

- AHCI/NVMe hardening (slice 3E+).
- Device manager unificado (DMA API, IRQ routing, ownership) (slice 3F+).
- USB mass storage (slice 3G+, opcional para Etapa 3).
- VirtIO-net/block como prioridade VM (slice 3H+, já parcialmente coberto).
- VMware SVGA II (slice 3I+, secundário).

Estes serão planejados em documentos separados quando os slices anteriores fecharem.

## 11. Bug fix oportunista identificado

Corrigido no Slice 3B: o command/event ring deixou de usar `% 64` e agora respeita os 256 TRBs alocados, com wrap antes do Link TRB do command ring, validação de cycle bit no event ring e cobertura em `tests/drivers/test_xhci_address_device.c`.

## 12. Notas para continuação da Etapa 3

Este plano deve continuar sem executar comandos nesta máquina de review/edit. Antes de liberar Slice 3E ou qualquer próxima onda, o desenvolvedor deve:

1. Usar o aceite operacional da Etapa 2 registrado em 2026-05-18 como pré-requisito já atendido.
2. Re-ler `docs/plans/active/capyos-master-plan.md §6` para confirmar a Etapa 3 ainda está como definida.
3. Executar externamente o gate USB HID completo do Slice 3D e registrar resultado antes de aplicar Slice 3E.
4. Quando o gate de Slice 3D fechar, abrir o plano subordinado
   [`etapa-3-slice-3e-plan.md`](etapa-3-slice-3e-plan.md) (rascunho
   entregue em 2026-05-21) e seguir o checklist de pré-requisitos
   antes de começar Slice 3E.1.

## 13. Slice 3D — scaffolding de gate externo (entregue 2026-05-21)

Para destravar a execução externa do Slice 3D, o seguinte foi
acrescentado no workspace de review/edit (sem rodar comandos):

- **Gate puro host-testável** em `include/drivers/usb/usb_hid_smoke.h` +
  `src/drivers/usb/usb_hid_smoke.c`: `usb_hid_keyboard_smoke_gate_observed`
  + `usb_hid_keyboard_smoke_observe` com latch idempotente
  (`USB_HID_KEYBOARD_SMOKE_GATE_VERSION = 1`).
- **Marker canônico** `USB_HID_KEYBOARD_SMOKE_MARKER = "[smoke] usb-hid-keyboard ready"`.
- **I/O kernel-only** em `src/drivers/usb/usb_hid_smoke_io.c`
  (chama `com1_puts`) + **stub host-side** em
  `tests/stubs/stub_usb_hid_smoke_io.c` (no-op) para manter
  `usb_hid.c` host-testável.
- **Wiring** em `src/drivers/usb/usb_hid.c`: `usb_hid_init` conta HID
  keyboards em `USB_DEV_CONFIGURED` no campo
  `g_hid.kbd_configured_count`; `kbd_buffer_push` incrementa
  `g_hid.kbd_chars_received` em cada caractere ASCII real bufferizado
  e chama `usb_hid_keyboard_smoke_observe`; o emissor é acionado
  exatamente uma vez na primeira transição.
- **Host test** em `tests/drivers/test_usb_hid_smoke_gate.c`
  registrado em `tests/test_runner.c` como
  `run_usb_hid_smoke_gate_tests`. Cobre: gate bloqueado por qualquer
  metade ausente; gate observa apenas com as duas metades; latch
  emite uma vez; observe atualiza contadores mesmo bloqueado;
  NULL state sem UB; estabilidade do literal do marker.
- **Alvo Makefile** `smoke-x64-vmware-usb-hid-keyboard` que reusa
  `tools/scripts/smoke_x64_vmware.py` com dois markers em ordem:
  `[net] DHCP: lease acquired.` + `[smoke] usb-hid-keyboard ready`.
- **Runbook** em
  `docs/operations/etapa-3-external-validation-playbook.md`
  orquestrando host gates, build gates e o smoke externo, com
  matriz de triagem por sintoma.

Próxima ação externa (operador / release CI): executar a Section 1
do runbook na plataforma oficial VMware + UEFI + E1000 com teclado
USB HID real anexado.

## 14. Follow-ups conhecidos (não bloqueiam o gate de 3D)

Itens descobertos durante a inspeção estática do Slice 3D e
explicitamente fora do escopo do gate atual. Devem ser tratados
em slices futuras quando a Etapa 3 abrir o device manager
unificado, ou já na Etapa 4 conforme o caso.

### 14.1 USB como input backend do runtime (endereçado em 2026-05-21)

**Estado original:** o dispatcher central em
`src/arch/x86_64/input_runtime/backend_management.c` reconhecia
apenas EFI/PS2/HYPERV/COM1. `usb_hid_keyboard_poll` existia e era
alimentado por `SYSTEM_WORK_USB_POLL`, mas nenhum chamador do
shell drenava esse ring buffer. O critério forte do plano Etapa 3
§6 ("teclado USB funcional fora do `EFI ConIn`") permanecia
aberto.

**Refatoração entregue (2026-05-21):**

- Novo enum value `X64_INPUT_BACKEND_USB` em
  `include/arch/x86_64/input_runtime.h:18`, com `order[]`
  expandido de 4 para 5 entradas para acomodar todos os backends
  possíveis sem dropar registros silenciosos.
- Novos campos `has_usb` em `x64_input_config` e
  `x64_input_runtime`.
- `x64_input_runtime_init`
  (`src/arch/x86_64/input_runtime/backend_management.c:218-307`)
  agora trata USB como native: quando `prefer_native` está
  ativo, registra a ordem `PS/2 → Hyper-V → USB → EFI`. No
  caminho de firmware-preferred (raro pós-ExitBootServices),
  a ordem fica `EFI → PS/2 → Hyper-V → USB`. COM1 segue como
  canal auxiliar sempre ao fim.
- `prefer_native` agora considera `has_usb` (qualquer backend
  nativo retira a preferência por firmware).
- `x64_input_probe_backends` (`backend_management.c:92-105`)
  detecta o controlador XHCI via `xhci_find` em qualquer cenário
  (não apenas no caminho "last resort"). Apenas detecção; a
  inicialização real (`xhci_init` + `xhci_start`) continua
  centralizada em `usb_core_init` (`kernel_work_usb_bringup`)
  para evitar leak duplo de DCBAA/rings.
- `polling.c::x64_input_poll_char`
  (`src/arch/x86_64/input_runtime/polling.c:213-228`) drena o
  ring USB via `usb_hid_keyboard_poll`. A classe HID já produz
  ASCII (`'\n'` para Enter, `'\b'` para Backspace, etc.), então o
  forwarding é direto — sem retradução de scancode.
- `x64_input_backend_name(USB)` retorna `"usb"` em
  `status_hyperv.c:55-56`.
- `kernel_main.c:748,761` encaminha `input_probe.has_usb` ao
  config.

**Critério Etapa 3 §6 atingido:** com USB no order[] do native
group, após `retire_firmware_backend` o desktop session opera
sem `EFI ConIn`. O smoke marker `[smoke] usb-hid-keyboard ready`
continua autoritativo para validar a entrega XHCI; o shell
gráfico agora consome do mesmo ring que alimenta o marker.

**Cobertura ausente:** `backend_management.c` e `polling.c` têm
dependências MMIO/firmware pesadas (xhci, ps/2, hyperv, efi),
fora do alcance do host build. A integração USB é validada por:

- inspeção estática contra os contratos do enum/struct;
- o gate externo `smoke-x64-vmware-usb-hid-keyboard` já
  testemunha que `usb_hid_keyboard_poll` recebe caracteres
  reais;
- novo critério de aceite no runbook: digitar uma letra no
  shell gráfico produz o caractere correto (encerra o critério
  Etapa 3 §6 quando observado externamente).

### 14.2 Cantos do event ring com múltiplos endpoints ativos (endereçado em 2026-05-21)

**Estado original:** `xhci_wait_transfer_completion` retornava `-4`
quando encontrava um Transfer Event não-matching SEM consumir o
ring; `xhci_poll_interrupt` retornava `0` no mesmo cenário sem
consumir. Para o gate de Slice 3D (um único teclado USB) o caminho
de boot só dispara um TRB/IOC por vez, mas em uma VM com teclado +
mouse + composite HID o ring estagnava.

**Refatoração entregue (2026-05-21):** o consumo do event ring foi
unificado em `xhci_event_pump` + `xhci_dispatch_event`
(`src/drivers/usb/xhci.c:176-255`). O dispatcher rotea cada evento
para seu dono:

- `TRB_TYPE_CMD_COMPLETE` → `xhci->cmd_pending`;
- `TRB_TYPE_TRANSFER` com EP0 owner → `xhci->ep0_pending[slot]`;
- `TRB_TYPE_TRANSFER` com interrupt EP owner →
  `xhci->intr_pending[slot]`;
- demais eventos → contados em `xhci->event_stray_count` e o ring
  avança normalmente.

`xhci_event_pump` para em `type == 0` (Reserved per xHCI 1.2 §6.4.4)
para não consumir memória zerada após um wrap do consumidor. Os
três pontos de espera (`xhci_wait_command_completion`,
`xhci_wait_transfer_completion`, `xhci_poll_interrupt`) agora
delegam ao pump e consultam seu próprio `*_pending[]`, eliminando
a estagnação multi-endpoint.

**Cobertura de teste:** 5 novos testes em
`tests/drivers/test_xhci_address_device.c` exercitam o dispatcher
diretamente (`test_event_pump_*`). Os dois testes antigos que
travavam a semântica "preserve unmatched" foram reescritos para
travar a nova semântica "drena strays e conta em
`event_stray_count`" (`test_*_drains_stray_transfer_event`).

**Risco residual:** o pump ainda não notifica via `klog` quando
`event_stray_count` cresce. Em produção, um stray persistente
indica device released vs. evento em voo, vale logar. Follow-up
menor para Etapa 3 Slice 3F.

### 14.3 Hotplug não desaloca rings de interrupt (endereçado em 2026-05-21)

**Estado original:** `usb_enumerate_devices` em
`src/drivers/usb/usb_core.c` reusava `g_devices` mas, quando um
device desaparecia (CCS=0) ou um port reset substituía o device,
o slot antigo permanecia com `intr_rings[old_slot_id]`,
`intr_buffers[old_slot_id]`, `ep0_rings[old_slot_id]`,
`device_contexts[old_slot_id]` e `dcbaa[old_slot_id]` em uso.
Leak determinístico por ciclo de desconexão.

**Refatoração entregue (2026-05-21):**

- Nova API pública `xhci_release_slot(xhci, slot_id)` em
  `src/drivers/usb/xhci.c:697-755`. Emite `Disable Slot` per xHCI
  1.2 §4.6.4, libera EP0 ring + device context + interrupt ring +
  interrupt buffer, zera DCBAA entry e estado por-slot (índices,
  cycle bits, EP address/DCI, buffer length), e invalida latches
  pendentes em `ep0_pending[slot]`/`intr_pending[slot]`. Tolera
  falha do command (devices já desconectados frequentemente
  retornam CC != SUCCESS) — sempre libera os ponteiros para
  evitar leak.
- Novo helper interno `usb_release_stale_slots` em
  `src/drivers/usb/usb_core.c:189-221`. Para cada slot do snapshot
  anterior cuja porta perdeu o device (CCS=0) ou foi
  status-changed (CSC=1), chama `xhci_release_slot`. Integrado ao
  INÍCIO de `usb_enumerate_devices` (não ao fim — ver "Correção
  pós-audit" abaixo).

**Cobertura de teste:** 6 novos testes em
`tests/drivers/test_xhci_address_device.c` (`test_release_slot_*`)
cobrem: rejeição de inputs inválidos, free do estado ADDRESSED,
free do estado CONFIGURED completo, limpeza de latches pendentes,
tolerância a falha do Disable Slot, e idempotência em slot limpo.
Os tests usam `kmalloc_aligned`/`kfree_aligned` reais via
`tests/stubs/stub_kmem.c` para exercitar o caminho de free
completo.

**Cobertura ausente:** o helper `usb_release_stale_slots`
não é diretamente host-testado porque `usb_core.c` inteiro é
stubbado em host tests (`tests/stubs/stub_usb_core.c`). A lógica
é linear (varredura O(N) sobre snapshot, port-status MMIO por
entrada), validada por inspeção estática e pelo gate externo.

**Correção pós-audit (2026-05-21):** a primeira implementação
chamava o teardown ao FINAL de `usb_enumerate_devices` e
comparava `previous.slot_id` com `current.slot_id`. Inspeção
identificou colisão de slot reuse: se o controlador xHCI
recicla um slot ID logo após o port-cycle, a nova
`xhci_enable_slot` retorna o ID antigo, mas
`xhci->device_contexts[slot]` ainda aponta ao device velho,
fazendo `xhci_address_device` retornar -1 antes do teardown
rodar. Corrigido movendo `usb_release_stale_slots` para o
início de `usb_enumerate_devices` e usando o estado de porta
(CCS=0 ou CSC=1) como gatilho ao invés de comparar slot_ids.

## 15. Achados do audit pós-Slice 3D (2026-05-21, não bloqueiam)

Inspeção dirigida descobriu issues menores fora do escopo dos
follow-ups §14.1-§14.3. Cataloguei aqui para tratamento em
slices futuras quando a Etapa 3 abrir o device manager
unificado.

### 15.1 Hotplug periódico não está armado (endereçado em 2026-05-21)

**Estado original:** `usb_hotplug_check` em `src/drivers/usb/usb_core.c`
fazia varredura passiva por CSC e chamava `usb_enumerate_devices`,
mas nenhum work item ou loop do kernel a invocava. Sub-bug
descoberto durante o fix: a função NÃO limpava CSC após detecção
(write 0 a RW1C bit não tem efeito), então mesmo se fosse armada
ficaria em loop infinito.

**Refatoração entregue (2026-05-21):**

- Nova API `xhci_port_ack_csc` em `src/drivers/usb/xhci.c:615-633`.
  Read-modify-write da PORTSC preservando RW/RWS bits e mascarando
  todos os change bits (CSC/PEC/WRC/OCC/PRC/PLC/CEC) exceto CSC,
  que é setado para ack via RW1C. Garante que apenas CSC é
  limpo, nenhum outro change bit é colateralmente afetado.
- Novos constants em `include/drivers/usb/xhci.h:58-67`:
  `XHCI_PORTSC_PEC/WRC/OCC/PLC/CEC` e `XHCI_PORTSC_CHANGE_BITS`
  (máscara composta).
- `usb_hotplug_check` reescrito
  (`src/drivers/usb/usb_core.c:332-354`): itera portas, ack'a CSC
  via helper, e chama `usb_enumerate_devices` UMA vez no fim se
  qualquer porta mudou (não N vezes — single re-enumeração
  cobre todos via `usb_release_stale_slots` já implementado em
  §14.3).
- `kernel_work_usb_poll` (`src/arch/x86_64/kernel_services.c:663-678`)
  agora chama `usb_hotplug_check()` ANTES de `usb_poll_all()`. Custo
  steady-state: N×MMIO read por tick (N = max_ports, tipicamente 4-8).
  Re-enumeração só roda quando CSC realmente fora setado.

**Cobertura de teste:** 2 novos testes em
`tests/drivers/test_xhci_address_device.c`:
- `test_port_ack_csc_rejects_invalid_inputs` (NULL/port out of
  range/uninitialized controller).
- `test_port_ack_csc_clears_only_csc_bit` (mock PORTSC,
  verifica write pattern: CSC=1, outros change bits=0, RW bits
  preservados; segunda porta não corrompida).

**Interação com smoke gate:** o latch idempotente em
`usb_hid_keyboard_smoke_state` garante que o marker emite no
máximo uma vez por boot, independentemente de hot-plug. Se o
keyboard inicial já fez fire, hot-plug de um novo keyboard não
re-emite. Se o keyboard chegou só depois do boot, o smoke gate
ainda vai emitir na primeira tecla.

**Limitação aceita:** `usb_hid_init` não é re-invocado após
hot-plug, então `g_hid.kbd_configured_count` não bumps para
keyboards adicionados pós-boot. `usb_hid_keyboard_available()`
fica stale mas o caminho de dados via `usb_poll_all` continua
funcional (identifica HID por descriptor, não por slot
armazenado). Fix completo (re-init usb_hid em hot-plug) fica
para slice de "input device manager" no futuro.

### 15.2 Slot leak quando `xhci_address_device` falha (endereçado em 2026-05-21)

**Estado original:** em `src/drivers/usb/xhci.c`, quando o command
de Address Device falhava, a função zerava DCBAA local e
liberava `device_ctx`/`ep0_ring`, mas NÃO emitia `Disable Slot`
no controlador. O slot ficava "Enabled" do lado xHCI, lingering
até um teardown explícito vir de `usb_release_stale_slots`
(somente quando o port subsequente reportasse CCS=0/CSC=1).

**Refatoração entregue (2026-05-21):** os ponteiros per-slot
(`dcbaa[slot]`, `device_contexts[slot]`, `ep0_rings[slot]`)
agora são armazenados ANTES da emissão do Address Device TRB. Na
branch de falha, `xhci_release_slot` é invocado para centralizar
o teardown: emite Disable Slot ao controlador (recuperando a
ownership do slot), libera as alocações via os ponteiros já
armazenados, e zera o estado do slot. Tolera falha do Disable
Slot (CC != SUCCESS) — devices em estado intermediário
frequentemente respondem assim.

Arquivos tocados: `src/drivers/usb/xhci.c:673-701`.

**Cobertura de teste:** não há host test direto da branch de
falha de `xhci_address_device` (requer mock de portsc MMIO).
A correção é exercitada indiretamente pelos testes existentes
de `xhci_release_slot` (`test_release_slot_frees_addressed_state`,
`test_release_slot_tolerates_disable_failure`) e validada por
inspeção contra o gate externo.

### 15.3 LEDs de teclado não dirigidos (endereçado em 2026-05-21)

**Estado original:** `usb_hid.c` não enviava `Set Report` (output
0x21/0x09) para acender Caps Lock / Num Lock / Scroll Lock. Caps
Lock também não afetava a tradução de letras — pressionar Caps e
depois 'a' continuava emitindo 'a' minúsculo. UX problema mas
não bug de segurança/correctness.

**Refatoração entregue (2026-05-21):**

- Nova API pública `usb_hid_send_led_report(slot_id, interface,
  bitmap)` em `include/drivers/usb/usb_core.h:119-125` e impl
  em `src/drivers/usb/usb_core.c:332-348`. Constroi o setup
  packet HID Class (`bmRequestType=0x21`, `bRequest=0x09`,
  `wValue=0x0200` ReportType=Output ReportID=0, `wIndex=interface`,
  payload=1 byte) e despacha via `xhci_control_transfer`.
- `struct usb_hid_state` ganhou `led_state`, `kbd_slot_id`,
  `kbd_interface` (`src/drivers/usb/usb_hid.c:57-67`).
- `usb_hid_init` captura `slot_id` + `interface_number` do
  device descriptor quando encontra o keyboard.
- `usb_hid_handle_keyboard_report` detecta presses de Caps Lock
  (usage 0x39), Num Lock (0x53), Scroll Lock (0x47) — toggle do
  bit correspondente em `led_state`, despacha SET_REPORT uma vez
  ao fim do processamento do report.
- Caps Lock agora afeta a tradução de letras (a→A, A→a). Shift
  inverte (Shift+a sob Caps = a). Símbolos (`1`, `!`, etc.) não
  são afetados.

Arquivos tocados: `include/drivers/usb/usb_core.h`,
`src/drivers/usb/usb_core.c`, `src/drivers/usb/usb_hid.c`,
`tests/stubs/stub_usb_core.c`.

**Cobertura de teste:** 2 novos testes em
`tests/drivers/test_usb_hid_init.c`:
- `test_keyboard_report_handler_toggles_caps_lock_led` verifica
  que primeira pressão dispatcha SET_REPORT com bitmap=0x02,
  release sozinho não dispatcha, segunda pressão limpa CapsLock,
  e slot_id/interface vêm do device descriptor capturado.
- `test_keyboard_report_handler_caps_lock_affects_letters`
  verifica 'a' lowercase pré-Caps, 'A' uppercase com Caps,
  Shift+a sob Caps = 'a' (inversão), '1' não afetado por Caps.

**Limitação aceita:** Num Lock e Scroll Lock só atualizam LEDs;
não há keypad numérico para Num Lock afetar funcionalmente.
Scroll Lock é UX-only em sistemas modernos.

### 15.4 Ctrl combinations não traduzidas (endereçado em 2026-05-21)

**Estado original:** `usb_hid_handle_keyboard_report` aplicava
apenas Shift (`modifiers & 0x22`). Ctrl, Alt, GUI eram ignorados
na tradução para ASCII. O shell não recebia Ctrl+C / Ctrl+D /
Ctrl+L corretamente — uma limitação fundamental de UX shell.

**Refatoração entregue (2026-05-21):** detecta Ctrl modifier
(`modifiers & 0x11` — bit 0 = Left Ctrl, bit 4 = Right Ctrl per
USB HID Usage Tables §10) e traduz alpha keys para control codes
(Ctrl+A → 0x01, …, Ctrl+Z → 0x1A). Funciona com Shift simultâneo
(Ctrl+Shift+A ainda emite 0x01). Non-alpha keys sob Ctrl
passam inalteradas — sem mutação espúria.

Arquivos tocados: `src/drivers/usb/usb_hid.c:152-189`.

**Cobertura de teste:** 2 novos testes em
`tests/drivers/test_usb_hid_init.c`:
- `test_keyboard_report_handler_translates_ctrl_combinations`
  cobre Left Ctrl+A, Right Ctrl+C, Ctrl+Shift+A.
- `test_keyboard_report_handler_passes_ctrl_with_non_alpha`
  cobre Ctrl+1 e Ctrl+Space (passagem inalterada).

**Limitação conhecida:** Alt/GUI modifiers continuam ignorados.
Slice de teclado layouts (Etapa 5+) vai expandir o mapeamento.

### 15.5 Port reset não espera explicitamente por PED (endereçado em 2026-05-21)

**Estado original:** `xhci_port_reset` esperava por PRC (Port Reset
Change) e retornava, mas não verificava PED (Port Enabled/
Disabled). Per xHCI 1.2 §4.3 step 6, o controlador auto-seta PED
após reset, mas existe uma janela pequena. `xhci_address_device`
então rejeitava com -2 se PED ainda não estivesse setado. Em
VMware funcionava; em hardware real ou emuladores menos
compliant podia falhar 1 em 1000 tentativas.

**Refatoração entregue (2026-05-21):** após observar PRC e
limpá-lo, `xhci_port_reset` agora busy-waits por até 10000
iterações até PED ser observado. Bounded — retorna -3
explicitamente se PED nunca for setado (cenário de controlador
não-compliant). Para VMware nenhuma mudança observável (PED já
está setado atomicamente com PRC).

Arquivos tocados: `src/drivers/usb/xhci.c:587-612`.

### 15.6 Interrupter modo polling (intencional, documentar)

`xhci_init` não habilita o controller-wide `INTE` bit nem o
interrupter-zero `IMAN.IE`. O kernel usa polling do event ring
via `SYSTEM_WORK_USB_POLL` (10ms tick). Adequado para Slice
3D; para low-latency input em Etapa 5+ vai precisar trocar
para interrupt-driven.
