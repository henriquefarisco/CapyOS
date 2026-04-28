#include "internal/system_info_internal.h"

static void perf_print_u64(uint64_t value) {
    char buf[24];
    char tmp[24];
    int pos = 0;
    int tp = 0;
    if (value == 0) {
        shell_print("0");
        return;
    }
    while (value > 0 && tp < (int)sizeof(tmp)) {
        tmp[tp++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (tp > 0 && pos < (int)sizeof(buf) - 1) {
        buf[pos++] = tmp[--tp];
    }
    buf[pos] = '\0';
    shell_print(buf);
}

static void perf_print_size_kib(size_t bytes) {
    perf_print_u64((uint64_t)(bytes / 1024u));
    shell_print(" KiB");
}

int cmd_perf_boot(struct shell_context *ctx, int argc, char **argv) {
    struct boot_metrics metrics;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Usage: perf-boot\nShows boot timing metrics by stage.\n");
        return 0;
    }
    boot_metrics_get(&metrics);
    shell_print("Boot performance:\n");
    shell_print("  stages: ");
    shell_print_number(metrics.count);
    shell_newline();
    for (uint32_t i = 0; i < metrics.count; i++) {
        shell_print("  ");
        shell_print(metrics.stages[i].name[0] ? metrics.stages[i].name : "(unnamed)");
        shell_print(": ");
        perf_print_u64(metrics.stages[i].duration_us);
        shell_print(" us\n");
    }
    shell_print("  total_boot_to_login: ");
    perf_print_u64(metrics.total_boot_us);
    shell_print(" us\n");
    return 0;
}

int cmd_perf_net(struct shell_context *ctx, int argc, char **argv) {
    struct dns_cache_stats dns;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Usage: perf-net\nShows network stack and DNS cache counters.\n");
        return 0;
    }
    shell_print("Network performance:\n");
#if defined(__x86_64__)
    {
        struct net_stack_status st;
        if (net_stack_status(&st) == 0) {
            shell_print("  driver: ");
            shell_print(net_driver_name(st.nic.kind));
            shell_print(" ready=");
            shell_print(st.ready ? "yes" : "no");
            shell_print(" dhcp_attempts=");
            shell_print_number(st.dhcp_attempts);
            shell_print(" dhcp_lease=");
            shell_print(st.dhcp_lease_acquired ? "yes" : "no");
            shell_newline();
            shell_print("  frames_rx=");
            perf_print_u64(st.stats.frames_rx);
            shell_print(" frames_tx=");
            perf_print_u64(st.stats.frames_tx);
            shell_print(" drop=");
            perf_print_u64(st.stats.frames_drop);
            shell_newline();
            shell_print("  arp_hits=");
            perf_print_u64(st.stats.arp_hits);
            shell_print(" arp_misses=");
            perf_print_u64(st.stats.arp_misses);
            shell_print(" tcp_rx=");
            perf_print_u64(st.stats.tcp_rx);
            shell_print(" tcp_tx=");
            perf_print_u64(st.stats.tcp_tx);
            shell_newline();
        } else {
            shell_print("  stack: unavailable\n");
        }
    }
#else
    shell_print("  stack: unavailable on this architecture\n");
#endif
    dns_cache_stats_get(&dns);
    shell_print("  dns_entries=");
    shell_print_number(dns.entries);
    shell_print(" dns_hits=");
    perf_print_u64(dns.hits);
    shell_print(" dns_misses=");
    perf_print_u64(dns.misses);
    shell_print(" dns_expired=");
    perf_print_u64(dns.expired);
    shell_newline();
    return 0;
}

int cmd_perf_fs(struct shell_context *ctx, int argc, char **argv) {
    struct buffer_cache_stats stats;
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Usage: perf-fs\nShows filesystem buffer-cache counters.\n");
        return 0;
    }
    buffer_cache_stats_get(&stats);
    shell_print("Filesystem performance:\n");
    shell_print("  buffer_cache valid=");
    shell_print_number(stats.valid);
    shell_print("/");
    shell_print_number(stats.capacity);
    shell_print(" dirty=");
    shell_print_number(stats.dirty);
    shell_print(" pinned=");
    shell_print_number(stats.pinned);
    shell_newline();
    shell_print("  hits=");
    perf_print_u64(stats.hits);
    shell_print(" misses=");
    perf_print_u64(stats.misses);
    shell_print(" evictions=");
    perf_print_u64(stats.evictions);
    shell_print(" writebacks=");
    perf_print_u64(stats.writebacks);
    shell_newline();
    shell_print("  read_errors=");
    perf_print_u64(stats.read_errors);
    shell_print(" write_errors=");
    perf_print_u64(stats.write_errors);
    shell_newline();
    return 0;
}

int cmd_perf_mem(struct shell_context *ctx, int argc, char **argv) {
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print("Usage: perf-mem\nShows kernel heap usage counters.\n");
        return 0;
    }
    shell_print("Memory performance:\n");
    shell_print("  heap_used=");
    perf_print_size_kib(kheap_used());
    shell_print(" (");
    perf_print_u64((uint64_t)kheap_used());
    shell_print(" bytes)\n");
    shell_print("  heap_size=");
    perf_print_size_kib(kheap_size());
    shell_print(" (");
    perf_print_u64((uint64_t)kheap_size());
    shell_print(" bytes)\n");
    return 0;
}
