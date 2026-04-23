static int cmd_recovery_network(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-network\nMostra o estado detalhado da rede durante a recuperacao e aponta a remediacao minima para promover o target de rede.\n",
            "Usage: recovery-network\nShows detailed network state during recovery and points to the minimum remediation needed to promote the network target.\n",
            "Uso: recovery-network\nMuestra el estado detallado de la red durante la recuperacion e indica la remediacion minima para promover el objetivo de red.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-network indisponivel",
                                          "recovery-network unavailable",
                                          "recovery-network no disponible"));
    return -1;
#else
    {
        struct net_stack_status st;
        char ip[16], mask[16], gw[16], dns[16];
        if (net_stack_status(&st) != 0) {
            shell_print_error(localization_select(
                language,
                "estado da rede indisponivel no runtime atual",
                "network state unavailable in the current runtime",
                "estado de red no disponible en el runtime actual"));
            return -1;
        }

        net_ipv4_format(st.ipv4.addr, ip);
        net_ipv4_format(st.ipv4.mask, mask);
        net_ipv4_format(st.ipv4.gateway, gw);
        net_ipv4_format(st.ipv4.dns, dns);

        shell_print("driver=");
        shell_print(net_driver_name(st.nic.kind));
        shell_print(" detected=");
        shell_print(st.nic.found ? "yes" : "no");
        shell_print(" runtime=");
        shell_print(st.runtime_supported ? "validated" : "missing");
        shell_print(" ready=");
        shell_print(st.ready ? "yes" : "no");
        shell_newline();

        if (st.nic.found) {
            shell_print("pci=");
            shell_print_number(st.nic.bus);
            shell_print(":");
            shell_print_number(st.nic.device);
            shell_print(".");
            shell_print_number(st.nic.function);
            shell_print(" vendor=");
            shell_print_number(st.nic.vendor_id);
            shell_print(" device=");
            shell_print_number(st.nic.device_id);
            shell_newline();
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

        shell_print("tx=");
        shell_print_number((uint32_t)st.stats.frames_tx);
        shell_print(" rx=");
        shell_print_number((uint32_t)st.stats.frames_rx);
        shell_print(" drop=");
        shell_print_number((uint32_t)st.stats.frames_drop);
        shell_print(" arp=");
        shell_print_number(st.arp_entries);
        shell_newline();

        shell_print("remediation: ");
        if (!st.nic.found) {
            shell_print(localization_select(
                language,
                "nenhuma NIC foi detectada; confirme que a VM possui um adaptador de rede anexado.",
                "no NIC was detected; confirm that the VM has a network adapter attached.",
                "no se detecto ninguna NIC; confirma que la VM tenga un adaptador de red conectado."));
        } else if (!st.runtime_supported && st.nic.kind == NET_NIC_KIND_VMXNET3) {
            shell_print(localization_select(
                language,
                "VMXNET3 foi detectado sem driver validado; troque a VM para E1000 antes de promover o target de rede.",
                "VMXNET3 was detected without a validated driver; switch the VM to E1000 before promoting the network target.",
                "VMXNET3 fue detectado sin driver validado; cambia la VM a E1000 antes de promover el objetivo de red."));
        } else if (!st.runtime_supported) {
            shell_print(localization_select(
                language,
                "a NIC atual nao possui driver validado; ajuste a VM para um hardware suportado antes de promover a rede.",
                "the current NIC does not have a validated driver; adjust the VM to supported hardware before promoting networking.",
                "la NIC actual no tiene un driver validado; ajusta la VM a hardware soportado antes de promover la red."));
        } else if (!st.ready) {
            shell_print(localization_select(
                language,
                "o runtime de rede existe, mas ainda nao esta pronto; confira cabos virtuais, configuracao IPv4 e tente net-refresh/net-status.",
                "the network runtime exists, but it is not ready yet; check virtual cabling, IPv4 configuration and try net-refresh/net-status.",
                "el runtime de red existe, pero aun no esta listo; revisa el cableado virtual, la configuracion IPv4 e intenta net-refresh/net-status."));
        } else {
            shell_print(localization_select(
                language,
                "os prerequisitos de rede parecem saudaveis para tentar recovery-resume network/full.",
                "network prerequisites look healthy enough to try recovery-resume network/full.",
                "los prerequisitos de red parecen saludables para intentar recovery-resume network/full."));
        }
        shell_newline();
    }
    return 0;
#endif
}

static struct shell_command g_system_info_commands[17];
static int g_system_info_commands_initialized = 0;

static void init_system_info_commands(void) {
    if (g_system_info_commands_initialized) {
        return;
    }
    g_system_info_commands[0].name = "print-me";
    g_system_info_commands[0].handler = cmd_print_me;
    g_system_info_commands[1].name = "print-id";
    g_system_info_commands[1].handler = cmd_print_id;
    g_system_info_commands[2].name = "print-host";
    g_system_info_commands[2].handler = cmd_print_host;
    g_system_info_commands[3].name = "print-version";
    g_system_info_commands[3].handler = cmd_print_version;
    g_system_info_commands[4].name = "print-time";
    g_system_info_commands[4].handler = cmd_print_time;
    g_system_info_commands[5].name = "print-insomnia";
    g_system_info_commands[5].handler = cmd_print_insomnia;
    g_system_info_commands[6].name = "print-envs";
    g_system_info_commands[6].handler = cmd_print_envs;
    g_system_info_commands[7].name = "service-status";
    g_system_info_commands[7].handler = cmd_service_status;
    g_system_info_commands[8].name = "job-status";
    g_system_info_commands[8].handler = cmd_job_status;
    g_system_info_commands[9].name = "update-status";
    g_system_info_commands[9].handler = cmd_update_status;
    g_system_info_commands[10].name = "update-history";
    g_system_info_commands[10].handler = cmd_update_history;
    g_system_info_commands[11].name = "recovery-status";
    g_system_info_commands[11].handler = cmd_recovery_status;
    g_system_info_commands[12].name = "recovery-report";
    g_system_info_commands[12].handler = cmd_recovery_report;
    g_system_info_commands[13].name = "recovery-history";
    g_system_info_commands[13].handler = cmd_recovery_history;
    g_system_info_commands[14].name = "recovery-storage";
    g_system_info_commands[14].handler = cmd_recovery_storage;
    g_system_info_commands[15].name = "recovery-network";
    g_system_info_commands[15].handler = cmd_recovery_network;
    g_system_info_commands[16].name = "recovery-storage-check";
    g_system_info_commands[16].handler = cmd_recovery_storage_check;
    g_system_info_commands_initialized = 1;
}

const struct shell_command *shell_commands_system_info(size_t *count) {
    init_system_info_commands();
    if (count) {
        *count = 17;
    }
    return g_system_info_commands;
}
