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

clean:
	rm -rf $(BUILD)
.PHONY: all run clean
