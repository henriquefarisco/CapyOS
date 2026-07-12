/* Strict, deterministic readiness policy shared by Wizard and background
 * bootstrap. The injected seam keeps these tests independent of a real NIC. */

static struct net_stack_status g_network_status;
static int g_network_status_calls;
static int g_network_poll_calls;
static int g_network_dhcp_calls;
static int g_network_dhcp_succeeds;

static int network_test_status(struct net_stack_status *out) {
    g_network_status_calls++;
    if (!out) return -1;
    *out = g_network_status;
    return 0;
}

static int network_test_poll(void) {
    g_network_poll_calls++;
    return 0;
}

static int network_test_dhcp(uint32_t timeout_ms) {
    g_network_dhcp_calls++;
    EXPECT(timeout_ms > 0u, "network prepare must pass a bounded DHCP timeout");
    if (g_network_dhcp_succeeds) {
        g_network_status.dhcp_attempts++;
        g_network_status.dhcp_lease_acquired = 1;
        g_network_status.ipv4.addr = 0x0A000002u;
        g_network_status.ipv4.dns = 0x08080808u;
        return 0;
    }
    return -1;
}

static void network_test_reset(void) {
    memset(&g_network_status, 0, sizeof(g_network_status));
    g_network_status.initialized = 1;
    g_network_status.runtime_supported = 1;
    g_network_status.nic.found = 1;
    g_network_status.ready = 1;
    g_network_status_calls = 0;
    g_network_poll_calls = 0;
    g_network_dhcp_calls = 0;
    g_network_dhcp_succeeds = 0;
}

static void test_capypkg_network_requires_address_and_dns(void) {
    network_test_reset();
    EXPECT(capypkg_network_status_usable(&g_network_status) == 0,
           "link-ready with zero IPv4/DNS must not start remote packages");
    g_network_status.ipv4.addr = 0x0A000002u;
    EXPECT(capypkg_network_status_usable(&g_network_status) == 0,
           "an address without DNS must remain not-ready");
    g_network_status.ipv4.dns = 0x08080808u;
    EXPECT(capypkg_network_status_usable(&g_network_status) == 1,
           "usable unicast IPv4 and DNS must permit package traffic");
    g_network_status.dhcp_attempts = 3u;
    g_network_status.dhcp_lease_acquired = 0;
    EXPECT(capypkg_network_status_usable(&g_network_status) == 1,
           "valid static/fallback addresses remain usable after DHCP timeout");
    g_network_status.ipv4.dns = 0x7F000001u;
    EXPECT(capypkg_network_status_usable(&g_network_status) == 0,
           "loopback cannot be accepted as the remote DNS endpoint");
}

static void test_capypkg_network_prepare_advances_dhcp(void) {
    static const struct capypkg_network_ops ops = {
        network_test_status, network_test_poll, network_test_dhcp,
    };
    struct capypkg_network_prepare_result result;
    network_test_reset();
    g_network_dhcp_succeeds = 1;
    EXPECT(capypkg_network_prepare(&ops, 4u, 500u, &result) == 1,
           "bounded DHCP success must make package network ready immediately");
    EXPECT(g_network_dhcp_calls == 1 && result.dhcp_attempted == 1u,
           "prepare must issue exactly one DHCP attempt per call");
    EXPECT(g_network_poll_calls == 0,
           "successful DHCP snapshot must not spend the poll budget");
}

static void test_capypkg_network_prepare_is_bounded(void) {
    static const struct capypkg_network_ops ops = {
        network_test_status, network_test_poll, network_test_dhcp,
    };
    struct capypkg_network_prepare_result result;
    network_test_reset();
    EXPECT(capypkg_network_prepare(&ops, 3u, 250u, &result) == 0,
           "missing IPv4/DNS must remain not-ready after bounded work");
    EXPECT(g_network_dhcp_calls == 1,
           "failed prepare must not loop indefinitely in DHCP");
    EXPECT(g_network_poll_calls == 3 && result.polls == 3u,
           "failed prepare must consume exactly the configured poll budget");
    EXPECT(g_network_status_calls == 5,
           "status snapshots must remain bounded and deterministic");
}
