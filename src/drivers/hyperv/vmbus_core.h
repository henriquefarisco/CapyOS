#ifndef VMBUS_CORE_H
#define VMBUS_CORE_H

#include <stdint.h>

int vmbus_post_msg(void *msg, uint32_t len);
int vmbus_wait_message(void *buf, uint32_t maxlen, int timeout_loops);
void vmbus_signal_relid(uint32_t relid);
void vmbus_signal_monitor(uint8_t monitor_id);
int vmbus_signal_event(uint32_t connection_id);

#endif /* VMBUS_CORE_H */
