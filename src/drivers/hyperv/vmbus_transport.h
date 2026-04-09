#ifndef VMBUS_TRANSPORT_H
#define VMBUS_TRANSPORT_H

#include <stdint.h>

int vmbus_transport_init(void);
int vmbus_transport_prepare_hypercall(void);
int vmbus_transport_prepare_synic(void);
int vmbus_transport_hypercall_prepared(void);
int vmbus_transport_synic_ready(void);
int vmbus_transport_post_msg(void *msg, uint32_t len, uint32_t conn_id);
int vmbus_transport_wait_message(void *buf, uint32_t maxlen, int timeout_loops);
void vmbus_transport_signal_relid(uint32_t relid);
void vmbus_transport_signal_monitor(uint8_t monitor_id);
int vmbus_transport_signal_event(uint32_t connection_id);
uint64_t vmbus_transport_interrupt_page(void);
uint64_t vmbus_transport_monitor_page1(void);
uint64_t vmbus_transport_monitor_page2(void);
void vmbus_transport_drain_simp(void);

#endif /* VMBUS_TRANSPORT_H */
