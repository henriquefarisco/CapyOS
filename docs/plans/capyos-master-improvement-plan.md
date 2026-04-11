# CapyOS Master Improvement Plan v2.0

> Gerado em 2026-04-10 com base em auditoria completa do codigo-fonte.
> Versao atual: 0.8.0-alpha.0 | Trilha: UEFI/GPT/x86_64
> **Atualizado**: 2026-04-11 | Consolidacao preparada para `develop` com fases iniciais validadas.

---

## Status de Implementacao

| Fase | Status | Resumo |
|---|---|---|
| 1. Estabilizacao | **CONCLUIDO** | O0 removido, legado 32-bit deletado, desktop loop corrigido, autostart nao-bloqueante |
| 2. Refatoracao | **PARCIAL** | kstring.h/kstring.c criado; desmembramento de kernel_main.c planejado para proxima iteracao |
| 3. Teclado | **CONCLUIDO** | AltGr, CapsLock, Ctrl combos, arrows, F1-F12, Home/End/PgUp/PgDn, Delete/Insert |
| 4. Drivers | **CONCLUIDO** | VirtIO-Net, RTL8139 implementados; print-pci command; wired stack_driver/net_probe |
| 5. Performance | **CONCLUIDO** | fbcon_scroll com rep movsq, splash delays reduzidos ~5x |
| 6. Boot flow | **CONCLUIDO** | boot_media/boot_mode fields no handoff struct |
| 7. Network | **CONCLUIDO** | DNS cache TTL expiry no lookup |
| Extra | **CONCLUIDO** | CRLF→LF normalizado em todo o codebase; drivers/io.h e drivers/irq.h criados |
| 2.1 Refatoracao | **CONCLUIDO** | font8x8 extraido (-309 linhas), COM1 extraido (-30 linhas), kstring wired, streq/local_copy/buffer_append delegam para kstring. kernel_main.c: 2917→2551 linhas (-12.5%) |
| 4.5 VMXNET3 | **CONCLUIDO** | Driver VMware VMXNET3 implementado e wired em stack_driver/net_probe |
| Verificacao | **CONCLUIDO** | 0 broken refs, 17 new files verified, 6 new .o in Makefile, all modified files verified intact |

---

## Sumario Executivo

Auditoria completa do CapyOS identificou **8 dominios criticos** que precisam de
intervencao estrutural. Este plano organiza o trabalho em **7 fases sequenciais**,
cada uma com marcos verificaveis. A prioridade absoluta e fazer a UI subir na ISO
e eliminar todo codigo legado 32-bit.

---

## DIAGNOSTICO: Problemas Raiz Identificados

### D1. UI nao sobe na ISO (CRITICO)

**Arquivo**: `src/shell/commands/extended.c:194-214`
**Arquivo**: `src/arch/x86_64/kernel_shell_runtime.c:367-371`

O fluxo de autostart do desktop funciona assim:
1. `x64_kernel_begin_shell_session()` chama `x64_kernel_run_shell_alias("desktopstart")`
2. `cmd_desktop_start()` chama `mouse_ps2_init()` e entra num loop bloqueante:
   ```c
   while (g_desktop_active) {
       desktop_run_frame(&g_desktop);
       for (volatile int d = 0; d < 50000; d++) {}  // spin delay
   }
   ```
3. `desktop_run_frame()` processa mouse e renderiza, mas **NAO** le teclado via
   `kernel_input_getc()` — usa apenas `desktop_handle_input()` que nunca e chamado
   do frame loop.
4. O `login_runtime_run()` fica travado porque `try_shell_command("desktopstart")`
   bloqueia o thread principal indefinidamente.

**Problemas adicionais na ISO**:
- `mouse_ps2_init()` pode falhar silenciosamente em VMs sem PS/2 emulado
- O desktop loop nao tem fallback para voltar ao CLI se inicializacao falhar
- Nao ha integracao entre o input runtime do kernel (`kernel_input_getc`) e o
  input do desktop (mouse_poll + teclado direto)
- O delay `volatile int d` e extremamente impreciso e consome 100% da CPU

### D2. Codigo legado 32-bit ainda presente

Arquivos que devem ser removidos (nao sao referenciados pelo build x86_64):

```
boot/boot.s              - Bootloader BIOS 16-bit
boot/stage1.s            - Stage1 BIOS 16-bit
boot/stage2.asm          - Stage2 32-bit
src/arch/x86/boot/       - kernel_entry.s (32-bit entry)
src/arch/x86/cpu/        - gdt.c, gdt_flush.s (GDT 32-bit)
src/arch/x86/hw/         - ports.c (I/O 32-bit)
src/arch/x86/interrupts/ - idt.c, isr.c, interrupts.s, default_stub.s
src/arch/x86/linker.ld   - Linker script 32-bit Multiboot
include/arch/x86/        - Todos os headers (cpu/gdt.h, cpu/idt.h, hw/io.h, etc.)
```

**PROBLEMA CRITICO**: `src/drivers/input/keyboard/core.c` ainda inclui headers x86:
```c
#include "arch/x86/cpu/isr.h"   // depende do IDT 32-bit
#include "arch/x86/hw/io.h"     // depende do I/O 32-bit
```
Este arquivo e compilado no build x64 mas depende de headers legados.

### D3. kernel_main.c e um monolito de ~2943 linhas (~100KB)

Responsabilidades misturadas neste unico arquivo:
1. **Dados de fonte 8x8** (linhas 121-249) — ~130 linhas de array estatico
2. **Driver de framebuffer console** (fbcon_*) — ~200 linhas
3. **Driver serial COM1** — ~30 linhas
4. **Validacao ACPI/RSDP** — ~40 linhas
5. **Sistema de temas** — ~40 linhas
6. **Boot splash com barra de progresso** — ~60 linhas
7. **Banner ASCII** — ~40 linhas
8. **Utilitarios de string** (streq, local_copy, buffer_append_*) — ~100 linhas
9. **Recovery report/history** — ~200 linhas
10. **Service manager wiring** — ~120 linhas
11. **ExitBootServices** — ~100 linhas
12. **Input runtime coordination** — ~100 linhas
13. **Storage/volume wrappers** — ~80 linhas
14. **Network bootstrap** — ~50 linhas
15. **Entry point kernel_main64()** — ~400 linhas
16. **Wrappers de login runtime** — ~80 linhas

### D4. Performance: O0 no kernel inteiro

```c
// src/arch/x86_64/kernel_main.c:4
#pragma GCC optimize("O0")
```
Isto **desabilita todas as otimizacoes** no arquivo mais critico do kernel.
Impacto: framebuffer lento, boot splash lento, scroll lento, tudo degradado.

### D5. Keyboard BR-ABNT2 incompleto

`include/drivers/input/keyboard_layout.h` define apenas:
- `base[128]` — mapa normal
- `shift[128]` — mapa com Shift
- `dead[128]` — flags de dead keys

**Faltam**:
- **AltGr** (Right Alt) — essencial para BR: AltGr+q nao funciona
- Teclas de funcao F2-F12 (so F1 e tratada como hotkey de help)
- Arrow keys (cima/baixo/esquerda/direita)
- Home, End, Page Up, Page Down, Insert, Delete
- Caps Lock toggle
- Ctrl+C/Ctrl+D/Ctrl+L
- Numpad Enter
- Print Screen, Scroll Lock, Pause

### D6. Compatibilidade de drivers limitada

**Rede — drivers com runtime funcional**:
- E1000 (Intel 8254x) — OK
- Tulip (DEC 21x4x) — OK
- Hyper-V NetVSC — OK (multi-stage)

**Rede — detectados mas SEM driver**:
- RTL8139 (Realtek) — muito comum em QEMU
- VirtIO-Net — padrao KVM/QEMU
- VMXNET3 (VMware) — padrao VMware

**Storage**:
- EFI Block I/O — OK
- AHCI — OK
- NVMe — OK
- Hyper-V StorVSC — OK

**Compatibilidade por VM**:
| VM          | NIC         | Storage     | Input      | Status       |
|-------------|-------------|-------------|------------|--------------|
| QEMU        | E1000       | virtio/AHCI | PS/2       | Funcional    |
| Hyper-V G2  | NetVSC      | StorVSC     | VMBus kbd  | Parcial      |
| VirtualBox  | E1000       | AHCI        | PS/2       | Funcional    |
| VMware      | VMXNET3/E1000| AHCI       | PS/2       | Sem VMXNET3  |
| QEMU+virtio | virtio-net  | virtio-blk  | virtio-inp | Sem rede     |

### D7. Boot flow inconsistencias

- UEFI loader le kernel de `\\boot\\capyos64.bin` via file protocol
- ExitBootServices e adiado ate que todos os backends nativos estejam prontos
- Periodo hibrido (Boot Services ativos + kernel rodando) causa complexidade
  enorme no input/storage runtime
- ISO via El Torito precisa de efiboot.img FAT para Hyper-V Gen2
- Nao ha validacao do kernel ELF (checksum, signature)

### D8. Network stack e hardware identification

- PCI scan e brute-force por bus/dev/func — lento, sem cache
- Nao usa ACPI/MCFG para PCI Express enhanced config
- Probe de rede so checa classe 0x02 — pode perder NICs em bridges
- Hyper-V NetVSC tem init multi-stage complexo com 7 fases
- DNS cache nao tem TTL decay

---

## FASE 1: Estabilizacao Critica (Prioridade Maxima)

> Objetivo: UI funcionando na ISO + remocao de legado 32-bit

### 1.1 Corrigir Desktop Loop para Integrar com Kernel Input

**Arquivo**: `src/shell/commands/extended.c`

Problemas no `cmd_desktop_start()`:
- Loop bloqueante sem input de teclado
- Spin delay impreciso (`volatile int d`)
- Sem fallback se PS/2 mouse falhar

Correcoes necessarias:
- [ ] Integrar `kernel_input_getc()` no frame loop do desktop
- [ ] Substituir spin delay por timer do PIT/APIC
- [ ] Adicionar fallback: se `mouse_ps2_init()` falhar, operar sem mouse
- [ ] Adicionar tecla Escape/Ctrl+Q para sair do desktop e voltar ao CLI
- [ ] Chamar `desktop_handle_input()` de dentro do frame loop
- [ ] Verificar framebuffer valido antes de entrar no loop

### 1.2 Corrigir Autostart do Desktop

**Arquivo**: `src/arch/x86_64/kernel_shell_runtime.c:367-371`

O autostart via `x64_kernel_run_shell_alias("desktopstart")` e chamado
dentro de `x64_kernel_begin_shell_session()` que e invocado dentro do
`init_shell_context()` callback do login runtime. Como o desktop loop
e bloqueante, o login runtime nunca recebe controle de volta.

Correcoes:
- [ ] Mover autostart para apos o login, como flag no session_context
- [ ] Permitir que o login runtime decida quando iniciar o desktop
- [ ] Adicionar flag `--no-desktop` para sessoes CLI-only

### 1.3 Remover Todo Codigo Legado 32-bit

Remover completamente:
- [ ] `boot/boot.s`, `boot/stage1.s`, `boot/stage2.asm`
- [ ] `src/arch/x86/` (todo o diretorio)
- [ ] `include/arch/x86/` (todo o diretorio)
- [ ] Referencias legadas no Makefile (alvos `all32`, `iso-bios`, etc. ja mapeiam
      para `legacy-disabled`, mas os alvos podem ser removidos)
- [ ] Limpar `.gitignore` se houver entradas legadas

### 1.4 Migrar keyboard/core.c para x86_64

**Arquivo**: `src/drivers/input/keyboard/core.c`

Este arquivo inclui headers x86 32-bit:
```c
#include "arch/x86/cpu/isr.h"
#include "arch/x86/hw/io.h"
```

Correcoes:
- [ ] Criar `include/arch/x86_64/io.h` com `inb`/`outb` inline para long mode
- [ ] Usar o IRQ handler do x86_64 (`src/arch/x86_64/interrupts.c`) em vez do
      ISR handler do x86 32-bit
- [ ] Ou: mover `inb`/`outb` para um header compartilhado `include/drivers/io.h`
      que funcione em ambas as arquiteturas (sao identicos em instrucao)
- [ ] Atualizar `irq_install_handler()` para usar a IDT do x86_64
- [ ] Remover dependencia de `tty.h` (usar callback pattern como o input_runtime)

### 1.5 Remover #pragma GCC optimize("O0")

**Arquivo**: `src/arch/x86_64/kernel_main.c:4`

- [ ] Remover `#pragma GCC optimize("O0")`
- [ ] Se algum codigo depender de O0 (ex: spin delays), marcar apenas essas
      funcoes com `__attribute__((optimize("O0")))` individualmente
- [ ] Testar estabilidade apos remocao

---

## FASE 2: Refatoracao de Monolitos

> Objetivo: Segregar responsabilidades, descentralizar modulos

### 2.1 Desmembrar kernel_main.c (~2943 linhas → ~8 arquivos)

Extrair para arquivos dedicados:

| Componente | Destino | Linhas aprox |
|---|---|---|
| Font bitmap 8x8 | `src/gui/font8x8_data.c` + header | ~130 |
| Framebuffer console (fbcon_*) | `src/drivers/console/fbcon.c` + header | ~200 |
| COM1 serial driver | `src/drivers/serial/com1.c` + header | ~40 |
| ACPI/RSDP validation | `src/drivers/acpi/rsdp.c` (extender) | ~40 |
| Theme system | `src/gui/theme.c` + header | ~50 |
| Boot splash + banner | `src/boot/boot_splash.c` (extender boot_ui) | ~100 |
| String utilities | `src/util/kstring.c` + header | ~100 |
| Recovery report/history | `src/core/recovery.c` + header | ~200 |
| Service wiring | `src/core/service_wiring.c` + header | ~120 |

**kernel_main.c** ficaria apenas com:
- Entry point `kernel_main64()`
- Sequencia de boot (8 stages)
- Login runtime wiring
- Glue code minimo entre subsistemas

Meta: kernel_main.c com ~600-800 linhas.

### 2.2 Desmembrar system_init.c (2461 linhas)

Extrair:
- [ ] `src/core/first_boot.c` — assistente de primeira inicializacao
- [ ] `src/core/settings.c` — leitura/gravacao de configuracoes
- [ ] `src/core/theme.c` — aplicacao de temas (unificar com 2.1)
- [ ] `src/core/setup_log.c` — log buffer do setup

### 2.3 Extrair Input Runtime como Modulo Independente

O `input_runtime.c` (30KB) ja e separado, mas o kernel_main.c tem muitos
wrappers que deveriam estar no proprio modulo:

- [ ] Mover wrappers `kernel_input_getc`, `kernel_input_readline` para
      `src/arch/x86_64/input_runtime.c`
- [ ] Expor API limpa sem depender de globals do kernel_main

### 2.4 Extrair Storage Runtime Wrappers

- [ ] Mover `mount_root_CAPYFS`, `mount_encrypted_data_volume`, etc.
      inteiramente para `kernel_volume_runtime.c`
- [ ] Eliminar duplicacao de `local_copy()` entre kernel_main, kernel_shell_runtime,
      system_init (aparece em pelo menos 3 arquivos)

### 2.5 Consolidar Funcoes Utilitarias Duplicadas

Funcoes duplicadas identificadas:
- `local_copy()` — em kernel_main.c, kernel_shell_runtime.c
- `streq()` / `strings_equal()` / `layout_name_equal()` — em 5+ arquivos
- `cstring_length()` / `local_length()` / `ui_banner_strlen()` — em 4+ arquivos
- `memory_zero()` / `ds_memset()` — em 3+ arquivos
- `buffer_append_text()` / `buffer_append()` — em 2+ arquivos

Acao:
- [ ] Criar `src/util/kstring.c` com: `kstrlen`, `kstrcpy`, `kstreq`, `kmemzero`,
      `kmemcpy`, `ksnprintf_u32`, `ksnprintf_hex`
- [ ] Criar `include/util/kstring.h` com declaracoes
- [ ] Substituir todas as duplicatas pelo header compartilhado

---

## FASE 3: Melhorias de Teclado e Input

> Objetivo: Suporte completo a teclado BR-ABNT2 e teclas especiais

### 3.1 Expandir keyboard_layout struct

**Arquivo**: `include/drivers/input/keyboard_layout.h`

Adicionar campos:
```c
struct keyboard_layout {
    const char *name;
    const char *description;
    const char base[128];
    const char shift[128];
    const char altgr[128];          // NOVO: mapa AltGr (Right Alt)
    const char shift_altgr[128];    // NOVO: mapa Shift+AltGr
    const uint8_t dead[128];
    const uint8_t special[128];     // NOVO: flags para teclas especiais
};
```

### 3.2 Implementar AltGr no Core do Teclado

**Arquivo**: `src/drivers/input/keyboard/core.c`

- [ ] Detectar Right Alt (scancode E0 38) como AltGr
- [ ] Adicionar estado `altgr_on` similar a `shift_on`
- [ ] Consultar `current_layout->altgr[]` quando AltGr ativo
- [ ] Tratar Ctrl+Alt como AltGr equivalente (padrao Windows/Linux)

### 3.3 Implementar Caps Lock

- [ ] Detectar scancode 0x3A (Caps Lock toggle)
- [ ] Manter estado `capslock_on`
- [ ] Aplicar inversao de case quando ativo (base↔shift para letras apenas)

### 3.4 Implementar Teclas Especiais

- [ ] Arrow keys: E0 48 (up), E0 50 (down), E0 4B (left), E0 4D (right)
- [ ] Home (E0 47), End (E0 4F), PgUp (E0 49), PgDn (E0 51)
- [ ] Insert (E0 52), Delete (E0 53)
- [ ] F1-F12 (0x3B-0x46, 0x57-0x58)
- [ ] Ctrl: esquerdo (0x1D), direito (E0 1D) — Ctrl+C (0x03), Ctrl+D (0x04),
      Ctrl+L (limpar tela)

### 3.5 Completar Layout BR-ABNT2

**Arquivo**: `src/drivers/input/keyboard/layouts/br_abnt2.c`

Adicionar mapa `altgr`:
- [ ] AltGr+1 = ¹, AltGr+2 = ², AltGr+3 = ³ (se suportado)
- [ ] AltGr+e = Euro (se encoding suportar)
- [ ] AltGr+q = / (padrao ABNT2)
- [ ] Testar todos os dead keys: ´ ` ^ ~ ¨
- [ ] Verificar tecla extra 0x56 (a esquerda do Z) = \\ e | com Shift
- [ ] Tecla / do numpad (0x73 via E0 35) ja esta mapeada — validar

### 3.6 Integrar com Desktop Input

- [ ] `desktop_handle_input()` precisa receber keycodes estendidos (arrows, etc.)
- [ ] Terminal GUI precisa processar arrow keys para historico de comandos
- [ ] Scroll no terminal com PgUp/PgDn

---

## FASE 4: Compatibilidade de Drivers e Hardware

> Objetivo: Suporte a mais VMs e hardware real

### 4.1 Driver VirtIO-Net

**Prioridade**: Alta (padrao QEMU/KVM)

- [ ] Implementar `src/drivers/net/virtio_net.c`
- [ ] Detectar via PCI: vendor=0x1AF4, device=0x1000 (legacy) ou 0x1041 (modern)
- [ ] Implementar virtqueue (split ring ou packed ring)
- [ ] Registrar no `net_probe.c` como `NET_NIC_KIND_VIRTIO_NET` com
      `runtime_supported = 1`
- [ ] Integrar com `stack_driver.c` para send/recv frames

### 4.2 Driver RTL8139

**Prioridade**: Media (comum em QEMU com `-net nic,model=rtl8139`)

- [ ] Implementar `src/drivers/net/rtl8139.c`
- [ ] MMIO: init, reset, set MAC, enable TX/RX, IRQ handling
- [ ] Ring buffer de RX (4 paginas)
- [ ] TX: 4 descriptors
- [ ] Registrar no `net_probe.c`

### 4.3 Melhorar Deteccao PCI/PCIe

**Arquivo**: `src/drivers/pcie/pcie.c`

- [ ] Usar ACPI MCFG table para PCIe enhanced config (MMIO-based)
- [ ] Cachear dispositivos encontrados em array estatico
- [ ] Logar todos os dispositivos PCI encontrados (para debug)
- [ ] Suporte a PCI bridges (class 0x06, subclass 0x04) para scan recursivo

### 4.4 Melhorar GPU Detection

**Arquivo**: `src/drivers/gpu/gpu_core.c`

- [ ] Detectar e logar GPU via PCI class 0x03
- [ ] Identificar vendor (Intel, AMD, NVIDIA, VMware SVGA)
- [ ] Para VMware: detectar SVGA II (vendor=0x15AD, device=0x0405)
- [ ] Para VirtualBox: detectar VBoxVGA/VMSVGA
- [ ] Manter UEFI GOP como fallback universal

### 4.5 Melhorar Hyper-V Compatibility

Problemas conhecidos:
- VMBus channel discovery e sensivel a ordem
- ExitBootServices timing afeta disponibilidade de input/storage
- NetVSC precisa de 7 fases para ficar ready

Melhorias:
- [ ] Timeout mais agressivo no handshake VMBus (atual pode travar)
- [ ] Retry automatico no NetVSC se fase falhar
- [ ] Melhor diagnostico: `hyperv-status` command no shell
- [ ] Testar com Hyper-V Gen1 (BIOS legacy — nao suportado, mas logar aviso)

### 4.6 USB Keyboard/Mouse Support (Futuro)

- [ ] xHCI driver basico ja existe (`src/drivers/usb/xhci.c`)
- [ ] Implementar USB HID class driver para teclado
- [ ] Implementar USB HID class driver para mouse
- [ ] Registrar como backend no input_runtime

---

## FASE 5: Performance e Otimizacoes

> Objetivo: Boot mais rapido, rendering mais fluido, menor uso de CPU

### 5.1 Otimizacoes de Framebuffer

- [ ] **memcpy/memmove otimizado**: usar `rep movsb/movsq` para scroll e fill
- [ ] **Double buffering**: alocar backbuffer, renderizar nele, copiar para FB
      de uma vez (evita tearing)
- [ ] `fbcon_scroll()` atual copia pixel-por-pixel — usar block copy
- [ ] `fbcon_fill_rect_px()` pode usar `rep stosd` para preencher linhas

### 5.2 Otimizacoes de Boot

- [ ] Remover spin delays do splash (usar PIT/APIC timer real)
- [ ] Splash delay atual: ~2.2M + 3.2M iterations × 15 steps ≈ segundos desperdicados
- [ ] Paralelizar probes: storage detection + network probe em paralelo
      (requer scheduler funcional)

### 5.3 Timer-Based Frame Loop

**Arquivo**: `src/shell/commands/extended.c:207-210`

Substituir:
```c
for (volatile int d = 0; d < 50000; d++) {}
```
Por:
```c
x64_platform_timer_wait_ms(16);  // ~60 FPS
```

### 5.4 Scheduler Improvements

- [ ] Atualmente `SCHED_POLICY_COOPERATIVE` — sem preempcao real
- [ ] APIC timer ja esta configurado a 100Hz (`apic_timer_start(100)`)
- [ ] Implementar time-slice preemption no scheduler_tick
- [ ] Permitir que o desktop loop rode como task em vez de bloquear o shell

### 5.5 Memory Management

- [ ] PMM ja existe mas VMM e basico
- [ ] Implementar page-level allocation para framebuffer backbuffer
- [ ] Mapear MMIO regions de drivers via VMM (atualmente acesso direto)

---

## FASE 6: Revisao do Boot Flow

> Objetivo: Robustez em todos os caminhos de boot

### 6.1 Boot via ISO (UEFI)

Fluxo atual:
```
UEFI firmware
  → EFI/BOOT/BOOTX64.EFI (uefi_loader.c)
    → Le \\boot\\capyos64.bin (ELF64)
    → Aloca pages, carrega PT_LOAD segments
    → Preenche boot_handoff struct
    → Salta para e_entry (kernel_main64)
```

Melhorias:
- [ ] Validar ELF magic e e_machine antes de carregar
- [ ] Verificar checksum/hash do kernel (integrity check)
- [ ] Exibir versao do loader na tela UEFI antes de carregar kernel
- [ ] Adicionar menu de boot no loader (normal / recovery / diagnostics)
- [ ] Fallback se kernel nao encontrado: exibir mensagem util

### 6.2 Boot via HDD/NVMe (Provisionado)

Fluxo atual:
```
UEFI firmware
  → ESP (FAT32) / EFI/BOOT/BOOTX64.EFI
    → Descobre particao BOOT via GPT type GUID
    → Le manifest (gen_manifest.py gera manifest.bin)
    → Le kernel de BOOT partition via BlockIO
    → Preenche handoff com referencia a particao DATA
    → Salta para kernel
```

Melhorias:
- [ ] Verificar integridade do manifest antes de usar
- [ ] Suporte a A/B boot slots no loader (boot_slot.c ja existe no kernel)
- [ ] Timeout com fallback para recovery se kernel falhar 3x
- [ ] Detectar e logar tipo de midia (HDD, SSD, NVMe, VHDx)

### 6.3 Boot via VHDx (Hyper-V)

Fluxo:
```
Hyper-V Gen2 firmware
  → El Torito UEFI (efiboot.img FAT)
    → BOOTX64.EFI
    → Kernel via BlockIO (StorVSC/VMBus)
```

Melhorias:
- [ ] Testar com VHDx fixo e dinamico
- [ ] Validar que efiboot.img contem loader e kernel atualizados
- [ ] Diagnostico se VMBus storage nao responder

### 6.4 ExitBootServices Timing

Problema: O kernel mantem Boot Services ativos ate que todos os backends
nativos estejam prontos. Isso cria um periodo hibrido complexo.

Melhorias:
- [ ] Documentar claramente os estados possiveis (BS active / transition / done)
- [ ] Adicionar timeout maximo para ExitBootServices (ex: 30 segundos)
- [ ] Se timeout expirar, forcar EBS e operar em modo degradado
- [ ] Klog todas as transicoes de estado

### 6.5 Revisao de Handoff Structure

**Arquivo**: `include/boot/handoff.h`

- [ ] Verificar que todos os campos sao preenchidos pelo loader
- [ ] Validar versao minima do handoff no kernel (atualmente version >= 2/3/7)
- [ ] Adicionar campo para boot mode (normal/recovery/diagnostic)
- [ ] Adicionar campo para boot media type (ISO/HDD/NVMe/VHDx)

---

## FASE 7: Network Stack e Hardware ID (Melhoria Continua)

### 7.1 Melhorar Network Probe

- [ ] Logar TODOS os dispositivos PCI encontrados (nao so classe 0x02)
- [ ] Criar `print-pci` shell command para listar dispositivos
- [ ] Identificar bridges PCI e scan recursivo de buses secundarios
- [ ] Detectar NICs embutidas em bridges (comum em hardware real)

### 7.2 DHCP Client

- [ ] DHCP discover/offer/request/ack ja existe parcialmente
- [ ] Verificar que funciona end-to-end com E1000 e virtio-net
- [ ] Timeout e retry em DHCP discover
- [ ] Aplicar configuracao recebida (IP, mask, GW, DNS)

### 7.3 TCP Stack Hardening

**Arquivo**: `src/net/tcp.c` (10KB)

- [ ] Verificar state machine completa (LISTEN→SYN_RCVD→ESTABLISHED→...)
- [ ] Implementar retransmission timer
- [ ] Window size management
- [ ] Proper RST handling

### 7.4 HTTP Client

**Arquivo**: `src/net/http.c` (8KB)

- [ ] Verificar que GET funciona end-to-end
- [ ] Chunked transfer encoding
- [ ] Redirect handling (301/302)
- [ ] Timeout em connect e receive

---

## Matriz de Dependencias entre Fases

```
Fase 1 (Estabilizacao)
  ├── 1.1 Desktop Loop Fix
  ├── 1.2 Autostart Fix (depende de 1.1)
  ├── 1.3 Remover Legado 32-bit
  ├── 1.4 Migrar keyboard/core.c (depende de 1.3)
  └── 1.5 Remover O0 pragma

Fase 2 (Refatoracao) — depende de Fase 1
  ├── 2.1 Desmembrar kernel_main.c
  ├── 2.2 Desmembrar system_init.c
  ├── 2.3 Extrair Input Runtime
  ├── 2.4 Extrair Storage Wrappers
  └── 2.5 Consolidar Utilitarios (pode comecar em paralelo com 2.1-2.4)

Fase 3 (Teclado) — pode comecar apos 1.4
  ├── 3.1 Expandir layout struct
  ├── 3.2 Implementar AltGr (depende de 3.1)
  ├── 3.3 Caps Lock
  ├── 3.4 Teclas especiais
  ├── 3.5 Completar BR-ABNT2 (depende de 3.1, 3.2)
  └── 3.6 Integrar com Desktop (depende de 1.1)

Fase 4 (Drivers) — pode comecar apos Fase 1
  ├── 4.1 VirtIO-Net
  ├── 4.2 RTL8139
  ├── 4.3 PCI/PCIe melhorado
  ├── 4.4 GPU detection
  ├── 4.5 Hyper-V fixes
  └── 4.6 USB HID (futuro)

Fase 5 (Performance) — depende de Fase 2
  ├── 5.1 Framebuffer otimizado
  ├── 5.2 Boot otimizado
  ├── 5.3 Timer-based frame loop
  ├── 5.4 Scheduler preemptivo
  └── 5.5 Memory management

Fase 6 (Boot Flow) — pode comecar apos Fase 1
  ├── 6.1 ISO boot robustez
  ├── 6.2 HDD/NVMe boot
  ├── 6.3 VHDx boot
  ├── 6.4 EBS timing
  └── 6.5 Handoff revision
```

---

## Ordem de Execucao Recomendada

### Sprint 1: Critico (Semana 1-2)
1. **1.5** Remover `#pragma GCC optimize("O0")`
2. **1.3** Remover codigo legado 32-bit
3. **1.4** Migrar keyboard/core.c para x86_64
4. **1.1** Corrigir desktop loop (input integration)
5. **1.2** Corrigir autostart do desktop

### Sprint 2: Estrutural (Semana 3-4)
6. **2.5** Consolidar funcoes utilitarias duplicadas
7. **2.1** Desmembrar kernel_main.c (primeira rodada — extrair font, fbcon, COM1)
8. **3.1** Expandir keyboard_layout struct
9. **3.2** Implementar AltGr
10. **3.3** Implementar Caps Lock

### Sprint 3: Compatibilidade (Semana 5-6)
11. **3.4** Teclas especiais (arrows, F-keys, etc.)
12. **3.5** Completar BR-ABNT2
13. **4.1** Driver VirtIO-Net
14. **4.3** Melhorar PCI detection
15. **5.3** Timer-based frame loop

### Sprint 4: Robustez (Semana 7-8)
16. **2.1** (cont.) Desmembrar kernel_main.c (recovery, services, wrappers)
17. **2.2** Desmembrar system_init.c
18. **5.1** Framebuffer otimizado
19. **6.1-6.3** Revisao de boot flows
20. **6.4** ExitBootServices timing

---

## Metricas de Sucesso

| Metrica | Antes | Meta |
|---|---|---|
| kernel_main.c linhas | 2943 | < 800 |
| system_init.c linhas | 2461 | < 600 |
| Arquivos legados x86 | 9+ | 0 |
| VMs com rede funcional | 3 (QEMU/VBox/Hyper-V) | 5+ (+ VMware, KVM-virtio) |
| Teclas BR-ABNT2 mapeadas | ~60 | ~95+ |
| Boot time (splash→login) | ~8-12s estimado | < 3s |
| Desktop FPS | irregular (spin loop) | 30-60 FPS estavel |
| Funcoes duplicadas | ~15 | 0 |
| #pragma O0 no kernel | sim | nao |

---

## Arquivos-Chave para Cada Dominio

### Boot Flow
- `src/boot/uefi_loader.c` — UEFI loader (3310 linhas)
- `src/arch/x86_64/kernel_main.c` — entry point (2943 linhas)
- `include/boot/handoff.h` — boot handoff struct
- `src/boot/boot_ui.c` — boot splash/warnings UI

### Kernel Core
- `src/arch/x86_64/kernel_main.c` — MONOLITO principal
- `src/core/system_init.c` — MONOLITO setup/config
- `src/arch/x86_64/kernel_shell_runtime.c` — shell bootstrap
- `src/core/login_runtime.c` — login loop

### Desktop/GUI
- `src/gui/desktop.c` — desktop session manager
- `src/gui/compositor.c` — window compositor
- `src/gui/terminal.c` — GUI terminal emulator
- `src/gui/taskbar.c` — taskbar
- `src/shell/commands/extended.c` — desktop-start command

### Input
- `src/drivers/input/keyboard/core.c` — keyboard driver
- `src/drivers/input/keyboard/layouts/br_abnt2.c` — layout BR
- `src/arch/x86_64/input_runtime.c` — multi-backend input
- `include/drivers/input/keyboard_layout.h` — layout struct

### Network
- `src/net/stack.c` — network stack core
- `src/drivers/net/net_probe.c` — NIC detection
- `src/drivers/net/e1000.c` — E1000 driver
- `src/net/hyperv_runtime.c` — Hyper-V networking
- `src/drivers/pcie/pcie.c` — PCI bus access

### Storage
- `src/arch/x86_64/storage_runtime.c` — storage dispatch
- `src/drivers/storage/ahci.c` — AHCI driver
- `src/drivers/nvme/nvme.c` — NVMe driver
- `src/arch/x86_64/storage_runtime_hyperv.c` — Hyper-V storage
