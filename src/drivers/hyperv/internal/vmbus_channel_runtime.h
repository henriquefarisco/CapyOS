#ifndef VMBUS_CHANNEL_RUNTIME_H
#define VMBUS_CHANNEL_RUNTIME_H

#include <stdint.h>

struct vmbus_channel_runtime;

struct vmbus_channel_runtime_ops {
  int (*is_connected)(void);
  int (*post_msg)(void *msg, uint32_t len);
  int (*wait_message)(void *buf, uint32_t maxlen, int timeout_loops);
  void (*signal_relid)(uint32_t relid);
  void (*signal_monitor)(uint8_t monitor_id);
  int (*signal_event)(uint32_t connection_id);
};

void vmbus_channel_runtime_reset_common(struct vmbus_channel_runtime *channel);
int vmbus_channel_runtime_open_common(
    struct vmbus_channel_runtime *channel,
    const struct vmbus_channel_runtime_ops *ops);
int vmbus_channel_runtime_send_inband_common(
    struct vmbus_channel_runtime *channel, const void *payload,
    uint32_t payload_len, uint64_t trans_id,
    const struct vmbus_channel_runtime_ops *ops);
int vmbus_channel_runtime_read_common(struct vmbus_channel_runtime *channel,
                                      void *buffer, uint32_t buffer_size,
                                      uint32_t *out_packet_len);

#endif /* VMBUS_CHANNEL_RUNTIME_H */
