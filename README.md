# 🌌 NoirOS

> **NoirOS** é um sistema operacional hobby desenvolvido do zero por **Henrique Schwarz Souza Farisco**.  
> Inspirado pela cosmologia 🌠, evolui passo a passo — como o próprio universo, da **Singularidade** à **Consciência**.

---

## ✨ Visão Geral

NoirOS é um kernel **x86 32-bit** construído com uma **toolchain customizada** (`i686-elf-gcc`) e projetado para rodar em **QEMU** ou hardware real.  
Boot via **Multiboot**, configura sua própria **IDT + PIC**, trata **interrupções de teclado** e oferece um terminal VGA em modo texto para interação.

---

## 🛠️ Stack Tecnológico

- **Linguagem:** C + Assembly (NASM, ELF32)
- **Compilador:** `i686-elf-gcc` (cross-compiler, freestanding)
- **Linker:** `i686-elf-ld` com `linker.ld` customizado
- **Boot:** Multiboot v1 (suporte GRUB/QEMU)
- **Modo CPU:** Protected Mode (x86, 32-bit)
- **Drivers:** 
  - Teclado (IRQ1, scancode set 1, com Shift/Backspace)
  - VGA modo texto (framebuffer em `0xB8000`)

---

## ✨ Estado Atual

- **Boot:** Multiboot v1 (GRUB/QEMU) via `kernel_entry.s`
- **GDT:** tabela própria mínima (null, code=0x9A, data=0x92)
- **IDT:** 256 entradas, exceções 0..31 com mensagens na tela, IRQs 32..47
- **PIC:** remapeado para 0x20/0x28; IRQ0 (PIT) + IRQ1 (teclado) habilitados
- **PIT (timer):** programado em 100 Hz, contador de ticks (`pit_ticks()`)
- **Drivers:**
  - **VGA texto** (80×25): escrita, scroll, backspace, newline, cursor de hardware
  - **Teclado (IRQ1):** scancodes set 1, shift/backspace/enter
- **Loop principal:** `sti(); for(;;) hlt();`

Saída esperada no boot:
```
NoirOS 1 - Versao Singularity esta rodando!
Ola Mundo!
>
```
Se ocorrer exceção (ex.: #PF Page Fault), mensagem clara é exibida antes do travamento.

---

## 📂 Estrutura

```
boot/boot.s           ← boot sector opcional (não buildado)
include/              ← headers (gdt.h, idt.h, isr.h, io.h, vga.h, keyboard.h, pit.h, debug.h …)
src/
├── kernel_entry.s   ← Multiboot header + _start
├── linker.ld        ← script de link (ELF i386, base 1MiB)
├── kernel.c         ← kernel_main (init VGA, GDT, IDT, PIC, PIT, loop)
├── gdt.c/.h + gdt_flush.s  ← GDT mínima
├── idt.c/.h, isr.c, interrupts.s  ← IDT + ISRs/IRQs
├── keyboard.c/.h
├── vga.c/.h         ← texto + cursor HW
├── pit.c/.h         ← temporizador (IRQ0)
├── debug.c/.h       ← saída porta 0xE9 (QEMU -debugcon)
└── ports.c/.h, io.h ← inb/outb/cli/sti/hlt
Makefile
README.md
```

---

## ⚡ Funcionalidades (v0.1 — *Singularity*)

- [x] Kernel ELF bootável via Multiboot
- [x] GDT própria instalada
- [x] Configuração da IDT (256 entradas)
- [x] PIC remapeado + masking
- [x] Handler de teclado (IRQ1)
- [x] Driver VGA texto (print, scroll, backspace, newlines, cursor HW)
- [x] PIT 100Hz + IRQ0 habilitado
- [x] Mensagens de exceção (#PF mostra CR2)
- [x] Main loop com `sti(); for(;;) hlt();`
- [ ] Gerenciamento de memória (paging, allocators)
- [ ] System calls (user ↔ kernel)
- [ ] Suporte a filesystem
- [ ] Escalonador de processos
- [ ] Multitarefa
- [ ] Programas userland
- [ ] Shell / interpretador de comandos
- [ ] Stack de rede
- [ ] Interface gráfica

---

## ⚙️ Build & Execução

```bash
make clean && make        # compila kernel ELF
make run                  # roda QEMU: -kernel build/kernel.bin -m 64
```

### Debug no QEMU

* Porta 0xE9 (debugcon):

  ```bash
  qemu-system-i386 -kernel build/kernel.bin -m 64 -debugcon stdio -serial none
  ```
* Ver traps/interrupções:

  ```bash
  qemu-system-i386 -kernel build/kernel.bin -d int,cpu_reset -no-reboot -no-shutdown
  ```

---

## 🚧 Roadmap Próximo (v0.2 “Inflaton”)

* [x] GDT mínima instalada (já feito ✅)
* [x] Mensagens de exceções (#PF mostra CR2) ✅
* [x] Cursor VGA de hardware ✅
* [x] PIT 100Hz + IRQ0 habilitado ✅
* [ ] Allocator inicial (bump + kalloc/kfree)
* [ ] Paging (ativar CR0.PG, mapear 0..4MiB identidade)
* [ ] Scheduler rudimentar (usar ticks do PIT)
* [ ] Melhorar tratamento de exceções (#PF: decodificar err_code)
* [ ] API de impressão numérica (print_hex, print_dec)

---

## 📝 Notas

* `boot/boot.s` é apenas demonstração de boot sector real mode.
  Não é buildado nem necessário quando usamos GRUB/QEMU (`-kernel`).
* O kernel é freestanding (`-ffreestanding -nostdlib`), não usa libc.

---

## 📜 Licença

MIT (livre uso/estudo).
