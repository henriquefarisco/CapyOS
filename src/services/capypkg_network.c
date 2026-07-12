#include "services/capypkg_network.h"

#include <stddef.h>

static void network_zero(void *ptr, size_t len) {
    uint8_t *dst = (uint8_t *)ptr;
    while (len--) *dst++ = 0u;
}

static int ipv4_unicast_usable(uint32_t ip) {
    uint8_t first = (uint8_t)(ip >> 24);
    if (ip == 0u || ip == UINT32_MAX) return 0;
    /* 0/8, loopback and multicast/reserved destinations cannot be used as
     * the local package-fetch endpoint or a recursive DNS server. */
    return first != 0u && first != 127u && first < 224u;
}

static int network_link_ready(const struct net_stack_status *status) {
    return status && status->initialized && status->runtime_supported &&
           status->nic.found && status->ready;
}

int capypkg_network_status_usable(const struct net_stack_status *status) {
    if (!network_link_ready(status)) return 0;
    if (!ipv4_unicast_usable(status->ipv4.addr) ||
        !ipv4_unicast_usable(status->ipv4.dns)) {
        return 0;
    }
    return 1;
}

int capypkg_network_prepare(
    const struct capypkg_network_ops *ops,
    uint32_t poll_budget,
    uint32_t dhcp_timeout_ms,
    struct capypkg_network_prepare_result *out) {
    struct capypkg_network_prepare_result result;
    int have_status = 0;

    network_zero(&result, sizeof(result));
    if (!ops || !ops->status) {
        if (out) *out = result;
        return 0;
    }

    have_status = ops->status(&result.status) == 0;
    if (have_status && capypkg_network_status_usable(&result.status)) {
        if (out) *out = result;
        return 1;
    }

    /* DHCP is useful only after the link/runtime exists and an address or DNS
     * endpoint is genuinely missing. Run it once per bounded prepare call;
     * the caller controls the total retry budget. */
    if (dhcp_timeout_ms > 0u && have_status &&
        network_link_ready(&result.status) &&
        (!ipv4_unicast_usable(result.status.ipv4.addr) ||
         !ipv4_unicast_usable(result.status.ipv4.dns)) &&
        ops->dhcp_acquire) {
        result.dhcp_attempted = 1u;
        (void)ops->dhcp_acquire(dhcp_timeout_ms);
        have_status = ops->status(&result.status) == 0;
        if (have_status && capypkg_network_status_usable(&result.status)) {
            if (out) *out = result;
            return 1;
        }
    }

    for (uint32_t poll = 0u; poll < poll_budget && ops->poll; ++poll) {
        (void)ops->poll();
        result.polls++;
        if (ops->status(&result.status) == 0 &&
            capypkg_network_status_usable(&result.status)) {
            if (out) *out = result;
            return 1;
        }
    }
    if (out) *out = result;
    return 0;
}
