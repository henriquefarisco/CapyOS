# Diretorios basicos
BUILD     = build
BUILD_GEN = $(BUILD)/generated
SRC_DIR   = src
BEARSSL_DIR = third_party/bearssl
BEARSSL_SRCS := $(filter-out $(BEARSSL_DIR)/src/rand/sysrng.c,$(shell find $(BEARSSL_DIR)/src -type f -name '*.c' | sort))
BEARSSL_OBJS := $(patsubst $(BEARSSL_DIR)/%.c,$(BUILD)/x86_64/third_party/bearssl/%.o,$(BEARSSL_SRCS))

# Trilhas legadas BIOS/x86_32 removidas: build/release oficiais sao UEFI/x86_64.
CROSS64 ?= x86_64-elf
TOOLCHAIN64 ?= host

# Toolchain 64-bit
# Caminho oficial suportado em WSL/Linux host: make all64 iso-uefi
# No WSL, alguns builds do x86_64-linux-gnu-ld.gold abortam no link do kernel.
# O host toolchain (x86_64-linux-gnu-*) fica como padrao; use TOOLCHAIN64=elf
# apenas quando quiser forcar a toolchain cruzada.
ifeq ($(TOOLCHAIN64),elf)
  ifeq ($(shell which $(CROSS64)-gcc 2>/dev/null),)
    $(warning Using x86_64-linux-gnu fallback toolchain; release reproducibility still requires x86_64-elf-*)
    CC64      := x86_64-linux-gnu-gcc
    LD64      := x86_64-linux-gnu-ld
    OBJCOPY64 := x86_64-linux-gnu-objcopy
    STACKPROTECT64 := -fno-stack-protector
    $(warning Fallback toolchain disables kernel stack protector; x86_64-linux-gnu emits TLS canary reads via %fs:0x28 in freestanding code)
  else
    CC64      := $(CROSS64)-gcc
    ifeq ($(origin LD64), undefined)
      ifneq ($(shell which x86_64-linux-gnu-ld 2>/dev/null),)
        LD64 := x86_64-linux-gnu-ld
      else
        LD64 := $(CROSS64)-ld
      endif
    endif
    OBJCOPY64 := $(CROSS64)-objcopy
    STACKPROTECT64 := -fstack-protector-strong
    $(warning Using $(LD64) for kernel linking; set LD64=$(CROSS64)-ld to force the cross linker)
  endif
else
  CC64      := x86_64-linux-gnu-gcc
  ifeq ($(origin LD64), undefined)
    LD64 := x86_64-linux-gnu-ld
  endif
  OBJCOPY64 := x86_64-linux-gnu-objcopy
  STACKPROTECT64 := -fno-stack-protector
  $(warning Using x86_64-linux-gnu host toolchain by default; set TOOLCHAIN64=elf to force x86_64-elf-*)
  $(warning Host toolchain disables kernel stack protector; x86_64-linux-gnu emits TLS canary reads via %fs:0x28 in freestanding code)
endif
CFLAGS64  := -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=small -mno-red-zone -fno-asynchronous-unwind-tables -fno-unwind-tables -fcf-protection=none -fno-pic -fno-pie -fno-plt -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-strict-aliasing -fno-tree-vectorize $(STACKPROTECT64) -Iinclude -I$(BUILD_GEN) -I$(BEARSSL_DIR)/inc -I$(BEARSSL_DIR)/src -Ithird_party/tinf
DEPFLAGS64 := -MMD -MP
LDFLAGS64 := -nostdlib

# Toolchain EFI (gnu-efi)
EFI_CC := x86_64-linux-gnu-gcc
EFI_LD := x86_64-linux-gnu-ld
EFI_CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 -Iinclude -fno-stack-protector -fcf-protection=none -fpic -fshort-wchar -DEFI_FUNCTION_WRAPPER
EFI_LDFLAGS := -nostdlib -znocombreloc -shared -Bsymbolic -L/usr/lib -T /usr/lib/elf_x86_64_efi.lds
EFI_LIBS := /usr/lib/crt0-efi-x86_64.o -lefi -lgnuefi

# --- Host compiler (used by C host tools and unit tests) ---
HOST_CC     ?= gcc
HOST_TOOL_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itools/host/include -Ithird_party/tinf

# Ferramentas host C (substituem scripts Python).
GEN_MANIFEST_HOST := $(BUILD)/tools/gen_manifest
MK_EFIBOOT_HOST := $(BUILD)/tools/mk_efiboot_img
GEN_BOOT_CONFIG := $(BUILD)/tools/gen_boot_config
MANIFEST64 := $(BUILD)/manifest.bin
BOOT_CONFIG_BIN := $(BUILD)/boot_config.bin

BOOT_LAYOUT ?= us
BOOT_LANGUAGE ?= en
BOOT_HOSTNAME ?=
BOOT_THEME ?=
BOOT_ADMIN_USER ?=
BOOT_ADMIN_PASS ?=
BOOT_SPLASH ?=

# Artefatos 64-bit (UEFI/long mode)
CAPYOS_ELF64    = $(BUILD)/capyos64.bin
UEFI_LOADER     = $(BUILD)/boot/uefi_loader.efi
UEFI_LOADER_ELF = $(BUILD)/boot/uefi_loader.so
LINKER64_SCRIPT = $(SRC_DIR)/arch/x86_64/linker64.ld

# Build 64-bit: entry64 + kernel_main64 + drivers + core + fs + shell + security
CAPYOS64_OBJS = \
	$(BUILD)/x86_64/arch/x86_64/boot/entry64.o \
	$(BUILD)/x86_64/arch/x86_64/interrupts.o \
	$(BUILD)/x86_64/arch/x86_64/cpu/interrupts_asm.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/prelude_ports.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/keyboard_decode_probe.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/polling.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/backend_management.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/status_hyperv.o \
	$(BUILD)/x86_64/arch/x86_64/hyperv_input_gate.o \
	$(BUILD)/x86_64/arch/x86_64/hyperv_runtime_coordinator.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_main.o \
	$(BUILD)/x86_64/arch/x86_64/framebuffer_console.o \
	$(BUILD)/x86_64/arch/x86_64/boot_splash.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_io_helpers.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_services.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_runtime_ops.o \
	$(BUILD)/x86_64/arch/x86_64/native_runtime_gate.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_platform_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_shell_dispatch.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_shell_runtime.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_volume_runtime/io_key_helpers.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_volume_runtime/key_storage_probe.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_volume_runtime/mount_initialize.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_volume_runtime/filesystem_helpers.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_volume_runtime/public_mount_api.o \
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
	$(BUILD)/x86_64/lang/localization.o \
	$(BUILD)/x86_64/auth/login_runtime.o \
	$(BUILD)/x86_64/net/bootstrap/network_bootstrap_config.o \
	$(BUILD)/x86_64/net/bootstrap/network_bootstrap_diag.o \
	$(BUILD)/x86_64/net/bootstrap/network_bootstrap.o \
	$(BUILD)/x86_64/config/system_setup.o \
	$(BUILD)/x86_64/config/system_setup_wizard.o \
	$(BUILD)/x86_64/config/system_settings.o \
	$(BUILD)/x86_64/config/first_boot/logging.o \
	$(BUILD)/x86_64/config/first_boot/storage_users.o \
	$(BUILD)/x86_64/config/first_boot/program.o \
	$(BUILD)/x86_64/services/update_agent.o \
	$(BUILD)/x86_64/auth/user.o \
	$(BUILD)/x86_64/auth/user_prefs.o \
	$(BUILD)/x86_64/kernel/log/klog.o \
	$(BUILD)/x86_64/kernel/log/klog_persist.o \
	$(BUILD)/x86_64/services/service_boot_policy.o \
	$(BUILD)/x86_64/services/service_manager.o \
	$(BUILD)/x86_64/auth/session.o \
	$(BUILD)/x86_64/auth/user_home.o \
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
	$(BUILD)/x86_64/drivers/net/virtio_net.o \
	$(BUILD)/x86_64/drivers/net/rtl8139.o \
	$(BUILD)/x86_64/drivers/net/vmxnet3.o \
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
	$(BUILD)/x86_64/net/services/dns.o \
	$(BUILD)/x86_64/net/hyperv/hyperv_runtime.o \
	$(BUILD)/x86_64/net/hyperv/hyperv_runtime_gate.o \
	$(BUILD)/x86_64/net/hyperv/hyperv_runtime_policy.o \
	$(BUILD)/x86_64/net/hyperv/hyperv_platform_diag.o \
	$(BUILD)/x86_64/net/protocols/stack_arp.o \
	$(BUILD)/x86_64/net/core/stack_driver.o \
	$(BUILD)/x86_64/net/protocols/stack_icmp.o \
	$(BUILD)/x86_64/net/protocols/stack_ipv4.o \
	$(BUILD)/x86_64/net/core/stack_services.o \
	$(BUILD)/x86_64/net/core/stack_selftest.o \
	$(BUILD)/x86_64/net/core/stack_utils.o \
	$(BUILD)/x86_64/net/core/stack.o \
	$(BUILD)/x86_64/fs/cache/buffer_cache.o \
	$(BUILD)/x86_64/fs/storage/block_device.o \
	$(BUILD)/x86_64/fs/storage/offset_wrapper.o \
	$(BUILD)/x86_64/fs/storage/chunk_wrapper.o \
	$(BUILD)/x86_64/fs/storage/partition.o \
	$(BUILD)/x86_64/fs/capyfs/runtime/prelude_ops.o \
	$(BUILD)/x86_64/fs/capyfs/runtime/file_io.o \
	$(BUILD)/x86_64/fs/capyfs/runtime/inode_block_alloc.o \
	$(BUILD)/x86_64/fs/capyfs/runtime/directory_entries.o \
	$(BUILD)/x86_64/fs/capyfs/runtime/namespace_ops.o \
	$(BUILD)/x86_64/fs/capyfs/runtime/format_mount.o \
	$(BUILD)/x86_64/fs/capyfs/capyfs_check.o \
	$(BUILD)/x86_64/fs/vfs/vfs.o \
	$(BUILD)/x86_64/security/crypt.o \
	$(BUILD)/x86_64/security/csprng.o \
	$(BUILD)/x86_64/security/tls.o \
	$(BUILD)/x86_64/security/tls_trust_anchors.o \
	$(BEARSSL_OBJS) \
	$(BUILD)/x86_64/util/stack_protector.o \
	$(BUILD)/x86_64/util/kstring.o \
	$(BUILD)/x86_64/shell/core/shell_main/context_commands.o \
	$(BUILD)/x86_64/shell/core/shell_main/string_path_helpers.o \
	$(BUILD)/x86_64/shell/core/shell_main/output_files.o \
	$(BUILD)/x86_64/shell/core/shell_main/diagnostics.o \
	$(BUILD)/x86_64/shell/core/shell_main/run_loop.o \
	$(BUILD)/x86_64/shell/commands/help.o \
	$(BUILD)/x86_64/shell/commands/session.o \
	$(BUILD)/x86_64/shell/commands/system_info/basic_print_commands.o \
	$(BUILD)/x86_64/shell/commands/system_info/service_job_status.o \
	$(BUILD)/x86_64/shell/commands/system_info/update_status.o \
	$(BUILD)/x86_64/shell/commands/system_info/performance_commands.o \
	$(BUILD)/x86_64/shell/commands/system_info/recovery_overview.o \
	$(BUILD)/x86_64/shell/commands/system_info/recovery_storage.o \
	$(BUILD)/x86_64/shell/commands/system_info/recovery_network_registry.o \
	$(BUILD)/x86_64/shell/commands/system_control/service_helpers.o \
	$(BUILD)/x86_64/shell/commands/system_control/config_commands.o \
	$(BUILD)/x86_64/shell/commands/system_control/jobs_updates.o \
	$(BUILD)/x86_64/shell/commands/system_control/service_target_resume.o \
	$(BUILD)/x86_64/shell/commands/system_control/recovery_login_verify.o \
	$(BUILD)/x86_64/shell/commands/system_control/recovery_storage.o \
	$(BUILD)/x86_64/shell/commands/system_control/power_runtime_registry.o \
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
	$(BUILD)/x86_64/arch/x86_64/cpu/context_switch.o \
	$(BUILD)/x86_64/arch/x86_64/syscall/syscall_entry.o \
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
	$(BUILD)/x86_64/net/services/socket.o \
	$(BUILD)/x86_64/net/protocols/tcp.o \
	$(BUILD)/x86_64/net/services/dns_cache.o \
	$(BUILD)/x86_64/net/services/http_encoding.o \
	$(BUILD)/x86_64/net/services/http/prelude_headers_encoding.o \
	$(BUILD)/x86_64/net/services/http/url_request_builder.o \
	$(BUILD)/x86_64/net/services/http/transport.o \
	$(BUILD)/x86_64/net/services/http/request_response.o \
	$(BUILD)/x86_64/net/services/http/redirect_download.o \
	$(BUILD)/x86_64/third_party/tinf/tinflate.o \
	$(BUILD)/x86_64/third_party/tinf/tinfgzip.o \
	$(BUILD)/x86_64/third_party/tinf/tinfzlib.o \
	$(BUILD)/x86_64/third_party/tinf/adler32.o \
	$(BUILD)/x86_64/third_party/tinf/crc32.o \
	$(BUILD)/x86_64/security/ed25519.o \
	$(BUILD)/x86_64/boot/boot_slot.o \
	$(BUILD)/x86_64/services/package_manager.o \
	$(BUILD)/x86_64/drivers/input/mouse.o \
	$(BUILD)/x86_64/gui/core/font8x8_data.o \
	$(BUILD)/x86_64/gui/core/event.o \
	$(BUILD)/x86_64/gui/core/font.o \
	$(BUILD)/x86_64/gui/core/compositor.o \
	$(BUILD)/x86_64/gui/widgets/widget.o \
	$(BUILD)/x86_64/gui/terminal/terminal.o \
	$(BUILD)/x86_64/lang/capylang.o \
	$(BUILD)/x86_64/fs/capyfs/capyfs_journal_integration.o \
	$(BUILD)/x86_64/boot/boot_metrics.o \
	$(BUILD)/x86_64/arch/x86_64/smp.o \
	$(BUILD)/x86_64/auth/auth_policy.o \
	$(BUILD)/x86_64/kernel/pipe.o \
	$(BUILD)/x86_64/drivers/usb/usb_core.o \
	$(BUILD)/x86_64/drivers/usb/usb_hid.o \
	$(BUILD)/x86_64/drivers/gpu/gpu_core.o \
	$(BUILD)/x86_64/drivers/rtc/rtc.o \
	$(BUILD)/x86_64/drivers/serial/com1.o \
	$(BUILD)/x86_64/gui/desktop/taskbar.o \
	$(BUILD)/x86_64/security/sha512.o \
	$(BUILD)/x86_64/gui/desktop/desktop.o \
	$(BUILD)/x86_64/gui/desktop/desktop_runtime.o \
	$(BUILD)/x86_64/apps/calculator.o \
	$(BUILD)/x86_64/apps/file_manager.o \
	$(BUILD)/x86_64/apps/text_editor.o \
	$(BUILD)/x86_64/apps/task_manager.o \
	$(BUILD)/x86_64/apps/html_viewer/common.o \
	$(BUILD)/x86_64/apps/html_viewer/navigation_state.o \
	$(BUILD)/x86_64/apps/html_viewer/response_classification.o \
	$(BUILD)/x86_64/apps/html_viewer/text_url_helpers.o \
	$(BUILD)/x86_64/apps/html_viewer/async_runtime.o \
	$(BUILD)/x86_64/apps/html_viewer/ui_runtime.o \
	$(BUILD)/x86_64/apps/html_viewer/ui_input.o \
	$(BUILD)/x86_64/apps/html_viewer/ui_mouse.o \
	$(BUILD)/x86_64/apps/html_viewer/forms_and_response.o \
	$(BUILD)/x86_64/apps/html_viewer/html_tree_helpers.o \
	$(BUILD)/x86_64/apps/html_viewer/ui_shell.o \
	$(BUILD)/x86_64/apps/html_viewer/render_primitives.o \
	$(BUILD)/x86_64/apps/html_viewer/render_tree.o \
	$(BUILD)/x86_64/apps/html_viewer/html_parser.o \
	$(BUILD)/x86_64/apps/html_viewer/app_entry_async.o \
	$(BUILD)/x86_64/apps/html_viewer/resource_loading.o \
	$(BUILD)/x86_64/apps/html_viewer/public_api.o \
	$(BUILD)/x86_64/apps/css_parser/common.o \
	$(BUILD)/x86_64/apps/css_parser/parse.o \
	$(BUILD)/x86_64/apps/css_parser/apply.o \
	$(BUILD)/x86_64/apps/settings.o \
	$(BUILD)/x86_64/shell/commands/extended.o \
	$(BUILD)/x86_64/gui/window/window_manager.o \
	$(BUILD)/x86_64/gui/window/notification.o \
	$(BUILD)/x86_64/gui/core/bmp_loader.o \
	$(BUILD)/x86_64/gui/core/png_loader.o \
	$(BUILD)/x86_64/gui/core/jpeg_loader.o
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

$(BUILD)/x86_64/third_party/bearssl/%.o: $(BEARSSL_DIR)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) $(DEPFLAGS64) -c $< -o $@

$(BUILD)/x86_64/third_party/tinf/%.o: third_party/tinf/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) $(DEPFLAGS64) -c $< -o $@

$(CAPYOS_ELF64): $(CAPYOS64_OBJS) $(SRC_DIR)/arch/x86_64/linker64.ld | $(BUILD)
	$(LD64) -T $(LINKER64_SCRIPT) $(LDFLAGS64) -o $@ $(CAPYOS64_OBJS)

.PHONY: prepare-x64-toolchain
prepare-x64-toolchain: | $(BUILD)
	@mkdir -p $(BUILD)/x86_64
	@if [ ! -f "$(BUILD)/x86_64/.toolchain" ] || [ "$$(cat "$(BUILD)/x86_64/.toolchain")" != "$(TOOLCHAIN64)" ]; then \
		echo "[build] Switching x64 toolchain to $(TOOLCHAIN64); cleaning x64 artifacts."; \
		rm -rf "$(BUILD)/x86_64" "$(CAPYOS_ELF64)"; \
		mkdir -p "$(BUILD)/x86_64"; \
		printf '%s\n' "$(TOOLCHAIN64)" > "$(BUILD)/x86_64/.toolchain"; \
	fi

.PHONY: all64
ifeq ($(X64_TOOLCHAIN_PREPARED),1)
all64: $(CAPYOS_ELF64)
else
all64: prepare-x64-toolchain
	$(MAKE) $(CAPYOS_ELF64) TOOLCHAIN64=$(TOOLCHAIN64) X64_TOOLCHAIN_PREPARED=1
endif

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
# Official WSL/Linux host path: make all64 iso-uefi
iso-uefi: $(UEFI_LOADER) $(CAPYOS_ELF64) $(MANIFEST64) $(BOOT_CONFIG_BIN) $(MK_EFIBOOT_HOST)
	mkdir -p $(EFI_BOOT)
	cp $(UEFI_LOADER) $(BOOTX64)
	mkdir -p $(ISO_DIR_EFI)/boot
	cp $(CAPYOS_ELF64) $(ISO_DIR_EFI)/boot/capyos64.bin
	cp $(MANIFEST64) $(ISO_DIR_EFI)/boot/manifest.bin
	cp $(BOOT_CONFIG_BIN) $(ISO_DIR_EFI)/boot/capycfg.bin
	$(MK_EFIBOOT_HOST) --out $(EFI_BOOT)/efiboot.img --size 8M --spc 2 --label EFIBOOT --bootx64 $(UEFI_LOADER) --kernel $(CAPYOS_ELF64) --manifest $(MANIFEST64) --bootcfg $(BOOT_CONFIG_BIN)
	@ISO_OUT="$(ISO_IMG_EFI)"; if [ -e "$$ISO_OUT" ] && ! rm -f "$$ISO_OUT" 2>/dev/null; then ISO_OUT_ALT="$$ISO_OUT.$$(date +%s).iso"; echo "[warn] Nao foi possivel sobrescrever $$ISO_OUT (provavel lock/perm). Gerando $$ISO_OUT_ALT"; ISO_OUT="$$ISO_OUT_ALT"; fi; xorriso -as mkisofs -R -f -e EFI/BOOT/efiboot.img -no-emul-boot -o "$$ISO_OUT" $(ISO_DIR_EFI); printf '%s\n' "$$ISO_OUT" > $(BUILD)/CapyOS-Installer-UEFI.last-built.txt; echo "[ok] ISO UEFI gerada em $$ISO_OUT"; echo "[ok] Ultima ISO registrada em $(BUILD)/CapyOS-Installer-UEFI.last-built.txt"

# Manifest 64-bit (para BOOT partition GPT) - LBA relativo default = 1 (logo apÃƒÆ’Ã‚Â³s o manifest)
$(MANIFEST64): $(CAPYOS_ELF64) | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $(GEN_MANIFEST_HOST) tools/host/src/gen_manifest.c
	$(GEN_MANIFEST_HOST) --kernel $(CAPYOS_ELF64) --out $(MANIFEST64) --kernel-lba 1

.PHONY: manifest64
manifest64: $(MANIFEST64)

.PHONY: release-checksums
release-checksums:
	@ISO_OUT="$$(cat $(BUILD)/CapyOS-Installer-UEFI.last-built.txt)"; \
	if [ ! -f "$$ISO_OUT" ]; then echo "[err] ISO nao encontrada: $$ISO_OUT"; exit 1; fi; \
	sha256sum "$(CAPYOS_ELF64)" "$(UEFI_LOADER)" "$(MANIFEST64)" "$(BOOT_CONFIG_BIN)" "$$ISO_OUT" > "$(RELEASE_SHA256)"; \
	echo "[ok] Checksums de release gravados em $(RELEASE_SHA256)"

.PHONY: verify-release-checksums
verify-release-checksums: release-checksums
	sha256sum -c "$(RELEASE_SHA256)"

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

$(GEN_MANIFEST_HOST): tools/host/src/gen_manifest.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $@ $<

$(MK_EFIBOOT_HOST): tools/host/src/mk_efiboot_img.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $@ $<

# Ferramentas host legadas (mantidas para compatibilidade de testes).
GRUB_CFG_GEN := $(BUILD)/tools/gen_grub_cfg
GRUB_CFG_ISO := $(BUILD)/grub.iso.cfg
GRUB_CFG_DISK := $(BUILD)/grub.disk.cfg

ISO_DIR_EFI ?= build/iso-uefi-root
ISO_IMG_EFI ?= build/CapyOS-Installer-UEFI.iso
RELEASE_SHA256 := $(BUILD)/release-artifacts.sha256
EFI_BOOT := $(ISO_DIR_EFI)/EFI/BOOT
BOOTX64 := $(EFI_BOOT)/BOOTX64.EFI
EFI_STUB := $(BUILD)/boot/uefi_loader.efi

run run-disk run-installer-iso iso disk-img disk-bootable run-disk-boot install-grub-device \
all32 iso-bios iso-bios-legacy bios legacy mbr: legacy-disabled
# --- Host-side unit tests (gcc) ---
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itools/host/include -Ithird_party/tinf -DUNIT_TEST
TEST_BIN    := $(BUILD)/tests/unit_tests
TEST_SRCS   := tests/test_runner.c tests/test_block_wrappers.c tests/test_partition.c tests/test_keyboard_layouts.c tests/test_grub_cfg_builder.c tests/test_boot_manifest.c tests/test_boot_writer.c tests/test_gen_boot_config.c tests/test_user_home.c tests/test_html_viewer.c tests/test_http_encoding.c tests/stub_kmem.c tests/stub_scheduler.c tests/test_csprng.c tests/test_localization.c tests/test_klog.c tests/test_auth_policy.c tests/test_login_runtime.c tests/test_capyfs_check.c tests/test_service_manager.c tests/test_service_boot_policy.c tests/test_work_queue.c tests/test_update_agent.c \
               tests/stub_vga.c src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c \
               src/fs/capyfs/capyfs_check.c \
               src/boot/boot_manifest.c src/boot/boot_writer.c \
               tests/test_efi_block.c src/drivers/storage/efi_block.c \
               tests/test_net_dns.c src/net/services/dns.c \
               tests/test_net_probe.c src/drivers/net/net_probe.c src/drivers/net/netvsc.c \
               src/auth/login_runtime.c src/auth/user_home.c src/services/service_boot_policy.c \
               tests/test_hyperv_runtime.c src/net/hyperv/hyperv_runtime.c \
               tests/test_input_hyperv_gate.c src/arch/x86_64/hyperv_input_gate.c \
               tests/test_hyperv_runtime_gate.c src/net/hyperv/hyperv_runtime_gate.c \
               tests/test_hyperv_runtime_policy.c src/net/hyperv/hyperv_runtime_policy.c \
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
               src/drivers/input/keyboard/layouts/br_abnt2.c src/drivers/input/keyboard/layouts/us.c tools/host/src/grub_cfg_builder.c tools/host/src/gen_boot_config.c \
	               src/security/csprng.c src/security/crypt.c src/lang/localization.c src/kernel/log/klog.c src/auth/auth_policy.c src/services/service_manager.c src/core/work_queue.c src/services/update_agent.c src/apps/html_viewer/common.c src/apps/html_viewer/navigation_state.c src/apps/html_viewer/response_classification.c src/apps/html_viewer/text_url_helpers.c src/apps/html_viewer/async_runtime.c src/apps/html_viewer/ui_runtime.c src/apps/html_viewer/ui_input.c src/apps/html_viewer/ui_mouse.c src/apps/html_viewer/forms_and_response.c src/apps/html_viewer/html_tree_helpers.c src/apps/html_viewer/ui_shell.c src/apps/html_viewer/render_primitives.c src/apps/html_viewer/render_tree.c src/apps/html_viewer/html_parser.c src/apps/html_viewer/app_entry_async.c src/apps/html_viewer/resource_loading.c src/apps/html_viewer/public_api.c src/apps/css_parser/common.c src/apps/css_parser/parse.c src/apps/css_parser/apply.c src/net/services/http_encoding.c src/gui/core/png_loader.c src/gui/core/jpeg_loader.c third_party/tinf/tinflate.c third_party/tinf/tinfgzip.c third_party/tinf/tinfzlib.c third_party/tinf/adler32.c third_party/tinf/crc32.c \
               src/util/kstring.c \
               tests/test_pmm.c src/memory/pmm.c \
               tests/test_task.c src/kernel/task.c \
               tests/test_dns_cache.c src/net/services/dns_cache.c \
               tests/test_boot_slot.c src/boot/boot_slot.c

$(GRUB_CFG_GEN): tools/host/src/gen_grub_cfg.c tools/host/src/grub_cfg_builder.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $@ tools/host/src/gen_grub_cfg.c tools/host/src/grub_cfg_builder.c

$(GEN_BOOT_CONFIG): tools/host/src/gen_boot_config.c | $(BUILD)
	@mkdir -p $(BUILD)/tools
	$(HOST_CC) $(HOST_TOOL_CFLAGS) -o $@ tools/host/src/gen_boot_config.c

$(BOOT_CONFIG_BIN): $(GEN_BOOT_CONFIG) | $(BUILD)
	@if [ -n "$(strip $(BOOT_HOSTNAME)$(BOOT_THEME)$(BOOT_ADMIN_USER)$(BOOT_ADMIN_PASS)$(BOOT_SPLASH))" ] && { [ -z "$(BOOT_HOSTNAME)" ] || [ -z "$(BOOT_THEME)" ] || [ -z "$(BOOT_ADMIN_USER)" ] || [ -z "$(BOOT_ADMIN_PASS)" ]; }; then echo "[err] BOOT_HOSTNAME, BOOT_THEME, BOOT_ADMIN_USER and BOOT_ADMIN_PASS must all be set when preseeding installer setup"; exit 2; fi
	$(GEN_BOOT_CONFIG) $@ --layout "$(BOOT_LAYOUT)" --language "$(BOOT_LANGUAGE)" $(if $(strip $(BOOT_HOSTNAME)),--hostname "$(BOOT_HOSTNAME)",) $(if $(strip $(BOOT_THEME)),--theme "$(BOOT_THEME)",) $(if $(strip $(BOOT_ADMIN_USER)),--admin-user "$(BOOT_ADMIN_USER)",) $(if $(strip $(BOOT_ADMIN_PASS)),--admin-pass "$(BOOT_ADMIN_PASS)",) $(if $(strip $(BOOT_SPLASH)),--splash "$(BOOT_SPLASH)",)

$(GRUB_CFG_ISO): $(GRUB_CFG_GEN) | $(BUILD)
	$(GRUB_CFG_GEN) $@ iso

$(GRUB_CFG_DISK): $(GRUB_CFG_GEN) | $(BUILD)
	$(GRUB_CFG_GEN) $@ disk

test: $(TEST_BIN)
	@echo "Executando testes unitarios de host..."
	$(TEST_BIN)

.PHONY: layout-audit
layout-audit:
	@echo "Auditando organizacao de codigo..."
	python3 tools/scripts/audit_source_layout.py --strict

.PHONY: layout-audit-report
layout-audit-report:
	@echo "Gerando relatorio de organizacao de codigo..."
	python3 tools/scripts/audit_source_layout.py

.PHONY: version-audit
version-audit:
	@echo "Auditando manifesto de versao..."
	python3 tools/scripts/audit_version_manifest.py

.PHONY: boot-perf-baseline-selftest
boot-perf-baseline-selftest:
	@echo "Validando parser de baseline de boot..."
	python3 tools/scripts/check_boot_perf_baseline.py --self-test

.PHONY: boot-perf-baseline
boot-perf-baseline:
	@if [ -z "$(BOOT_PERF_LOG)" ]; then echo "Usage: make boot-perf-baseline BOOT_PERF_LOG=build/ci/smoke_x64_cli.boot2.log"; exit 2; fi
	python3 tools/scripts/check_boot_perf_baseline.py --log "$(BOOT_PERF_LOG)" $(BOOT_PERF_BASELINE_ARGS)

.PHONY: check-toolchain
check-toolchain:
	@echo "Verificando dependencias do toolchain..."
	@which $(CC64) >/dev/null 2>&1 || (echo "[err] $(CC64) nao encontrado"; exit 1)
	@which $(LD64) >/dev/null 2>&1 || (echo "[err] $(LD64) nao encontrado"; exit 1)
	@which $(OBJCOPY64) >/dev/null 2>&1 || (echo "[err] $(OBJCOPY64) nao encontrado"; exit 1)
	@which xorriso >/dev/null 2>&1 || (echo "[err] xorriso nao encontrado (necessario para iso-uefi)"; exit 1)
	@echo "[ok] Todas as dependencias encontradas."

.PHONY: release-check
release-check:
	@echo "Executando gates de release robusta..."
	$(MAKE) check-toolchain TOOLCHAIN64=elf
	$(MAKE) test
	$(MAKE) layout-audit
	$(MAKE) version-audit
	$(MAKE) boot-perf-baseline-selftest
	$(MAKE) all64 TOOLCHAIN64=elf
	$(MAKE) iso-uefi TOOLCHAIN64=elf
	$(MAKE) verify-release-checksums TOOLCHAIN64=elf
	@echo "[ok] Gates de release robusta passaram."

smoke-x64-cli: all64 iso-uefi manifest64
	@echo "Executando smoke test x64 (first-boot + login + persistencia)..."
	python3 tools/scripts/smoke_x64_cli.py $(SMOKE_X64_CLI_ARGS)

smoke-x64-boot-perf: all64 iso-uefi manifest64
	@echo "Executando smoke test x64 de performance de boot..."
	python3 tools/scripts/smoke_x64_cli.py --boot-perf-only --log build/ci/smoke_x64_boot_perf.log --disk build/ci/smoke_x64_boot_perf.img $(SMOKE_X64_BOOT_PERF_ARGS)

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
.PHONY: all all64 iso-uefi manifest64 release-checksums verify-release-checksums disk-gpt provision-vhd legacy-disabled clean test layout-audit layout-audit-report version-audit boot-perf-baseline boot-perf-baseline-selftest check-toolchain release-check smoke-x64-cli smoke-x64-boot-perf smoke-x64-cli-nvme smoke-x64-iso inspect-disk

-include $(CAPYOS64_DEPS) $(UEFI_LOADER_DEP)
