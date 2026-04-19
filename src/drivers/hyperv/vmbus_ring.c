#include "arch/x86_64/framebuffer_console.h"
#include "vmbus_ring.h"

#include <stdint.h>

#include "drivers/hyperv/hyperv.h"

#ifndef UNIT_TEST
#endif

struct vmpacket_descriptor {
  uint16_t type;
  uint16_t offset8;
  uint16_t len8;
  uint16_t flags;
  uint64_t trans_id;
} __attribute__((packed));

static void ring_memzero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void ring_memcpy(void *dst, const void *src, uint32_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static void ring_log(const char *s) {
#ifndef UNIT_TEST
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void ring_log_hex(uint64_t value) {
#ifndef UNIT_TEST
  fbcon_print_hex(value);
#else
  (void)value;
#endif
}

void vmbus_ring_init(volatile struct hv_ring_buffer *ring) {
  if (!ring) {
    return;
  }
  ring->write_index = 0;
  ring->read_index = 0;
  ring->interrupt_mask = 0;
  ring->pending_send_size = 0;
  ring->feature_bits = 1;
}

static uint32_t ring_data_size(uint32_t ring_size) {
  return ring_size > VMBUS_PAGE_SIZE ? (ring_size - VMBUS_PAGE_SIZE) : 0u;
}

static uint32_t ring_load_acquire_write_index(
    volatile struct hv_ring_buffer *ring) {
  uint32_t value = ring->write_index;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  return value;
}

static uint32_t ring_bytes_to_read(volatile struct hv_ring_buffer *ring,
                                   uint32_t data_size) {
  uint32_t write_index = ring_load_acquire_write_index(ring);
  uint32_t read_index = ring->read_index;
  return (write_index >= read_index) ? (write_index - read_index)
                                     : (write_index + data_size - read_index);
}

static uint32_t ring_bytes_to_write(volatile struct hv_ring_buffer *ring,
                                    uint32_t data_size) {
  uint32_t write_index = ring->write_index;
  uint32_t read_index = ring->read_index;

  return (write_index >= read_index)
             ? (data_size - (write_index - read_index))
             : (read_index - write_index);
}

static void ring_copy_out(void *dst, volatile struct hv_ring_buffer *ring,
                          uint32_t data_size, uint32_t offset, uint32_t len) {
  uint8_t *out = (uint8_t *)dst;
  if (!out || !ring || data_size == 0u) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    out[i] = ring->buffer[(offset + i) % data_size];
  }
}

static void ring_copy_in(volatile struct hv_ring_buffer *ring,
                         uint32_t data_size, uint32_t offset, const void *src,
                         uint32_t len) {
  const uint8_t *in = (const uint8_t *)src;
  if (!in || !ring || data_size == 0u) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    ring->buffer[(offset + i) % data_size] = in[i];
  }
}

int vmbus_write_inband_packet_runtime(
    uint32_t child_relid, uint32_t connection_id, uint8_t monitor_id,
    uint8_t monitor_allocated, uint16_t is_dedicated_interrupt,
    volatile struct hv_ring_buffer *send_ring, uint32_t send_ring_size,
    const void *payload, uint32_t payload_len, uint16_t flags,
    uint64_t trans_id, vmbus_signal_relid_fn signal_relid,
    vmbus_signal_monitor_fn signal_monitor,
    vmbus_signal_event_fn signal_event) {
  struct vmpacket_descriptor desc;
  uint64_t prev_indices = 0u;
  uint32_t data_size = 0;
  uint32_t packet_len = 0;
  uint32_t aligned_len = 0;
  uint32_t total_len = 0;
  uint32_t write_index = 0;
  uint32_t interrupt_mask = 0u;
  uint32_t read_index = 0u;

  if (!send_ring || !payload || payload_len == 0u || !signal_event) {
    return -1;
  }

  data_size = ring_data_size(send_ring_size);
  if (data_size == 0u) {
    return -2;
  }

  packet_len = (uint32_t)sizeof(desc) + payload_len;
  aligned_len = (packet_len + 7u) & ~7u;
  total_len = aligned_len + VMBUS_PKT_TRAILER;
  if (ring_bytes_to_write(send_ring, data_size) <= total_len) {
    return -3;
  }

  desc.type = VMBUS_PKT_DATA_INBAND;
  desc.offset8 = (uint16_t)(sizeof(desc) >> 3);
  desc.len8 = (uint16_t)(aligned_len >> 3);
  desc.flags = flags;
  desc.trans_id = trans_id;

  write_index = send_ring->write_index;
  prev_indices = ((uint64_t)write_index << 32);
  ring_copy_in(send_ring, data_size, write_index, &desc,
               (uint32_t)sizeof(desc));
  ring_copy_in(send_ring, data_size, write_index + (uint32_t)sizeof(desc),
               payload, payload_len);
  if (aligned_len > packet_len) {
    uint8_t pad[8];
    ring_memzero(pad, sizeof(pad));
    ring_copy_in(send_ring, data_size, write_index + packet_len, pad,
                 aligned_len - packet_len);
  }
  ring_copy_in(send_ring, data_size, write_index + aligned_len, &prev_indices,
               (uint32_t)sizeof(prev_indices));

  __asm__ volatile("" ::: "memory");
  send_ring->write_index = (write_index + total_len) % data_size;
  __asm__ volatile("" ::: "memory");

  interrupt_mask = send_ring->interrupt_mask;
  if (interrupt_mask != 0u) {
    ring_log("[vmbus] signal_skip mask=");
    ring_log_hex((uint64_t)interrupt_mask);
    ring_log("\n");
    return 0;
  }

  __asm__ volatile("" ::: "memory");
  read_index = send_ring->read_index;
  if (write_index != read_index) {
    ring_log("[vmbus] signal_skip old_write=");
    ring_log_hex((uint64_t)write_index);
    ring_log(" read=");
    ring_log_hex((uint64_t)read_index);
    ring_log("\n");
    return 0;
  }

  if (monitor_allocated && signal_monitor) {
    ring_log("[vmbus] signal_monitor relid=");
    ring_log_hex((uint64_t)child_relid);
    ring_log(" mon=");
    ring_log_hex((uint64_t)monitor_id);
    ring_log("\n");
    if (signal_relid) {
      signal_relid(child_relid);
    }
    signal_monitor(monitor_id);
    return 0;
  }

  if (!is_dedicated_interrupt && signal_relid) {
    ring_log("[vmbus] signal_relid relid=");
    ring_log_hex((uint64_t)child_relid);
    ring_log("\n");
    signal_relid(child_relid);
  }
  ring_log("[vmbus] signal_event chan_conn=");
  ring_log_hex((uint64_t)connection_id);
  ring_log(" event_conn=");
  ring_log_hex((uint64_t)connection_id);
  ring_log(" flag=");
  ring_log_hex((uint64_t)0u);
  ring_log(" mode=fast");
  ring_log(" trans_id=");
  ring_log_hex(trans_id);
  ring_log("\n");
  {
    int rc = signal_event(connection_id);
    ring_log("[vmbus] signal_event rc=");
    ring_log_hex((uint64_t)(uint32_t)rc);
    ring_log("\n");
    return rc;
  }
}

int vmbus_read_raw_packet_runtime(volatile struct hv_ring_buffer *recv_ring,
                                  uint32_t recv_ring_size, void *buffer,
                                  uint32_t buffer_size,
                                  uint32_t *out_packet_len) {
  struct vmpacket_descriptor desc;
  uint32_t data_size = 0;
  uint32_t available = 0;
  uint32_t packet_len = 0;
  uint32_t total_len = 0;
  uint32_t read_index = 0;

  if (!recv_ring || !buffer) {
    return -1;
  }

  data_size = ring_data_size(recv_ring_size);
  if (data_size == 0u) {
    return -2;
  }

  available = ring_bytes_to_read(recv_ring, data_size);
  if (available < (uint32_t)sizeof(desc)) {
    return 0;
  }

  read_index = recv_ring->read_index;
  ring_copy_out(&desc, recv_ring, data_size, read_index,
                (uint32_t)sizeof(desc));
  packet_len = (uint32_t)desc.len8 << 3;
  if (packet_len < (uint32_t)sizeof(desc) ||
      packet_len > (available - (uint32_t)VMBUS_PKT_TRAILER)) {
    if (available <= (uint32_t)VMBUS_PKT_TRAILER) {
      return 0;
    }
    packet_len = available - (uint32_t)VMBUS_PKT_TRAILER;
  }
  total_len = packet_len + (uint32_t)VMBUS_PKT_TRAILER;
  if (available < total_len) {
    return 0;
  }
  if (packet_len > buffer_size) {
    if (out_packet_len) {
      *out_packet_len = packet_len;
    }
    return -3;
  }

  ring_copy_out(buffer, recv_ring, data_size, read_index, packet_len);
  __asm__ volatile("" ::: "memory");
  recv_ring->read_index = (read_index + total_len) % data_size;
  if (out_packet_len) {
    *out_packet_len = packet_len;
  }
  return 1;
}

int vmbus_packet_extract_inband(const void *packet, uint32_t packet_len,
                                const uint8_t **payload,
                                uint32_t *payload_len);

int vmbus_packet_extract_payload(const void *packet, uint32_t packet_len,
                                const uint8_t **payload,
                                uint32_t *payload_len) {
  struct vmpacket_descriptor desc;
  uint32_t declared_len = 0u;
  uint32_t offset = 0u;
  uint32_t data_len = 0u;

  if (!packet || !payload || !payload_len ||
      packet_len < (uint32_t)sizeof(desc)) {
    return -1;
  }

  ring_memcpy(&desc, packet, (uint32_t)sizeof(desc));
  if (desc.type != VMBUS_PKT_DATA_INBAND &&
      desc.type != VMBUS_PKT_DATA_USING_XFER_PAGES &&
      desc.type != VMBUS_PKT_DATA_USING_GPADL &&
      desc.type != VMBUS_PKT_DATA_USING_GPA_DIRECT &&
      desc.type != VMBUS_PKT_COMP &&
      desc.type != VMBUS_PKT_DATA_USING_ADDITIONAL_PKT &&
      desc.type != VMBUS_PKT_ADDITIONAL_DATA &&
      desc.type != VMBUS_PKT_CANCEL_REQUEST) {
    return 0;
  }

  declared_len = (uint32_t)desc.len8 << 3;
  if (declared_len < (uint32_t)sizeof(desc) || declared_len > packet_len) {
    declared_len = packet_len;
  }
  offset = (uint32_t)desc.offset8 << 3;
  if (offset < (uint32_t)sizeof(desc) || offset > declared_len) {
    offset = (uint32_t)sizeof(desc);
  }
  data_len = declared_len - offset;
  if (data_len == 0u) {
    return -5;
  }

  *payload = ((const uint8_t *)packet) + offset;
  *payload_len = data_len;
  return 1;
}

int vmbus_packet_extract_inband(const void *packet, uint32_t packet_len,
                                const uint8_t **payload,
                                uint32_t *payload_len) {
  struct vmpacket_descriptor desc;
  int rc = 0;

  if (!packet || packet_len < (uint32_t)sizeof(desc)) {
    return -4;
  }
  ring_memcpy(&desc, packet, (uint32_t)sizeof(desc));
  if (desc.type == VMBUS_PKT_COMP) {
    return 0;
  }
  if (desc.type != VMBUS_PKT_DATA_INBAND) {
    return -2;
  }
  rc = vmbus_packet_extract_payload(packet, packet_len, payload, payload_len);
  return rc;
}
