#include "internal/network_internal.h"

#include "core/version.h"

static void shell_print_signed_number(int32_t value) {
  uint32_t magnitude = 0u;
  if (value < 0) {
    shell_print("-");
    magnitude = (uint32_t)(-(value + 1)) + 1u;
    shell_print_number(magnitude);
    return;
  }
  shell_print_number((uint32_t)value);
}

static void print_hyperv_runtime_dump(const struct net_stack_status *st) {
  struct system_runtime_platform platform;

  if (!st || st->nic.kind != NET_NIC_KIND_HYPERV_NETVSC) {
    return;
  }

  net_cli_hyperv_platform_blocked(&platform);
  shell_print("hyperv.runtime driver=");
  shell_print(net_driver_name(st->nic.kind));
  shell_print(" stage=");
  shell_print(net_cli_hyperv_stage_label(st));
  shell_print(" vmbus=");
  shell_print(net_cli_hyperv_bus_label(st));
  shell_print(" cache=");
  shell_print(net_cli_hyperv_offer_cache_label(st));
  shell_print(" phase=");
  shell_print(net_cli_hyperv_runtime_phase_label(st));
  shell_print(" gate=");
  shell_print(net_cli_hyperv_gate_label(st));
  shell_print(" next=");
  shell_print(net_cli_hyperv_effective_next_label(st, &platform));
  shell_print(" block=");
  shell_print(net_cli_hyperv_block_label(st, &platform));
  shell_newline();
  shell_print("hyperv.build version=");
  shell_print(CAPYOS_VERSION_FULL);
  shell_print(" feature=");
  shell_print(CAPYOS_FEATURE_HYPERV_RUNTIME);
  shell_print(" diag=");
  shell_print(CAPYOS_FEATURE_NETWORK_DIAG);
  shell_newline();

  shell_print("hyperv.refresh attempts=");
  shell_print_number(st->hyperv_refresh_attempts);
  shell_print(" changes=");
  shell_print_number(st->hyperv_refresh_changes);
  shell_print(" last_result=");
  shell_print_signed_number(st->hyperv_last_result);
  shell_print(" last_error=");
  shell_print_signed_number(st->hyperv_last_error);
  shell_newline();

  shell_print("hyperv.platform bootsvc=");
  shell_print(platform.boot_services_active ? "active" : "inactive");
  shell_print(" ebs=");
  shell_print(system_exit_boot_services_gate_label(
      platform.exit_boot_services_gate));
  shell_print(" vmbus=");
  shell_print(hyperv_vmbus_stage_label(platform.hyperv_vmbus_stage));
  shell_print(" storage-fw=");
  shell_print(platform.firmware_block_io_active ? "on" : "off");
  shell_print(" hybrid=");
  shell_print(platform.hybrid_boot ? "yes" : "no");
  shell_print(" input-gate=");
  shell_print(system_hyperv_input_gate_label(platform.hyperv_input_gate));
  shell_print(" input-native=");
  shell_print(platform.native_input_ready ? "yes" : "no");
  shell_print(" storage-native=");
  shell_print(platform.native_storage_ready ? "yes" : "no");
  shell_print(" storage-synth=");
  shell_print(platform.synthetic_storage_ready ? "yes" : "no");
  shell_newline();
}

static void print_storvsc_runtime_dump(void) {
  struct system_runtime_platform platform;
  struct storvsc_controller_status storvsc_status;

  if (x64_storage_runtime_hyperv_controller_status(&storvsc_status) != 0) {
    return;
  }

  system_runtime_platform_get(&platform);
  shell_print("storvsc.runtime bus=");
  shell_print(hyperv_vmbus_stage_label(storvsc_status.vmbus_stage));
  shell_print(" cache=");
  shell_print(x64_storage_runtime_hyperv_offer_cached() ? "ready" : "miss");
  shell_print(" stage=");
  shell_print(hyperv_vmbus_stage_label(storvsc_status.stage));
  shell_print(" controller=");
  shell_print(storvsc_status.enabled ? "enabled" : "disabled");
  shell_print(" phase=");
  shell_print(x64_storage_runtime_hyperv_phase_name());
  shell_print(" gate=");
  shell_print(x64_storage_runtime_hyperv_gate_label(
      platform.boot_services_active));
  shell_print(" next=");
  shell_print(x64_storage_runtime_hyperv_next_action_label(
      platform.boot_services_active));
  shell_print(" block=");
  shell_print(x64_storage_runtime_hyperv_block_reason(
      platform.boot_services_active));
  shell_newline();

  shell_print("storvsc.refresh attempts=");
  shell_print_number(x64_storage_runtime_hyperv_attempt_count());
  shell_print(" changes=");
  shell_print_number(x64_storage_runtime_hyperv_change_count());
  shell_print(" last_action=");
  shell_print(x64_storage_runtime_hyperv_last_action_label(
      platform.boot_services_active));
  shell_print(" last_result=");
  shell_print_signed_number(x64_storage_runtime_hyperv_last_result());
  shell_print(" last_error=");
  shell_print_signed_number(storvsc_status.last_error);
  shell_newline();
}

int net_cmd_status(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-status",
                        net_cli_text(language, NET_HELP_STATUS))) {
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_RUNTIME_UNAVAILABLE));
  return -1;
#else
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }

  char ip[16], mask[16], gw[16], dns[16];
  net_ipv4_format(st.ipv4.addr, ip);
  net_ipv4_format(st.ipv4.mask, mask);
  net_ipv4_format(st.ipv4.gateway, gw);
  net_ipv4_format(st.ipv4.dns, dns);

  shell_print("driver=");
  shell_print(net_driver_name(st.nic.kind));
  shell_print(" mode=");
  shell_print(net_cli_current_mode(ctx));
  shell_print(" detected=");
  shell_print(st.nic.found ? "yes" : "no");
  shell_print(" runtime=");
  shell_print(net_cli_runtime_label(&st));
  shell_print(" ready=");
  shell_print(st.ready ? "yes\n" : "no\n");
  shell_print("dhcp=");
  if (!shell_string_equal(net_cli_current_mode(ctx), "dhcp")) {
    shell_print("off");
  } else if (st.dhcp_lease_acquired) {
    shell_print("lease");
  } else if (st.dhcp_last_error != 0) {
    shell_print("fallback");
  } else {
    shell_print("pending");
  }
  shell_print(" attempts=");
  shell_print_number(st.dhcp_attempts);
  if (st.dhcp_last_error != 0) {
    shell_print(" last_error=");
    shell_print_signed_number(st.dhcp_last_error);
  }
  shell_newline();

  if (st.nic.found) {
    shell_print("pci=");
    shell_print_number(st.nic.bus);
    shell_print(":");
    shell_print_number(st.nic.device);
    shell_print(".");
    shell_print_number(st.nic.function);
    shell_print(" vendor=");
    net_cli_print_hex16(st.nic.vendor_id);
    shell_print(" device=");
    net_cli_print_hex16(st.nic.device_id);
    shell_newline();
    if (st.nic.kind == NET_NIC_KIND_HYPERV_NETVSC &&
        (st.nic.vmbus_relid != 0u || st.nic.vmbus_connection_id != 0u)) {
      shell_print("vmbus=relid:");
      shell_print_number(st.nic.vmbus_relid);
      shell_print(" conn:");
      shell_print_number(st.nic.vmbus_connection_id);
      shell_print(" dedicated=");
      shell_print(st.nic.vmbus_dedicated_interrupt ? "yes\n" : "no\n");
    }
    if (st.nic.kind == NET_NIC_KIND_HYPERV_NETVSC) {
      struct system_runtime_platform platform;
      int platform_blocked = net_cli_hyperv_platform_blocked(&platform);
      shell_print("netvsc=");
      shell_print(net_cli_hyperv_stage_label(&st));
      shell_print(" vmbus=");
      shell_print(net_cli_hyperv_bus_label(&st));
      shell_print(" cache=");
      shell_print(net_cli_hyperv_offer_cache_label(&st));
      shell_print(" controller=");
      shell_print(st.hyperv_runtime_enabled ? "enabled" : "disabled");
      shell_print(" phase=");
      shell_print(net_cli_hyperv_runtime_phase_label(&st));
      shell_print(" next=");
      shell_print(net_cli_hyperv_effective_next_label(&st, &platform));
      shell_print(" gate=");
      shell_print(net_cli_hyperv_gate_label(&st));
      shell_print(" block=");
      shell_print(net_cli_hyperv_block_label(&st, &platform));
      if (st.hyperv_last_error != 0) {
        shell_print(" last_error=");
        shell_print_signed_number(st.hyperv_last_error);
      }
      shell_newline();
      shell_print("platform=");
      shell_print(platform_blocked ? "hybrid" : "native");
      shell_print(" bootsvc=");
      shell_print(platform.boot_services_active ? "active" : "inactive");
      shell_print(" ebs=");
      shell_print(system_exit_boot_services_gate_label(
          platform.exit_boot_services_gate));
      shell_print(" vmbus=");
      shell_print(hyperv_vmbus_stage_label(platform.hyperv_vmbus_stage));
      shell_print(" storage-fw=");
      shell_print(platform.firmware_block_io_active ? "on" : "off");
      shell_print(" input-gate=");
      shell_print(system_hyperv_input_gate_label(platform.hyperv_input_gate));
      shell_print(" input-native=");
      shell_print(platform.native_input_ready ? "yes" : "no");
      shell_print(" storage-native=");
      shell_print(platform.native_storage_ready ? "yes" : "no");
      shell_print(" storage-synth=");
      shell_print(platform.synthetic_storage_ready ? "yes" : "no");
      shell_newline();
      shell_print("build=");
      shell_print(CAPYOS_VERSION_FULL);
      shell_print(" feature=");
      shell_print(CAPYOS_FEATURE_HYPERV_RUNTIME);
      shell_print(" diag=");
      shell_print(CAPYOS_FEATURE_NETWORK_DIAG);
      shell_print(" runtime-native=yes");
      shell_newline();
      {
        struct storvsc_controller_status storvsc_status;
        if (x64_storage_runtime_hyperv_controller_status(&storvsc_status) == 0) {
          shell_print("storvsc=bus:");
          shell_print(hyperv_vmbus_stage_label(storvsc_status.vmbus_stage));
          shell_print(" cache:");
          shell_print(x64_storage_runtime_hyperv_offer_cached() ? "ready"
                                                                : "miss");
          shell_print(" stage:");
          shell_print(hyperv_vmbus_stage_label(storvsc_status.stage));
          shell_print(" controller:");
          shell_print(storvsc_status.enabled ? "enabled" : "disabled");
          shell_print(" phase:");
          shell_print(x64_storage_runtime_hyperv_phase_name());
          shell_print(" gate:");
          shell_print(x64_storage_runtime_hyperv_gate_label(
              platform.boot_services_active));
          shell_print(" next:");
          shell_print(x64_storage_runtime_hyperv_next_action_label(
              platform.boot_services_active));
          shell_print(" block:");
          shell_print(x64_storage_runtime_hyperv_block_reason(
              platform.boot_services_active));
          if (storvsc_status.last_error != 0) {
            shell_print(" last_error=");
            shell_print_signed_number(storvsc_status.last_error);
          }
          shell_newline();
        }
      }
    }
  }

  shell_print("ipv4=");
  shell_print(ip);
  shell_print(" mask=");
  shell_print(mask);
  shell_print(" gw=");
  shell_print(gw);
  shell_print(" dns=");
  shell_print(dns);
  shell_newline();

  shell_print("arp_entries=");
  shell_print_number(st.arp_entries);
  shell_print(" tx=");
  shell_print_number((uint32_t)st.stats.frames_tx);
  shell_print(" rx=");
  shell_print_number((uint32_t)st.stats.frames_rx);
  shell_print(" drop=");
  shell_print_number((uint32_t)st.stats.frames_drop);
  shell_newline();
  return 0;
#endif
}

int net_cmd_dump_runtime(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-dump-runtime",
                        net_cli_text(language, NET_HELP_DUMP))) {
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_RUNTIME_UNAVAILABLE));
  return -1;
#else
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }

  shell_print("runtime.initialized=");
  shell_print(st.initialized ? "yes" : "no");
  shell_print(" ready=");
  shell_print(st.ready ? "yes" : "no");
  shell_print(" driver=");
  shell_print(net_driver_name(st.nic.kind));
  shell_print(" runtime=");
  shell_print(net_cli_runtime_label(&st));
  shell_print(" dhcp=");
  shell_print(st.dhcp_lease_acquired ? "lease" : "none");
  shell_print(" dhcp_attempts=");
  shell_print_number(st.dhcp_attempts);
  shell_print(" dhcp_last_error=");
  shell_print_signed_number(st.dhcp_last_error);
  shell_newline();

  if (st.nic.kind == NET_NIC_KIND_HYPERV_NETVSC) {
    print_hyperv_runtime_dump(&st);
    print_storvsc_runtime_dump();
  } else {
    shell_print("runtime.refresh attempts=");
    shell_print_number(st.hyperv_refresh_attempts);
    shell_print(" changes=");
    shell_print_number(st.hyperv_refresh_changes);
    shell_print(" last_result=");
    shell_print_signed_number(st.hyperv_last_result);
    shell_newline();
  }

  shell_print("ipv4.addr=");
  {
    char ip[16], mask[16], gw[16], dns[16];
    net_ipv4_format(st.ipv4.addr, ip);
    net_ipv4_format(st.ipv4.mask, mask);
    net_ipv4_format(st.ipv4.gateway, gw);
    net_ipv4_format(st.ipv4.dns, dns);
    shell_print(ip);
    shell_print(" mask=");
    shell_print(mask);
    shell_print(" gw=");
    shell_print(gw);
    shell_print(" dns=");
    shell_print(dns);
  }
  shell_newline();

  shell_print("stats.tx=");
  shell_print_number((uint32_t)st.stats.frames_tx);
  shell_print(" rx=");
  shell_print_number((uint32_t)st.stats.frames_rx);
  shell_print(" drop=");
  shell_print_number((uint32_t)st.stats.frames_drop);
  shell_print(" arp=");
  shell_print_number(st.arp_entries);
  shell_newline();
  return 0;
#endif
}

int net_cmd_ip(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-ip",
                        net_cli_text(language, NET_HELP_IP))) {
    return 0;
  }
#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }

  char ip[16], mask[16];
  net_ipv4_format(st.ipv4.addr, ip);
  net_ipv4_format(st.ipv4.mask, mask);
  shell_print("ipv4=");
  shell_print(ip);
  shell_print(" mask=");
  shell_print(mask);
  shell_newline();
  return 0;
#endif
}

int net_cmd_refresh(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-refresh",
                        net_cli_text(language, NET_HELP_REFRESH))) {
    return 0;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }

  if (st.ready) {
    shell_print_ok(net_cli_text(language, NET_REFRESH_NOOP));
    shell_newline();
    return net_cmd_status(ctx, 1, argv);
  }

  {
    int rc = net_stack_refresh_runtime();
    if (rc > 0) {
      shell_print_ok(net_cli_text(language, NET_REFRESHED));
      shell_newline();
      return net_cmd_status(ctx, 1, argv);
    }
    if (rc == 0) {
      if (st.nic.kind == NET_NIC_KIND_HYPERV_NETVSC) {
        struct system_runtime_platform platform;
        if (net_cli_hyperv_platform_blocked(&platform)) {
          shell_print_ok(net_cli_text(language, NET_REFRESH_WAIT_PLATFORM));
          shell_newline();
          shell_print("hyperv.wait build=");
          shell_print(CAPYOS_VERSION_FULL);
          shell_print(" feature=");
          shell_print(CAPYOS_FEATURE_HYPERV_RUNTIME);
          shell_print(" ebs=");
          shell_print(system_exit_boot_services_gate_label(
              platform.exit_boot_services_gate));
          shell_print(" input-gate=");
          shell_print(system_hyperv_input_gate_label(
              platform.hyperv_input_gate));
          shell_print(" storage-fw=");
          shell_print(platform.firmware_block_io_active ? "on" : "off");
        } else if (!platform.native_storage_ready) {
          shell_print_ok(net_cli_text(language, NET_REFRESH_WAIT_STORAGE));
        } else if (!st.hyperv_bus_connected) {
          shell_print_ok(net_cli_text(language, NET_REFRESH_WAIT_BUS));
        } else if (!st.hyperv_offer_ready) {
          shell_print_ok(net_cli_text(language, NET_REFRESH_WAIT_OFFER));
        } else {
          shell_print_ok(net_cli_text(language, NET_REFRESH_NOOP));
        }
      } else {
        shell_print_ok(net_cli_text(language, NET_REFRESH_NOOP));
      }
      shell_newline();
      return net_cmd_status(ctx, 1, argv);
    }
    if (st.nic.kind == NET_NIC_KIND_HYPERV_NETVSC) {
      shell_print_error(net_cli_text(language, NET_REFRESH_FAILED));
      shell_newline();
      return net_cmd_status(ctx, 1, argv);
    }
  }

  shell_print_ok(net_cli_text(language, NET_REFRESH_NOOP));
  shell_newline();
  return net_cmd_status(ctx, 1, argv);
#endif
}

int net_cmd_dns(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-dns",
                        net_cli_text(language, NET_HELP_DNS))) {
    return 0;
  }
#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }
  char dns[16];
  net_ipv4_format(st.ipv4.dns, dns);
  shell_print("dns=");
  shell_print(dns);
  shell_newline();
  return 0;
#endif
}

int net_cmd_gw(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-gw",
                        net_cli_text(language, NET_HELP_GW))) {
    return 0;
  }
#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }
  char gw[16];
  net_ipv4_format(st.ipv4.gateway, gw);
  shell_print("gateway=");
  shell_print(gw);
  shell_newline();
  return 0;
#endif
}
