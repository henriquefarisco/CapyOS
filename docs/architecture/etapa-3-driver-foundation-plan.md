# Etapa 3 — Driver foundation plan (XHCI + USB HID + storage)

**Status:** Slice 3A implementado em código sem validação externa (user override em 2026-05-15). Slices 3B-3D em preparação. Plano subordinado ao master plan ativo.

**Escopo:** detalha a primeira onda de trabalho da Etapa 3 nova (Driver framework + entrada USB HID + storage estável), conforme `docs/plans/active/capyos-master-plan.md` §6.

**Audiência:** desenvolvedor que vai pegar a Etapa 3 assim que a Etapa 2 fechar externamente.

## Implementation log

| Slice | Estado | Notas |
|---|---|---|
| 3A | Código entregue 2026-05-15, validação externa pendente | bug state machine + off-by-one ring corrigidos; 6 testes host-side adicionados |
| 3B | Pendente | aguardando |
| 3C | Pendente | aguardando |
| 3D | Pendente | aguardando |

## 1. Estado atual auditado (2026-05-15)

### 1.1 XHCI

- `src/drivers/usb/xhci.c` (358 LOC) entrega `xhci_find`, `xhci_init`, `xhci_reset`, `xhci_start`, `xhci_stop`, `xhci_port_reset`, `xhci_port_get_status`.
- `xhci_init` aloca DCBAA, command ring (256 TRBs, link no fim), event ring (256 TRBs sem ERST formal), programa `DCBAAP` e `CRCR`, configura `CONFIG.MaxSlotsEn`.
- `xhci_enable_slot` (linhas 305-331): emite Enable Slot TRB, faz polling no event ring (com bug — usa `% 64` mas o ring tem 256 entradas), retorna `slot_id`.
- **STUB**: `xhci_address_device` (linhas 333-339) — não monta Input Context, não emite Address Device TRB.
- **STUB**: `xhci_find_keyboard` (linhas 341-347) — placeholder.
- **STUB**: `xhci_keyboard_poll` (linhas 349-357) — placeholder.
- ERST formal **ausente** — driver lê event ring diretamente sem programar `ERSTBA`/`ERSTSZ`/`ERDP`.
- Interrupter **não habilitado** (`XHCI_CMD_INTE` nunca é setado).

### 1.2 USB core

- `src/drivers/usb/usb_core.c` (108 LOC).
- `usb_core_init` chama `xhci_find` + `xhci_init` + `xhci_start`. OK.
- `usb_enumerate_devices` apenas detecta `PORTSC.CCS` + reset; **não chama** `xhci_enable_slot` nem `xhci_address_device`. Marca `dev.state = USB_DEV_ATTACHED` e para.
- Estados definidos (`include/drivers/usb/usb_core.h`): `DISCONNECTED, ATTACHED, ADDRESSED, CONFIGURED, ERROR` — só `ATTACHED` é usado.
- **STUB**: `usb_poll_all` (linhas 93-97).
- Hotplug: `usb_hotplug_check` re-chama enumerate quando `PORTSC.CSC` set. Lógica básica OK.

### 1.3 USB HID

- `src/drivers/usb/usb_hid.c` (173 LOC).
- Tabelas scancode→ASCII (boot protocol US layout) e shift variant prontas.
- **BUG crítico de máquina de estados**: `usb_hid_init` (linha 75) verifica `dev.state != USB_DEV_CONFIGURED` e pula. Como enumerate só seta `ATTACHED`, **nenhum HID é descoberto**.
- `process_kbd_report` está marcado `__attribute__((unused))` (linha 118) — nunca é chamado porque não há transfer.
- `usb_hid_keyboard_poll` chama `usb_poll_all` (stub) — não há leitura real.

### 1.4 Storage (contexto, fora do primeiro slice)

- `src/drivers/storage/ahci.c` (599 LOC): substancial, sem TODOs `grep`-able. Cobertura provável: identify, R/W via FIS.
- `src/drivers/nvme/nvme.c` (538 LOC): substancial. Avaliação detalhada fica para slice 4 (fora desta primeira onda).

### 1.5 Tests

- **Zero** arquivos de teste host-side para USB/XHCI/HID em `tests/`.
- Padrão de testes existente em `tests/test_*.c` usa stubs em `tests/stub_*.c`.

## 2. Estratégia de slicing

Quebra a primeira parte da Etapa 3 em 4 slices verticais auditáveis. Cada slice fecha código + testes host-side + documentação + critérios de validação externa.

| Slice | Tema | Risco | Saída | Smoke externo |
|---|---|---|---|---|
| 3A | Bug fix de regressão: state machine USB | baixo | `usb_hid_init` deixa de ser broken por contrato; estado intermediário ADDRESSED é alcançado mesmo com stubs | regressão local via `make test` |
| 3B | XHCI device enumeration real | médio | `xhci_address_device` real + Input Context; enumerate progride para ADDRESSED | smoke VMware com XHCI |
| 3C | Control transfer + descriptor parsing | médio | GET_DESCRIPTOR sobre control ring; parsing de device/config/interface/endpoint descriptors; estado CONFIGURED | smoke USB HID descriptor |
| 3D | Interrupt transfer + HID polling real | alto | Transfer ring por endpoint interrupt; `usb_poll_all` lê e dispara `process_kbd_report` | `smoke-x64-vmware-usb-hid-keyboard` |

Slices subsequentes (3E, 3F, …) cobrem AHCI/NVMe hardening, device manager unificado, política de fallback. Ficam fora deste documento.

## 3. Slice 3A — Bug fix do state machine USB (lowest hanging fruit)

**Objetivo:** corrigir a regressão silenciosa que impede `usb_hid_init` de descobrir devices mesmo se a enumeração estivesse perfeita.

### 3A.1 Alvos de código

- `@/Volumes/CapyOS/src/drivers/usb/usb_hid.c:75` — relaxar gate para aceitar `USB_DEV_ADDRESSED` também, e marcar requisito de `class_code` populado.
- Documentar contrato em `@/Volumes/CapyOS/include/drivers/usb/usb_core.h:16-22` explicitando que ADDRESSED é pré-requisito mínimo de descoberta, CONFIGURED é pré-requisito de polling efetivo.

### 3A.2 Tests host-side a criar

Novo arquivo `tests/test_usb_hid_init.c`:

1. `test_usb_hid_init_skips_attached_only` — device com `state=ATTACHED` mas `class_code=0` deve ser pulado.
2. `test_usb_hid_init_finds_addressed_kbd` — device com `state=ADDRESSED`, `class_code=HID`, `subclass=BOOT`, `protocol=KBD` deve ser registrado como keyboard mesmo sem endpoints.
3. `test_usb_hid_init_finds_addressed_mouse` — análogo para mouse.
4. `test_usb_hid_keyboard_poll_returns_zero_without_transfer` — sem transfer ring real, poll retorna 0 sem crash.

Stub correspondente: `tests/stub_usb_core.c` com `usb_get_device_count`/`usb_get_device` controláveis.

### 3A.3 Critérios de aceite

- [ ] `usb_hid_init` retorna 0 quando há ao menos um HID em estado ADDRESSED com class_code/subclass/protocol corretos.
- [ ] `usb_hid_keyboard_poll` é seguro chamar mesmo sem transfer ring (`return 0; *out_char = 0`).
- [ ] Nenhum teste host-side existente regrediu.

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

### 4.1 Alvos de código

- `@/Volumes/CapyOS/src/drivers/usb/xhci.c`:
  - Adicionar suporte a Input Context (32 ou 64 bytes dependendo de `HCCPARAMS1.CSZ`).
  - Adicionar suporte a Device Context (slot ctx + EP0 ctx).
  - Implementar `xhci_address_device` real: aloca Input Context, popula Slot Context (route_string=0, context_entries=1, root_hub_port_number=port+1, speed=PORTSC.PortSpeed), popula EP0 Context (EP Type=Control Bidirectional, MaxPacketSize=8/16/32/64 por speed, Avg TRB Length=8, TR Dequeue Pointer = transfer ring para EP0), liga Input Context A0+A1, emite Address Device TRB com `param=input_ctx_ptr`, `control=(TRB_TYPE_ADDRESS_DEV<<10)|(slot_id<<24)|cycle`, faz polling até Command Completion com CC=1.
  - Tabela DCBAA em `slot_id` aponta para Device Context recém-alocado.
  - Corrigir bugs do polling de event ring: `xhci_enable_slot` usa `% 64` para um ring de 256 entradas (linhas 314, 325). Trocar por `% 256`.
- `@/Volumes/CapyOS/src/drivers/usb/usb_core.c`:
  - `usb_enumerate_devices`: para cada porta com CCS+PED, depois de `xhci_port_reset` chamar `xhci_enable_slot` + `xhci_address_device`, gravar `slot_id`, avançar `state=USB_DEV_ADDRESSED`.

### 4.2 Tests host-side a criar

Novo arquivo `tests/test_xhci_address_device.c`:

1. `test_input_context_a0_a1_set` — verifica que Input Control Context tem A0 e A1 setados após chamada de address.
2. `test_slot_context_root_hub_port_indexing` — root_hub_port_number é 1-indexed (verifica para porta 0 → valor 1).
3. `test_ep0_max_packet_size_per_speed` — Low Speed=8, Full Speed=8, High Speed=64, Super Speed=512.
4. `test_command_completion_polls_event_ring_full_size` — assegura que o índice circular usa 256, não 64.

Stub: `tests/stub_xhci_mmio.c` mockando MMIO + transfer rings em memória.

### 4.3 Critérios de aceite

- [ ] Em VMware com USB HID virtual, `usb_enumerate_devices` retorna ≥1 com `state=ADDRESSED` e `slot_id` válido.
- [ ] Logs do kernel mostram "XHCI slot N addressed for port M".
- [ ] Falha de Address Device não derruba kernel; log e estado=ERROR.
- [ ] Event ring polling não tem off-by-one nem corrupção quando `evt_ring_idx > 64`.

### 4.4 Validação externa

- `make smoke-x64-vmware-usb-hid-keyboard` (novo, ainda parcial neste slice — apenas testa enumeração até ADDRESSED).
- `make test` + `make all64` + `make release-check` passam.

### 4.5 LOC estimadas

- Código produtivo: ~200-280 LOC em `xhci.c` (Input Context structs, alloc helpers, address_device), ~20 LOC em `usb_core.c`.
- Testes: ~250-300 LOC.
- Total: commit médio, risco médio (lógica TRB tem cantos sutis).

## 5. Slice 3C — Control transfer + descriptor parsing

**Objetivo:** ler descriptors via control transfer e popular `usb_device_info` completo.

### 5.1 Alvos de código

- `@/Volumes/CapyOS/src/drivers/usb/xhci.c`:
  - Adicionar `xhci_control_transfer(slot_id, setup, buf, len, dir)` que monta Setup Stage TRB + Data Stage TRB + Status Stage TRB no transfer ring de EP0, toca doorbell 1, faz polling até Transfer Event.
- `@/Volumes/CapyOS/src/drivers/usb/usb_core.c`:
  - `usb_get_descriptor(slot_id, type, index, buf, len)` wrapper que monta SETUP packet GET_DESCRIPTOR (bmRequestType=0x80, bRequest=6, wValue=type<<8|index, wIndex=0, wLength=len) e chama control transfer.
  - Após address device: ler Device Descriptor (18 bytes), pegar `bMaxPacketSize0` (atualizar EP0 se diferente da estimativa), ler Configuration Descriptor (primeiro 9 bytes para wTotalLength, depois total), parsear interface + endpoint descriptors, popular `usb_device_info.class_code/subclass/protocol/endpoint_count/endpoints[]`, `usb_device_info.is_keyboard/is_mouse`, avançar `state=USB_DEV_CONFIGURED`.

### 5.2 Tests host-side

Novo `tests/test_usb_descriptor_parse.c`:

1. `test_parse_device_descriptor_minimal` — buffer 18 bytes válido → extrai class_code/subclass/protocol/MaxPacketSize0.
2. `test_parse_config_descriptor_kbd` — config descriptor canônico de HID keyboard → identifica interface HID, subclass=BOOT, protocol=KBD, endpoint interrupt IN.
3. `test_parse_config_descriptor_mouse` — análogo mouse.
4. `test_parse_truncated_descriptor_rejected` — buffer truncado retorna erro sem crash.
5. `test_parse_too_many_endpoints_clipped` — config descriptor com >USB_MAX_ENDPOINTS endpoints é clipado, sem overflow.

### 5.3 Critérios de aceite

- [ ] HID keyboard em VMware atinge `state=CONFIGURED` com `class_code=3`, `subclass=1`, `protocol=1`, ≥1 endpoint interrupt IN.
- [ ] `usb_hid_init` (já corrigido em 3A) agora encontra o keyboard.
- [ ] Descriptor parsing rejeita inputs malformados sem crash.

### 5.4 Validação externa

- `make smoke-x64-vmware-usb-hid-keyboard` agora chega até CONFIGURED.
- Smoke envia 1 caractere via teclado virtual VMware e checa que `usb_hid_init` reconheceu o device (mas ainda não recebe o caractere — isso é 3D).

### 5.5 LOC estimadas

- Código produtivo: ~150-200 LOC.
- Testes: ~200-250 LOC.

## 6. Slice 3D — Interrupt transfer + HID polling real

**Objetivo:** receber input reports via interrupt endpoint e entregar caracteres reais.

### 6.1 Alvos de código

- `@/Volumes/CapyOS/src/drivers/usb/xhci.c`:
  - Adicionar Configure Endpoint command com Input Context que liga o endpoint interrupt IN do HID (EP type = Interrupt IN, MaxPacketSize do endpoint descriptor, Avg TRB Length = 8, TR Dequeue Pointer = novo transfer ring por EP).
  - Por endpoint, criar transfer ring (256 TRBs com link no fim).
  - Pre-armar 8 Normal TRBs no transfer ring com `param=data_buffer`, `status=transfer_length`, `control=(TRB_TYPE_NORMAL<<10)|(IOC=1)|cycle` para receber reports.
  - Doorbell `db_base + 4*slot_id` com valor = endpoint index para iniciar transfer.
  - `xhci_poll_transfer(slot_id, ep_addr, out_buf, out_len)` lê event ring por Transfer Event matching slot+ep, copia buffer, re-arma TRB.
- `@/Volumes/CapyOS/src/drivers/usb/usb_core.c`:
  - `usb_poll_all` chama `xhci_poll_transfer` para cada device com endpoint interrupt e propaga para `usb_hid` callback.
- `@/Volumes/CapyOS/src/drivers/usb/usb_hid.c`:
  - Tirar `__attribute__((unused))` de `process_kbd_report` (linha 118).
  - Wireup: `usb_hid_keyboard_poll` puxa report via `usb_poll_all`, alimenta `process_kbd_report` que enche o ring buffer.
  - Idem para mouse: `usb_hid_mouse_poll` retorna deltas reais.

### 6.2 Tests host-side

Novo `tests/test_usb_hid_report_processing.c`:

1. `test_process_kbd_report_single_key` — buffer com keys[0]=4 (USB HID 'a') sem shift gera ASCII 'a' no ring buffer.
2. `test_process_kbd_report_shift_a` — keys[0]=4 com modifiers=0x02 gera 'A'.
3. `test_process_kbd_report_no_repeat` — mesmo report consecutivo não duplica caractere.
4. `test_process_kbd_report_release_then_press` — solta tecla, pressiona de novo, conta como nova press.
5. `test_kbd_buffer_overflow` — encher buffer não corrompe; ring buffer permanece consistente.

Novo `tests/test_xhci_transfer_ring.c`:

6. `test_normal_trb_layout` — Normal TRB tem param=buf, status.length=N, control.type=NORMAL, IOC=1.
7. `test_transfer_ring_link_wrap` — após 255 TRBs, link TRB volta ao início com cycle toggled.

### 6.3 Critérios de aceite

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
- **Layout audit:** novos arquivos vão para `src/drivers/usb/` e `include/drivers/usb/` (já existem); testes para `tests/test_usb_*.c`. Limite 900 LOC/arquivo: `xhci.c` está em 358 LOC; após 3B-3D pode chegar a 700-800 LOC — aceitável, mas avaliar split em `xhci_init.c` + `xhci_device.c` + `xhci_transfer.c` se passar de 700.
- **Makefile:** novos `tests/test_usb_*.c` precisam ser registrados no Makefile como objects do `make test`. Verificar pattern em `tests/test_*.c` existentes.

## 8. Riscos identificados

| Risco | Mitigação |
|---|---|
| VMware XHCI difere de spec real em cantos | Implementar contra spec xHCI 1.2 estrita; smoke em VMware como gate, mas tests host-side com TRBs canônicos |
| Event ring sem ERST formal pode falhar em VMware | Slice 3B já programa ERST formal (`ERSTBA`, `ERSTSZ=1`, `ERDP`) antes de qualquer Address Device |
| Transfer ring + interrupter sem IRQ pode estourar CPU | Aceitar polling para Etapa 3; IRQ-driven fica para Etapa 4 (CapyDisplay+scheduler) ou backlog |
| Cycle bit bugs causam loops infinitos | Polling com timeout iter máximo + log de timeout; nunca loop infinito |
| Endpoint indexing confuso (USB vs xHCI) | Helper `xhci_ep_index(addr)` = `(addr & 0x0F) * 2 + ((addr & 0x80) ? 1 : 0)`, testado |

## 9. Sequência de execução recomendada

1. **Pré-requisito externo:** aceite operacional da Etapa 2 (gates `smoke-x64-vmware-mouse-events` + `release-check` em VMware+UEFI+E1000 fora desta máquina).
2. Slice **3A** (bug fix state machine) — 1 commit, ~30 min de revisão.
3. Slice **3B** (XHCI address device) — 1-2 commits, ~1-2h de revisão.
4. Slice **3C** (descriptor parsing) — 1-2 commits, ~1-2h de revisão.
5. Slice **3D** (interrupt transfer + HID polling) — 2-3 commits, ~2-4h de revisão.
6. Smoke externo `smoke-x64-vmware-usb-hid-keyboard` deve passar antes de liberar a próxima onda da Etapa 3 (storage hardening + device manager unificado).

## 10. Itens fora deste plano

- AHCI/NVMe hardening (slice 3E+).
- Device manager unificado (DMA API, IRQ routing, ownership) (slice 3F+).
- USB mass storage (slice 3G+, opcional para Etapa 3).
- VirtIO-net/block como prioridade VM (slice 3H+, já parcialmente coberto).
- VMware SVGA II (slice 3I+, secundário).

Estes serão planejados em documentos separados quando os slices anteriores fecharem.

## 11. Bug fix oportunista identificado

`@/Volumes/CapyOS/src/drivers/usb/xhci.c:314` e `:325` — `xhci->cmd_ring_idx` e `xhci->evt_ring_idx` usam `% 64` mas os anéis têm **256** entradas. Off-by-one: depois de 64 comandos, o índice volta para 0 enquanto a TRB no offset 64 ainda é válida do ciclo anterior. Bug a corrigir em **Slice 3A** (oportunista, baixo custo).

## 12. Notas para validação local (estática)

Este plano não foi executado: apenas inspeção e edição. Antes de iniciar Slice 3A, o desenvolvedor deve:

1. Confirmar com o operador da Etapa 2 que `smoke-x64-vmware-mouse-events` e `release-check` passaram externamente.
2. Re-ler `docs/plans/active/capyos-master-plan.md §6` para confirmar a Etapa 3 ainda está como definida.
3. Re-validar o estado do código deste plano com `git log --since=2026-05-15 -- src/drivers/usb` antes de aplicar slice 3A.
