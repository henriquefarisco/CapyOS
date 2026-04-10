#ifndef VMBUS_RING_H
#define VMBUS_RING_H

#include <stdint.h>

#ifndef VMBUS_PAGE_SIZE
#define VMBUS_PAGE_SIZE 4096u
#endif

#ifndef VMBUS_PKT_TRAILER
#define VMBUS_PKT_TRAILER 8u
#endif

#ifndef VMBUS_PKT_DATA_INBAND
#define VMBUS_PKT_DATA_INBAND 0x6u
#endif

#ifndef VMBUS_PKT_DATA_USING_XFER_PAGES
#define VMBUS_PKT_DATA_USING_XFER_PAGES 0x7u
#endif

#ifndef VMBUS_PKT_DATA_USING_GPADL
#define VMBUS_PKT_DATA_USING_GPADL 0x8u
#endif

#ifndef VMBUS_PKT_DATA_USING_GPA_DIRECT
#define VMBUS_PKT_DATA_USING_GPA_DIRECT 0x9u
#endif

#ifndef VMBUS_PKT_CANCEL_REQUEST
#define VMBUS_PKT_CANCEL_REQUEST 0xAu
#endif

#ifndef VMBUS_PKT_COMP
#define VMBUS_PKT_COMP 0xBu
#endif

#ifndef VMBUS_PKT_DATA_USING_ADDITIONAL_PKT
#define VMBUS_PKT_DATA_USING_ADDITIONAL_PKT 0xCu
#endif

#ifndef VMBUS_PKT_ADDITIONAL_DATA
#define VMBUS_PKT_ADDITIONAL_DATA 0xDu
#endif

struct hv_ring_buffer {
  uint32_t write_index;
  uint32_t read_index;
  uint32_t interrupt_mask;
  uint32_t pending_send_size;
  uint32_t reserved1[12];
  uint32_t feature_bits;
  uint8_t reserved2[VMBUS_PAGE_SIZE - 68];
  uint8_t buffer[];
} __attribute__((packed));

typedef void (*vmbus_signal_relid_fn)(uint32_t relid);
typedef void (*vmbus_signal_monitor_fn)(uint8_t monitor_id);
typedef int (*vmbus_signal_event_fn)(uint32_t connection_id);

void vmbus_ring_init(volatile struct hv_ring_buffer *ring);
int vmbus_write_inband_packet_runtime(
    uint32_t child_relid, uint32_t connection_id, uint8_t monitor_id,
    uint8_t monitor_allocated, uint16_t is_dedicated_interrupt,
    volatile struct hv_ring_buffer *send_ring, uint32_t send_ring_size,
    const void *payload, uint32_t payload_len, uint16_t flags,
    uint64_t trans_id, vmbus_signal_relid_fn signal_relid,
    vmbus_signal_monitor_fn signal_monitor,
    vmbus_signal_event_fn signal_event);
int vmbus_read_raw_packet_runtime(volatile struct hv_ring_buffer *recv_ring,
                                  uint32_t recv_ring_size, void *buffer,
                                  uint32_t buffer_size,
                                  uint32_t *out_packet_len);

#endif /* VMBUS_RING_H */
