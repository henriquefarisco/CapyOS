#ifndef DRIVERS_NET_NETVSC_VMBUS_H
#define DRIVERS_NET_NETVSC_VMBUS_H

#include "drivers/net/netvsc_backend.h"

int netvsc_vmbus_offer_cached(struct vmbus_offer_info *out);
int netvsc_vmbus_offer_refresh_connected(struct vmbus_offer_info *out);
void netvsc_vmbus_ops_init(struct netvsc_backend_ops *out);

#endif /* DRIVERS_NET_NETVSC_VMBUS_H */
