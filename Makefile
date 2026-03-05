# Diretorios basicos
BUILD     = build
BUILD_GEN = $(BUILD)/generated
SRC_DIR   = src

# Trilhas legadas BIOS/x86_32 removidas: build/release oficiais sao UEFI/x86_64.
CROSS64 ?= x86_64-elf

# Toolchain 64-bit (fallback para x86_64-linux-gnu-* se x86_64-elf-* nao existir)
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

# Toolchain EFI (gnu-efi)
EFI_CC := x86_64-linux-gnu-gcc
EFI_LD := x86_64-linux-gnu-ld
EFI_CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 -Iinclude -fno-stack-protector -fpic -fshort-wchar -DEFI_FUNCTION_WRAPPER
EFI_LDFLAGS := -nostdlib -znocombreloc -shared -Bsymbolic -L/usr/lib -T /usr/lib/elf_x86_64_efi.lds
EFI_LIBS := /usr/lib/crt0-efi-x86_64.o -lefi -lgnuefi

# Artefatos 64-bit (UEFI/long mode)
NOIROS_ELF64    = $(BUILD)/noiros64.bin
UEFI_LOADER     = $(BUILD)/boot/uefi_loader.efi
UEFI_LOADER_ELF = $(BUILD)/boot/uefi_loader.so
LINKER64_SCRIPT = $(SRC_DIR)/arch/x86_64/linker64.ld

# Build 64-bit: entry64 + kernel_main64 + drivers + core + fs + shell + security
NOIROS64_OBJS = \
	$(BUILD)/x86_64/arch/x86_64/entry64.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_main.o \
	$(BUILD)/x86_64/arch/x86_64/stubs.o \
	$(BUILD)/x86_64/arch/x86_64/kmem64.o \
	$(BUILD)/x86_64/core/kcon.o \
	$(BUILD)/x86_64/core/login_runtime.o \
	$(BUILD)/x86_64/core/network_bootstrap.o \
	$(BUILD)/x86_64/core/system_init.o \
	$(BUILD)/x86_64/core/user.o \
	$(BUILD)/x86_64/core/session.o \
	$(BUILD)/x86_64/drivers/pcie/pcie.o \
	$(BUILD)/x86_64/drivers/net/e1000.o \
	$(BUILD)/x86_64/drivers/net/tulip.o \
	$(BUILD)/x86_64/drivers/net/net_probe.o \
	$(BUILD)/x86_64/drivers/nvme/nvme.o \
	$(BUILD)/x86_64/drivers/usb/xhci.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_keyboard.o \
	$(BUILD)/x86_64/drivers/input/keyboard/core.o \
	$(BUILD)/x86_64/drivers/input/keyboard/layouts/us.o \
	$(BUILD)/x86_64/drivers/input/keyboard/layouts/br_abnt2.o \
	$(BUILD)/x86_64/drivers/storage/efi_block.o \
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
	$(BUILD)/x86_64/shell/commands/user_manage.o \
	$(BUILD)/x86_64/shell/commands/filesystem_navigation.o \
	$(BUILD)/x86_64/shell/commands/filesystem_content.o \
	$(BUILD)/x86_64/shell/commands/filesystem_manage.o \
	$(BUILD)/x86_64/shell/commands/filesystem_search.o

EFI_LOADER_SRC = $(SRC_DIR)/boot/uefi_loader.c

all: all64

# Create build directories if they don't exist.
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD_GEN):
	mkdir -p $(BUILD_GEN)

$(BUILD)/boot:
	mkdir -p $(BUILD)/boot

$(BUILD)/x86_64/%.o: $(SRC_DIR)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) -c $< -o $@

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

LEGACY_DISABLED_MSG = "[legacy] Caminho BIOS/x86_32 foi removido. Use apenas UEFI/x86_64."

.PHONY: legacy-disabled
legacy-disabled:
	@echo $(LEGACY_DISABLED_MSG)
	@echo "[legacy] Alvos suportados: all64, iso-uefi, disk-gpt, provision-vhd, inspect-disk, smoke-x64-cli, test"
	@exit 2

# Ferramentas host para testes auxiliares (mantidas para compatibilidade de testes).
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

run run-disk run-installer-iso iso disk-img disk-bootable run-disk-boot install-grub-device \
all32 iso-bios iso-bios-legacy bios legacy mbr: legacy-disabled
# --- Host-side unit tests (gcc) ---
HOST_CC     ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itools/host/include -DUNIT_TEST
HOST_TOOL_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itools/host/include
TEST_BIN    := $(BUILD)/tests/unit_tests
TEST_SRCS   := tests/test_runner.c tests/test_block_wrappers.c tests/test_partition.c tests/test_keyboard_layouts.c tests/test_grub_cfg_builder.c tests/test_boot_manifest.c tests/test_boot_writer.c tests/stub_kmem.c tests/test_csprng.c \
               tests/stub_vga.c src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c \
               src/boot/boot_manifest.c src/boot/boot_writer.c \
               tests/test_efi_block.c src/drivers/storage/efi_block.c \
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

smoke-x64-cli: all64 iso-uefi manifest64
	@echo "Executando smoke test x64 (first-boot + login + persistencia)..."
	python3 tools/scripts/smoke_x64_cli.py

# Host-side GPT/ESP/BOOT audit for installed disks or disk images.
# Usage:
#   make inspect-disk IMG=build/disk-gpt.img
#   make inspect-disk IMG=/mnt/c/.../NoirOSGenII.vhd
.PHONY: inspect-disk
inspect-disk:
	@if [ -z "$(IMG)" ]; then echo "Usage: make inspect-disk IMG=build/disk-gpt.img"; exit 2; fi
	python3 tools/scripts/inspect_disk.py "$(IMG)"

$(TEST_BIN): $(TEST_SRCS) | $(BUILD)
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -rf $(BUILD)
.PHONY: all all64 iso-uefi manifest64 disk-gpt provision-vhd legacy-disabled clean test smoke-x64-cli inspect-disk

