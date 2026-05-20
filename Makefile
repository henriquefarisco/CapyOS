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
    $(info [build] Using $(LD64) for kernel linking; set LD64=$(CROSS64)-ld to force the cross linker)
  endif
else
  CC64      := x86_64-linux-gnu-gcc
  ifeq ($(origin LD64), undefined)
    LD64 := x86_64-linux-gnu-ld
  endif
  OBJCOPY64 := x86_64-linux-gnu-objcopy
  STACKPROTECT64 := -fno-stack-protector
  $(info [build] Using x86_64-linux-gnu host toolchain by default; set TOOLCHAIN64=elf to force x86_64-elf-*)
  $(info [build] Host toolchain disables kernel stack protector; x86_64-linux-gnu emits TLS canary reads via %fs:0x28 in freestanding code)
endif
CFLAGS64  := -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=small -mno-red-zone -fno-asynchronous-unwind-tables -fno-unwind-tables -fcf-protection=none -fno-pic -fno-pie -fno-plt -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-strict-aliasing -fno-tree-vectorize $(STACKPROTECT64) -Iinclude -Isrc -I$(BUILD_GEN) -I$(BEARSSL_DIR)/inc -I$(BEARSSL_DIR)/src -Ithird_party/tinf
# Bug fix critico (2026-05-05 sessao 4b): habilita preemptive
# scheduler por default. Sem essa flag, `scheduler_set_running(1)`
# nunca e chamado, `task_yield()` vira no-op, e tasks adicionadas
# ao run_queue NUNCA sao escalonadas. Resultado pre-fix: tasks
# ring 3 spawnadas via scheduler_add permaneciam no run_queue sem
# ser picked. Este comentario fica como justificativa historica
# do default ON para processos userland futuros.
# Ver kernel/scheduler.c::scheduler_yield e
# arch/x86_64/preemptive_boot.c::capyos_preemptive_mark_running.
CFLAGS64  += -DCAPYOS_PREEMPTIVE_SCHEDULER
# EXTRA_CFLAGS64 is appended last so callers can flip build-time
# feature flags without editing CFLAGS64 in place. Examples:
#   make all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'
# Combine with `make clean` first, since the per-source .d files
# do not track preprocessor macros and would otherwise reuse
# stale objects compiled without the new flag.
CFLAGS64  += $(EXTRA_CFLAGS64)
DEPFLAGS64 := -MMD -MP
LDFLAGS64 := -nostdlib

# ── alpha.241 modular build profile ─────────────────────────────────────────
# PROFILE selects which subsystems are linked into the kernel ELF:
#   full      (default) — kernel + services + desktop session + apps
#   core-only           — kernel + services + capysh only; desktop+apps off
#
# Desktop session, window manager and apps source code is OWNED by the
# sibling CapyUI repo (since alpha.241). When CAPYUI_DIR points at a
# valid sibling checkout the cross-repo compile path is used. When the
# sibling is absent OR the migration has not been applied yet (run
# `python3 tools/scripts/migrate_to_capyui.py --apply` to do it), the
# build falls back to the legacy in-tree copies so existing checkouts
# keep working during the transition.
PROFILE ?= full
ifneq ($(PROFILE),full)
  ifneq ($(PROFILE),core-only)
    $(error Unknown PROFILE=$(PROFILE); expected full or core-only)
  endif
endif

# Keep this path relative by default. GNU Make's path functions split on
# whitespace, and this workspace commonly lives under "Área de trabalho".
CAPYUI_DIR ?= ../CapyUI
ifneq ($(strip $(CAPYUI_DIR)),)
  ifneq ($(wildcard $(CAPYUI_DIR)/src/desktop/desktop_runtime.c),)
    $(info [build] CapyUI sibling detected; sourcing desktop+window+apps from $(CAPYUI_DIR)/src/)
    DESKTOP_SRC_ROOT := $(CAPYUI_DIR)/src/desktop
    WINDOW_SRC_ROOT  := $(CAPYUI_DIR)/src/window
    APPS_SRC_ROOT    := $(CAPYUI_DIR)/src/apps
  else
    $(info [build] CapyUI present but desktop subtree not migrated yet; using in-tree fallback)
    DESKTOP_SRC_ROOT := $(SRC_DIR)/gui/desktop
    WINDOW_SRC_ROOT  := $(SRC_DIR)/gui/window
    APPS_SRC_ROOT    := $(SRC_DIR)/apps
  endif
else
  $(info [build] CapyUI sibling not found at ../CapyUI; using in-tree desktop sources)
  DESKTOP_SRC_ROOT := $(SRC_DIR)/gui/desktop
  WINDOW_SRC_ROOT  := $(SRC_DIR)/gui/window
  APPS_SRC_ROOT    := $(SRC_DIR)/apps
endif

ifeq ($(PROFILE),core-only)
  CFLAGS64 += -DCAPYOS_PROFILE_CORE_ONLY
  $(info [build] PROFILE=core-only: kernel ELF will NOT include desktop+window+apps)
endif

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
	$(BUILD)/x86_64/lang/app_language.o \
	$(BUILD)/x86_64/auth/login_runtime.o \
	$(BUILD)/x86_64/auth/login_runtime/contract_policy.o \
	$(BUILD)/x86_64/auth/login_runtime/credential_buffer.o \
	$(BUILD)/x86_64/auth/login_runtime/credential_input.o \
	$(BUILD)/x86_64/auth/login_runtime/credential_interaction.o \
	$(BUILD)/x86_64/auth/login_runtime/credential_view_model.o \
	$(BUILD)/x86_64/auth/login_runtime/session_pipeline.o \
	$(BUILD)/x86_64/auth/login_runtime/render_action_ui_event.o \
	$(BUILD)/x86_64/auth/login_runtime/route_controller.o \
	$(BUILD)/x86_64/auth/login_runtime/presenter_binding.o \
	$(BUILD)/x86_64/auth/login_runtime/mount_commit.o \
	$(BUILD)/x86_64/auth/login_runtime/handoff_dispatch.o \
	$(BUILD)/x86_64/auth/login_runtime/queue_activation.o \
	$(BUILD)/x86_64/auth/login_runtime/frame_surface.o \
	$(BUILD)/x86_64/auth/login_runtime/compositor_damage.o \
	$(BUILD)/x86_64/auth/login_runtime/present_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/schedule_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/vsync_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/scanout_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/display_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/output_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/blit_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/framebuffer_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/flush_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/barrier_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/fence_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/timeline_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/sync_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/deadline_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/completion_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/ack_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/retire_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/cleanup_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/seal_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/audit_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/record_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/receipt_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/ledger_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/journal_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/archive_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/retention_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/expiry_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/purge_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/tombstone_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/compaction_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/reclaim_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/release_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/gui_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_surface_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_compositor_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_damage_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_present_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_schedule_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_vsync_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_scanout_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_display_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_output_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_blit_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_commit_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_flip_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_vblank_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_event_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/window_input_plan.o \
	$(BUILD)/x86_64/auth/login_runtime/pipeline_safety.o \
	$(BUILD)/x86_64/auth/login_runtime/view_model.o \
	$(BUILD)/x86_64/auth/loginwindow_auth_submit.o \
	$(BUILD)/x86_64/auth/loginwindow_auth_submit_userdb.o \
	$(BUILD)/x86_64/auth/loginwindow_recovery_decision.o \
	$(BUILD)/x86_64/auth/loginwindow_session_handoff.o \
	$(BUILD)/x86_64/auth/login_window_gui_layout.o \
	$(BUILD)/x86_64/net/bootstrap/network_bootstrap_config.o \
	$(BUILD)/x86_64/net/bootstrap/network_bootstrap_diag.o \
	$(BUILD)/x86_64/net/bootstrap/network_bootstrap.o \
	$(BUILD)/x86_64/config/system_setup.o \
	$(BUILD)/x86_64/config/system_setup_wizard.o \
	$(BUILD)/x86_64/config/system_settings.o \
	$(BUILD)/x86_64/config/first_boot/logging.o \
	$(BUILD)/x86_64/config/first_boot/storage_users.o \
	$(BUILD)/x86_64/config/first_boot/program.o \
	$(BUILD)/x86_64/config/first_boot/modules.o \
	$(BUILD)/x86_64/services/update_agent.o \
	$(BUILD)/x86_64/services/update_agent_parse.o \
	$(BUILD)/x86_64/services/update_agent_apply.o \
	$(BUILD)/x86_64/services/update_agent_prepare.o \
	$(BUILD)/x86_64/services/update_agent_transact.o \
	$(BUILD)/x86_64/services/capypkg/capypkg_state.o \
	$(BUILD)/x86_64/services/capypkg/capypkg_manifest.o \
	$(BUILD)/x86_64/services/capypkg/capypkg_repo.o \
	$(BUILD)/x86_64/services/capypkg/capypkg_install.o \
	$(BUILD)/x86_64/services/capypkg_bootstrap.o \
	$(BUILD)/x86_64/services/install_profile.o \
	$(BUILD)/x86_64/auth/user.o \
	$(BUILD)/x86_64/auth/user_helpers.o \
	$(BUILD)/x86_64/auth/userdb_io.o \
	$(BUILD)/x86_64/auth/userdb_auth.o \
	$(BUILD)/x86_64/auth/user_password_hash.o \
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
	$(BUILD)/x86_64/security/crypt_kdf.o \
	$(BUILD)/x86_64/security/crypt_aes_xts.o \
	$(BUILD)/x86_64/security/crypt_hkdf.o \
	$(BUILD)/x86_64/security/volume_header.o \
	$(BUILD)/x86_64/security/volume_provider.o \
	$(BUILD)/x86_64/security/volume_provider_rekey.o \
	$(BUILD)/x86_64/security/volume_provider_rekey_execute.o \
	$(BUILD)/x86_64/security/volume_provider_rekey_copy.o \
	$(BUILD)/x86_64/security/volume_provider_rekey_commit.o \
	$(BUILD)/x86_64/security/volume_provider_rekey_recovery.o \
	$(BUILD)/x86_64/security/volume_provider_rekey_orchestrator.o \
	$(BUILD)/x86_64/security/csprng.o \
	$(BUILD)/x86_64/security/tls_hostname.o \
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
	$(BUILD)/x86_64/shell/commands/system_control/updates_arm.o \
	$(BUILD)/x86_64/shell/commands/system_control/service_target_resume.o \
	$(BUILD)/x86_64/shell/commands/system_control/recovery_login_verify.o \
	$(BUILD)/x86_64/shell/commands/system_control/recovery_storage.o \
	$(BUILD)/x86_64/shell/commands/system_control/capypkg_commands.o \
	$(BUILD)/x86_64/shell/commands/system_control/capy_command.o \
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
	$(BUILD)/x86_64/kernel/syscall_net.o \
	$(BUILD)/x86_64/kernel/syscall_net_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_syscall.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_clock.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_clock_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_random.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_random_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_devfs.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_process.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_process_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_cpuinfo.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_proc.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_fd.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_fd_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_mmap.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_mmap_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_futex.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_futex_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_eventfd.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_eventfd_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_net.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_net_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_epoll.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_epoll_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_signal.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_memfd.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_memfd_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_inotify.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_clone.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_shm.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_vfs.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_vfs_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_vfs_router.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_procfs.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_procfs_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_tmpfs.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_tmpfs_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_brk.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_brk_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_arch_prctl.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_arch_prctl_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_exit.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_exit_init.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_ioctl.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_fcntl.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_io.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_stat.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_path.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_statx.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_dirent.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_at.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_dup.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_umask.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_wait.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_kill.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_trunc.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_sysinfo.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_priority.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_pgrp.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_fs_mut.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_mlock.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_creds.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_fs_meta.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_link.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_sync.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_utime.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_setid.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_chdir.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_xattr.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_statfs.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_advise.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_rlimit_legacy.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_caps.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_itimer.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_lock.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_sched_prio.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_posix_timer.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_time_legacy.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_sandbox.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_mincore.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_numa.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_settod.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_jit_aux.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_namespace.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_exec_ext.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_pipe_zero.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_proc_vm.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_openat2.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_pkey.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_landlock.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_seccomp.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_fanotify.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_io_uring.o \
	$(BUILD)/x86_64/kernel/linux_compat/linux_modern_misc.o \
	$(BUILD)/x86_64/kernel/elf_loader.o \
	$(BUILD)/x86_64/kernel/embedded_hello.o \
	$(BUILD)/x86_64/kernel/user_init.o \
	$(BUILD)/x86_64/kernel/user_task_init.o \
	$(BUILD)/x86_64/kernel/process.o \
	$(BUILD)/x86_64/kernel/process_iter.o \
	$(BUILD)/x86_64/kernel/embedded_progs.o \
	$(BUILD)/x86_64/kernel/module_gate.o \
	$(HELLO_BLOB_OBJ) \
	$(EXECTARGET_BLOB_OBJ) \
	$(CAPYSH_BLOB_OBJ) \
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
	$(BUILD)/x86_64/security/ed25519_group.o \
	$(BUILD)/x86_64/security/ed25519_encode.o \
	$(BUILD)/x86_64/security/ed25519_scalar.o \
	$(BUILD)/x86_64/security/fe25519.o \
	$(BUILD)/x86_64/security/sha256.o \
	$(BUILD)/x86_64/security/blake2b.o \
	$(BUILD)/x86_64/security/argon2.o \
	$(BUILD)/x86_64/security/chacha20_poly1305.o \
	$(BUILD)/x86_64/security/x25519.o \
	$(BUILD)/x86_64/boot/boot_slot.o \
	$(BUILD)/x86_64/drivers/input/mouse.o \
	$(BUILD)/x86_64/gui/core/font8x8_data.o \
	$(BUILD)/x86_64/gui/core/event.o \
	$(BUILD)/x86_64/gui/core/font.o \
	$(BUILD)/x86_64/gui/core/compositor.o \
	$(BUILD)/x86_64/gui/core/compositor_theme.o \
	$(BUILD)/x86_64/gui/core/compositor_render.o \
	$(BUILD)/x86_64/gui/widgets/widget.o \
	$(BUILD)/x86_64/gui/widgets/context_menu.o \
	$(BUILD)/x86_64/gui/widgets/inline_prompt.o \
	$(BUILD)/x86_64/gui/terminal/terminal.o \
	$(BUILD)/x86_64/fs/capyfs/capyfs_journal_integration.o \
	$(BUILD)/x86_64/boot/boot_metrics.o \
	$(BUILD)/x86_64/arch/x86_64/smp.o \
	$(BUILD)/x86_64/auth/auth_policy.o \
	$(BUILD)/x86_64/auth/privilege.o \
	$(BUILD)/x86_64/kernel/pipe.o \
	$(BUILD)/x86_64/kernel/stdin_buf.o \
	$(BUILD)/x86_64/drivers/usb/usb_core.o \
	$(BUILD)/x86_64/drivers/usb/usb_descriptors.o \
	$(BUILD)/x86_64/drivers/usb/usb_hid.o \
	$(BUILD)/x86_64/drivers/gpu/gpu_core.o \
	$(BUILD)/x86_64/drivers/rtc/rtc.o \
	$(BUILD)/x86_64/drivers/serial/serial_com1.o \
	$(BUILD)/x86_64/security/sha512.o \
	$(BUILD)/x86_64/shell/commands/extended.o \
	$(DESKTOP_OBJS) \
	$(WINDOW_OBJS) \
	$(APPS_OBJS)
CAPYOS64_DEPS = $(CAPYOS64_OBJS:.o=.d)

# ── Desktop / window / apps object lists (alpha.241 modular profile) ────────
# Empty when PROFILE=core-only; populated from CapyUI when sibling repo
# is present (otherwise from the in-tree fallback path under src/gui|apps).
ifeq ($(PROFILE),full)
DESKTOP_OBJS := \
	$(BUILD)/x86_64/capyui-desktop/desktop.o \
	$(BUILD)/x86_64/capyui-desktop/desktop_runtime.o \
	$(BUILD)/x86_64/capyui-desktop/desktop_mouse.o \
	$(BUILD)/x86_64/capyui-desktop/desktop_icons.o \
	$(BUILD)/x86_64/capyui-desktop/desktop_icons_context.o \
	$(BUILD)/x86_64/capyui-desktop/desktop_smoke_readiness.o \
	$(BUILD)/x86_64/capyui-desktop/taskbar.o \
	$(BUILD)/x86_64/capyui-desktop/taskbar_menu.o \
	$(BUILD)/x86_64/capyui-desktop/taskbar_menu_input.o
WINDOW_OBJS := \
	$(BUILD)/x86_64/capyui-window/window_manager.o \
	$(BUILD)/x86_64/capyui-window/window_dispatcher.o \
	$(BUILD)/x86_64/capyui-window/notification.o
APPS_OBJS := \
	$(BUILD)/x86_64/capyui-apps/calculator.o \
	$(BUILD)/x86_64/capyui-apps/file_manager.o \
	$(BUILD)/x86_64/capyui-apps/file_manager_view.o \
	$(BUILD)/x86_64/capyui-apps/file_manager_dnd.o \
	$(BUILD)/x86_64/capyui-apps/text_editor.o \
	$(BUILD)/x86_64/capyui-apps/task_manager.o \
	$(BUILD)/x86_64/capyui-apps/settings.o \
	$(BUILD)/x86_64/capyui-apps/settings_view.o \
	$(BUILD)/x86_64/capyui-apps/settings_actions.o
else
DESKTOP_OBJS :=
WINDOW_OBJS :=
APPS_OBJS :=
endif

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

# ── alpha.241: cross-repo compile rules for CapyUI-owned subtrees ───────────
# These pattern rules build desktop/window/apps from whatever source
# directory CAPYUI_DIR resolved to above (either the sibling CapyUI repo
# after migration, or the in-tree fallback). The object output paths
# (`capyui-desktop/`, `capyui-window/`, `capyui-apps/`) deliberately do
# NOT match the in-tree source tree under `src/` to make the modular
# boundary obvious in build logs.
$(BUILD)/x86_64/capyui-desktop/%.o: $(DESKTOP_SRC_ROOT)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) -I$(DESKTOP_SRC_ROOT) -I$(DESKTOP_SRC_ROOT)/internal $(DEPFLAGS64) -c $< -o $@

$(BUILD)/x86_64/capyui-window/%.o: $(WINDOW_SRC_ROOT)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) $(DEPFLAGS64) -c $< -o $@

$(BUILD)/x86_64/capyui-apps/%.o: $(APPS_SRC_ROOT)/%.c | $(BUILD) $(BUILD_GEN)
	@mkdir -p $(dir $@)
	$(CC64) $(CFLAGS64) -I$(APPS_SRC_ROOT)/internal $(DEPFLAGS64) -c $< -o $@

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
# Keep generated user code off FPU/SSE/MMX instructions: CapyOS does
# not save/restore those states for ring-3 tasks yet.
USERLAND_CFLAGS = -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=small \
                  -fno-asynchronous-unwind-tables -fno-unwind-tables \
                  -fcf-protection=none -fno-pic -fno-pie -fno-plt \
                  -fno-omit-frame-pointer -fno-strict-aliasing \
                  -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx \
                  -mno-80387 -msoft-float \
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

# F4 seção c parte 2/2 (2026-05-08): libcapy-net high-level userland
# TCP client façade. Built as a separate object set so binaries that
# don't need network I/O (hello, capysh) aren't forced to link the
# extra ~6 KB. Future libcapy-net users (browser adapters,
# update-agent fetch path) will pull this in via $(CAPYLIBC_NET_OBJS).
CAPYLIBC_NET_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_endian.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_inet.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_tcp.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_error.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_resolve.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_url.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_tls.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-net/capy_net_http.o \
	$(CAPYLIBC_TLS_OBJS)

CAPYLIBC_TLS_OBJS = \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_config.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_context.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_trust.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_trust_bundle.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_backend_plan.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_bearssl_state.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_bearssl_adapter.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls_backend.o \
	$(CAPYLIBC_BUILD_DIR)/lib/capylibc-tls/capy_tls.o

.PHONY: capylibc capylibc-net capylibc-tls
capylibc: $(CAPYLIBC_OBJS)
	@echo "[ok] capylibc objects assembled: $(CAPYLIBC_OBJS)"

capylibc-net: $(CAPYLIBC_NET_OBJS)
	@echo "[ok] capylibc-net objects assembled: $(CAPYLIBC_NET_OBJS)"

capylibc-tls: $(CAPYLIBC_TLS_OBJS)
	@echo "[ok] capylibc-tls objects assembled: $(CAPYLIBC_TLS_OBJS)"

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

# Sessao 6 (2026-05-05): regras do navegador legado erradicadas.
# Serao reabertas somente por adaptador versionado na etapa correta.

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

.PHONY: sign-release-checksums
sign-release-checksums: release-checksums
	@if [ -z "$(RELEASE_PRIVATE_KEY)" ]; then echo "[err] informe RELEASE_PRIVATE_KEY=/caminho/offline/ed25519.pem"; exit 2; fi
	@if [ -n "$(RELEASE_PUBLIC_KEY)" ]; then \
		python3 tools/scripts/sign_release.py --input "$(RELEASE_SHA256)" --private-key "$(RELEASE_PRIVATE_KEY)" --signature "$(RELEASE_SIGNATURE)" --public-key-out "$(RELEASE_PUBLIC_KEY)" --force; \
	else \
		python3 tools/scripts/sign_release.py --input "$(RELEASE_SHA256)" --private-key "$(RELEASE_PRIVATE_KEY)" --signature "$(RELEASE_SIGNATURE)" --force; \
	fi

.PHONY: verify-release-signature
verify-release-signature:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -n "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then \
		python3 tools/scripts/verify_release_signature.py --input "$(RELEASE_SHA256)" --signature "$(RELEASE_SIGNATURE)" --public-key "$(RELEASE_PUBLIC_KEY)" --expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)"; \
	else \
		python3 tools/scripts/verify_release_signature.py --input "$(RELEASE_SHA256)" --signature "$(RELEASE_SIGNATURE)" --public-key "$(RELEASE_PUBLIC_KEY)"; \
	fi

.PHONY: verify-release-signature-selftest
verify-release-signature-selftest:
	python3 tools/scripts/verify_release_signature.py --self-test $(RELEASE_VERIFY_SELFTEST_ARGS)

.PHONY: smoke-marker-policy-selftest
smoke-marker-policy-selftest:
	python3 tools/scripts/smoke_marker_policy.py
	python3 tools/scripts/smoke_x64_vmware.py --self-test
	python3 tools/scripts/release_ci_smoke_evidence.py --self-test

.PHONY: release-public-key-fingerprint
release-public-key-fingerprint:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	python3 tools/scripts/release_public_key_fingerprint.py --public-key "$(RELEASE_PUBLIC_KEY)" $(RELEASE_PUBLIC_KEY_FINGERPRINT_ARGS)

.PHONY: release-public-key-manifest
release-public-key-manifest:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	python3 tools/scripts/release_public_key_manifest.py \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--output "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		$(RELEASE_PUBLIC_KEY_MANIFEST_ARGS)

.PHONY: release-public-materials-check
release-public-materials-check:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	python3 tools/scripts/release_public_materials_check.py \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		$(RELEASE_PUBLIC_MATERIALS_CHECK_ARGS)

.PHONY: release-publication-manifest
release-publication-manifest:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	python3 tools/scripts/release_publication_manifest.py \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--output "$(RELEASE_PUBLICATION_MANIFEST)" \
		$(RELEASE_PUBLICATION_MANIFEST_ARGS)

.PHONY: verify-release-publication-manifest
verify-release-publication-manifest:
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	python3 tools/scripts/verify_release_publication_manifest.py \
		--manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		$(RELEASE_PUBLICATION_VERIFY_ARGS)

.PHONY: release-ci-publication-contract
release-ci-publication-contract:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_publication_contract.py \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		$(RELEASE_CI_PUBLICATION_CONTRACT_ARGS)

.PHONY: release-publication-gate
release-publication-gate:
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	python3 tools/scripts/release_publication_gate.py \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		$(RELEASE_PUBLICATION_GATE_ARGS)

.PHONY: release-official-handoff-manifest
release-official-handoff-manifest:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_official_handoff_manifest.py \
		--release-tag "$(RELEASE_TAG)" \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		$(RELEASE_OFFICIAL_HANDOFF_ARGS)

.PHONY: verify-release-official-handoff-manifest
verify-release-official-handoff-manifest:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_official_handoff_manifest.py \
		--verify \
		--release-tag "$(RELEASE_TAG)" \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		$(RELEASE_OFFICIAL_HANDOFF_VERIFY_ARGS)

.PHONY: release-ci-smoke-readiness
release-ci-smoke-readiness:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_readiness.py \
		--release-tag "$(RELEASE_TAG)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		$(RELEASE_CI_SMOKE_READINESS_ARGS)

.PHONY: release-ci-smoke-evidence
release-ci-smoke-evidence:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_evidence.py \
		--release-tag "$(RELEASE_TAG)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_SMOKE_EVIDENCE_MANIFEST)" \
		$(RELEASE_CI_SMOKE_EVIDENCE_ARGS)

.PHONY: verify-release-ci-smoke-evidence
verify-release-ci-smoke-evidence:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_evidence.py \
		--verify \
		--release-tag "$(RELEASE_TAG)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_SMOKE_EVIDENCE_MANIFEST)" \
		$(RELEASE_CI_SMOKE_EVIDENCE_VERIFY_ARGS)

.PHONY: release-ci-smoke-acceptance
release-ci-smoke-acceptance:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_acceptance.py \
		--release-tag "$(RELEASE_TAG)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-evidence-manifest "$(RELEASE_SMOKE_EVIDENCE_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_SMOKE_ACCEPTANCE_MANIFEST)" \
		$(RELEASE_CI_SMOKE_ACCEPTANCE_ARGS)

.PHONY: verify-release-ci-smoke-acceptance
verify-release-ci-smoke-acceptance:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_acceptance.py \
		--verify \
		--release-tag "$(RELEASE_TAG)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-evidence-manifest "$(RELEASE_SMOKE_EVIDENCE_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_SMOKE_ACCEPTANCE_MANIFEST)" \
		$(RELEASE_CI_SMOKE_ACCEPTANCE_VERIFY_ARGS)

.PHONY: release-ci-smoke-promotion
release-ci-smoke-promotion:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_promotion.py \
		--release-tag "$(RELEASE_TAG)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-evidence-manifest "$(RELEASE_SMOKE_EVIDENCE_MANIFEST)" \
		--smoke-acceptance-manifest "$(RELEASE_SMOKE_ACCEPTANCE_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_SMOKE_PROMOTION_MANIFEST)" \
		$(RELEASE_CI_SMOKE_PROMOTION_ARGS)

.PHONY: verify-release-ci-smoke-promotion
verify-release-ci-smoke-promotion:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_smoke_promotion.py \
		--verify \
		--release-tag "$(RELEASE_TAG)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--handoff-manifest "$(RELEASE_OFFICIAL_HANDOFF_MANIFEST)" \
		--smoke-evidence-manifest "$(RELEASE_SMOKE_EVIDENCE_MANIFEST)" \
		--smoke-acceptance-manifest "$(RELEASE_SMOKE_ACCEPTANCE_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		--output "$(RELEASE_SMOKE_PROMOTION_MANIFEST)" \
		$(RELEASE_CI_SMOKE_PROMOTION_VERIFY_ARGS)

.PHONY: release-ci-official-provisioning-contract
release-ci-official-provisioning-contract:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_official_provisioning_contract.py \
		--release-tag "$(RELEASE_TAG)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		$(RELEASE_CI_OFFICIAL_PROVISIONING_ARGS)

.PHONY: release-ci-tag-gate
release-ci-tag-gate:
	@if [ -z "$(RELEASE_TAG)" ]; then echo "[err] informe RELEASE_TAG=0.8.0-alpha.N+YYYYMMDD ou v0.8.0-alpha.N+YYYYMMDD"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY=/caminho/publico/ed25519.pub.pem"; exit 2; fi
	@if [ -z "$(RELEASE_PUBLIC_KEY_SHA256)" ]; then echo "[err] informe RELEASE_PUBLIC_KEY_SHA256=<fingerprint esperado>"; exit 2; fi
	@if [ -z "$(SMOKE_X64_VMWARE_ARGS)" ]; then echo "[err] informe SMOKE_X64_VMWARE_ARGS=..."; exit 2; fi
	python3 tools/scripts/release_ci_tag_gate.py \
		--release-tag "$(RELEASE_TAG)" \
		--checksums "$(RELEASE_SHA256)" \
		--signature "$(RELEASE_SIGNATURE)" \
		--public-key "$(RELEASE_PUBLIC_KEY)" \
		--expected-public-key-sha256 "$(RELEASE_PUBLIC_KEY_SHA256)" \
		--public-key-manifest "$(RELEASE_PUBLIC_KEY_MANIFEST)" \
		--publication-manifest "$(RELEASE_PUBLICATION_MANIFEST)" \
		--smoke-vmware-args "$(SMOKE_X64_VMWARE_ARGS)" \
		$(RELEASE_CI_TAG_GATE_ARGS)

.PHONY: release-ci-preflight
release-ci-preflight:
	RELEASE_PUBLIC_KEY="$(RELEASE_PUBLIC_KEY)" \
	RELEASE_PUBLIC_KEY_SHA256="$(RELEASE_PUBLIC_KEY_SHA256)" \
	RELEASE_PUBLIC_KEY_MANIFEST="$(RELEASE_PUBLIC_KEY_MANIFEST)" \
	SMOKE_X64_VMWARE_ARGS="$(SMOKE_X64_VMWARE_ARGS)" \
	python3 tools/scripts/release_ci_preflight.py $(RELEASE_CI_PREFLIGHT_ARGS)

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
	@echo "[legacy] Alvos suportados: all64, iso-uefi, disk-gpt, provision-vhd, inspect-disk, smoke-x64-cli, smoke-x64-vmware-mouse-events, smoke-x64-iso, test"
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
RELEASE_SIGNATURE := $(RELEASE_SHA256).sig
RELEASE_PUBLIC_KEY_MANIFEST := $(BUILD)/release-public-key.manifest
RELEASE_PUBLICATION_MANIFEST := $(BUILD)/release-publication.manifest
RELEASE_OFFICIAL_HANDOFF_MANIFEST := $(BUILD)/release-official-handoff.manifest
RELEASE_SMOKE_EVIDENCE_MANIFEST := $(BUILD)/release-smoke-evidence.manifest
RELEASE_SMOKE_ACCEPTANCE_MANIFEST := $(BUILD)/release-smoke-acceptance.manifest
RELEASE_SMOKE_PROMOTION_MANIFEST := $(BUILD)/release-smoke-promotion.manifest
RELEASE_PRIVATE_KEY ?=
RELEASE_PUBLIC_KEY ?=
RELEASE_PUBLIC_KEY_SHA256 ?=
RELEASE_TAG ?=
RELEASE_PUBLIC_KEY_FINGERPRINT_ARGS ?= --format env
RELEASE_PUBLIC_KEY_MANIFEST_ARGS ?= --force
RELEASE_PUBLIC_MATERIALS_CHECK_ARGS ?=
RELEASE_PUBLICATION_MANIFEST_ARGS ?= --force
RELEASE_PUBLICATION_VERIFY_ARGS ?=
RELEASE_PUBLICATION_GATE_ARGS ?=
RELEASE_CI_PUBLICATION_CONTRACT_ARGS ?=
RELEASE_CI_TAG_GATE_ARGS ?=
RELEASE_CI_OFFICIAL_PROVISIONING_ARGS ?=
RELEASE_OFFICIAL_HANDOFF_ARGS ?= --force
RELEASE_OFFICIAL_HANDOFF_VERIFY_ARGS ?=
RELEASE_CI_SMOKE_READINESS_ARGS ?=
RELEASE_CI_SMOKE_EVIDENCE_ARGS ?= --force
RELEASE_CI_SMOKE_EVIDENCE_VERIFY_ARGS ?=
RELEASE_CI_SMOKE_ACCEPTANCE_ARGS ?= --force
RELEASE_CI_SMOKE_ACCEPTANCE_VERIFY_ARGS ?=
RELEASE_CI_SMOKE_PROMOTION_ARGS ?= --force
RELEASE_CI_SMOKE_PROMOTION_VERIFY_ARGS ?=
RELEASE_VERIFY_SELFTEST_ARGS ?=
RELEASE_CI_PREFLIGHT_ARGS ?=
EFI_BOOT := $(ISO_DIR_EFI)/EFI/BOOT
BOOTX64 := $(EFI_BOOT)/BOOTX64.EFI
EFI_STUB := $(BUILD)/boot/uefi_loader.efi

run run-disk run-installer-iso iso disk-img disk-bootable run-disk-boot install-grub-device \
all32 iso-bios iso-bios-legacy bios legacy mbr: legacy-disabled
# --- Host-side unit tests (gcc) ---
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Isrc -Iuserland/include -Itools/host/include -Ithird_party/tinf -DUNIT_TEST
TEST_BIN    := $(BUILD)/tests/unit_tests
# --- Host test sources, organized by domain to mirror the
#     tests/<domain>/ folder layout introduced on 2026-05-15.
TEST_SRCS   := \
               tests/test_runner.c \
               \
               tests/stubs/stub_kmem.c \
               tests/stubs/stub_context_switch.c \
               tests/stubs/stub_vga.c \
               tests/stubs/stub_usb_core.c \
               tests/stubs/stub_vmm.c \
               tests/stubs/stub_syscall_deps.c \
               tests/stubs/stub_arch_sched_hooks.c \
               \
               tests/auth/test_auth_policy.c \
               tests/auth/test_login_runtime.c \
               tests/auth/test_login_runtime_credential_pre_pipeline.c \
               tests/auth/test_login_runtime_credential_input_view.c \
               tests/auth/test_login_runtime_credential_input_view_panel.c \
               tests/auth/test_login_runtime_credential_audit_view.c \
               tests/auth/test_login_runtime_credential_ui_session.c \
               tests/auth/test_login_runtime_credential_screen.c \
               tests/auth/test_login_runtime_credential_screen_view_model.c \
               tests/auth/test_login_runtime_credential_action_event.c \
               tests/auth/test_login_runtime_credential_route_controller.c \
               tests/auth/test_login_runtime_credential_presenter_binding.c \
               tests/auth/test_login_runtime_credential_mount_commit.c \
               tests/auth/test_login_runtime_credential_handoff_dispatch.c \
               tests/auth/test_login_runtime_credential_queue_activation.c \
               tests/auth/test_login_runtime_credential_frame_surface.c \
               tests/auth/test_login_runtime_credential_compositor_damage.c \
               tests/auth/test_login_runtime_credential_present_plan.c \
               tests/auth/test_login_runtime_credential_schedule_vsync.c \
               tests/auth/test_login_runtime_credential_scanout_display.c \
               tests/auth/test_login_runtime_credential_output_blit.c \
               tests/auth/test_login_runtime_credential_framebuffer_flush.c \
               tests/auth/test_login_runtime_credential_barrier_fence.c \
               tests/auth/test_login_runtime_credential_timeline_sync.c \
               tests/auth/test_login_runtime_credential_deadline_completion.c \
               tests/auth/test_login_runtime_credential_ack_retire.c \
               tests/auth/test_login_runtime_credential_cleanup_seal.c \
               tests/auth/test_login_runtime_credential_audit_record.c \
               tests/auth/test_login_runtime_credential_receipt_ledger.c \
               tests/auth/test_login_runtime_credential_journal_archive.c \
               tests/auth/test_login_runtime_credential_retention_expiry.c \
               tests/auth/test_login_runtime_credential_expiry_plan.c \
               tests/auth/test_login_runtime_credential_purge.c \
               tests/auth/test_login_runtime_credential_tombstone.c \
               tests/auth/test_login_runtime_credential_compaction_reclaim.c \
               tests/auth/test_login_runtime_credential_release_gui.c \
               tests/auth/test_login_runtime_credential_window_surface.c \
               tests/auth/test_login_runtime_credential_window_compositor_damage.c \
               tests/auth/test_login_runtime_credential_window_present.c \
               tests/auth/test_login_runtime_credential_window_schedule.c \
               tests/auth/test_login_runtime_credential_window_vsync.c \
               tests/auth/test_login_runtime_credential_window_scanout.c \
               tests/auth/test_login_runtime_credential_window_display.c \
               tests/auth/test_login_runtime_credential_window_output.c \
               tests/auth/test_login_runtime_credential_window_blit.c \
               tests/auth/test_login_runtime_credential_window_commit.c \
               tests/auth/test_login_runtime_credential_window_flip.c \
               tests/auth/test_login_runtime_credential_window_vblank.c \
               tests/auth/test_login_runtime_credential_window_event.c \
               tests/auth/test_login_runtime_credential_window_input.c \
               tests/auth/test_login_runtime_credential_pipeline_safety_report.c \
               tests/auth/test_loginwindow_auth_submit.c \
               tests/auth/test_loginwindow_recovery_decision.c \
               tests/auth/test_loginwindow_session_handoff.c \
               tests/auth/test_audit_events.c \
               tests/auth/test_user_home.c \
               tests/auth/test_user_password_hash.c src/auth/user_password_hash.c \
               tests/auth/test_user_task_init.c src/kernel/user_task_init.c \
               tests/auth/test_user_init.c src/kernel/user_init.c \
               tests/auth/test_privilege.c src/auth/privilege.c \
               src/auth/login_runtime.c src/auth/login_runtime/contract_policy.c src/auth/login_runtime/credential_buffer.c src/auth/login_runtime/credential_input.c src/auth/login_runtime/credential_interaction.c src/auth/login_runtime/credential_view_model.c src/auth/login_runtime/session_pipeline.c src/auth/login_runtime/render_action_ui_event.c src/auth/login_runtime/route_controller.c src/auth/login_runtime/presenter_binding.c src/auth/login_runtime/mount_commit.c src/auth/login_runtime/handoff_dispatch.c src/auth/login_runtime/queue_activation.c src/auth/login_runtime/frame_surface.c src/auth/login_runtime/compositor_damage.c src/auth/login_runtime/present_plan.c src/auth/login_runtime/schedule_plan.c src/auth/login_runtime/vsync_plan.c src/auth/login_runtime/scanout_plan.c src/auth/login_runtime/display_plan.c src/auth/login_runtime/output_plan.c src/auth/login_runtime/blit_plan.c src/auth/login_runtime/framebuffer_plan.c src/auth/login_runtime/flush_plan.c src/auth/login_runtime/barrier_plan.c src/auth/login_runtime/fence_plan.c src/auth/login_runtime/timeline_plan.c src/auth/login_runtime/sync_plan.c src/auth/login_runtime/deadline_plan.c src/auth/login_runtime/completion_plan.c src/auth/login_runtime/ack_plan.c src/auth/login_runtime/retire_plan.c src/auth/login_runtime/cleanup_plan.c src/auth/login_runtime/seal_plan.c src/auth/login_runtime/audit_plan.c src/auth/login_runtime/record_plan.c src/auth/login_runtime/receipt_plan.c src/auth/login_runtime/ledger_plan.c src/auth/login_runtime/journal_plan.c src/auth/login_runtime/archive_plan.c src/auth/login_runtime/retention_plan.c src/auth/login_runtime/expiry_plan.c src/auth/login_runtime/purge_plan.c src/auth/login_runtime/tombstone_plan.c src/auth/login_runtime/compaction_plan.c src/auth/login_runtime/reclaim_plan.c src/auth/login_runtime/release_plan.c src/auth/login_runtime/gui_plan.c src/auth/login_runtime/window_plan.c src/auth/login_runtime/window_surface_plan.c src/auth/login_runtime/window_compositor_plan.c src/auth/login_runtime/window_damage_plan.c src/auth/login_runtime/window_present_plan.c src/auth/login_runtime/window_schedule_plan.c src/auth/login_runtime/window_vsync_plan.c src/auth/login_runtime/window_scanout_plan.c src/auth/login_runtime/window_display_plan.c src/auth/login_runtime/window_output_plan.c src/auth/login_runtime/window_blit_plan.c src/auth/login_runtime/window_commit_plan.c src/auth/login_runtime/window_flip_plan.c src/auth/login_runtime/window_vblank_plan.c src/auth/login_runtime/window_event_plan.c src/auth/login_runtime/window_input_plan.c src/auth/login_runtime/pipeline_safety.c src/auth/login_runtime/view_model.c src/auth/loginwindow_auth_submit.c src/auth/loginwindow_recovery_decision.c src/auth/loginwindow_session_handoff.c src/auth/user_home.c src/auth/auth_policy.c \
               \
               tests/security/test_csprng.c \
               tests/security/test_crypt_vectors.c tests/security/test_crypt_vectors_aead.c tests/security/test_crypt_vectors_kdf.c \
               tests/security/test_volume_header.c src/security/volume_header.c \
               tests/security/test_volume_provider.c tests/security/test_volume_provider_rekey.c tests/security/test_volume_provider_execute.c tests/security/test_volume_provider_rekey_execute.c tests/security/test_volume_provider_rekey_copy.c tests/security/test_volume_provider_rekey_commit.c tests/security/test_volume_provider_rekey_recovery.c tests/security/test_volume_provider_rekey_orchestrator.c src/security/volume_provider.c src/security/volume_provider_rekey.c src/security/volume_provider_rekey_execute.c src/security/volume_provider_rekey_copy.c src/security/volume_provider_rekey_commit.c src/security/volume_provider_rekey_recovery.c src/security/volume_provider_rekey_orchestrator.c \
               tests/security/test_tls_hostname.c src/security/tls_hostname.c \
               src/security/csprng.c src/security/crypt.c src/security/crypt_kdf.c src/security/crypt_aes_xts.c src/security/crypt_hkdf.c src/security/ed25519.c src/security/ed25519_group.c src/security/ed25519_encode.c src/security/ed25519_scalar.c src/security/fe25519.c src/security/sha256.c src/security/sha512.c src/security/blake2b.c src/security/argon2.c src/security/chacha20_poly1305.c src/security/x25519.c \
               \
               tests/boot/test_boot_manifest.c tests/boot/test_boot_writer.c \
               tests/boot/test_grub_cfg_builder.c tests/boot/test_gen_boot_config.c \
               tests/boot/test_efi_block.c src/drivers/storage/efi_block.c \
               tests/boot/test_boot_slot.c src/boot/boot_slot.c \
               src/boot/boot_manifest.c src/boot/boot_writer.c \
               tools/host/src/grub_cfg_builder.c tools/host/src/gen_boot_config.c \
               \
               tests/fs/test_block_wrappers.c \
               tests/fs/test_partition.c src/fs/storage/block_device.c src/fs/storage/chunk_wrapper.c src/fs/storage/offset_wrapper.c src/fs/storage/partition.c \
               tests/fs/test_capyfs_check.c src/fs/capyfs/capyfs_check.c \
               tests/fs/test_capyfs_journal_cause.c src/fs/capyfs/capyfs_journal_integration.c \
               tests/fs/test_journal.c src/fs/journal/journal.c \
               tests/fs/test_buffer_cache_pacing.c src/fs/cache/buffer_cache.c \
               \
               tests/gui/test_gui_event.c tests/gui/test_gui_event_helpers.c src/gui/core/event.c \
               tests/gui/test_compositor_events.c src/gui/core/compositor.c src/gui/core/compositor_theme.c \
               tests/gui/test_gui_window_dispatcher.c tests/gui/test_gui_window_dispatcher_lifecycle.c $(WINDOW_SRC_ROOT)/window_dispatcher.c \
               tests/gui/test_desktop_smoke_readiness.c $(DESKTOP_SRC_ROOT)/desktop_smoke_readiness.c \
               \
               tests/net/test_http_encoding.c src/net/services/http_encoding.c \
               tests/net/test_net_dns.c src/net/services/dns.c \
               tests/net/test_net_probe.c src/drivers/net/net_probe.c src/drivers/net/netvsc.c \
               tests/net/test_dns_cache.c src/net/services/dns_cache.c \
               tests/net/test_syscall_net.c src/kernel/syscall_net.c \
               tests/net/test_syscall_net_init.c src/kernel/syscall_net_init.c \
               \
               tests/userland/test_capylibc_abi.c \
               tests/userland/test_capylibc_net.c \
               tests/userland/test_capylibc_net_url.c \
               tests/userland/test_capylibc_net_http.c \
                   userland/lib/capylibc-net/capy_net_endian.c \
                   userland/lib/capylibc-net/capy_net_inet.c \
                   userland/lib/capylibc-net/capy_net_tcp.c \
                   userland/lib/capylibc-net/capy_net_error.c \
                   userland/lib/capylibc-net/capy_net_resolve.c \
                   userland/lib/capylibc-net/capy_net_url.c \
                   userland/lib/capylibc-net/capy_net_tls.c \
                   userland/lib/capylibc-net/capy_net_http.c \
               tests/userland/test_capylibc_tls.c \
               tests/userland/test_capylibc_tls_trust.c \
               tests/userland/test_capylibc_tls_backend.c \
                   userland/lib/capylibc-tls/capy_tls_config.c \
                   userland/lib/capylibc-tls/capy_tls_context.c \
                   userland/lib/capylibc-tls/capy_tls_trust.c \
                   userland/lib/capylibc-tls/capy_tls_trust_bundle.c \
                   userland/lib/capylibc-tls/capy_tls_backend_plan.c \
                   userland/lib/capylibc-tls/capy_tls_bearssl_state.c \
                   userland/lib/capylibc-tls/capy_tls_bearssl_adapter.c \
                   userland/lib/capylibc-tls/capy_tls_backend.c \
                   userland/lib/capylibc-tls/capy_tls.c \
               \
               tests/drivers/test_keyboard_layouts.c src/drivers/input/keyboard/layouts/br_abnt2.c src/drivers/input/keyboard/layouts/us.c \
               tests/drivers/test_hyperv_runtime.c src/net/hyperv/hyperv_runtime.c \
               tests/drivers/test_input_hyperv_gate.c src/arch/x86_64/hyperv_input_gate.c \
               tests/drivers/test_hyperv_runtime_gate.c src/net/hyperv/hyperv_runtime_gate.c \
               tests/drivers/test_hyperv_runtime_policy.c src/net/hyperv/hyperv_runtime_policy.c \
               tests/drivers/test_native_runtime_gate.c src/arch/x86_64/native_runtime_gate.c \
               tests/drivers/test_netvsc_backend.c src/drivers/net/netvsc_backend.c \
               tests/drivers/test_netvsc_runtime.c src/drivers/net/netvsc_runtime.c \
               tests/drivers/test_netvsc_session.c src/drivers/net/netvsc_session.c \
               tests/drivers/test_netvsp.c src/drivers/net/netvsp.c \
               tests/drivers/test_netvsc_control.c \
               tests/drivers/test_rndis.c src/drivers/net/rndis.c \
               tests/drivers/test_storvsp.c src/drivers/storage/storvsp.c \
               tests/drivers/test_storvsc_session.c src/drivers/storage/storvsc_session.c \
               tests/drivers/test_storvsc_backend.c src/drivers/storage/storvsc_backend.c \
               tests/drivers/test_storvsc_runtime.c src/drivers/storage/storvsc_runtime.c \
               tests/drivers/test_storage_runtime_hyperv_plan.c src/arch/x86_64/storage_runtime_hyperv_plan.c \
               tests/drivers/test_usb_hid_init.c src/drivers/usb/usb_hid.c \
               tests/drivers/test_xhci_address_device.c src/drivers/usb/xhci.c \
               tests/drivers/test_usb_descriptor_parse.c src/drivers/usb/usb_descriptors.c \
               \
               tests/kernel/test_klog.c src/kernel/log/klog.c \
               tests/kernel/test_pmm.c src/memory/pmm.c \
               tests/kernel/test_task.c src/kernel/task.c \
               tests/kernel/test_task_iter.c src/kernel/task_iter.c \
               tests/kernel/test_task_stats.c \
               tests/kernel/test_process_iter.c src/kernel/process.c src/kernel/process_iter.c src/memory/vmm_regions.c \
               tests/kernel/test_process_destroy.c \
               tests/kernel/test_vmm_anon_regions.c \
               tests/kernel/test_context_switch.c src/kernel/scheduler.c \
               tests/kernel/test_syscall_msr.c \
               tests/kernel/test_fault_classify.c src/arch/x86_64/fault_classify.c \
               tests/kernel/test_pmm_refcount.c src/memory/pmm_refcount.c \
               tests/kernel/test_vmm_cow.c src/memory/vmm_cow.c \
               tests/kernel/test_tss_layout.c src/arch/x86_64/tss.c \
               tests/kernel/test_cpu_local.c src/arch/x86_64/cpu/cpu_local.c \
               tests/kernel/test_enter_user_mode.c src/arch/x86_64/process_user_mode.c \
               tests/kernel/test_pipe.c src/kernel/pipe.c \
               tests/kernel/test_op_budget.c src/util/op_budget.c \
               tests/kernel/test_syscall_pipe_priority.c src/kernel/syscall.c \
               tests/kernel/test_stdin_buf.c src/kernel/stdin_buf.c \
               tests/kernel/test_process_current_dynamic.c \
               src/util/kstring.c \
               \
               tests/kernel/linux_compat/test_linux_clock.c src/kernel/linux_compat/linux_clock.c \
               tests/kernel/linux_compat/test_linux_syscall.c src/kernel/linux_compat/linux_syscall.c \
               tests/kernel/linux_compat/test_linux_random.c src/kernel/linux_compat/linux_random.c \
               tests/kernel/linux_compat/test_linux_devfs.c src/kernel/linux_compat/linux_devfs.c \
               tests/kernel/linux_compat/test_linux_process.c src/kernel/linux_compat/linux_process.c \
               tests/kernel/linux_compat/test_linux_cpuinfo.c src/kernel/linux_compat/linux_cpuinfo.c \
               tests/kernel/linux_compat/test_linux_proc.c src/kernel/linux_compat/linux_proc.c \
               tests/kernel/linux_compat/test_linux_fd.c src/kernel/linux_compat/linux_fd.c \
               tests/kernel/linux_compat/test_linux_mmap.c src/kernel/linux_compat/linux_mmap.c \
               tests/kernel/linux_compat/test_linux_futex.c src/kernel/linux_compat/linux_futex.c \
               tests/kernel/linux_compat/test_linux_eventfd.c src/kernel/linux_compat/linux_eventfd.c \
               tests/kernel/linux_compat/test_linux_net.c src/kernel/linux_compat/linux_net.c \
               tests/kernel/linux_compat/test_linux_epoll.c src/kernel/linux_compat/linux_epoll.c \
               tests/kernel/linux_compat/test_linux_signal.c src/kernel/linux_compat/linux_signal.c \
               tests/kernel/linux_compat/test_linux_memfd.c src/kernel/linux_compat/linux_memfd.c \
               tests/kernel/linux_compat/test_linux_inotify.c src/kernel/linux_compat/linux_inotify.c \
               tests/kernel/linux_compat/test_linux_clone.c src/kernel/linux_compat/linux_clone.c \
               tests/kernel/linux_compat/test_linux_shm.c src/kernel/linux_compat/linux_shm.c \
               tests/kernel/linux_compat/test_linux_vfs.c src/kernel/linux_compat/linux_vfs.c \
               tests/kernel/linux_compat/test_linux_vfs_router.c tests/kernel/linux_compat/test_linux_vfs_router_specialfd.c src/kernel/linux_compat/linux_vfs_router.c \
               tests/kernel/linux_compat/test_linux_procfs.c src/kernel/linux_compat/linux_procfs.c \
               tests/kernel/linux_compat/test_linux_tmpfs.c src/kernel/linux_compat/linux_tmpfs.c \
               tests/kernel/linux_compat/test_linux_brk.c src/kernel/linux_compat/linux_brk.c \
               tests/kernel/linux_compat/test_linux_arch_prctl.c src/kernel/linux_compat/linux_arch_prctl.c \
               tests/kernel/linux_compat/test_linux_exit.c src/kernel/linux_compat/linux_exit.c \
               tests/kernel/linux_compat/test_linux_ioctl.c src/kernel/linux_compat/linux_ioctl.c \
               tests/kernel/linux_compat/test_linux_fcntl.c src/kernel/linux_compat/linux_fcntl.c \
               tests/kernel/linux_compat/test_linux_io.c src/kernel/linux_compat/linux_io.c \
               tests/kernel/linux_compat/test_linux_stat.c src/kernel/linux_compat/linux_stat.c \
               tests/kernel/linux_compat/test_linux_path.c src/kernel/linux_compat/linux_path.c \
               tests/kernel/linux_compat/test_linux_statx.c src/kernel/linux_compat/linux_statx.c \
               tests/kernel/linux_compat/test_linux_dirent.c src/kernel/linux_compat/linux_dirent.c \
               tests/kernel/linux_compat/test_linux_at.c src/kernel/linux_compat/linux_at.c \
               tests/kernel/linux_compat/test_linux_dup.c src/kernel/linux_compat/linux_dup.c \
               tests/kernel/linux_compat/test_linux_umask.c src/kernel/linux_compat/linux_umask.c \
               tests/kernel/linux_compat/test_linux_wait.c src/kernel/linux_compat/linux_wait.c \
               tests/kernel/linux_compat/test_linux_kill.c src/kernel/linux_compat/linux_kill.c \
               tests/kernel/linux_compat/test_linux_trunc.c src/kernel/linux_compat/linux_trunc.c \
               tests/kernel/linux_compat/test_linux_sysinfo.c src/kernel/linux_compat/linux_sysinfo.c \
               tests/kernel/linux_compat/test_linux_priority.c src/kernel/linux_compat/linux_priority.c \
               tests/kernel/linux_compat/test_linux_pgrp.c src/kernel/linux_compat/linux_pgrp.c \
               tests/kernel/linux_compat/test_linux_fs_mut.c src/kernel/linux_compat/linux_fs_mut.c \
               tests/kernel/linux_compat/test_linux_mlock.c src/kernel/linux_compat/linux_mlock.c \
               tests/kernel/linux_compat/test_linux_creds.c src/kernel/linux_compat/linux_creds.c \
               tests/kernel/linux_compat/test_linux_fs_meta.c src/kernel/linux_compat/linux_fs_meta.c \
               tests/kernel/linux_compat/test_linux_link.c src/kernel/linux_compat/linux_link.c \
               tests/kernel/linux_compat/test_linux_sync.c src/kernel/linux_compat/linux_sync.c \
               tests/kernel/linux_compat/test_linux_utime.c src/kernel/linux_compat/linux_utime.c \
               tests/kernel/linux_compat/test_linux_setid.c src/kernel/linux_compat/linux_setid.c \
               tests/kernel/linux_compat/test_linux_chdir.c src/kernel/linux_compat/linux_chdir.c \
               tests/kernel/linux_compat/test_linux_xattr.c src/kernel/linux_compat/linux_xattr.c \
               tests/kernel/linux_compat/test_linux_statfs.c src/kernel/linux_compat/linux_statfs.c \
               tests/kernel/linux_compat/test_linux_advise.c src/kernel/linux_compat/linux_advise.c \
               tests/kernel/linux_compat/test_linux_rlimit_legacy.c src/kernel/linux_compat/linux_rlimit_legacy.c \
               tests/kernel/linux_compat/test_linux_caps.c src/kernel/linux_compat/linux_caps.c \
               tests/kernel/linux_compat/test_linux_itimer.c src/kernel/linux_compat/linux_itimer.c \
               tests/kernel/linux_compat/test_linux_lock.c src/kernel/linux_compat/linux_lock.c \
               tests/kernel/linux_compat/test_linux_sched_prio.c src/kernel/linux_compat/linux_sched_prio.c \
               tests/kernel/linux_compat/test_linux_posix_timer.c src/kernel/linux_compat/linux_posix_timer.c \
               tests/kernel/linux_compat/test_linux_time_legacy.c src/kernel/linux_compat/linux_time_legacy.c \
               tests/kernel/linux_compat/test_linux_sandbox.c src/kernel/linux_compat/linux_sandbox.c \
               tests/kernel/linux_compat/test_linux_mincore.c src/kernel/linux_compat/linux_mincore.c \
               tests/kernel/linux_compat/test_linux_numa.c src/kernel/linux_compat/linux_numa.c \
               tests/kernel/linux_compat/test_linux_settod.c src/kernel/linux_compat/linux_settod.c \
               tests/kernel/linux_compat/test_linux_jit_aux.c src/kernel/linux_compat/linux_jit_aux.c \
               tests/kernel/linux_compat/test_linux_namespace.c src/kernel/linux_compat/linux_namespace.c \
               tests/kernel/linux_compat/test_linux_exec_ext.c src/kernel/linux_compat/linux_exec_ext.c \
               tests/kernel/linux_compat/test_linux_pipe_zero.c src/kernel/linux_compat/linux_pipe_zero.c \
               tests/kernel/linux_compat/test_linux_proc_vm.c src/kernel/linux_compat/linux_proc_vm.c \
               tests/kernel/linux_compat/test_linux_openat2.c src/kernel/linux_compat/linux_openat2.c \
               tests/kernel/linux_compat/test_linux_pkey.c src/kernel/linux_compat/linux_pkey.c \
               tests/kernel/linux_compat/test_linux_landlock.c src/kernel/linux_compat/linux_landlock.c \
               tests/kernel/linux_compat/test_linux_seccomp.c src/kernel/linux_compat/linux_seccomp.c \
               tests/kernel/linux_compat/test_linux_fanotify.c src/kernel/linux_compat/linux_fanotify.c \
               tests/kernel/linux_compat/test_linux_io_uring.c src/kernel/linux_compat/linux_io_uring.c \
               tests/kernel/linux_compat/test_linux_modern_misc.c src/kernel/linux_compat/linux_modern_misc.c \
               \
               tests/services/test_service_manager.c src/services/service_manager.c \
               tests/services/test_service_boot_policy.c src/services/service_boot_policy.c \
               tests/services/test_service_runner.c src/services/service_runner.c \
               tests/services/test_work_queue.c src/core/work_queue.c \
               tests/services/test_update_agent.c src/services/update_agent.c src/services/update_agent_parse.c src/services/update_agent_apply.c src/services/update_agent_prepare.c \
               tests/services/test_update_transact.c src/services/update_agent_transact.c \
               tests/services/test_capypkg.c src/services/capypkg/capypkg_state.c src/services/capypkg/capypkg_manifest.c src/services/capypkg/capypkg_repo.c src/services/capypkg/capypkg_install.c \
               tests/services/test_install_profile.c src/services/install_profile.c \
               \
               tests/apps/test_hello_program.c \
               tests/apps/test_embedded_progs.c src/kernel/embedded_progs.c \
               \
               tests/lang/test_localization.c src/lang/localization.c \
               \
               third_party/tinf/tinflate.c third_party/tinf/tinfgzip.c third_party/tinf/tinfzlib.c third_party/tinf/adler32.c third_party/tinf/crc32.c

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

# Quick iteration target: builds and runs ONLY the capypkg unit tests
# against the in-tree adapter sources, without dragging in the full
# host test aggregate. Useful during package adapter development.
TEST_CAPYPKG_BIN := $(BUILD)/tests/capypkg_tests
TEST_CAPYPKG_SRCS := \
	tests/services/test_capypkg.c \
	src/services/capypkg/capypkg_state.c \
	src/services/capypkg/capypkg_manifest.c \
	src/services/capypkg/capypkg_repo.c \
	src/services/capypkg/capypkg_install.c \
	src/security/sha256.c \
	src/kernel/log/klog.c
TEST_CAPYPKG_MAIN := $(BUILD)/tests/capypkg_main.c

.PHONY: test-capypkg
test-capypkg: $(TEST_CAPYPKG_BIN)
	@echo "Executando testes unitarios capypkg..."
	$(TEST_CAPYPKG_BIN)
	@echo "[ok] capypkg unit tests passed."

$(TEST_CAPYPKG_BIN): $(TEST_CAPYPKG_SRCS) | $(BUILD)
	@mkdir -p $(BUILD)/tests
	@printf '#include <stdint.h>\nuint64_t pit_ticks(void){static uint64_t t=0;return ++t;}\nint run_capypkg_tests(void);\nint main(void){return run_capypkg_tests();}\n' > $(TEST_CAPYPKG_MAIN)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(TEST_CAPYPKG_SRCS) $(TEST_CAPYPKG_MAIN)

.PHONY: modules-index
# modules-index: aggregate per-repo capypkg manifests (produced by
# `make package` in each sibling repository) into a single index file
# the CapyOS in-tree adapter consumes. Output:
#   build/capypkg/modules-index.txt
# Override --workspace if the sibling repos do not live under ../.
modules-index:
	@echo "Agregando manifests dos repositorios externos..."
	python3 tools/scripts/build_modules_index.py \
	  --output build/capypkg/modules-index.txt

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
	$(MAKE) verify-release-signature-selftest
	$(MAKE) smoke-marker-policy-selftest
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

smoke-x64-vmware-dhcp: all64 iso-uefi manifest64
	@echo "Executando smoke test VMware+E1000 DHCP..."
	python3 tools/scripts/smoke_x64_vmware.py $(SMOKE_X64_VMWARE_ARGS)

smoke-x64-vmware-gui-session: all64 iso-uefi manifest64
	@echo "Executando smoke test VMware+E1000 gui-session..."
	python3 tools/scripts/smoke_x64_vmware.py \
		--marker "[net] DHCP: lease acquired." \
		--marker "[smoke] gui-session ready" \
		$(SMOKE_X64_VMWARE_ARGS)

smoke-x64-vmware-mouse-events: all64 iso-uefi manifest64
	@echo "Executando smoke test VMware+E1000 mouse-events..."
	python3 tools/scripts/smoke_x64_vmware.py \
		--marker "[net] DHCP: lease acquired." \
		--marker "[smoke] gui-session ready" \
		--marker "[smoke] mouse-events ready" \
		$(SMOKE_X64_VMWARE_ARGS)

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

# Sessao 6 (2026-05-05): target legado de spawn do navegador erradicado.
# Sera reaberto somente por adaptador versionado na etapa correta, com
# seu proprio smoke harness.

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
.PHONY: all all64 iso-uefi manifest64 release-checksums verify-release-checksums disk-gpt provision-vhd legacy-disabled clean test layout-audit layout-audit-report version-audit boot-perf-baseline boot-perf-baseline-selftest check-toolchain release-check smoke-x64-cli smoke-x64-boot-perf smoke-x64-vmware-dhcp smoke-x64-vmware-gui-session smoke-x64-vmware-mouse-events smoke-x64-cli-nvme smoke-x64-hello-user smoke-x64-hello-segfault smoke-x64-preemptive smoke-x64-preemptive-demo smoke-x64-preemptive-user smoke-x64-preemptive-user-2task smoke-x64-preemptive-all smoke-x64-fork-cow smoke-x64-exec smoke-x64-fork-wait smoke-x64-pipe smoke-x64-fork-crash smoke-x64-capysh smoke-x64-iso inspect-disk capylibc hello-elf hello-blob exectarget-elf exectarget-blob capysh-elf capysh-blob

-include $(CAPYOS64_DEPS) $(UEFI_LOADER_DEPS)
