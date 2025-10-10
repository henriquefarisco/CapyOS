# --- Toolchain (i686-elf é o oficial; fallback = gcc -m32) ---
AS      := nasm
CROSS   ?= i686-elf

ifeq ($(shell which $(CROSS)-gcc 2>/dev/null),)
  # Fallback (usa compilador do host em 32-bit) — menos isolado
  CC      := gcc
  LD      := ld
  OBJCOPY := objcopy
  CFLAGS  := -m32 -ffreestanding -O2 -Wall -Wextra -Wa,--noexecstack -Iinclude
  LDFLAGS := -nostdlib -m elf_i386
else
  # Oficial (cross bare-metal)
  CC      := $(CROSS)-gcc
  LD      := $(CROSS)-ld
  OBJCOPY := $(CROSS)-objcopy
  CFLAGS  := -ffreestanding -O2 -Wall -Wextra -Wa,--noexecstack -Iinclude
  LDFLAGS := -nostdlib
endif

# toolchain = conjunto compilador/ligador; freestanding = sem libc/ambiente do SO; -m elf_i386 força ld a gerar ELF 32-bit; ELF = Executable and Linkable Format

BUILD    = build
SRC_DIR  = src
BOOT_DIR = boot

KERNEL_ELF = $(BUILD)/kernel.bin
BOOT_BIN   = $(BUILD)/bootloader.bin

LINKER_SCRIPT = $(SRC_DIR)/arch/x86/linker.ld

C_SRCS   = $(shell find $(SRC_DIR) -name '*.c')
S_SRCS   = $(shell find $(SRC_DIR) -name '*.s')
ASM_SRCS = $(shell find $(SRC_DIR) -name '*.asm')

C_OBJS   = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(C_SRCS))
S_OBJS   = $(patsubst $(SRC_DIR)/%.s,$(BUILD)/%.o,$(S_SRCS))
ASM_OBJS = $(patsubst $(SRC_DIR)/%.asm,$(BUILD)/%.o,$(ASM_SRCS))

OBJS = $(C_OBJS) $(S_OBJS) $(ASM_OBJS)


all: $(BOOT_BIN) $(KERNEL_ELF)

# Bootloader
$(BOOT_BIN): $(BOOT_DIR)/boot.s | $(BUILD)
	$(AS) -f bin $< -o $@

# Create build directory if it doesn't exist
$(BUILD):
	mkdir -p $(BUILD)

$(KERNEL_ELF): $(OBJS) $(LINKER_SCRIPT)
	$(LD) -T $(LINKER_SCRIPT) $(LDFLAGS) -o $@ $(OBJS)

# Regras genéricas para objetos
$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC_DIR)/%.s | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: $(SRC_DIR)/%.asm | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

run: all
	@echo "Iniciando QEMU (Multiboot direto no ELF)..."
	qemu-system-i386 -kernel $(KERNEL_ELF) -m 64

# Disk image (raw) for persistent tests (default 64 MiB)
DISK_IMG ?= build/disk.img
DISK_SIZE ?= 64M

disk-img: $(BUILD)
	@echo "Criando imagem de disco em $(DISK_IMG) (tamanho $(DISK_SIZE))"
	truncate -s $(DISK_SIZE) $(DISK_IMG)

run-disk: all disk-img
	@echo "Iniciando QEMU com disco IDE persistente..."
	qemu-system-i386 -kernel $(KERNEL_ELF) -m 64 -drive file=$(DISK_IMG),if=ide,format=raw

# ISO (requer grub-mkrescue e xorriso instalados)
ISO_DIR := build/iso-root
ISO_IMG := build/NoirOS.iso

$(ISO_DIR): $(BUILD)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.bin
	echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "NoirOS" { multiboot /boot/kernel.bin; boot }' >> $(ISO_DIR)/boot/grub/grub.cfg

iso: all $(ISO_DIR)
	@which grub-mkrescue >/dev/null 2>&1 || { echo 'grub-mkrescue nao encontrado'; exit 1; }
	grub-mkrescue -o $(ISO_IMG) $(ISO_DIR)
	@echo "ISO gerada em $(ISO_IMG)"

clean:
	rm -rf $(BUILD)
.PHONY: all run clean
