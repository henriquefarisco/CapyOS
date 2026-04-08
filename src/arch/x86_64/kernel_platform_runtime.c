#include "arch/x86_64/kernel_platform_runtime.h"

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/hyperv_input_gate.h"
#include "arch/x86_64/native_runtime_gate.h"
#include "arch/x86_64/platform_timer.h"
#include "arch/x86_64/storage_runtime.h"
#include "arch/x86_64/timebase.h"
#include "core/system_init.h"
#include "core/version.h"
#include "drivers/storage/storvsc_runtime.h"

static int diag_io_ready(const struct x64_platform_diag_io *io) {
  return io && io->print && io->print_hex64 && io->print_dec_u32 && io->putc;
}

static int handoff_has_runtime_flag(const struct boot_handoff *handoff,
                                    uint32_t flag) {
  return (x64_kernel_handoff_runtime_flags(handoff) & flag) != 0U;
}

static int handoff_uses_legacy_runtime_contract(
    const struct boot_handoff *handoff) {
  return handoff && handoff->version < 5;
}

static int handoff_hybrid_boot(const struct boot_handoff *handoff) {
  return handoff_has_runtime_flag(handoff, BOOT_HANDOFF_RUNTIME_HYBRID_BOOT);
}

uint32_t x64_kernel_handoff_runtime_flags(const struct boot_handoff *handoff) {
  return handoff ? handoff->runtime_flags : 0U;
}

int x64_kernel_handoff_boot_services_active(const struct boot_handoff *handoff) {
  if (!handoff || handoff->efi_system_table == 0) {
    return 0;
  }
  if (handoff_has_runtime_flag(handoff,
                               BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE)) {
    return 1;
  }
  return handoff_uses_legacy_runtime_contract(handoff);
}

int x64_kernel_handoff_has_firmware_input(const struct boot_handoff *handoff) {
  if (!handoff || handoff->efi_system_table == 0) {
    return 0;
  }
  if (handoff_has_runtime_flag(handoff, BOOT_HANDOFF_RUNTIME_FIRMWARE_INPUT)) {
    return 1;
  }
  return handoff_uses_legacy_runtime_contract(handoff);
}

int x64_kernel_handoff_has_firmware_block_io(
    const struct boot_handoff *handoff) {
  if (!handoff || handoff->efi_block_io == 0) {
    return 0;
  }
  if (handoff_has_runtime_flag(handoff,
                               BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO)) {
    return 1;
  }
  return handoff_uses_legacy_runtime_contract(handoff);
}

int x64_kernel_handoff_has_exit_boot_services_contract(
    const struct boot_handoff *handoff) {
  return handoff && handoff->version >= 6 && handoff->efi_system_table != 0 &&
         handoff->efi_image_handle != 0 && handoff->efi_map_key != 0 &&
         handoff->memmap != 0 && handoff->memmap_size != 0 &&
         handoff->memmap_capacity != 0;
}

const char *x64_kernel_input_ps2_fallback_state(
    const struct x64_input_runtime *runtime) {
  if (!runtime || !runtime->has_ps2) {
    return "off";
  }
  return runtime->ps2_fallback_parked ? "parked" : "active";
}

void x64_kernel_print_platform_runtime_mode(
    const struct boot_handoff *handoff, const struct x64_platform_diag_io *io) {
  uint32_t flags = x64_kernel_handoff_runtime_flags(handoff);

  if (!diag_io_ready(io)) {
    return;
  }

  io->print("[boot] Runtime mode: ");
  if (handoff_hybrid_boot(handoff)) {
    io->print("hybrid");
  } else if (handoff_uses_legacy_runtime_contract(handoff)) {
    io->print("legacy-implicit");
  } else {
    io->print("native");
  }
  io->print(" flags=");
  io->print_hex64((uint64_t)flags);
  io->putc('\n');
  io->print("[boot] Build: ");
  io->print(CAPYOS_VERSION_FULL);
  io->print(" feature=");
  io->print(CAPYOS_FEATURE_HYPERV_RUNTIME);
  io->print(" diag=");
  io->print(CAPYOS_FEATURE_NETWORK_DIAG);
  io->putc('\n');
  io->print("[boot] Boot services: ");
  io->print(x64_kernel_handoff_boot_services_active(handoff) ? "ativos"
                                                             : "inativos");
  io->putc('\n');
  io->print("[boot] Firmware input: ");
  io->print(x64_kernel_handoff_has_firmware_input(handoff) ? "ativo"
                                                           : "inativo");
  io->print("  Firmware block I/O: ");
  io->print(x64_kernel_handoff_has_firmware_block_io(handoff) ? "ativo"
                                                              : "inativo");
  io->putc('\n');
  io->print("[boot] ExitBootServices contract: ");
  io->print(x64_kernel_handoff_has_exit_boot_services_contract(handoff)
                ? "presente"
                : "ausente");
  if (x64_kernel_handoff_has_exit_boot_services_contract(handoff)) {
    io->print(" image=");
    io->print_hex64(handoff->efi_image_handle);
    io->print(" map_key=");
    io->print_hex64(handoff->efi_map_key);
  }
  io->putc('\n');
}

void x64_kernel_print_platform_tables_status(
    const struct x64_platform_diag_io *io) {
  if (!diag_io_ready(io)) {
    return;
  }
  io->print("[cpu] Native GDT/IDT: ");
  io->print(x64_platform_tables_status());
  if (x64_platform_tables_active()) {
    io->print(" (faults/IRQs basicos armados)");
  } else {
    io->print(" (mantendo descritores do firmware)");
  }
  io->putc('\n');
}

void x64_kernel_print_platform_timer_status(
    const struct x64_platform_diag_io *io) {
  if (!diag_io_ready(io)) {
    return;
  }
  io->print("[irq] Timer path: ");
  io->print(x64_platform_timer_status());
  io->print(" @ ");
  io->print_dec_u32(x64_platform_timer_hz());
  io->print(" Hz");
  if (x64_platform_timer_active()) {
    io->print(" (IRQ0 ativo)");
  } else {
    io->print(" (fallback monotonic)");
  }
  io->putc('\n');
}

void x64_kernel_print_input_runtime_status(
    const struct x64_input_runtime *runtime,
    const struct x64_platform_diag_io *io) {
  struct system_runtime_platform platform;

  if (!runtime || !diag_io_ready(io)) {
    return;
  }
  system_runtime_platform_get(&platform);
  io->print("[input] Mode: ");
  io->print(x64_input_priority_mode(runtime));
  io->print(" primary=");
  io->print(x64_input_primary_backend_name(runtime));
  io->print(" last=");
  io->print(x64_input_last_backend_name(runtime));
  io->print(" native=");
  io->print(x64_input_has_native_backend(runtime) ? "ready" : "no");
  io->print(" firmware=");
  io->print(x64_input_firmware_state(runtime));
  io->print(" ps2=");
  io->print(x64_kernel_input_ps2_fallback_state(runtime));
  io->print(" hyperv=");
  io->print(x64_input_hyperv_state(runtime));
  io->print(" gate=");
  io->print(system_hyperv_input_gate_label(platform.hyperv_input_gate));
  io->putc('\n');
  if (runtime->hyperv_preferred || runtime->hyperv_event_count ||
      runtime->hyperv_promotion_attempts || runtime->hyperv_prepare_attempts ||
      runtime->hyperv_degrade_count) {
    io->print("[input] Hyper-V metrics: pref=");
    io->print(runtime->hyperv_preferred ? "yes" : "no");
    io->print(" confirmed=");
    io->print(runtime->hyperv_confirmed ? "yes" : "no");
    io->print(" prepared=");
    io->print(runtime->hyperv_transport_prepared ? "yes" : "no");
    io->print(" events=");
    io->print_dec_u32(runtime->hyperv_event_count);
    io->print(" prepare_attempts=");
    io->print_dec_u32(runtime->hyperv_prepare_attempts);
    io->print(" attempts=");
    io->print_dec_u32(runtime->hyperv_promotion_attempts);
    io->print(" degrades=");
    io->print_dec_u32(runtime->hyperv_degrade_count);
    io->putc('\n');
  }
}

void x64_kernel_print_storage_runtime_status(
    const struct boot_handoff *handoff, const struct x64_platform_diag_io *io) {
  struct storvsc_controller_status storvsc_status;
  const char *hyperv_bus = "disconnected";
  const char *hyperv_block = "n/a";

  if (!diag_io_ready(io)) {
    return;
  }

  io->print("[storage] Runtime backend: ");
  io->print(x64_storage_runtime_backend_name());
  io->print(" data=");
  io->print(x64_storage_runtime_data_path());
  io->print(" firmware=");
  io->print(x64_storage_runtime_uses_firmware() ? "on" : "off");
  io->print(" native=");
  io->print(x64_storage_runtime_native_candidate_name());
  io->print("/");
  io->print(x64_storage_runtime_native_data_path());
  io->print(" ready=");
  io->print(x64_storage_runtime_has_native_candidate() ? "yes" : "no");
  io->putc('\n');
  if (x64_storage_runtime_hyperv_controller_status(&storvsc_status) == 0) {
    hyperv_bus = hyperv_vmbus_stage_label(storvsc_status.vmbus_stage);
    hyperv_block = x64_storage_runtime_hyperv_block_reason(
        x64_kernel_handoff_boot_services_active(handoff));
    io->print("[storage] Hyper-V StorVSC: bus=");
    io->print(hyperv_bus);
    io->print(" cache=");
    io->print(x64_storage_runtime_hyperv_offer_cached() ? "ready" : "miss");
    io->print(" stage=");
    io->print(hyperv_vmbus_stage_label(storvsc_status.stage));
    io->print(" controller=");
    io->print(storvsc_status.enabled ? "enabled" : "disabled");
    io->print(" phase=");
    io->print(x64_storage_runtime_hyperv_phase_name());
    io->print(" gate=");
    io->print(x64_storage_runtime_hyperv_gate_label(
        x64_kernel_handoff_boot_services_active(handoff) ? 1 : 0));
    io->print(" next=");
    io->print(x64_storage_runtime_hyperv_next_action_label(
        x64_kernel_handoff_boot_services_active(handoff) ? 1 : 0));
    io->print(" last=");
    io->print(x64_storage_runtime_hyperv_last_action_label(
        x64_kernel_handoff_boot_services_active(handoff) ? 1 : 0));
    io->print(" block=");
    io->print(hyperv_block);
    io->print(" attempts=");
    io->print_dec_u32(x64_storage_runtime_hyperv_attempt_count());
    io->print(" changes=");
    io->print_dec_u32(x64_storage_runtime_hyperv_change_count());
    io->print(" openid=");
    io->print_dec_u32(storvsc_status.open_id);
    io->print(" gpadl=");
    io->print_dec_u32(storvsc_status.gpadl_handle);
    io->print(" sring=");
    io->print_dec_u32(storvsc_status.send_ring_size);
    io->print(" rring=");
    io->print_dec_u32(storvsc_status.recv_ring_size);
    io->print(" gpadl_status=");
    io->print_dec_u32(storvsc_status.last_gpadl_status);
    io->print(" open_status=");
    io->print_dec_u32(storvsc_status.last_open_status);
    io->print(" open_msg=");
    io->print_dec_u32(storvsc_status.last_open_msgtype);
    io->print(" open_relid=");
    io->print_dec_u32(storvsc_status.last_open_relid);
    io->print(" open_obs=");
    io->print_dec_u32(storvsc_status.last_open_observed_id);
    io->print(" vp=");
    io->print_dec_u32(storvsc_status.last_target_vcpu);
    io->print(" down=");
    io->print_dec_u32(storvsc_status.last_downstream_offset);
    io->print(" retries=");
    io->print_dec_u32(storvsc_status.last_retry_count);
    if (storvsc_status.offer_ready) {
      io->print(" relid=");
      io->print_dec_u32(storvsc_status.offer.child_relid);
      io->print(" conn=");
      io->print_dec_u32(storvsc_status.offer.connection_id);
    }
    if (storvsc_status.last_error != 0) {
      io->print(" last_error=");
      io->print_dec_u32((uint32_t)storvsc_status.last_error);
    }
    io->putc('\n');
    if (storvsc_status.phase == STORVSC_RUNTIME_CONTROL ||
        storvsc_status.phase == STORVSC_RUNTIME_FAILED ||
        storvsc_status.control_wait_budget != 0u ||
        storvsc_status.last_control.read_rc != 0 ||
        storvsc_status.last_control.extract_rc != 0 ||
        storvsc_status.last_control.parse_rc != 0 ||
        storvsc_status.last_control.packet_len != 0u ||
        storvsc_status.last_control.operation != 0u ||
        storvsc_status.last_control.status != 0u) {
      io->print("[storvsc] ctrl recv=");
      io->print_hex64((uint64_t)(uint32_t)storvsc_status.last_control.read_rc);
      io->print(" extract=");
      io->print_hex64(
          (uint64_t)(uint32_t)storvsc_status.last_control.extract_rc);
      io->print(" parse=");
      io->print_hex64((uint64_t)(uint32_t)storvsc_status.last_control.parse_rc);
      io->print(" len=");
      io->print_dec_u32(storvsc_status.last_control.packet_len);
      io->print(" payload=");
      io->print_dec_u32(storvsc_status.last_control.payload_len);
      io->print(" desc=");
      io->print_dec_u32(storvsc_status.last_control.packet_type);
      io->print(" flags=");
      io->print_dec_u32(storvsc_status.last_control.packet_flags);
      io->print(" op=");
      io->print_dec_u32(storvsc_status.last_control.operation);
      io->print(" status=");
      io->print_dec_u32(storvsc_status.last_control.status);
      io->print(" trans=");
      io->print_hex64(storvsc_status.last_control.trans_id);
      io->print(" expected=");
      io->print_dec_u32(storvsc_status.control_expected_operation);
      io->print(" expected_trans=");
      io->print_hex64(STORVSC_CONTROL_TRANS_ID);
      io->print(" waits=");
      io->print_dec_u32(storvsc_status.control_wait_budget);
      io->putc('\n');
    }
  }
}

void x64_kernel_print_native_runtime_gate_status(
    const struct boot_handoff *handoff,
    const struct x64_input_runtime *runtime, int exit_attempted, int exit_done,
    uint64_t exit_status, const struct x64_platform_diag_io *io) {
  struct x64_native_runtime_gate_status gate;

  if (!diag_io_ready(io)) {
    return;
  }

  x64_native_runtime_gate_eval(handoff, runtime, exit_attempted, exit_done,
                               exit_status, &gate);
  io->print("[boot] Native runtime gate: ");
  io->print(system_exit_boot_services_gate_label(gate.gate));
  if (gate.last_status != 0u) {
    io->print(" status=");
    io->print_hex64(gate.last_status);
  }
  io->putc('\n');
}

void x64_kernel_print_cmd_info(const struct boot_handoff *handoff, int rsdp_valid,
                               const struct x64_input_runtime *runtime,
                               const struct x64_platform_diag_io *io) {
  if (!runtime || !diag_io_ready(io)) {
    return;
  }

  io->print("handoff.magic=");
  io->print_hex64(handoff ? (uint64_t)handoff->magic : 0);
  io->print(" ver=");
  io->print_hex64(handoff ? (uint64_t)handoff->version : 0);
  io->putc('\n');

  io->print("rsdp=");
  io->print_hex64(handoff ? handoff->rsdp : 0);
  io->print(" valid=");
  io->print_hex64(rsdp_valid ? 1 : 0);
  io->putc('\n');

  io->print("fb.base=");
  io->print_hex64(handoff ? handoff->fb.base : 0);
  io->print(" w=");
  io->print_hex64(handoff ? (uint64_t)handoff->fb.width : 0);
  io->print(" h=");
  io->print_hex64(handoff ? (uint64_t)handoff->fb.height : 0);
  io->print(" pitch=");
  io->print_hex64(handoff ? (uint64_t)handoff->fb.pitch : 0);
  io->putc('\n');

  io->print("memmap.ptr=");
  io->print_hex64(handoff ? handoff->memmap : 0);
  io->print(" desc=");
  io->print_hex64(handoff ? (uint64_t)handoff->memmap_desc_size : 0);
  io->print(" entries=");
  io->print_hex64(handoff ? (uint64_t)handoff->memmap_entries : 0);
  io->putc('\n');

  io->print("efi.blockio=");
  io->print_hex64(handoff ? handoff->efi_block_io : 0);
  io->print(" blk=");
  io->print_hex64(handoff ? (uint64_t)handoff->efi_block_size : 0);
  io->print(" last=");
  io->print_hex64(handoff ? handoff->efi_disk_last_lba : 0);
  io->print(" data.start=");
  io->print_hex64(handoff ? handoff->data_lba_start : 0);
  io->print(" data.count=");
  io->print_hex64(handoff ? handoff->data_lba_count : 0);
  io->putc('\n');

  io->print("efi.blockio.raw=");
  io->print_hex64(handoff ? handoff->efi_block_io_raw : 0);
  io->print(" last.raw=");
  io->print_hex64(handoff ? handoff->efi_disk_last_lba_raw : 0);
  io->print(" data.start.raw=");
  io->print_hex64(handoff ? handoff->data_lba_start_raw : 0);
  io->print(" data.count.raw=");
  io->print_hex64(handoff ? handoff->data_lba_count_raw : 0);
  io->putc('\n');

  io->print("runtime.flags=");
  io->print_hex64(x64_kernel_handoff_runtime_flags(handoff));
  io->print(" bootsvc=");
  io->print_hex64(x64_kernel_handoff_boot_services_active(handoff) ? 1 : 0);
  io->print(" gdt.idt=");
  io->print(x64_platform_tables_status());
  io->print(" timer=");
  io->print(x64_platform_timer_status());
  io->putc('\n');

  io->print("input.mode=");
  io->print(x64_input_priority_mode(runtime));
  io->print(" primary=");
  io->print(x64_input_primary_backend_name(runtime));
  io->print(" last=");
  io->print(x64_input_last_backend_name(runtime));
  io->print(" ps2=");
  io->print(x64_kernel_input_ps2_fallback_state(runtime));
  io->print(" hyperv=");
  io->print(x64_input_hyperv_state(runtime));
  io->putc('\n');

  io->print("input.hyperv.pref=");
  io->print(runtime->hyperv_preferred ? "1" : "0");
  io->print(" confirmed=");
  io->print(runtime->hyperv_confirmed ? "1" : "0");
  io->print(" events=");
  io->print_dec_u32(runtime->hyperv_event_count);
  io->print(" attempts=");
  io->print_dec_u32(runtime->hyperv_promotion_attempts);
  io->print(" degrades=");
  io->print_dec_u32(runtime->hyperv_degrade_count);
  io->putc('\n');
}

void x64_kernel_print_active_efi_runtime_trace(
    const struct x64_platform_diag_io *io) {
  const struct efi_block_device *efi_disk = x64_storage_runtime_active_efi();

  if (!diag_io_ready(io)) {
    return;
  }

  io->print(" efi_status=");
  io->print_hex64(efi_disk ? efi_disk->ctx.last_status : 0);
  io->print(" efi_code=");
  io->print_dec_u32(
      efi_disk ? (uint32_t)(efi_disk->ctx.last_status & 0xFFFFFFFFULL) : 0);
  io->print(" lba=");
  io->print_dec_u32(efi_disk ? efi_disk->ctx.last_block_no : 0);
  io->print(" media=");
  io->print_dec_u32(efi_disk ? efi_disk->ctx.last_media_id : 0);
  io->print(" efi_last_err=");
  io->print_hex64(efi_disk ? efi_disk->ctx.last_error_status : 0);
  io->print(" err_lba=");
  io->print_dec_u32(efi_disk ? efi_disk->ctx.last_error_block_no : 0);
  io->print(" err_media=");
  io->print_dec_u32(efi_disk ? efi_disk->ctx.last_error_media_id : 0);
  io->print(" err_count=");
  io->print_dec_u32(efi_disk ? efi_disk->ctx.error_count : 0);
}

void x64_kernel_update_system_runtime_platform_status(
    const struct boot_handoff *handoff,
    const struct x64_input_runtime *runtime, int exit_attempted,
    int exit_done, uint64_t exit_status) {
  struct system_runtime_platform status;
  struct storvsc_controller_status storvsc_status;
  struct x64_native_runtime_gate_status gate;
  int storvsc_ready = 0;

  if (x64_storage_runtime_hyperv_controller_status(&storvsc_status) == 0 &&
      storvsc_status.ready) {
    storvsc_ready = 1;
  }

  status.boot_services_active =
      x64_kernel_handoff_boot_services_active(handoff) ? 1 : 0;
  status.firmware_input_active =
      x64_kernel_handoff_has_firmware_input(handoff) ? 1 : 0;
  status.firmware_block_io_active = x64_storage_runtime_uses_firmware() ? 1 : 0;
  status.hybrid_boot = handoff_hybrid_boot(handoff) ? 1 : 0;
  status.native_input_ready =
      x64_input_has_native_backend(runtime) ? 1 : 0;
  status.native_storage_ready =
      (x64_storage_runtime_has_device() && !x64_storage_runtime_uses_firmware())
          ? 1
          : 0;
  status.synthetic_storage_ready = storvsc_ready;
  status.hyperv_vmbus_stage = vmbus_runtime_stage();
  x64_native_runtime_gate_eval(handoff, runtime, exit_attempted, exit_done,
                               exit_status, &gate);
  status.hyperv_input_gate = x64_hyperv_input_gate_state(
      runtime, status.boot_services_active);
  status.exit_boot_services_gate = gate.gate;
  status.exit_boot_services_status = gate.last_status;
  system_runtime_platform_set(&status);
}

void x64_kernel_print_timebase_status(const struct x64_platform_diag_io *io) {
  uint64_t hz = x64_timebase_hz();
  uint32_t mhz = (uint32_t)(hz / 1000000ULL);

  if (!diag_io_ready(io)) {
    return;
  }
  io->print("[time] Source: ");
  io->print(x64_timebase_source());
  io->print(" @ ");
  io->print_dec_u32(mhz);
  io->print(" MHz\n");
}
