# --- Toolchain (i686-elf é o oficial; fallback = gcc -m32) ---
AS      := nasm
CROSS   ?= i686-elf

ifeq ($(shell which $(CROSS)-gcc 2>/dev/null),)
  # Fallback (usa compilador do host em 32-bit) — menos isolado
  CC      := gcc
  LD      := ld
  OBJCOPY := objcopy
  CFLAGS  := -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude
  LDFLAGS := -nostdlib -m elf_i386
else
  # Oficial (cross bare-metal)
  CC      := $(CROSS)-gcc
  LD      := $(CROSS)-ld
  OBJCOPY := $(CROSS)-objcopy
  CFLAGS  := -ffreestanding -O2 -Wall -Wextra -Iinclude
  LDFLAGS := -nostdlib
endif

# toolchain = conjunto compilador/ligador; freestanding = sem libc/ambiente do SO; -m elf_i386 força ld a gerar ELF 32-bit; ELF = Executable and Linkable Format

BUILD    = build
SRC_DIR  = src
BOOT_DIR = boot

KERNEL_ELF = $(BUILD)/kernel.bin
BOOT_BIN   = $(BUILD)/bootloader.bin

OBJS = \
  $(BUILD)/kernel_entry.o \
  $(BUILD)/interrupts.o   \
  $(BUILD)/ports.o        \
  $(BUILD)/isr.o          \
  $(BUILD)/idt.o          \
  $(BUILD)/keyboard.o     \
  $(BUILD)/vga.o          \
  $(BUILD)/kernel.o       \
  $(BUILD)/debug.o        \
  $(BUILD)/gdt.o          \
  $(BUILD)/gdt_flush.o    \
  $(BUILD)/pit.o          \
  $(BUILD)/kmem.o


all: $(BOOT_BIN) $(KERNEL_ELF)

# Bootloader
$(BOOT_BIN): $(BOOT_DIR)/boot.s | $(BUILD)
	$(AS) -f bin $< -o $@

# Create build directory if it doesn't exist
$(BUILD):
	mkdir -p $(BUILD)

# Kernel objects
$(BUILD)/kernel_entry.o: $(SRC_DIR)/kernel_entry.s | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/vga.o: $(SRC_DIR)/vga.c include/vga.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: $(SRC_DIR)/interrupts.s | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/ports.o: $(SRC_DIR)/ports.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/isr.o: $(SRC_DIR)/isr.c include/isr.h include/io.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC_DIR)/isr.c -o $@

$(BUILD)/idt.o: $(SRC_DIR)/idt.c include/idt.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: $(SRC_DIR)/keyboard.c include/keyboard.h include/isr.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: $(SRC_DIR)/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/debug.o: $(SRC_DIR)/debug.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) $(SRC_DIR)/linker.ld
	$(LD) -T $(SRC_DIR)/linker.ld $(LDFLAGS) -o $@ $(OBJS)

# Regras genéricas para objetos
$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC_DIR)/%.s | $(BUILD)
	$(AS) -f elf32 $< -o $@

run: all
	@echo "Iniciando QEMU (Multiboot direto no ELF)..."
	qemu-system-i386 -kernel $(KERNEL_ELF) -m 64

clean:
	rm -rf $(BUILD)
.PHONY: all run clean
