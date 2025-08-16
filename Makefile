# Toolchain (use i686-elf-*; se não tiver, podemos adaptar para gcc -m32)
AS       = nasm
CC       = i686-elf-gcc
LD       = i686-elf-ld
OBJCOPY  = i686-elf-objcopy

CFLAGS   = -ffreestanding -m32 -O2 -Wall -Wextra -Iinclude
LDFLAGS  = -nostdlib -m elf_i386

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
  $(BUILD)/debug.o


all: $(BOOT_BIN) $(KERNEL_ELF)

# Bootloader em binário cru
$(BOOT_BIN): $(BOOT_DIR)/boot.s
	$(AS) -f bin $< -o $@

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

run: all
	@echo "Iniciando QEMU (Multiboot direto no ELF)..."
	qemu-system-i386 -kernel $(KERNEL_ELF) -m 64

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
.PHONY: all run clean
