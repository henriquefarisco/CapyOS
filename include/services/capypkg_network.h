#ifndef SERVICES_CAPYPKG_NETWORK_H
#define SERVICES_CAPYPKG_NETWORK_H

#include <stdint.h>

#include "net/stack.h"

/* Small injected seam shared by the first-boot wizard and the background
 * capypkg service. Keeping policy here prevents the two entry points from
 * drifting on what "network ready" means, while host tests can drive the
 * state machine without a NIC or timer. */
struct capypkg_network_ops {
    int (*status)(struct net_stack_status *out);
    int (*poll)(void);
    int (*dhcp_acquire)(uint32_t timeout_ms);
};

struct capypkg_network_prepare_result {
    struct net_stack_status status;
    uint32_t polls;
    uint8_t dhcp_attempted;
};

/* Strict readiness predicate for remote package traffic. A generic
 * `net_stack_status.ready` is not enough: HTTP/DNS also need usable IPv4 and
 * DNS endpoints. A valid static/fallback configuration remains usable even
 * when an earlier DHCP attempt timed out. */
int capypkg_network_status_usable(const struct net_stack_status *status);

/* Advance network state with a bounded amount of work. At most one DHCP
 * acquire and `poll_budget` polls are issued per call. Returns 1 when the
 * final snapshot is usable, 0 otherwise. */
int capypkg_network_prepare(
    const struct capypkg_network_ops *ops,
    uint32_t poll_budget,
    uint32_t dhcp_timeout_ms,
    struct capypkg_network_prepare_result *out);

#endif /* SERVICES_CAPYPKG_NETWORK_H */
