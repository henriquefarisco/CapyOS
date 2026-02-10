# Diretórios básicos
BUILD    = build
BUILD_GEN := $(BUILD)/generated
SRC_DIR  = src
BOOT_DIR = boot

# --- Toolchain (i686-elf é o oficial; fallback = gcc -m32) ---
AS      := nasm
CROSS   ?= i686-elf
CROSS64 ?= x86_64-elf
CROSS64 ?= x86_64-elf

HOST_32_OK := $(shell echo "int x;" | gcc -m32 -xc - -c -o /tmp/noiros-m32-check.$$ 2>/dev/null && echo yes || echo no)
# Se apenas metas 64-bit/UEFI/host-test forem pedidas, não obrigue toolchain 32-bit
NEED_32 := $(if $(MAKECMDGOALS),$(filter-out all64 iso-uefi manifest64 disk-gpt provision-vhd test clean,$(MAKECMDGOALS)),yes)

ifeq ($(shell which $(CROSS)-gcc 2>/dev/null),)
  # Fallback (usa compilador do host em 32-bit) – menos isolado
  ifneq ($(NEED_32),)
  ifeq ($(HOST_32_OK),no)
    $(error Toolchain 32-bit ausente. Instale i686-elf-gcc ou habilite gcc-multilib (pacote gcc-multilib).)
  endif
  endif
  CC      := gcc
  LD      := ld
  OBJCOPY := objcopy
  CFLAGS  := -m32 -ffreestanding -O2 -Wall -Wextra -Wa,--noexecstack -mno-sse -mno-mmx -mno-80387 -Iinclude -I$(BUILD_GEN)
  LDFLAGS := -nostdlib -m elf_i386
else
  # Oficial (cross bare-metal)
  CC      := $(CROSS)-gcc
  LD      := $(CROSS)-ld
  OBJCOPY := $(CROSS)-objcopy
  CFLAGS  := -ffreestanding -O2 -Wall -Wextra -Wa,--noexecstack -fstack-protector-strong -mno-sse -mno-mmx -mno-80387 -Iinclude -I$(BUILD_GEN)
  LDFLAGS := -nostdlib
endif

# Toolchain 64-bit (fallback para x86_64-linux-gnu-* se x86_64-elf-* não existir)
ifeq ($(shell which $(CROSS64)-gcc 2>/dev/null),)
  CC64      := x86_64-linux-gnu-gcc
  LD64      := x86_64-linux-gnu-ld
  OBJCOPY64 := x86_64-linux-gnu-objcopy
else
  CC64      := $(CROSS64)-gcc
  LD64      := $(CROSS64)-ld
  OBJCOPY64 := $(CROSS64)-objcopy
endif
CFLAGS64  := -ffreestanding -O2 -Wall -Wextra -m64 -fpie -mcmodel=small -mno-red-zone -fno-asynchronous-unwind-tables -fno-unwind-tables -Iinclude -I$(BUILD_GEN)
LDFLAGS64 := -nostdlib

# Toolchain EFI (assume gnu-efi via x86_64-linux-gnu-*)
EFI_CC := x86_64-linux-gnu-gcc
EFI_LD := x86_64-linux-gnu-ld
EFI_CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 -Iinclude -fno-stack-protector -fpic -fshort-wchar -DEFI_FUNCTION_WRAPPER
EFI_LDFLAGS := -nostdlib -znocombreloc -shared -Bsymbolic -L/usr/lib -T /usr/lib/elf_x86_64_efi.lds
EFI_LIBS := /usr/lib/crt0-efi-x86_64.o -lefi -lgnuefi

# toolchain = conjunto compilador/ligador; freestanding = sem libc/ambiente do SO; -m elf_i386 força ld a gerar ELF 32-bit; ELF = Executable and Linkable Format

# Saídas separadas: kernel do SO e kernel do instalador (NGIS)
NOIROS_ELF  = $(BUILD)/noiros.bin
NGIS_ELF    = $(BUILD)/installer.bin
BOOT_BIN    = $(BUILD)/bootloader.bin
# Artefatos 64-bit (UEFI/long mode)
NOIROS_ELF64 = $(BUILD)/noiros64.bin
NOIROS_EFI   = $(BUILD)/noiros64.efi
UEFI_LOADER  = $(BUILD)/boot/uefi_loader.efi
UEFI_LOADER_ELF = $(BUILD)/boot/uefi_loader.so

# Boot artifacts (stage1/2 + payloads)
STAGE1_BIN := $(BUILD)/boot/stage1.bin
STAGE2_BIN := $(BUILD)/boot/stage2.bin
BOOT_PAYLOAD_HDR := $(BUILD_GEN)/boot_payloads.h

LINKER_SCRIPT = $(SRC_DIR)/arch/x86/linker.ld
LINKER64_SCRIPT = $(SRC_DIR)/arch/x86_64/linker64.ld

ALL_C_SRCS   = $(shell find $(SRC_DIR) -name '*.c')
ALL_S_SRCS   = $(shell find $(SRC_DIR) -name '*.s')
ALL_ASM_SRCS = $(shell find $(SRC_DIR) -name '*.asm')

# 64-bit assembly sources (explicit for now)
ALL_S_SRCS64 = $(wildcard $(SRC_DIR)/arch/x86_64/*.S)

# Objetos comuns (assembly/asm e .s são compartilhados entre sabores)
COMMON_S_OBJS   = $(patsubst $(SRC_DIR)/%.s,$(BUILD)/%.o,$(ALL_S_SRCS))
COMMON_ASM_OBJS = $(patsubst $(SRC_DIR)/%.asm,$(BUILD)/%.o,$(ALL_ASM_SRCS))

# Objetos 64-bit (reaproveita C se agnóstico; exclui código específico de x86 32-bit)
EXCL64_C   := $(shell find $(SRC_DIR)/arch/x86 -name '*.c')
MAIN64_EXCLUDE      = $(MAIN_EXCLUDE) $(EXCL64_C)
INSTALLER64_EXCLUDE = $(INSTALLER_EXCLUDE) $(EXCL64_C)

MAIN64_C_SRCS      = $(filter-out $(MAIN64_EXCLUDE),$(ALL_C_SRCS))
INSTALLER64_C_SRCS = $(filter-out $(INSTALLER64_EXCLUDE),$(ALL_C_SRCS))

MAIN64_C_OBJS      = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/x86_64/%.o,$(MAIN64_C_SRCS))
INSTALLER64_C_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/x86_64/%.o,$(INSTALLER64_C_SRCS))

# Build 64-bit: entry64 + kernel_main64 + drivers + core + fs + shell + security
NOIROS64_OBJS = \
	$(BUILD)/x86_64/arch/x86_64/entry64.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_main.o \
	$(BUILD)/x86_64/arch/x86_64/stubs.o \
	$(BUILD)/x86_64/arch/x86_64/kmem64.o \
	$(BUILD)/x86_64/core/kcon.o \
	$(BUILD)/x86_64/core/user.o \
	$(BUILD)/x86_64/core/session.o \
	$(BUILD)/x86_64/drivers/pcie/pcie.o \
	$(BUILD)/x86_64/drivers/net/e1000.o \
	$(BUILD)/x86_64/drivers/net/net_probe.o \
	$(BUILD)/x86_64/drivers/nvme/nvme.o \
	$(BUILD)/x86_64/drivers/usb/xhci.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_keyboard.o \
	$(BUILD)/x86_64/drivers/storage/ramdisk.o \
	$(BUILD)/x86_64/net/stack.o \
	$(BUILD)/x86_64/fs/cache/buffer_cache.o \
	$(BUILD)/x86_64/fs/storage/block_device.o \
	$(BUILD)/x86_64/fs/storage/offset_wrapper.o \
	$(BUILD)/x86_64/fs/storage/chunk_wrapper.o \
	$(BUILD)/x86_64/fs/storage/partition.o \
	$(BUILD)/x86_64/fs/noirfs/noirfs.o \
	$(BUILD)/x86_64/fs/vfs/vfs.o \
	$(BUILD)/x86_64/security/crypt.o \
	$(BUILD)/x86_64/security/csprng.o \
	$(BUILD)/x86_64/shell/core/shell_main.o \
	$(BUILD)/x86_64/shell/commands/help.o \
	$(BUILD)/x86_64/shell/commands/session.o \
	$(BUILD)/x86_64/shell/commands/system_info.o \
	$(BUILD)/x86_64/shell/commands/system_control.o \
	$(BUILD)/x86_64/shell/commands/network.o \
	$(BUILD)/x86_64/shell/commands/filesystem_navigation.o \
	$(BUILD)/x86_64/shell/commands/filesystem_content.o \
	$(BUILD)/x86_64/shell/commands/filesystem_manage.o \
	$(BUILD)/x86_64/shell/commands/filesystem_search.o

EFI_LOADER_SRC = $(SRC_DIR)/boot/uefi_loader.c

# Fontes C: separar por sabor removendo a unidade de entrada principal do outro
EXCL32_C   := $(shell find $(SRC_DIR)/arch/x86_64 -name '*.c') $(shell find $(SRC_DIR)/net -name '*.c') $(shell find $(SRC_DIR)/drivers/net -name '*.c') $(SRC_DIR)/boot/uefi_loader.c $(SRC_DIR)/drivers/hyperv/vmbus_keyboard.c $(SRC_DIR)/drivers/nvme/nvme.c $(SRC_DIR)/drivers/usb/xhci.c $(SRC_DIR)/drivers/pcie/pcie.c
MAIN_EXCLUDE       = $(SRC_DIR)/core/installer_main.c $(SRC_DIR)/boot/embedded_payloads.c $(EXCL32_C)
INSTALLER_EXCLUDE  = $(SRC_DIR)/core/kernel.c $(SRC_DIR)/system/kernel_main.c $(EXCL32_C)

MAIN_C_SRCS      = $(filter-out $(MAIN_EXCLUDE),$(ALL_C_SRCS))
INSTALLER_C_SRCS = $(filter-out $(INSTALLER_EXCLUDE),$(ALL_C_SRCS))

MAIN_C_OBJS      = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(MAIN_C_SRCS))
INSTALLER_C_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(INSTALLER_C_SRCS))

NOIROS_OBJS    = $(COMMON_S_OBJS) $(COMMON_ASM_OBJS) $(MAIN_C_OBJS)
NGIS_OBJS      = $(COMMON_S_OBJS) $(COMMON_ASM_OBJS) $(INSTALLER_C_OBJS)


all: $(BOOT_BIN) $(NOIROS_ELF) $(NGIS_ELF) $(BOOT_CONFIG_BIN)

# Bootloader
$(BOOT_BIN): $(BOOT_DIR)/boot.s | $(BUILD)
	$(AS) -f bin $< -o $@

$(BUILD)/boot:
	mkdir -p $(BUILD)/boot

$(STAGE1_BIN): $(BOOT_DIR)/stage1.s | $(BUILD)/boot
	$(AS) -f bin $< -o $@

$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm | $(BUILD)/boot
	$(AS) -f bin $< -o $@

$(BOOT_PAYLOAD_HDR): $(STAGE1_BIN) $(STAGE2_BIN) $(NOIROS_ELF) | $(BUILD_GEN)
	@echo "Gerando payloads de boot (stage1, stage2, kernels)..."
	@echo "/* Gerado automaticamente. Contem imagens binarias para instalacao automatica do bootloader. */" > $@
	xxd -i -n stage1_image $(STAGE1_BIN) >> $@
	xxd -i -n stage2_image $(STAGE2_BIN) >> $@
	xxd -i -n noiros_image $(NOIROS_ELF) >> $@

# Explicit dependency: embedded_payloads.c includes generated header
$(BUILD)/src/boot/embedded_payloads.o: $(BOOT_PAYLOAD_HDR)

# Create build directory if it doesn't exist
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD_GEN):
	mkdir -p $(BUILD_GEN)

$(NOIROS_ELF): $(NOIROS_OBJS) $(LINKER_SCRIPT)
	$(LD) -T $(LINKER_SCRIPT) $(LDFLAGS) -o $@ $(NOIROS_OBJS)

$(NGIS_ELF): $(NGIS_OBJS) $(LINKER_SCRIPT)
	$(LD) -T $(LINKER_SCRIPT) $(LDFLAGS) -o $@ $(NGIS_OBJS)

# Regras genéricas para objetos

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/x86_64/%.o: $(SRC_DIR)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) -c $< -o $@

$(BUILD)/boot/embedded_payloads.o: $(BOOT_PAYLOAD_HDR)

$(BUILD)/%.o: $(SRC_DIR)/%.s | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: $(SRC_DIR)/%.asm | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(BUILD)/x86_64/%.o: $(SRC_DIR)/%.S | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) -c $< -o $@

$(NOIROS_ELF64): $(NOIROS64_OBJS) $(SRC_DIR)/arch/x86_64/linker64.ld | $(BUILD)
	$(LD64) -T $(LINKER64_SCRIPT) $(LDFLAGS64) -o $@ $(NOIROS64_OBJS)

.PHONY: all64
all64: $(NOIROS_ELF64)

# UEFI loader (stub) — compila só quando iso-uefi for chamado e gnu-efi estiver presente
$(UEFI_LOADER_ELF): $(EFI_LOADER_SRC) | $(BUILD) $(BUILD)/boot
	@if [ ! -f /usr/include/efi/efi.h ]; then echo \"gnu-efi headers ausentes. Instale gnu-efi.\"; exit 1; fi
	$(EFI_CC) $(EFI_CFLAGS) -c $(EFI_LOADER_SRC) -o $(BUILD)/boot/uefi_loader.o
	$(EFI_LD) $(EFI_LDFLAGS) -o $(UEFI_LOADER_ELF) $(BUILD)/boot/uefi_loader.o $(EFI_LIBS)

$(UEFI_LOADER): $(UEFI_LOADER_ELF) | $(BUILD) $(BUILD)/boot
	# UEFI espera PE/COFF (não ELF). Converte o ELF gerado pelo gnu-efi em BOOTX64.EFI.
	x86_64-linux-gnu-objcopy --subsystem=efi-app \
	  -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
	  -O pei-x86-64 $(UEFI_LOADER_ELF) $(UEFI_LOADER)

.PHONY: iso-uefi
iso-uefi: $(UEFI_LOADER) $(NOIROS_ELF64)
	mkdir -p $(EFI_BOOT)
	cp $(UEFI_LOADER) $(BOOTX64)
	# Opcional: incluir kernel64 em /boot
	mkdir -p $(ISO_DIR_EFI)/boot
	cp $(NOIROS_ELF64) $(ISO_DIR_EFI)/boot/noiros64.bin
	# Hyper-V Gen2 requer que o El Torito UEFI aponte para uma imagem FAT (nao para o .EFI direto)
	python3 tools/scripts/mk_efiboot_img.py --out $(EFI_BOOT)/efiboot.img --size 8M --spc 2 --label EFIBOOT --bootx64 $(UEFI_LOADER) --kernel $(NOIROS_ELF64)
	@ISO_OUT="$(ISO_IMG_EFI)"; if [ -e "$$ISO_OUT" ] && ! rm -f "$$ISO_OUT" 2>/dev/null; then ISO_OUT_ALT="$$ISO_OUT.$$(date +%s).iso"; echo "[warn] Nao foi possivel sobrescrever $$ISO_OUT (provavel lock/perm). Gerando $$ISO_OUT_ALT"; ISO_OUT="$$ISO_OUT_ALT"; fi; xorriso -as mkisofs -R -f -e EFI/BOOT/efiboot.img -no-emul-boot -o "$$ISO_OUT" $(ISO_DIR_EFI); echo "[ok] ISO UEFI gerada em $$ISO_OUT"

# Manifest 64-bit (para BOOT partition GPT) - LBA relativo default = 1 (logo após o manifest)
MANIFEST64 := $(BUILD)/manifest.bin

$(MANIFEST64): $(NOIROS_ELF64) tools/scripts/gen_manifest.py | $(BUILD)
	python3 tools/scripts/gen_manifest.py --kernel $(NOIROS_ELF64) --out $(MANIFEST64) --kernel-lba 1

.PHONY: manifest64
manifest64: $(MANIFEST64)

# GPT disk image helper (ESP FAT32 + BOOT raw + DATA). Não requer sudo.
DISK_GPT_IMG ?= build/disk-gpt.img
DISK_GPT_SIZE ?= 2G

.PHONY: disk-gpt
disk-gpt: $(UEFI_LOADER) $(NOIROS_ELF64) $(MANIFEST64)
	python3 tools/scripts/provision_gpt.py --img $(DISK_GPT_IMG) --size $(DISK_GPT_SIZE) --bootx64 $(UEFI_LOADER) --kernel $(NOIROS_ELF64) --manifest $(MANIFEST64) --allow-existing --confirm

# Provision an existing Hyper-V fixed VHD (or raw .img) with GPT/ESP/BOOT using the current build artifacts.
# Usage (inside WSL): make provision-vhd IMG=/mnt/c/ProgramData/Microsoft/Windows/Virtual\\ Hard\\ Disks/NoirOSGenII.vhd
.PHONY: provision-vhd
provision-vhd: $(UEFI_LOADER) $(NOIROS_ELF64)
	@if [ -z "$(IMG)" ]; then echo "Usage: make provision-vhd IMG=/mnt/c/ProgramData/Microsoft/Windows/Virtual\\ Hard\\ Disks/NoirOSGenII.vhd"; exit 2; fi
	python3 tools/scripts/provision_gpt.py --img "$(IMG)" --bootx64 $(UEFI_LOADER) --kernel $(NOIROS_ELF64) --auto-manifest --allow-existing --confirm

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
GRUB_CFG_GEN := $(BUILD)/tools/gen_grub_cfg
GRUB_CFG_ISO := $(BUILD)/grub.iso.cfg
GRUB_CFG_DISK := $(BUILD)/grub.disk.cfg
GEN_BOOT_CONFIG := $(BUILD)/tools/gen_boot_config
BOOT_CONFIG_BIN := $(BUILD)/boot_config.bin

ISO_DIR_EFI ?= build/iso-uefi-root
ISO_IMG_EFI ?= build/NoirOS-Installer-UEFI.iso
EFI_BOOT := $(ISO_DIR_EFI)/EFI/BOOT
BOOTX64 := $(EFI_BOOT)/BOOTX64.EFI
EFI_STUB := $(BUILD)/boot/uefi_loader.efi

$(ISO_DIR): $(BUILD) $(NGIS_ELF) $(NOIROS_ELF) $(GRUB_CFG_ISO)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(NGIS_ELF) $(ISO_DIR)/boot/installer.bin
	cp $(NOIROS_ELF) $(ISO_DIR)/boot/noiros.bin
	cp $(GRUB_CFG_ISO) $(ISO_DIR)/boot/grub/grub.cfg

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
disk-bootable: $(NOIROS_ELF) disk-img $(GRUB_CFG_DISK)
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
	 sudo cp $(GRUB_CFG_DISK) build/bootfs/boot/grub/grub.cfg; \
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
install-grub-device: $(NOIROS_ELF) $(GRUB_CFG_DISK)
	@if [ -z "$(DEV)" ] || [ -z "$(BOOTDEV)" ]; then \
	  echo "Uso: sudo make install-grub-device DEV=/dev/loopX BOOTDEV=/dev/loopXp1"; exit 1; \
	fi
	@echo "[root] Montando $(BOOTDEV) em build/bootfs..."
	sudo mkdir -p build/bootfs
	sudo mount $(BOOTDEV) build/bootfs
	sudo mkdir -p build/bootfs/boot/grub
	sudo cp $(NOIROS_ELF) build/bootfs/boot/noiros.bin
	sudo cp $(GRUB_CFG_DISK) build/bootfs/boot/grub/grub.cfg
	@echo "[root] Instalando GRUB em $(DEV) (i386-pc)..."
	sudo grub-install --target=i386-pc --boot-directory=build/bootfs/boot $(DEV)
	sync
	sudo umount build/bootfs
	@echo "GRUB instalado em $(DEV)."

# --- Host-side unit tests (gcc) ---
HOST_CC     ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itools/host/include -DUNIT_TEST
HOST_TOOL_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itools/host/include
TEST_BIN    := $(BUILD)/tests/unit_tests
TEST_SRCS   := tests/test_runner.c tests/test_block_wrappers.c tests/test_partition.c tests/test_keyboard_layouts.c tests/test_grub_cfg_builder.c tests/test_boot_manifest.c tests/test_boot_writer.c tests/stub_kmem.c tests/test_csprng.c \
               tests/stub_vga.c src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c \
               src/boot/boot_manifest.c src/boot/boot_writer.c \
               src/drivers/input/keyboard/layouts/br_abnt2.c src/drivers/input/keyboard/layouts/us.c tools/host/src/grub_cfg_builder.c \
               src/security/csprng.c src/security/crypt.c

$(GRUB_CFG_GEN): tools/host/src/gen_grub_cfg.c tools/host/src/grub_cfg_builder.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $@ tools/host/src/gen_grub_cfg.c tools/host/src/grub_cfg_builder.c

$(GEN_BOOT_CONFIG): tools/host/src/gen_boot_config.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $@ tools/host/src/gen_boot_config.c

$(BOOT_CONFIG_BIN): $(GEN_BOOT_CONFIG) | $(BUILD)
	$(GEN_BOOT_CONFIG) $@ --layout us

$(GRUB_CFG_ISO): $(GRUB_CFG_GEN) | $(BUILD)
	$(GRUB_CFG_GEN) $@ iso

$(GRUB_CFG_DISK): $(GRUB_CFG_GEN) | $(BUILD)
	$(GRUB_CFG_GEN) $@ disk

test: $(TEST_BIN)
	@echo "Executando testes unitarios de host..."
	$(TEST_BIN)

smoke-x64-cli: all64 iso-uefi
	@echo "Executando smoke test x64 (login + CLI)..."
	python3 tools/scripts/smoke_x64_cli.py --iso $(ISO_IMG_EFI)

$(TEST_BIN): $(TEST_SRCS) | $(BUILD)
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -rf $(BUILD)
.PHONY: all run run-disk run-installer-iso iso clean test smoke-x64-cli
