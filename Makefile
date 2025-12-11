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

# Saídas separadas: kernel do SO e kernel do instalador (NGIS)
NOIROS_ELF  = $(BUILD)/noiros.bin
NGIS_ELF    = $(BUILD)/installer.bin
BOOT_BIN    = $(BUILD)/bootloader.bin

LINKER_SCRIPT = $(SRC_DIR)/arch/x86/linker.ld

ALL_C_SRCS   = $(shell find $(SRC_DIR) -name '*.c')
ALL_S_SRCS   = $(shell find $(SRC_DIR) -name '*.s')
ALL_ASM_SRCS = $(shell find $(SRC_DIR) -name '*.asm')

# Objetos comuns (assembly/asm e .s são compartilhados entre sabores)
COMMON_S_OBJS   = $(patsubst $(SRC_DIR)/%.s,$(BUILD)/%.o,$(ALL_S_SRCS))
COMMON_ASM_OBJS = $(patsubst $(SRC_DIR)/%.asm,$(BUILD)/%.o,$(ALL_ASM_SRCS))

# Fontes C: separar por sabor removendo a unidade de entrada principal do outro
MAIN_EXCLUDE       = $(SRC_DIR)/core/installer_main.c
INSTALLER_EXCLUDE  = $(SRC_DIR)/core/kernel.c $(SRC_DIR)/system/kernel_main.c

MAIN_C_SRCS      = $(filter-out $(MAIN_EXCLUDE),$(ALL_C_SRCS))
INSTALLER_C_SRCS = $(filter-out $(INSTALLER_EXCLUDE),$(ALL_C_SRCS))

MAIN_C_OBJS      = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(MAIN_C_SRCS))
INSTALLER_C_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(INSTALLER_C_SRCS))

NOIROS_OBJS = $(COMMON_S_OBJS) $(COMMON_ASM_OBJS) $(MAIN_C_OBJS)
NGIS_OBJS   = $(COMMON_S_OBJS) $(COMMON_ASM_OBJS) $(INSTALLER_C_OBJS)


all: $(BOOT_BIN) $(NOIROS_ELF) $(NGIS_ELF)

# Bootloader
$(BOOT_BIN): $(BOOT_DIR)/boot.s | $(BUILD)
	$(AS) -f bin $< -o $@

# Create build directory if it doesn't exist
$(BUILD):
	mkdir -p $(BUILD)

$(NOIROS_ELF): $(NOIROS_OBJS) $(LINKER_SCRIPT)
	$(LD) -T $(LINKER_SCRIPT) $(LDFLAGS) -o $@ $(NOIROS_OBJS)

$(NGIS_ELF): $(NGIS_OBJS) $(LINKER_SCRIPT)
	$(LD) -T $(LINKER_SCRIPT) $(LDFLAGS) -o $@ $(NGIS_OBJS)

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

run: $(NOIROS_ELF)
	@echo "Iniciando QEMU (NoirOS kernel direto)..."
	qemu-system-i386 -kernel $(NOIROS_ELF) -m 64

# Disk image (raw) for persistent tests (default 64 MiB)
DISK_IMG ?= build/disk.img
DISK_SIZE ?= 128M

disk-img: $(BUILD)
	@if [ ! -f "$(DISK_IMG)" ]; then \
	  echo "Criando imagem de disco em $(DISK_IMG) (tamanho $(DISK_SIZE))"; \
	  truncate -s $(DISK_SIZE) $(DISK_IMG); \
	else \
	  echo "Imagem de disco existente: $(DISK_IMG)"; \
	fi

run-disk: $(NOIROS_ELF) disk-img
	@echo "Iniciando QEMU (NoirOS) com disco persistente..."
	qemu-system-i386 -kernel $(NOIROS_ELF) -m 64 -drive file=$(DISK_IMG),if=ide,format=raw

# ISO (requer grub-mkrescue e xorriso instalados)
ISO_DIR := build/iso-root
ISO_IMG := build/NoirOS-Installer.iso


$(ISO_DIR): $(BUILD) $(NGIS_ELF)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(NGIS_ELF) $(ISO_DIR)/boot/installer.bin
	echo 'set timeout=1' > $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "Noir Guided Installation System" { multiboot /boot/installer.bin; boot }' >> $(ISO_DIR)/boot/grub/grub.cfg

iso: $(NGIS_ELF) $(ISO_DIR)
	@which grub-mkrescue >/dev/null 2>&1 || { echo 'grub-mkrescue nao encontrado'; exit 1; }
	grub-mkrescue -o $(ISO_IMG) $(ISO_DIR)
	@echo "ISO gerada em $(ISO_IMG)"

# Executar instalador via ISO (CD-ROM)
run-installer-iso: iso disk-img
	@echo "Iniciando QEMU com ISO do instalador (NGIS)..."
	qemu-system-i386 -cdrom $(ISO_IMG) -m 64 -boot d -vga std -drive file=$(DISK_IMG),if=ide,format=raw

# Torna o disco bootável com GRUB (requer sudo e pacotes: grub-pc-bin, parted, dosfstools/e2fsprogs)
# Cria duas partições: sda1=ext2 (BOOT, ~32MiB) + sda2=resto (NoirFS cifrado)
disk-bootable: $(NOIROS_ELF) disk-img
	@echo "[root] Particionando $(DISK_IMG) em BOOT(ext2)+DATA(NoirFS)..."
	sudo parted -s $(DISK_IMG) mklabel msdos || true
	sudo parted -s $(DISK_IMG) unit MiB mkpart primary ext2 1 33
	sudo parted -s $(DISK_IMG) set 1 boot on
	sudo parted -s $(DISK_IMG) unit MiB mkpart primary 33 100%
	@echo "[root] Anexando loop device (com partscan)..."
	@LOOP=$$(sudo losetup --find --show --partscan $(DISK_IMG)); \
	 echo "Loop = $$LOOP"; \
	 echo "[root] Formatando $$LOOPp1 como ext2..."; \
	 sudo mkfs.ext2 -F $$LOOPp1; \
	 sudo mkdir -p build/bootfs; \
	 sudo mount $$LOOPp1 build/bootfs; \
	 sudo mkdir -p build/bootfs/boot/grub; \
	 sudo cp $(NOIROS_ELF) build/bootfs/boot/noiros.bin; \
	echo 'set timeout=3' | sudo tee build/bootfs/boot/grub/grub.cfg >/dev/null; \
	 echo 'set default=0' | sudo tee -a build/bootfs/boot/grub/grub.cfg >/dev/null; \
	 echo 'menuentry "NoirOS" { multiboot /boot/noiros.bin; boot }' | sudo tee -a build/bootfs/boot/grub/grub.cfg >/dev/null; \
	 echo "[root] Instalando GRUB em $$LOOP (i386-pc)..."; \
	 sudo grub-install --target=i386-pc --boot-directory=build/bootfs/boot $$LOOP; \
	 sync; \
	 sudo umount build/bootfs; \
	 sudo losetup -d $$LOOP; \
	 echo "Disco $(DISK_IMG) pronto para boot via GRUB.";

# Roda QEMU bootando pelo disco (GRUB -> NoirOS)
run-disk-boot: disk-bootable
	@echo "Iniciando QEMU (boot pelo disco via GRUB)..."
	qemu-system-i386 -m 64 -drive file=$(DISK_IMG),if=ide,format=raw -boot c -vga std

# Instala GRUB em um dispositivo existente (já particionado com sda1=ext2 de BOOT)
# Uso: sudo make install-grub-device DEV=/dev/loopX BOOTDEV=/dev/loopXp1
install-grub-device:
	@if [ -z "$(DEV)" ] || [ -z "$(BOOTDEV)" ]; then \
	  echo "Uso: sudo make install-grub-device DEV=/dev/loopX BOOTDEV=/dev/loopXp1"; exit 1; \
	fi
	@echo "[root] Montando $(BOOTDEV) em build/bootfs..."
	sudo mkdir -p build/bootfs
	sudo mount $(BOOTDEV) build/bootfs
	sudo mkdir -p build/bootfs/boot/grub
	sudo cp $(NOIROS_ELF) build/bootfs/boot/noiros.bin
	echo 'set timeout=3' | sudo tee build/bootfs/boot/grub/grub.cfg >/dev/null
	echo 'set default=0' | sudo tee -a build/bootfs/boot/grub/grub.cfg >/dev/null
	echo 'menuentry "NoirOS" { multiboot /boot/noiros.bin; boot }' | sudo tee -a build/bootfs/boot/grub/grub.cfg >/dev/null
	@echo "[root] Instalando GRUB em $(DEV) (i386-pc)..."
	sudo grub-install --target=i386-pc --boot-directory=build/bootfs/boot $(DEV)
	sync
	sudo umount build/bootfs
	@echo "GRUB instalado em $(DEV)."

# --- Host-side unit tests (gcc) ---
HOST_CC     ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -DUNIT_TEST
TEST_BIN    := $(BUILD)/tests/unit_tests
TEST_SRCS   := tests/test_runner.c tests/test_block_wrappers.c tests/test_partition.c tests/stub_kmem.c \
               src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c

test: $(TEST_BIN)
	@echo "Executando testes unitarios de host..."
	$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) | $(BUILD)
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -rf $(BUILD)
.PHONY: all run run-disk run-installer-iso iso clean test
