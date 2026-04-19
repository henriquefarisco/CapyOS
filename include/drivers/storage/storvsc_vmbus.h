#ifndef DRIVERS_STORAGE_STORVSC_VMBUS_H
#define DRIVERS_STORAGE_STORVSC_VMBUS_H

#include "drivers/storage/storvsc_backend.h"

int storvsc_vmbus_offer_cached(struct vmbus_offer_info *out);
int storvsc_vmbus_offer_refresh_connected(struct vmbus_offer_info *out);
int storvsc_vmbus_bus_connected(void);
void storvsc_vmbus_ops_init(struct storvsc_backend_ops *out);

#endif /* DRIVERS_STORAGE_STORVSC_VMBUS_H */
