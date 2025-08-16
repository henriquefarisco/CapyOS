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

## 📂 Estrutura do Projeto

```text
NoirOS/
├── boot/              # (boot sector opcional, não requerido para Multiboot)
├── build/             # artefatos de build (kernel.bin, .o)
├── include/           # headers do kernel (io.h, isr.h, idt.h, keyboard.h, vga.h)
├── src/               # código-fonte do kernel
│   ├── kernel_entry.s # Multiboot header + entry point
│   ├── interrupts.s   # ISRs + IRQ stubs
│   ├── linker.ld      # script do linker
│   ├── kernel.c       # kernel principal (init + loop)
│   ├── isr.c / .h     # dispatch ISR/IRQ + PIC remap/mask
│   ├── idt.c / .h     # configuração da IDT
│   ├── keyboard.c/.h  # driver de teclado
│   ├── vga.c / .h     # driver VGA texto
│   ├── io.h           # funções inline de I/O de porta
│   └── debug.c        # utilitários de debug (se houver)
└── Makefile           # sistema de build
```

---

## ⚡ Funcionalidades (v0.1 — *Singularity*)

- [x] **Kernel ELF bootável** via Multiboot
- [x] **GDT básica** (via entrada do bootloader)
- [x] **Configuração da IDT** (256 entradas)
- [x] **PIC remapeado + masking**
- [x] **Handler de teclado** (IRQ1)
- [x] **Driver VGA texto** (print, scroll, backspace, newlines)
- [x] **Main loop** com `hlt` (baixo uso de CPU)
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

## 🚀 Build & Execução

### 🔧 Requisitos

- `nasm`
- `i686-elf-gcc` + `i686-elf-ld` (cross-compiler)
- `qemu-system-i386`

Instalação no Ubuntu/Debian:
```bash
sudo apt update
sudo apt install nasm qemu-system-i386
# Para cross-compiler:
sudo apt install gcc-12-i686-linux-gnu binutils-i686-linux-gnu
```

### 🏗️ Build

```bash
make clean && make
```

Gera:

- `build/kernel.bin` → kernel ELF compatível com Multiboot

### ▶️ Rodando no QEMU

```bash
make run
```

Saída esperada (modo texto):

```
NoirOS 1 - Versao Singularity esta rodando!
Ola Mundo!
>
```

---

## 🌌 Versionamento — *Evolução Cósmica*

As versões do NoirOS seguem a **linha do tempo cosmológica**:

1. **Singularity** → *NoirOS 1* (antes do tempo)
2. **Inflaton** → *NoirOS 2* (inflação cósmica)
3. **Plasma** → *NoirOS 3* (universo quente e denso)
4. **Recombination** → *NoirOS 4* (luz se liberta)
5. **Galaxy** → *NoirOS 5*
6. **Star** → *NoirOS 6*
7. **System** → *NoirOS 7*
8. **Abiogenesis** → *NoirOS 8*
9. **Evolution** → *NoirOS 9*
10. **Consciousness** → *NoirOS 10*

---

## 📖 Roadmap

- **v0.1 (Singularity)** → Bootável, interrupções, teclado, VGA ✅
- **v0.2 (Inflaton)** → Paging + memory manager
- **v0.3 (Plasma)** → Processos + context switching
- **v0.4 (Recombination)** → Userland básico + system calls
- **v0.5+ (Galaxy …)** → Multitarefa, FS, rede, GUI

---

## 🧑‍💻 Autor

**Henrique Schwarz Souza Farisco**

- 🌐 Futuro criador do NoirOS
- 🚀 Dev de OS hobby & system hacker
- 📚 Apaixonado por programação low-level

---

## 🤝 Contribuindo

NoirOS é um **projeto pessoal/hobby**, mas ideias, recursos de aprendizado e contribuições são sempre bem-vindos.

- Fork 🍴
- Experimente ⚡
- Compartilhe conhecimento 📖
- Abra PRs 🛠️

---

## ⚠️ Disclaimer

Este é um projeto de aprendizado — **não** é um sistema operacional pronto para produção. Espere crashes, comportamento indefinido e explosões cósmicas 💥.

---
---
