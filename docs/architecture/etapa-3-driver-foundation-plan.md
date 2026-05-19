# Etapa 3 — Driver foundation plan (XHCI + USB HID + storage)

**Status:** Etapa 3 ativa desde 2026-05-18 após aceite operacional externo da Etapa 2 informado pelo operador. Slices 3A-3D foram implementados em código sem validação externa própria nesta máquina. Próximo gate ativo: validação externa do Slice 3D antes de liberar 3E.

**Escopo:** detalha a primeira onda de trabalho da Etapa 3 nova (Driver framework + entrada USB HID + storage estável), conforme `docs/plans/active/capyos-master-plan.md` §6.

**Audiência:** desenvolvedor que vai continuar a Etapa 3 ativa.

## Implementation log

| Slice | Estado | Notas |
|---|---|---|
| 3A | Código entregue 2026-05-15, validação externa própria pendente | bug de state machine corrigido; 6 testes host-side adicionados |
| 3B | Código entregue 2026-05-18, validação externa própria pendente | XHCI Address Device + enumeração até ADDRESSED + testes host-side de contexto/ring |
| 3C | Código entregue 2026-05-18, validação externa própria pendente | control transfer GET_DESCRIPTOR + descriptor parsing; device permanece ADDRESSED enriquecido até Configure Endpoint |
| 3D | Código entregue 2026-05-18, validação externa própria pendente | SET_CONFIGURATION + HID boot protocol + Configure Endpoint + interrupt transfer + HID polling real |

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
