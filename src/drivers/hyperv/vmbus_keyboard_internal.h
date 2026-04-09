#ifndef VMBUS_KEYBOARD_INTERNAL_H
#define VMBUS_KEYBOARD_INTERNAL_H

#include <stdint.h>

#include "vmbus_ring.h"

#ifndef VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED
#define VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED 1u
#endif

#ifndef SYNTH_KBD_PROTOCOL_REQUEST
#define SYNTH_KBD_PROTOCOL_REQUEST 1u
#endif
#ifndef SYNTH_KBD_PROTOCOL_RESPONSE
#define SYNTH_KBD_PROTOCOL_RESPONSE 2u
#endif
#ifndef SYNTH_KBD_EVENT
#define SYNTH_KBD_EVENT 3u
#endif
#ifndef SYNTH_KBD_VERSION
#define SYNTH_KBD_VERSION ((1u << 16) | 0u)
#endif
#ifndef SYNTH_KBD_PROTOCOL_ACCEPTED
#define SYNTH_KBD_PROTOCOL_ACCEPTED 0x1u
#endif
#ifndef SYNTH_KBD_INFO_BREAK
#define SYNTH_KBD_INFO_BREAK 0x2u
#endif
#ifndef SYNTH_KBD_INFO_E0
#define SYNTH_KBD_INFO_E0 0x4u
#endif

struct synth_kbd_protocol_request_msg {
  uint32_t type;
  uint32_t version_requested;
} __attribute__((packed));

struct synth_kbd_protocol_response_msg {
  uint32_t type;
  uint32_t proto_status;
} __attribute__((packed));

struct synth_kbd_keystroke_msg {
  uint32_t type;
  uint16_t make_code;
  uint16_t reserved0;
  uint32_t info;
} __attribute__((packed));

struct vmbus_keyboard {
  int initialized;
  int connected;
  int protocol_accepted;
  uint32_t child_relid;
  uint32_t connection_id;
  uint8_t monitor_id;
  uint8_t monitor_allocated;
  uint32_t open_id;
  uint32_t gpadl_handle;
  uint16_t is_dedicated_interrupt;
  uint8_t *ring_buffer;
  uint32_t ring_size;
  uint32_t send_ring_size;
  uint32_t recv_ring_size;
  volatile struct hv_ring_buffer *send_ring;
  volatile struct hv_ring_buffer *recv_ring;
};

struct vmbus_keyboard_protocol_ops {
  int (*send_packet)(struct vmbus_keyboard *kbd, const void *payload,
                     uint32_t payload_len, uint16_t flags, uint64_t trans_id);
  int (*read_packet)(struct vmbus_keyboard *kbd, void *buffer,
                     uint32_t buffer_size, uint32_t *out_packet_len);
  void (*drain_transport)(void);
  void (*cpu_relax)(void);
};

int vmbus_keyboard_protocol_process_packet(struct vmbus_keyboard *kbd,
                                          const uint8_t *packet,
                                          uint32_t packet_len,
                                          uint8_t *scancode, int *is_break,
                                          int *is_extended);
int vmbus_keyboard_protocol_send_request(
    struct vmbus_keyboard *kbd, const struct vmbus_keyboard_protocol_ops *ops);
int vmbus_keyboard_protocol_wait(struct vmbus_keyboard *kbd,
                                 const struct vmbus_keyboard_protocol_ops *ops);

#endif /* VMBUS_KEYBOARD_INTERNAL_H */
