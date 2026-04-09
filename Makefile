# Diretorios basicos
BUILD     = build
BUILD_GEN = $(BUILD)/generated
SRC_DIR   = src

# Trilhas legadas BIOS/x86_32 removidas: build/release oficiais sao UEFI/x86_64.
CROSS64 ?= x86_64-elf

# Toolchain 64-bit (fallback para x86_64-linux-gnu-* se x86_64-elf-* nao existir)
ifeq ($(shell which $(CROSS64)-gcc 2>/dev/null),)
  $(warning Using x86_64-linux-gnu fallback toolchain; release reproducibility still requires x86_64-elf-*)
  CC64      := x86_64-linux-gnu-gcc
  LD64      := x86_64-linux-gnu-ld
  OBJCOPY64 := x86_64-linux-gnu-objcopy
  STACKPROTECT64 := -fno-stack-protector
  $(warning Fallback toolchain disables kernel stack protector; x86_64-linux-gnu emits TLS canary reads via %fs:0x28 in freestanding code)
else
  CC64      := $(CROSS64)-gcc
  LD64      := $(CROSS64)-ld
  OBJCOPY64 := $(CROSS64)-objcopy
  STACKPROTECT64 := -fstack-protector-strong
endif
CFLAGS64  := -ffreestanding -O2 -Wall -Wextra -m64 -fpie -mcmodel=small -mno-red-zone -fno-asynchronous-unwind-tables -fno-unwind-tables $(STACKPROTECT64) -Iinclude -I$(BUILD_GEN)
DEPFLAGS64 := -MMD -MP
LDFLAGS64 := -nostdlib

# Toolchain EFI (gnu-efi)
EFI_CC := x86_64-linux-gnu-gcc
EFI_LD := x86_64-linux-gnu-ld
EFI_CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 -Iinclude -fno-stack-protector -fpic -fshort-wchar -DEFI_FUNCTION_WRAPPER
EFI_LDFLAGS := -nostdlib -znocombreloc -shared -Bsymbolic -L/usr/lib -T /usr/lib/elf_x86_64_efi.lds
EFI_LIBS := /usr/lib/crt0-efi-x86_64.o -lefi -lgnuefi

# Artefatos 64-bit (UEFI/long mode)
CAPYOS_ELF64    = $(BUILD)/capyos64.bin
UEFI_LOADER     = $(BUILD)/boot/uefi_loader.efi
UEFI_LOADER_ELF = $(BUILD)/boot/uefi_loader.so
LINKER64_SCRIPT = $(SRC_DIR)/arch/x86_64/linker64.ld

# Build 64-bit: entry64 + kernel_main64 + drivers + core + fs + shell + security
CAPYOS64_OBJS = \
	$(BUILD)/x86_64/arch/x86_64/entry64.o \
	$(BUILD)/x86_64/arch/x86_64/interrupts.o \
	$(BUILD)/x86_64/arch/x86_64/interrupts_asm.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/hyperv_input_gate.o \
	$(BUILD)/x86_64/arch/x86_64/hyperv_runtime_coordinator.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_main.o \
	$(BUILD)/x86_64/arch/x86_64/native_runtime_gate.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_platform_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_shell_dispatch.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_shell_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_volume_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/platform_timer.o \
	$(BUILD)/x86_64/arch/x86_64/storage_runtime_gpt.o \
	$(BUILD)/x86_64/arch/x86_64/storage_runtime_hyperv.o \
	$(BUILD)/x86_64/arch/x86_64/storage_runtime_hyperv_plan.o \
	$(BUILD)/x86_64/arch/x86_64/storage_runtime_native.o \
	$(BUILD)/x86_64/arch/x86_64/storage_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/stubs.o \
	$(BUILD)/x86_64/arch/x86_64/timebase.o \
	$(BUILD)/x86_64/arch/x86_64/kmem64.o \
	$(BUILD)/x86_64/boot/boot_menu.o \
	$(BUILD)/x86_64/boot/boot_ui.o \
	$(BUILD)/x86_64/core/kcon.o \
	$(BUILD)/x86_64/core/localization.o \
	$(BUILD)/x86_64/core/login_runtime.o \
	$(BUILD)/x86_64/core/network_bootstrap_config.o \
	$(BUILD)/x86_64/core/network_bootstrap_diag.o \
	$(BUILD)/x86_64/core/network_bootstrap.o \
	$(BUILD)/x86_64/core/system_init.o \
	$(BUILD)/x86_64/core/update_agent.o \
	$(BUILD)/x86_64/core/user.o \
	$(BUILD)/x86_64/core/user_prefs.o \
	$(BUILD)/x86_64/core/klog.o \
	$(BUILD)/x86_64/core/klog_persist.o \
	$(BUILD)/x86_64/core/service_boot_policy.o \
	$(BUILD)/x86_64/core/service_manager.o \
	$(BUILD)/x86_64/core/session.o \
	$(BUILD)/x86_64/core/work_queue.o \
	$(BUILD)/x86_64/drivers/acpi/acpi.o \
	$(BUILD)/x86_64/drivers/pcie/pcie.o \
	$(BUILD)/x86_64/drivers/net/e1000.o \
	$(BUILD)/x86_64/drivers/net/netvsc_backend.o \
	$(BUILD)/x86_64/drivers/net/netvsc_runtime.o \
	$(BUILD)/x86_64/drivers/net/netvsc_session.o \
	$(BUILD)/x86_64/drivers/net/netvsc_vmbus.o \
	$(BUILD)/x86_64/drivers/net/netvsp.o \
	$(BUILD)/x86_64/drivers/net/rndis.o \
	$(BUILD)/x86_64/drivers/net/netvsc.o \
	$(BUILD)/x86_64/drivers/net/tulip.o \
	$(BUILD)/x86_64/drivers/net/net_probe.o \
	$(BUILD)/x86_64/drivers/nvme/nvme.o \
	$(BUILD)/x86_64/drivers/hyperv/hyperv_stage.o \
	$(BUILD)/x86_64/drivers/usb/xhci.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_core.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_offers.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_channel_runtime.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_keyboard_protocol.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_ring.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_transport.o \
	$(BUILD)/x86_64/drivers/hyperv/vmbus_keyboard.o \
	$(BUILD)/x86_64/drivers/input/keyboard/core.o \
	$(BUILD)/x86_64/drivers/input/keyboard/layouts/us.o \
	$(BUILD)/x86_64/drivers/input/keyboard/layouts/br_abnt2.o \
	$(BUILD)/x86_64/drivers/storage/ahci.o \
	$(BUILD)/x86_64/drivers/storage/efi_block.o \
	$(BUILD)/x86_64/drivers/storage/ramdisk.o \
	$(BUILD)/x86_64/drivers/storage/storvsc_backend.o \
	$(BUILD)/x86_64/drivers/storage/storvsc_runtime.o \
	$(BUILD)/x86_64/drivers/storage/storvsp.o \
	$(BUILD)/x86_64/drivers/storage/storvsc_session.o \
	$(BUILD)/x86_64/drivers/storage/storvsc_vmbus.o \
	$(BUILD)/x86_64/net/dns.o \
	$(BUILD)/x86_64/net/hyperv_runtime.o \
	$(BUILD)/x86_64/net/hyperv_runtime_gate.o \
	$(BUILD)/x86_64/net/hyperv_runtime_policy.o \
	$(BUILD)/x86_64/net/hyperv_platform_diag.o \
	$(BUILD)/x86_64/net/stack_arp.o \
	$(BUILD)/x86_64/net/stack_driver.o \
	$(BUILD)/x86_64/net/stack_icmp.o \
	$(BUILD)/x86_64/net/stack_ipv4.o \
	$(BUILD)/x86_64/net/stack_services.o \
	$(BUILD)/x86_64/net/stack_selftest.o \
	$(BUILD)/x86_64/net/stack_utils.o \
	$(BUILD)/x86_64/net/stack.o \
	$(BUILD)/x86_64/fs/cache/buffer_cache.o \
	$(BUILD)/x86_64/fs/storage/block_device.o \
	$(BUILD)/x86_64/fs/storage/offset_wrapper.o \
	$(BUILD)/x86_64/fs/storage/chunk_wrapper.o \
	$(BUILD)/x86_64/fs/storage/partition.o \
	$(BUILD)/x86_64/fs/capyfs/capyfs.o \
	$(BUILD)/x86_64/fs/capyfs/capyfs_check.o \
	$(BUILD)/x86_64/fs/vfs/vfs.o \
	$(BUILD)/x86_64/security/crypt.o \
	$(BUILD)/x86_64/security/csprng.o \
	$(BUILD)/x86_64/util/stack_protector.o \
	$(BUILD)/x86_64/shell/core/shell_main.o \
	$(BUILD)/x86_64/shell/commands/help.o \
	$(BUILD)/x86_64/shell/commands/session.o \
	$(BUILD)/x86_64/shell/commands/system_info.o \
	$(BUILD)/x86_64/shell/commands/system_control.o \
	$(BUILD)/x86_64/shell/commands/network_common.o \
	$(BUILD)/x86_64/shell/commands/network_diag.o \
	$(BUILD)/x86_64/shell/commands/network_config.o \
	$(BUILD)/x86_64/shell/commands/network_query.o \
	$(BUILD)/x86_64/shell/commands/network.o \
	$(BUILD)/x86_64/shell/commands/user_manage.o \
	$(BUILD)/x86_64/shell/commands/filesystem_navigation.o \
	$(BUILD)/x86_64/shell/commands/filesystem_content.o \
	$(BUILD)/x86_64/shell/commands/filesystem_manage.o \
	$(BUILD)/x86_64/shell/commands/filesystem_search.o \
	$(BUILD)/x86_64/arch/x86_64/panic.o \
	$(BUILD)/x86_64/arch/x86_64/apic.o \
	$(BUILD)/x86_64/arch/x86_64/context_switch.o \
	$(BUILD)/x86_64/arch/x86_64/syscall_entry.o \
	$(BUILD)/x86_64/kernel/task.o \
	$(BUILD)/x86_64/kernel/scheduler.o \
	$(BUILD)/x86_64/kernel/spinlock.o \
	$(BUILD)/x86_64/kernel/worker.o \
	$(BUILD)/x86_64/kernel/syscall.o \
	$(BUILD)/x86_64/kernel/elf_loader.o \
	$(BUILD)/x86_64/kernel/process.o \
	$(BUILD)/x86_64/memory/pmm.o \
	$(BUILD)/x86_64/memory/vmm.o \
	$(BUILD)/x86_64/fs/journal/journal.o \
	$(BUILD)/x86_64/fs/fsck/fsck.o \
	$(BUILD)/x86_64/net/socket.o \
	$(BUILD)/x86_64/net/tcp.o \
	$(BUILD)/x86_64/net/dns_cache.o \
	$(BUILD)/x86_64/net/http.o \
	$(BUILD)/x86_64/security/ed25519.o \
	$(BUILD)/x86_64/core/boot_slot.o \
	$(BUILD)/x86_64/core/package_manager.o \
	$(BUILD)/x86_64/drivers/input/mouse.o \
	$(BUILD)/x86_64/gui/event.o \
	$(BUILD)/x86_64/gui/font.o \
	$(BUILD)/x86_64/gui/compositor.o \
	$(BUILD)/x86_64/gui/widget.o \
	$(BUILD)/x86_64/gui/terminal.o \
	$(BUILD)/x86_64/lang/capylang.o
CAPYOS64_DEPS = $(CAPYOS64_OBJS:.o=.d)

EFI_LOADER_SRC = $(SRC_DIR)/boot/uefi_loader.c
UEFI_LOADER_DEP = $(BUILD)/boot/uefi_loader.d

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
	$(CC64) $(CFLAGS64) $(DEPFLAGS64) -c $< -o $@

$(BUILD)/x86_64/%.o: $(SRC_DIR)/%.S | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) $(DEPFLAGS64) -c $< -o $@

$(CAPYOS_ELF64): $(CAPYOS64_OBJS) $(SRC_DIR)/arch/x86_64/linker64.ld | $(BUILD)
	$(LD64) -T $(LINKER64_SCRIPT) $(LDFLAGS64) -o $@ $(CAPYOS64_OBJS)

.PHONY: all64
all64: $(CAPYOS_ELF64)

# UEFI loader (stub) ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â compila sÃƒÆ’Ã‚Â³ quando iso-uefi for chamado e gnu-efi estiver presente
$(UEFI_LOADER_ELF): $(EFI_LOADER_SRC) | $(BUILD) $(BUILD)/boot
	@if [ ! -f /usr/include/efi/efi.h ]; then echo \"gnu-efi headers ausentes. Instale gnu-efi.\"; exit 1; fi
	$(EFI_CC) $(EFI_CFLAGS) -MMD -MP -MF $(UEFI_LOADER_DEP) -c $(EFI_LOADER_SRC) -o $(BUILD)/boot/uefi_loader.o
	$(EFI_LD) $(EFI_LDFLAGS) -o $(UEFI_LOADER_ELF) $(BUILD)/boot/uefi_loader.o $(EFI_LIBS)

$(UEFI_LOADER): $(UEFI_LOADER_ELF) | $(BUILD) $(BUILD)/boot
	# UEFI espera PE/COFF (nÃƒÆ’Ã‚Â£o ELF). Converte o ELF gerado pelo gnu-efi em BOOTX64.EFI.
	x86_64-linux-gnu-objcopy --subsystem=efi-app \
	  -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
	  -O pei-x86-64 $(UEFI_LOADER_ELF) $(UEFI_LOADER)

.PHONY: iso-uefi
iso-uefi: $(UEFI_LOADER) $(CAPYOS_ELF64)
	mkdir -p $(EFI_BOOT)
	cp $(UEFI_LOADER) $(BOOTX64)
	# Opcional: incluir kernel64 em /boot
	mkdir -p $(ISO_DIR_EFI)/boot
	cp $(CAPYOS_ELF64) $(ISO_DIR_EFI)/boot/capyos64.bin
	# Hyper-V Gen2 requer que o El Torito UEFI aponte para uma imagem FAT (nao para o .EFI direto)
	python3 tools/scripts/mk_efiboot_img.py --out $(EFI_BOOT)/efiboot.img --size 8M --spc 2 --label EFIBOOT --bootx64 $(UEFI_LOADER) --kernel $(CAPYOS_ELF64)
	@ISO_OUT="$(ISO_IMG_EFI)"; if [ -e "$$ISO_OUT" ] && ! rm -f "$$ISO_OUT" 2>/dev/null; then ISO_OUT_ALT="$$ISO_OUT.$$(date +%s).iso"; echo "[warn] Nao foi possivel sobrescrever $$ISO_OUT (provavel lock/perm). Gerando $$ISO_OUT_ALT"; ISO_OUT="$$ISO_OUT_ALT"; fi; xorriso -as mkisofs -R -f -e EFI/BOOT/efiboot.img -no-emul-boot -o "$$ISO_OUT" $(ISO_DIR_EFI); echo "[ok] ISO UEFI gerada em $$ISO_OUT"

# Manifest 64-bit (para BOOT partition GPT) - LBA relativo default = 1 (logo apÃƒÆ’Ã‚Â³s o manifest)
MANIFEST64 := $(BUILD)/manifest.bin

$(MANIFEST64): $(CAPYOS_ELF64) tools/scripts/gen_manifest.py | $(BUILD)
	python3 tools/scripts/gen_manifest.py --kernel $(CAPYOS_ELF64) --out $(MANIFEST64) --kernel-lba 1

.PHONY: manifest64
manifest64: $(MANIFEST64)

# GPT disk image helper (ESP FAT32 + BOOT raw + DATA). NÃƒÆ’Ã‚Â£o requer sudo.
DISK_GPT_IMG ?= build/disk-gpt.img
DISK_GPT_SIZE ?= 2G

.PHONY: disk-gpt
disk-gpt: $(UEFI_LOADER) $(CAPYOS_ELF64) $(MANIFEST64)
	python3 tools/scripts/provision_gpt.py --img $(DISK_GPT_IMG) --size $(DISK_GPT_SIZE) --bootx64 $(UEFI_LOADER) --kernel $(CAPYOS_ELF64) --manifest $(MANIFEST64) --allow-existing --confirm

# Provision an existing Hyper-V fixed VHD (or raw .img) with GPT/ESP/BOOT using the current build artifacts.
# Usage (inside WSL): make provision-vhd IMG=/mnt/c/ProgramData/Microsoft/Windows/Virtual\\ Hard\\ Disks/CapyOSGenII.vhd
.PHONY: provision-vhd
provision-vhd: $(UEFI_LOADER) $(CAPYOS_ELF64)
	@if [ -z "$(IMG)" ]; then echo "Usage: make provision-vhd IMG=/mnt/c/ProgramData/Microsoft/Windows/Virtual\\ Hard\\ Disks/CapyOSGenII.vhd"; exit 2; fi
	python3 tools/scripts/provision_gpt.py --img "$(IMG)" --bootx64 $(UEFI_LOADER) --kernel $(CAPYOS_ELF64) --auto-manifest --allow-existing --confirm

LEGACY_DISABLED_MSG = "[legacy] Caminho BIOS/x86_32 foi removido. Use apenas UEFI/x86_64."

.PHONY: legacy-disabled
legacy-disabled:
	@echo $(LEGACY_DISABLED_MSG)
	@echo "[legacy] Alvos suportados: all64, iso-uefi, disk-gpt, provision-vhd, inspect-disk, smoke-x64-cli, smoke-x64-iso, test"
	@exit 2

# Ferramentas host para testes auxiliares (mantidas para compatibilidade de testes).
GRUB_CFG_GEN := $(BUILD)/tools/gen_grub_cfg
GRUB_CFG_ISO := $(BUILD)/grub.iso.cfg
GRUB_CFG_DISK := $(BUILD)/grub.disk.cfg
GEN_BOOT_CONFIG := $(BUILD)/tools/gen_boot_config
BOOT_CONFIG_BIN := $(BUILD)/boot_config.bin

ISO_DIR_EFI ?= build/iso-uefi-root
ISO_IMG_EFI ?= build/CapyOS-Installer-UEFI.iso
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
TEST_SRCS   := tests/test_runner.c tests/test_block_wrappers.c tests/test_partition.c tests/test_keyboard_layouts.c tests/test_grub_cfg_builder.c tests/test_boot_manifest.c tests/test_boot_writer.c tests/stub_kmem.c tests/test_csprng.c tests/test_localization.c tests/test_klog.c tests/test_login_runtime.c tests/test_capyfs_check.c tests/test_service_manager.c tests/test_service_boot_policy.c tests/test_work_queue.c tests/test_update_agent.c \
               tests/stub_vga.c src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c \
               src/fs/capyfs/capyfs_check.c \
               src/boot/boot_manifest.c src/boot/boot_writer.c \
               tests/test_efi_block.c src/drivers/storage/efi_block.c \
               tests/test_net_dns.c src/net/dns.c \
               tests/test_net_probe.c src/drivers/net/net_probe.c src/drivers/net/netvsc.c \
               src/core/login_runtime.c src/core/service_boot_policy.c \
               tests/test_hyperv_runtime.c src/net/hyperv_runtime.c \
               tests/test_input_hyperv_gate.c src/arch/x86_64/hyperv_input_gate.c \
               tests/test_hyperv_runtime_gate.c src/net/hyperv_runtime_gate.c \
               tests/test_hyperv_runtime_policy.c src/net/hyperv_runtime_policy.c \
               tests/test_native_runtime_gate.c src/arch/x86_64/native_runtime_gate.c \
               tests/test_netvsc_backend.c src/drivers/net/netvsc_backend.c \
               tests/test_netvsc_runtime.c src/drivers/net/netvsc_runtime.c \
               tests/test_netvsc_session.c src/drivers/net/netvsc_session.c \
               tests/test_netvsp.c src/drivers/net/netvsp.c \
               tests/test_netvsc_control.c \
               tests/test_rndis.c src/drivers/net/rndis.c \
               tests/test_storvsp.c src/drivers/storage/storvsp.c \
               tests/test_storvsc_session.c src/drivers/storage/storvsc_session.c \
               tests/test_storvsc_backend.c src/drivers/storage/storvsc_backend.c \
               tests/test_storvsc_runtime.c src/drivers/storage/storvsc_runtime.c \
               tests/test_storage_runtime_hyperv_plan.c src/arch/x86_64/storage_runtime_hyperv_plan.c \
               tests/test_crypt_vectors.c \
               src/drivers/input/keyboard/layouts/br_abnt2.c src/drivers/input/keyboard/layouts/us.c tools/host/src/grub_cfg_builder.c \
               src/security/csprng.c src/security/crypt.c src/core/localization.c src/core/klog.c src/core/service_manager.c src/core/work_queue.c src/core/update_agent.c

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

.PHONY: check-toolchain
check-toolchain:
	python3 tools/scripts/check_deps.py

smoke-x64-cli: all64 iso-uefi manifest64
	@echo "Executando smoke test x64 (first-boot + login + persistencia)..."
	python3 tools/scripts/smoke_x64_cli.py $(SMOKE_X64_CLI_ARGS)

smoke-x64-cli-nvme: all64 iso-uefi manifest64
	@echo "Executando smoke test x64 (first-boot + login + persistencia) com NVMe..."
	python3 tools/scripts/smoke_x64_cli.py --storage-bus nvme --log build/ci/smoke_x64_cli_nvme.log --disk build/ci/smoke_x64_cli_nvme.img $(SMOKE_X64_CLI_NVME_ARGS)

smoke-x64-iso: all64 iso-uefi manifest64
	@echo "Executando smoke test da ISO oficial (instalacao + reboot + persistencia)..."
	python3 tools/scripts/smoke_x64_iso_install.py $(SMOKE_X64_ISO_ARGS)

# Host-side GPT/ESP/BOOT audit for installed disks or disk images.
# Usage:
#   make inspect-disk IMG=build/disk-gpt.img
#   make inspect-disk IMG=/mnt/c/.../CapyOSGenII.vhd
.PHONY: inspect-disk
inspect-disk:
	@if [ -z "$(IMG)" ]; then echo "Usage: make inspect-disk IMG=build/disk-gpt.img"; exit 2; fi
	python3 tools/scripts/inspect_disk.py "$(IMG)"

$(TEST_BIN): $(TEST_SRCS) | $(BUILD)
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(TEST_SRCS)

clean:
	@if [ ! -d "$(BUILD)" ]; then exit 0; fi; \
	ISO_LOCKED=0; \
	if [ -e "$(ISO_IMG_EFI)" ] && ! rm -f "$(ISO_IMG_EFI)" 2>/dev/null; then \
		ISO_LOCKED=1; \
		echo "[warn] Nao foi possivel remover $(ISO_IMG_EFI) durante clean (provavel lock no host/Hyper-V). Preservando a ISO e limpando o restante de $(BUILD)."; \
	fi; \
	if [ "$$ISO_LOCKED" -eq 0 ]; then \
		rm -rf "$(BUILD)"; \
	else \
		find "$(BUILD)" -mindepth 1 -maxdepth 1 ! -path "$(ISO_IMG_EFI)" -exec rm -rf {} +; \
		rmdir "$(BUILD)" 2>/dev/null || true; \
	fi
.PHONY: all all64 iso-uefi manifest64 disk-gpt provision-vhd legacy-disabled clean test check-toolchain smoke-x64-cli smoke-x64-cli-nvme smoke-x64-iso inspect-disk

-include $(CAPYOS64_DEPS) $(UEFI_LOADER_DEP)

