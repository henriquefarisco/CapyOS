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
# EXTRA_CFLAGS64 is appended last so callers can flip build-time
# feature flags without editing CFLAGS64 in place. Examples:
#   make all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'
# Combine with `make clean` first, since the per-source .d files
# do not track preprocessor macros and would otherwise reuse
# stale objects compiled without the new flag.
CFLAGS64  += $(EXTRA_CFLAGS64)
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
	$(BUILD)/x86_64/arch/x86_64/fault_classify.o \
	$(BUILD)/x86_64/arch/x86_64/process_user_mode.o \
	$(BUILD)/x86_64/arch/x86_64/cpu/cpu_local.o \
	$(BUILD)/x86_64/arch/x86_64/cpu/user_mode_entry.o \
	$(BUILD)/x86_64/arch/x86_64/cpu/interrupts_asm.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/prelude_ports.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/keyboard_decode_probe.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/polling.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/backend_management.o \
	$(BUILD)/x86_64/arch/x86_64/input_runtime/status_hyperv.o \
	$(BUILD)/x86_64/arch/x86_64/hyperv_input_gate.o \
	$(BUILD)/x86_64/arch/x86_64/hyperv_runtime_coordinator.o \
	$(BUILD)/x86_64/arch/x86_64/kernel_main.o \
	$(BUILD)/x86_64/arch/x86_64/preemptive_boot.o \
	$(BUILD)/x86_64/arch/x86_64/preemptive_demo.o \
	$(BUILD)/x86_64/arch/x86_64/tss.o \
	$(BUILD)/x86_64/arch/x86_64/arch_sched_hooks.o \
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
	$(BUILD)/x86_64/services/update_agent_transact.o \
	$(BUILD)/x86_64/auth/user.o \
	$(BUILD)/x86_64/auth/user_prefs.o \
	$(BUILD)/x86_64/kernel/log/klog.o \
	$(BUILD)/x86_64/kernel/log/klog_persist.o \
	$(BUILD)/x86_64/services/service_boot_policy.o \
	$(BUILD)/x86_64/services/service_manager.o \
	$(BUILD)/x86_64/services/service_runner.o \
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
	$(BUILD)/x86_64/util/op_budget.o \
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
	$(BUILD)/x86_64/kernel/task_iter.o \
	$(BUILD)/x86_64/kernel/scheduler.o \
	$(BUILD)/x86_64/kernel/spinlock.o \
	$(BUILD)/x86_64/kernel/worker.o \
	$(BUILD)/x86_64/kernel/syscall.o \
	$(BUILD)/x86_64/kernel/elf_loader.o \
	$(BUILD)/x86_64/kernel/embedded_hello.o \
	$(BUILD)/x86_64/kernel/user_init.o \
	$(BUILD)/x86_64/kernel/user_task_init.o \
	$(BUILD)/x86_64/kernel/process.o \
	$(BUILD)/x86_64/kernel/process_iter.o \
	$(BUILD)/x86_64/kernel/embedded_progs.o \
	$(HELLO_BLOB_OBJ) \
	$(EXECTARGET_BLOB_OBJ) \
	$(CAPYSH_BLOB_OBJ) \
	$(CAPYBROWSER_BLOB_OBJ) \
	$(BUILD)/x86_64/memory/pmm.o \
	$(BUILD)/x86_64/memory/pmm_refcount.o \
	$(BUILD)/x86_64/memory/vmm.o \
	$(BUILD)/x86_64/memory/vmm_cow.o \
	$(BUILD)/x86_64/memory/vmm_regions.o \
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
	$(BUILD)/x86_64/auth/privilege.o \
	$(BUILD)/x86_64/kernel/pipe.o \
	$(BUILD)/x86_64/kernel/stdin_buf.o \
	$(BUILD)/x86_64/kernel/browser_engine_spawn.o \
	$(BUILD)/x86_64/kernel/browser_smoke.o \
	$(BUILD)/x86_64/apps/browser_ipc/codec.o \
	$(BUILD)/x86_64/apps/browser_chrome/watchdog.o \
	$(BUILD)/x86_64/apps/browser_chrome/chrome.o \
	$(BUILD)/x86_64/apps/browser_chrome/runtime.o \
	$(BUILD)/x86_64/drivers/usb/usb_core.o \
	$(BUILD)/x86_64/drivers/usb/usb_hid.o \
	$(BUILD)/x86_64/drivers/gpu/gpu_core.o \
	$(BUILD)/x86_64/drivers/rtc/rtc.o \
	$(BUILD)/x86_64/drivers/serial/serial_com1.o \
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
	$(BUILD)/x86_64/apps/html_viewer/navigation_budget.o \
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

EFI_LOADER_SRCS = \
	$(SRC_DIR)/boot/uefi_loader/prelude_boot_files.c \
	$(SRC_DIR)/boot/uefi_loader/kernel_loader.c \
	$(SRC_DIR)/boot/uefi_loader/kernel_discovery_streaming.c \
	$(SRC_DIR)/boot/uefi_loader/installer_disk_selection.c \
	$(SRC_DIR)/boot/uefi_loader/recovery_gpt_layout.c \
	$(SRC_DIR)/boot/uefi_loader/fat32_writer.c \
	$(SRC_DIR)/boot/uefi_loader/installer_run.c \
	$(SRC_DIR)/boot/uefi_loader/acpi_log_gop.c \
	$(SRC_DIR)/boot/uefi_loader/efi_main.c
EFI_LOADER_OBJS = $(patsubst $(SRC_DIR)/boot/uefi_loader/%.c,$(BUILD)/boot/uefi_loader/%.o,$(EFI_LOADER_SRCS))
UEFI_LOADER_DEPS = $(EFI_LOADER_OBJS:.o=.d)

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

# ── Userland (capylibc) build rules ─────────────────────────────────
# capylibc's asm sources live under userland/. We reuse the kernel
# toolchain (CC64) and a subset of CFLAGS64 because the static
# library has the same freestanding requirements as the kernel: no
# stdlib, no PIE, x86_64 SysV ABI. M4 phase 5a only assembles the
# files (proving they parse and link); phase 5b will archive them
# into libcapylibc.a and link a real user binary.
USERLAND_DIR = userland
# Workspace quirk: some sandboxed checkouts can read existing build/
# subdirectories but cannot create new ones (SMB / network mounts).
# CAPYLIBC_BUILD_DIR is overridable so the developer can redirect
# the artifacts elsewhere, e.g.
#   make capylibc CAPYLIBC_BUILD_DIR=/tmp/capyos_capylibc
CAPYLIBC_BUILD_DIR ?= $(BUILD)/userland

# User-space C flags: drop -mno-red-zone (user code can use the red
# zone; SYSCALL itself does not clobber it) and add the userland
# include path so user binaries can `#include <capylibc/capylibc.h>`.
USERLAND_CFLAGS = -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=small \
                  -fno-asynchronous-unwind-tables -fno-unwind-tables \
                  -fcf-protection=none -fno-pic -fno-pie -fno-plt \
                  -fno-omit-frame-pointer -fno-strict-aliasing \
                  -fno-stack-protector \
                  -Iinclude -Iuserland/include

$(CAPYLIBC_BUILD_DIR)/%.o: $(USERLAND_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) $(DEPFLAGS64) -c $< -o $@

# Phase 5f: EXTRA_USERLAND_CFLAGS lets a smoke target inject extra
# preprocessor flags (e.g. `-DCAPYOS_HELLO_FAULT`) without editing
# USERLAND_CFLAGS in place. Mirrors the EXTRA_CFLAGS64 contract used
# by the kernel rules. The default is empty so production builds are
# unaffected.
EXTRA_USERLAND_CFLAGS ?=

$(CAPYLIBC_BUILD_DIR)/%.o: $(USERLAND_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC64) $(USERLAND_CFLAGS) $(EXTRA_USERLAND_CFLAGS) $(DEPFLAGS64) -c $< -o $@

CAPYLIBC_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc/crt0.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc/syscall_stubs.o

.PHONY: capylibc
capylibc: $(CAPYLIBC_OBJS)
	@echo "[ok] capylibc objects assembled: $(CAPYLIBC_OBJS)"

# ── User binary: hello (M4 phase 5b) ────────────────────────────────
# The first CapyOS user binary. Statically linked at the default
# x86_64 base address (0x400000), entry symbol `_start` from crt0.
# Phase 5c (this file) wraps the linked ELF into a kernel-linkable
# `.rodata` blob via objcopy; phase 5d will call
# `process_enter_user_mode` on it during boot.
HELLO_ELF = $(CAPYLIBC_BUILD_DIR)/bin/hello/hello.elf
HELLO_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/bin/hello/main.o \
	$(CAPYLIBC_OBJS)

$(HELLO_ELF): $(HELLO_OBJS)
	@mkdir -p $(dir $@)
	$(LD64) -nostdlib -static -e _start -o $@ $(HELLO_OBJS)

.PHONY: hello-elf
hello-elf: $(HELLO_ELF)
	@echo "[ok] user binary linked: $(HELLO_ELF)"

# Wrap the static ELF into an x86_64 ELF object the kernel linker
# can consume. objcopy with `-I binary -O elf64-x86-64` generates
# the magic symbols `_binary_hello_elf_start/_end/_size`; the
# `--rename-section` step moves the bytes into `.rodata` so the
# kernel image keeps the user code in read-only memory and so the
# section attributes match the kernel's other constant data. The
# objcopy input file MUST be named `hello.elf` (lowercase, no path
# components in the symbol stem) because the symbol stem is derived
# from the input basename verbatim. `cd` into the directory before
# running objcopy so the symbol prefix stays `_binary_hello_elf_`.
HELLO_BLOB_OBJ = $(CAPYLIBC_BUILD_DIR)/bin/hello/hello_elf_blob.o

$(HELLO_BLOB_OBJ): $(HELLO_ELF)
	@mkdir -p '$(dir $@)'
	cd '$(dir $<)' && $(OBJCOPY64) -I binary -O elf64-x86-64 \
		-B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		'$(notdir $<)' '$(abspath $@)'

.PHONY: hello-blob
hello-blob: $(HELLO_BLOB_OBJ)
	@echo "[ok] hello blob ready for kernel link: $(HELLO_BLOB_OBJ)"

# M5 phase B.1/B.2: second canonical user binary that exists only
# to be the target of an exec(). It writes "[exec-ok]\n" once and
# exits. Mirrors the hello rules verbatim except for the basename:
# the objcopy input file MUST be `exectarget.elf` so the magic
# symbols come out as `_binary_exectarget_elf_start/_end/_size`.
EXECTARGET_ELF = $(CAPYLIBC_BUILD_DIR)/bin/exectarget/exectarget.elf
EXECTARGET_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/bin/exectarget/main.o \
	$(CAPYLIBC_OBJS)

$(EXECTARGET_ELF): $(EXECTARGET_OBJS)
	@mkdir -p $(dir $@)
	$(LD64) -nostdlib -static -e _start -o $@ $(EXECTARGET_OBJS)

.PHONY: exectarget-elf
exectarget-elf: $(EXECTARGET_ELF)
	@echo "[ok] user binary linked: $(EXECTARGET_ELF)"

EXECTARGET_BLOB_OBJ = $(CAPYLIBC_BUILD_DIR)/bin/exectarget/exectarget_elf_blob.o

$(EXECTARGET_BLOB_OBJ): $(EXECTARGET_ELF)
	@mkdir -p '$(dir $@)'
	cd '$(dir $<)' && $(OBJCOPY64) -I binary -O elf64-x86-64 \
		-B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		'$(notdir $<)' '$(abspath $@)'

.PHONY: exectarget-blob
exectarget-blob: $(EXECTARGET_BLOB_OBJ)
	@echo "[ok] exectarget blob ready for kernel link: $(EXECTARGET_BLOB_OBJ)"

# M5 phase E.5: third canonical user binary, the interactive shell.
# Same template as exectarget; embeds /bin/capysh.elf into the kernel
# image as a third blob discovered through embedded_progs_lookup.
CAPYSH_ELF = $(CAPYLIBC_BUILD_DIR)/bin/capysh/capysh.elf
CAPYSH_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/bin/capysh/main.o \
	$(CAPYLIBC_OBJS)

$(CAPYSH_ELF): $(CAPYSH_OBJS)
	@mkdir -p $(dir $@)
	$(LD64) -nostdlib -static -e _start -o $@ $(CAPYSH_OBJS)

.PHONY: capysh-elf
capysh-elf: $(CAPYSH_ELF)
	@echo "[ok] user binary linked: $(CAPYSH_ELF)"

CAPYSH_BLOB_OBJ = $(CAPYLIBC_BUILD_DIR)/bin/capysh/capysh_elf_blob.o

$(CAPYSH_BLOB_OBJ): $(CAPYSH_ELF)
	@mkdir -p '$(dir $@)'
	cd '$(dir $<)' && $(OBJCOPY64) -I binary -O elf64-x86-64 \
		-B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		'$(notdir $<)' '$(abspath $@)'

.PHONY: capysh-blob
capysh-blob: $(CAPYSH_BLOB_OBJ)
	@echo "[ok] capysh blob ready for kernel link: $(CAPYSH_BLOB_OBJ)"

# F3.3b: browser engine binary (ring 3). Reads request frames from
# fd 0 and writes events to fd 1 using the IPC protocol defined in
# include/apps/browser_ipc.h. Reuses src/apps/browser_ipc/codec.c
# verbatim (the codec depends only on <stdint.h> and is intentionally
# free of kernel/libc symbols).
CAPYBROWSER_IPC_OBJ = $(CAPYLIBC_BUILD_DIR)/lib/browser_ipc/codec.o

$(CAPYBROWSER_IPC_OBJ): src/apps/browser_ipc/codec.c
	@mkdir -p $(dir $@)
	$(CC64) $(USERLAND_CFLAGS) $(EXTRA_USERLAND_CFLAGS) $(DEPFLAGS64) -c $< -o $@

CAPYBROWSER_ELF = $(CAPYLIBC_BUILD_DIR)/bin/capybrowser/capybrowser.elf
CAPYBROWSER_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/bin/capybrowser/main.o \
	$(CAPYBROWSER_IPC_OBJ) \
	$(CAPYLIBC_OBJS)

$(CAPYBROWSER_ELF): $(CAPYBROWSER_OBJS)
	@mkdir -p $(dir $@)
	$(LD64) -nostdlib -static -e _start -o $@ $(CAPYBROWSER_OBJS)

.PHONY: capybrowser-elf
capybrowser-elf: $(CAPYBROWSER_ELF)
	@echo "[ok] user binary linked: $(CAPYBROWSER_ELF)"

CAPYBROWSER_BLOB_OBJ = $(CAPYLIBC_BUILD_DIR)/bin/capybrowser/capybrowser_elf_blob.o

$(CAPYBROWSER_BLOB_OBJ): $(CAPYBROWSER_ELF)
	@mkdir -p '$(dir $@)'
	cd '$(dir $<)' && $(OBJCOPY64) -I binary -O elf64-x86-64 \
		-B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		'$(notdir $<)' '$(abspath $@)'

.PHONY: capybrowser-blob
capybrowser-blob: $(CAPYBROWSER_BLOB_OBJ)
	@echo "[ok] capybrowser blob ready for kernel link: $(CAPYBROWSER_BLOB_OBJ)"

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
$(BUILD)/boot/uefi_loader/%.o: $(SRC_DIR)/boot/uefi_loader/%.c | $(BUILD) $(BUILD)/boot
	@mkdir -p $(dir $@)
	@if [ ! -f /usr/include/efi/efi.h ]; then echo \"gnu-efi headers ausentes. Instale gnu-efi.\"; exit 1; fi
	$(EFI_CC) $(EFI_CFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(UEFI_LOADER_ELF): $(EFI_LOADER_OBJS) | $(BUILD) $(BUILD)/boot
	@if [ ! -f /usr/include/efi/efi.h ]; then echo \"gnu-efi headers ausentes. Instale gnu-efi.\"; exit 1; fi
	$(EFI_LD) $(EFI_LDFLAGS) -o $(UEFI_LOADER_ELF) $(EFI_LOADER_OBJS) $(EFI_LIBS)

$(UEFI_LOADER): $(UEFI_LOADER_ELF) | $(BUILD) $(BUILD)/boot
	# UEFI espera PE/COFF (nÃƒÆ’Ã‚Â£o ELF). Converte o ELF gerado pelo gnu-efi em BOOTX64.EFI.
	x86_64-linux-gnu-objcopy --subsystem=efi-app \
	  -j .text -j .rodata -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
	  -O pei-x86-64 $(UEFI_LOADER_ELF) $(UEFI_LOADER)

.PHONY: iso-uefi
# Official WSL/Linux host path: make all64 iso-uefi
iso-uefi: $(UEFI_LOADER) $(CAPYOS_ELF64) $(MANIFEST64) $(BOOT_CONFIG_BIN) $(MK_EFIBOOT_HOST)
	mkdir -p $(EFI_BOOT)
	cp $(UEFI_LOADER) $(BOOTX64)
	mkdir -p $(ISO_DIR_EFI)/boot
	printf 'INSTALLER=1\n' > $(ISO_DIR_EFI)/CAPYOS.INI
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
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Iuserland/include -Itools/host/include -Ithird_party/tinf -DUNIT_TEST
TEST_BIN    := $(BUILD)/tests/unit_tests
TEST_SRCS   := tests/test_runner.c tests/test_block_wrappers.c tests/test_partition.c tests/test_keyboard_layouts.c tests/test_grub_cfg_builder.c tests/test_boot_manifest.c tests/test_boot_writer.c tests/test_gen_boot_config.c tests/test_user_home.c tests/test_html_viewer.c tests/test_http_encoding.c tests/stub_kmem.c tests/stub_context_switch.c src/kernel/scheduler.c tests/test_csprng.c tests/test_localization.c tests/test_klog.c tests/test_auth_policy.c tests/test_login_runtime.c tests/test_capyfs_check.c tests/test_service_manager.c tests/test_service_boot_policy.c tests/test_work_queue.c tests/test_update_agent.c tests/test_audit_events.c tests/test_journal.c tests/test_capyfs_journal_cause.c tests/test_update_transact.c \
               tests/stub_vga.c src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c \
               src/fs/capyfs/capyfs_check.c src/fs/capyfs/capyfs_journal_integration.c \
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
	               src/security/csprng.c src/security/crypt.c src/lang/localization.c src/kernel/log/klog.c src/auth/auth_policy.c src/services/service_manager.c src/core/work_queue.c src/services/update_agent.c src/services/update_agent_transact.c src/apps/html_viewer/common.c src/apps/html_viewer/navigation_state.c src/apps/html_viewer/navigation_budget.c src/apps/html_viewer/response_classification.c src/apps/html_viewer/text_url_helpers.c src/apps/html_viewer/async_runtime.c src/apps/html_viewer/ui_runtime.c src/apps/html_viewer/ui_input.c src/apps/html_viewer/ui_mouse.c src/apps/html_viewer/forms_and_response.c src/apps/html_viewer/html_tree_helpers.c src/apps/html_viewer/ui_shell.c src/apps/html_viewer/render_primitives.c src/apps/html_viewer/render_tree.c src/apps/html_viewer/html_parser.c src/apps/html_viewer/app_entry_async.c src/apps/html_viewer/resource_loading.c src/apps/html_viewer/public_api.c src/apps/css_parser/common.c src/apps/css_parser/parse.c src/apps/css_parser/apply.c src/net/services/http_encoding.c src/gui/core/png_loader.c src/gui/core/jpeg_loader.c third_party/tinf/tinflate.c third_party/tinf/tinfgzip.c third_party/tinf/tinfzlib.c third_party/tinf/adler32.c third_party/tinf/crc32.c \
               src/util/kstring.c src/fs/journal/journal.c \
               tests/test_pmm.c src/memory/pmm.c \
               tests/test_task.c src/kernel/task.c \
               tests/test_task_iter.c src/kernel/task_iter.c \
               tests/test_task_stats.c \
               tests/test_process_iter.c src/kernel/process.c src/kernel/process_iter.c tests/stub_vmm.c src/memory/vmm_regions.c \
               tests/test_process_destroy.c \
               tests/test_vmm_anon_regions.c \
               tests/test_service_runner.c src/services/service_runner.c \
               tests/test_context_switch.c tests/stub_arch_sched_hooks.c \
               tests/test_syscall_msr.c \
               tests/test_fault_classify.c src/arch/x86_64/fault_classify.c \
               tests/test_pmm_refcount.c src/memory/pmm_refcount.c \
               tests/test_vmm_cow.c src/memory/vmm_cow.c \
               tests/test_tss_layout.c src/arch/x86_64/tss.c \
               tests/test_user_task_init.c src/kernel/user_task_init.c \
               tests/test_cpu_local.c src/arch/x86_64/cpu/cpu_local.c \
               tests/test_enter_user_mode.c src/arch/x86_64/process_user_mode.c \
               tests/test_capylibc_abi.c \
               tests/test_hello_program.c \
               tests/test_user_init.c src/kernel/user_init.c \
               tests/test_embedded_progs.c src/kernel/embedded_progs.c \
               tests/test_pipe.c src/kernel/pipe.c \
               tests/test_stdin_buf.c src/kernel/stdin_buf.c \
               tests/test_dns_cache.c src/net/services/dns_cache.c \
               tests/test_boot_slot.c src/boot/boot_slot.c \
               tests/test_op_budget.c src/util/op_budget.c \
               tests/test_privilege.c src/auth/privilege.c \
               tests/test_buffer_cache_pacing.c src/fs/cache/buffer_cache.c \
               tests/test_browser_ipc.c src/apps/browser_ipc/codec.c \
               tests/test_browser_watchdog.c src/apps/browser_chrome/watchdog.c \
               tests/test_browser_chrome.c src/apps/browser_chrome/chrome.c \
               tests/test_browser_chrome_runtime.c src/apps/browser_chrome/runtime.c \
               tests/test_browser_e2e.c

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

# M4 phase 5e: end-to-end Ring 3 smoke. Rebuilds the kernel with
# `-DCAPYOS_BOOT_RUN_HELLO` so kernel_main spawns the embedded
# `hello` user binary right after syscall_init; the smoke script
# parses the debug-console log for the success markers.
#
# `make clean` is forced because the per-source .d files do not
# track preprocessor macros, so a previous build without the flag
# would otherwise reuse stale objects and the `#ifdef` block in
# kernel_main.o would never be re-emitted.
smoke-x64-hello-user:
	@echo "Executando smoke test x64 (hello user binary)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_hello_user.py $(SMOKE_X64_HELLO_USER_ARGS)

# M4 phase 5f: end-to-end smoke for phase 4's fault-kill path.
# Builds `hello` with `-DCAPYOS_HELLO_FAULT` so the user binary
# emits a "before-fault" marker and then deliberately dereferences
# NULL. The kernel must NOT panic; instead the fault classifier must
# route through the kill path and `process_exit(128+14)`.
#
# `make clean` is forced for the same reason as `smoke-x64-hello-user`:
# the per-source `.d` files do not track preprocessor macros.
smoke-x64-hello-segfault:
	@echo "Executando smoke test x64 (hello segfault / phase 4 kill path)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_FAULT'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_hello_segfault.py $(SMOKE_X64_HELLO_SEGFAULT_ARGS)

# M4 phase 8b: wiring smoke for the preemptive scheduler flip.
# Builds the kernel with `-DCAPYOS_PREEMPTIVE_SCHEDULER` so the
# `#ifdef` blocks in kernel_main.c are emitted, then verifies that
# the boot path actually flips the policy from cooperative to PRIORITY
# and marks `sched_running=1` BEFORE the existing 100Hz APIC tick
# starts firing scheduler_tick. The smoke does NOT yet demonstrate
# that two competing tasks are preempted on quantum exhaustion -
# that end-to-end demo lands in phase 8c.
#
# `make clean` is forced for the same reason as `smoke-x64-hello-user`:
# the per-source `.d` files do not track preprocessor macros.
smoke-x64-preemptive:
	@echo "Executando smoke test x64 (preemptive scheduler wiring)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_preemptive.py $(SMOKE_X64_PREEMPTIVE_ARGS)

# M4 phase 8e: two-task kernel-mode preemption demo.
# Builds the kernel with both `-DCAPYOS_PREEMPTIVE_SCHEDULER` and
# `-DCAPYOS_PREEMPTIVE_DEMO` so the boot path one-way-jumps into
# busy_a (via the phase 8e first-task trampoline) instead of
# reaching the shell. The smoke verifies BOTH busy_a and busy_b
# emit their markers multiple times to debugcon, which is the
# canonical end-to-end proof of preemptive scheduling.
smoke-x64-preemptive-demo:
	@echo "Executando smoke test x64 (preemptive two-task demo)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_PREEMPTIVE_DEMO'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_preemptive_demo.py $(SMOKE_X64_PREEMPTIVE_DEMO_ARGS)

# M4 phase 8f.3: end-to-end ring-3 preemption smoke.
# Builds the kernel with `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO`
# AND the userland `hello` binary with `-DCAPYOS_HELLO_BUSY` so the
# embedded hello loops forever emitting markers via SYS_WRITE. The
# smoke proves three things in one boot:
#
#   1. The TSS scaffolding from phase 8f.1 actually works: the first
#      APIC tick fired from ring 3 does NOT triple-fault the kernel
#      (no TSS == #DF on every ring 3 -> ring 0 IRQ entry).
#   2. The per-task RSP0 swap from phase 8f.2 keeps subsequent ticks
#      landing on the right kernel stack.
#   3. iretq correctly returns to ring 3 after every tick is
#      serviced, so hello_busy continues looping and the marker
#      reappears in the debugcon log.
#
# `make clean` is forced for the same reason as the other preemptive
# smokes: per-source .d files do not track preprocessor macros.
smoke-x64-preemptive-user:
	@echo "Executando smoke test x64 (preemptive user-mode / hello_busy)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_BUSY'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_preemptive_user.py $(SMOKE_X64_PREEMPTIVE_USER_ARGS)

# M4 phase 8f.5: end-to-end TWO-task ring-3 preemption smoke.
# Builds the kernel with `CAPYOS_PREEMPTIVE_SCHEDULER +
# CAPYOS_BOOT_RUN_HELLO + CAPYOS_BOOT_RUN_TWO_BUSY` and userland
# with `CAPYOS_HELLO_BUSY`. The kernel spawns TWO copies of the
# embedded hello, arms the second via the synthetic IRET frame
# builder (8f.4), iretqs into the first (rank=0), and lets the
# scheduler dispatch the second on quantum exhaustion (rank=1).
# The smoke asserts BOTH `[busyU0]` AND `[busyU1]` markers appear
# at least N times in the debugcon log -- the canonical end-to-end
# proof that ring-3 tasks alternate under tick-driven preemption.
smoke-x64-preemptive-user-2task:
	@echo "Executando smoke test x64 (preemptive user-mode 2 tasks)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO -DCAPYOS_BOOT_RUN_TWO_BUSY' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_BUSY'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_preemptive_user_2task.py $(SMOKE_X64_PREEMPTIVE_USER_2TASK_ARGS)

# M5 phase A.7: SYS_FORK + CoW end-to-end smoke.
# Builds the kernel with `-DCAPYOS_PREEMPTIVE_SCHEDULER
# -DCAPYOS_BOOT_RUN_HELLO` and userland `hello` with
# `-DCAPYOS_HELLO_FORK`. The hello binary calls `capy_fork()` once
# and both branches loop forever emitting distinct markers via
# SYS_WRITE. The smoke asserts BOTH `[fork-parent]` AND
# `[fork-child]` markers appear at least N times in the debugcon
# log -- the canonical end-to-end proof that:
#
#   1. SYS_FORK returns +PID in the parent and 0 in the child.
#   2. The kernel arms the child task via the synthetic IRET frame
#      builder so it iretqs into ring 3 at the parent's saved RIP/RSP.
#   3. CoW page faults from BOTH branches are handled correctly
#      (each writes to its own stack page after fork).
#   4. The scheduler keeps both tasks runnable under preemption.
#
# `make clean` is forced for the same reason as the preemptive
# smokes: per-source .d files do not track preprocessor macros.
smoke-x64-fork-cow:
	@echo "Executando smoke test x64 (SYS_FORK + CoW)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_FORK'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_fork_cow.py $(SMOKE_X64_FORK_COW_ARGS)

# M5 phase B.7: SYS_EXEC end-to-end smoke. Builds the kernel with
# `-DCAPYOS_BOOT_RUN_HELLO` and userland `hello` with
# `-DCAPYOS_HELLO_EXEC`. The hello binary writes [before-exec],
# then calls capy_exec("/bin/exectarget"); the kernel resolves the
# path against embedded_progs, replaces the AS, and lands sysret
# at exectarget's _start, which writes [exec-ok] and exits.
#
# Validates end-to-end:
#   1. embedded_progs_lookup correctly maps "/bin/exectarget" to
#      the second embedded blob.
#   2. process_exec_replace builds the new AS, loads the new ELF,
#      and reloads CR3.
#   3. sys_exec rewrites the syscall return frame so sysret jumps
#      to the new entry point with a fresh user RSP.
#   4. The new program runs in ring 3 against the new AS without
#      reusing any state from hello.
smoke-x64-exec:
	@echo "Executando smoke test x64 (SYS_EXEC)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_EXEC'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_exec.py $(SMOKE_X64_EXEC_ARGS)

# M5 phase C.5: SYS_FORK + SYS_WAIT + SYS_EXIT smoke. Builds the
# kernel with `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO`
# and userland `hello` with `-DCAPYOS_HELLO_FORKWAIT`. The hello
# binary forks once; the child writes [child-running] and
# capy_exit(42); the parent capy_wait()s and writes [parent-reaped]
# (status==42) or [parent-bad-status] otherwise.
#
# Validates end-to-end:
#   1. SYS_EXIT routes through process_exit and flips the child to
#      PROC_STATE_ZOMBIE (otherwise the parent's wait would spin).
#   2. process_wait correctly walks process_by_pid + busy-yield +
#      reads exit_code + reaps the slot.
#   3. The child's exit code propagates byte-perfect through
#      task_exit -> process_exit -> child->exit_code -> *status.
smoke-x64-fork-wait:
	@echo "Executando smoke test x64 (SYS_FORK + SYS_WAIT)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_FORKWAIT'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_fork_wait.py $(SMOKE_X64_FORK_WAIT_ARGS)

# M5 phase D: SYS_PIPE + fork inheritance smoke. Builds the kernel
# with `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO` and
# userland `hello` with `-DCAPYOS_HELLO_PIPE`. The hello binary
# pipes "ping" from child to parent and writes [pipe-ok] on
# successful round-trip.
smoke-x64-pipe:
	@echo "Executando smoke test x64 (SYS_PIPE + fork)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_PIPE'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_pipe.py $(SMOKE_X64_PIPE_ARGS)

# M5 phase F: multi-process fault isolation smoke. Builds the kernel
# with `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO` and
# userland `hello` with `-DCAPYOS_HELLO_FORK_CRASH`. Hello forks; the
# child segfaults via NULL deref; the parent's wait() returns the
# 128+vector exit code and emits [parent-saw-crash]. The kernel
# MUST NOT panic.
smoke-x64-fork-crash:
	@echo "Executando smoke test x64 (fork + crash isolation)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO' \
	              EXTRA_USERLAND_CFLAGS='-DCAPYOS_HELLO_FORK_CRASH'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_fork_crash.py $(SMOKE_X64_FORK_CRASH_ARGS)

# M5 phase E.6: capysh interactive shell smoke. Builds the kernel
# with `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO
# -DCAPYOS_BOOT_RUN_CAPYSH`. The kernel boots straight into capysh
# (the embedded /bin/capysh binary). The harness drives QEMU's HMP
# monitor via a unix socket to type a script (`help`, `pid`,
# `exectarget`, `exit`) and validates the corresponding markers
# show up on the debug console.
#
# Validates end-to-end:
#   1. stdin_buf surfaces keyboard bytes to user space.
#   2. SYS_READ fd 0 blocks correctly until input arrives.
#   3. capysh's parser dispatches builtins (help / pid / exectarget /
#      exit) without falling through to "unknown".
#   4. The `exectarget` builtin completes a fork+exec+wait round-trip
#      with the embedded /bin/exectarget binary (proves A+B+C still
#      compose correctly under the shell).
smoke-x64-capysh:
	@echo "Executando smoke test x64 (capysh shell)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_HELLO -DCAPYOS_BOOT_RUN_CAPYSH'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_capysh.py $(SMOKE_X64_CAPYSH_ARGS)

# F3.3e: end-to-end browser-isolation smoke. Builds the kernel
# with `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_BROWSER_SMOKE`
# so kernel_main calls `kernel_boot_run_browser_smoke()` after the
# preemptive scheduler is armed. The boot CPU enters `hlt` while
# two scheduler-managed tasks run concurrently:
#
#   - the kernel `browser-poller` task drives the chrome/runtime
#     stack: NAVIGATE -> drain events -> PING -> PONG -> SHUTDOWN;
#   - the ring-3 capybrowser engine consumes commands from fd 0,
#     emits the canonical event sequence on fd 1.
#
# Both communicate via two kernel pipes set up by
# `browser_engine_spawn`. The harness validates 9 deterministic
# debugcon markers covering spawn, navigation, frame delivery,
# watchdog ping/pong, and graceful shutdown.
smoke-x64-browser-spawn:
	@echo "Executando smoke test x64 (browser spawn ponta-a-ponta)..."
	$(MAKE) clean
	$(MAKE) all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_BROWSER_SMOKE'
	$(MAKE) iso-uefi
	$(MAKE) manifest64
	python3 tools/scripts/smoke_x64_browser_spawn.py $(SMOKE_X64_BROWSER_SPAWN_ARGS)

# M4 phase 9: aggregate target that runs the FULL preemptive
# scheduler smoke matrix end-to-end. Exists so CI can invoke a
# single Make target instead of listing every smoke individually,
# and so a developer can sanity-check the entire 8a..8f stack
# against a known QEMU/OVMF environment with one command.
#
# Each sub-smoke does its own `make clean` + `make all64` with the
# right `EXTRA_CFLAGS64` because the per-source `.d` files do not
# track preprocessor macros. The aggregate therefore re-builds the
# kernel four times; this is the price of macro-gated boot
# variants and matches the trade-off the existing smokes already
# accept individually.
smoke-x64-preemptive-all:
	@echo "Executando suite completa de smokes preemptivos M4..."
	$(MAKE) smoke-x64-preemptive
	$(MAKE) smoke-x64-preemptive-demo
	$(MAKE) smoke-x64-preemptive-user
	$(MAKE) smoke-x64-preemptive-user-2task
	@echo "[ok] Suite preemptiva completa passou."

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
.PHONY: all all64 iso-uefi manifest64 release-checksums verify-release-checksums disk-gpt provision-vhd legacy-disabled clean test layout-audit layout-audit-report version-audit boot-perf-baseline boot-perf-baseline-selftest check-toolchain release-check smoke-x64-cli smoke-x64-boot-perf smoke-x64-cli-nvme smoke-x64-hello-user smoke-x64-hello-segfault smoke-x64-preemptive smoke-x64-preemptive-demo smoke-x64-preemptive-user smoke-x64-preemptive-user-2task smoke-x64-preemptive-all smoke-x64-fork-cow smoke-x64-exec smoke-x64-fork-wait smoke-x64-pipe smoke-x64-fork-crash smoke-x64-capysh smoke-x64-iso inspect-disk capylibc hello-elf hello-blob exectarget-elf exectarget-blob capysh-elf capysh-blob

-include $(CAPYOS64_DEPS) $(UEFI_LOADER_DEPS)
