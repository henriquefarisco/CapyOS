#include "arch/x86_64/framebuffer_console.h"
#include <stddef.h>
#include <stdint.h>

#include "drivers/hyperv/hyperv.h"
#include "internal/vmbus_core.h"
#include "internal/vmbus_mouse_protocol.h"
#include "internal/vmbus_transport.h"

#define MOUSE_VSC_SEND_RING_BUFFER_SIZE (16u * 1024u)
#define MOUSE_VSC_RECV_RING_BUFFER_SIZE (16u * 1024u)
#define MOUSE_VSC_OPEN_ID 0x48564D53u
#define MOUSE_VSC_GPADL_HANDLE 0x000E1E11u

#define PIPE_MESSAGE_DATA 1u
#define SYNTH_HID_PROTOCOL_REQUEST 0u
#define SYNTH_HID_PROTOCOL_RESPONSE 1u
#define SYNTH_HID_INITIAL_DEVICE_INFO 2u
#define SYNTH_HID_INITIAL_DEVICE_INFO_ACK 3u
#define SYNTH_HID_INPUT_REPORT 4u
#define SYNTH_HID_VERSION ((2u << 16) | 0u)

struct synthhid_header {
  uint32_t type;
  uint32_t size;
} __attribute__((packed));

struct synthhid_protocol_request_pipe {
  uint32_t pipe_type;
  uint32_t pipe_size;
  struct synthhid_header header;
  uint32_t version_requested;
} __attribute__((packed));

struct synthhid_device_info_ack_pipe {
  uint32_t pipe_type;
  uint32_t pipe_size;
  struct synthhid_header header;
  uint32_t reserved;
} __attribute__((packed));

struct vmbus_mouse {
  uint8_t initialized;
  uint8_t connected;
  uint8_t protocol_accepted;
  uint8_t device_info_seen;
  struct vmbus_channel_runtime channel;
  struct hyperv_mouse_report pending_report;
  uint8_t pending_ready;
};

static struct vmbus_mouse g_mouse;

static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

static int mouse_verbose_io(void) {
#ifdef CAPYOS_HYPERV_VERBOSE_IO
  return 1;
#else
  return 0;
#endif
}

static void mouse_log(const char *s) {
#ifndef UNIT_TEST
  if (!mouse_verbose_io()) {
    (void)s;
    return;
  }
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void mouse_memzero(void *ptr, uint32_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) return;
  for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void mouse_guid(struct hv_guid *guid) {
  if (!guid) return;
  guid->data1 = HV_MOUSE_GUID_DATA1;
  guid->data2 = HV_MOUSE_GUID_DATA2;
  guid->data3 = HV_MOUSE_GUID_DATA3;
  guid->data4[0] = HV_MOUSE_GUID_DATA4_0;
  guid->data4[1] = HV_MOUSE_GUID_DATA4_1;
  guid->data4[2] = HV_MOUSE_GUID_DATA4_2;
  guid->data4[3] = HV_MOUSE_GUID_DATA4_3;
  guid->data4[4] = HV_MOUSE_GUID_DATA4_4;
  guid->data4[5] = HV_MOUSE_GUID_DATA4_5;
  guid->data4[6] = HV_MOUSE_GUID_DATA4_6;
  guid->data4[7] = HV_MOUSE_GUID_DATA4_7;
}

static void vmbus_mouse_reset(struct vmbus_mouse *mouse) {
  if (!mouse) return;
  vmbus_channel_runtime_reset(&mouse->channel);
  mouse->initialized = 0;
  mouse->connected = 0;
  mouse->protocol_accepted = 0;
  mouse->device_info_seen = 0;
  mouse->pending_ready = 0;
  mouse_memzero(&mouse->pending_report, (uint32_t)sizeof(mouse->pending_report));
}

static void vmbus_mouse_channel_from_offer(
    const struct vmbus_offer_info *offer, struct vmbus_channel_runtime *channel) {
  if (!offer || !channel) return;
  mouse_memzero(channel, (uint32_t)sizeof(*channel));
  channel->child_relid = offer->child_relid;
  channel->connection_id = offer->connection_id;
  channel->monitor_id = offer->monitor_id;
  channel->monitor_allocated = offer->monitor_allocated;
  channel->is_dedicated_interrupt = offer->is_dedicated_interrupt;
  channel->open_id = MOUSE_VSC_OPEN_ID;
  channel->gpadl_handle = MOUSE_VSC_GPADL_HANDLE;
  channel->send_ring_size = MOUSE_VSC_SEND_RING_BUFFER_SIZE;
  channel->recv_ring_size = MOUSE_VSC_RECV_RING_BUFFER_SIZE;
}

static int vmbus_mouse_send_protocol_request(struct vmbus_mouse *mouse) {
  struct synthhid_protocol_request_pipe request;

  if (!mouse) return -1;
  mouse_memzero(&request, (uint32_t)sizeof(request));
  request.pipe_type = PIPE_MESSAGE_DATA;
  request.pipe_size = (uint32_t)sizeof(request.header) + sizeof(uint32_t);
  request.header.type = SYNTH_HID_PROTOCOL_REQUEST;
  request.header.size = sizeof(uint32_t);
  request.version_requested = SYNTH_HID_VERSION;
  return vmbus_channel_runtime_send_inband(
      &mouse->channel, &request, (uint32_t)sizeof(request),
      (uint64_t)(uintptr_t)&request);
}

static int vmbus_mouse_send_device_info_ack(struct vmbus_mouse *mouse) {
  struct synthhid_device_info_ack_pipe ack;

  if (!mouse) return -1;
  mouse_memzero(&ack, (uint32_t)sizeof(ack));
  ack.pipe_type = PIPE_MESSAGE_DATA;
  ack.pipe_size = (uint32_t)sizeof(ack.header) + sizeof(uint32_t);
  ack.header.type = SYNTH_HID_INITIAL_DEVICE_INFO_ACK;
  ack.header.size = sizeof(uint32_t);
  return vmbus_channel_runtime_send_inband(
      &mouse->channel, &ack, (uint32_t)sizeof(ack), (uint64_t)(uintptr_t)&ack);
}

static uint32_t load_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int vmbus_mouse_process_payload(struct vmbus_mouse *mouse,
                                       const uint8_t *payload,
                                       uint32_t payload_len) {
  uint32_t pipe_type = 0;
  uint32_t pipe_size = 0;
  const uint8_t *msg = NULL;
  uint32_t msg_len = 0;
  uint32_t msg_type = 0;
  uint32_t msg_size = 0;

  if (!mouse || !payload || payload_len < 16u) return 0;
  pipe_type = load_u32(payload);
  pipe_size = load_u32(payload + 4u);
  if (pipe_type != PIPE_MESSAGE_DATA || pipe_size > payload_len - 8u) return 0;
  msg = payload + 8u;
  msg_len = pipe_size;
  if (msg_len < 8u) return 0;
  msg_type = load_u32(msg);
  msg_size = load_u32(msg + 4u);
  if (msg_size > msg_len - 8u) msg_size = msg_len - 8u;

  if (msg_type == SYNTH_HID_PROTOCOL_RESPONSE) {
    uint32_t approved = 0u;
    if (msg_size >= 8u) {
      approved = load_u32(msg + 12u);
    } else if (msg_size >= 4u) {
      approved = load_u32(msg + 8u);
    }
    mouse->protocol_accepted = approved ? 1u : 0u;
    mouse->connected = mouse->protocol_accepted;
    return approved ? 2 : -1;
  }
  if (msg_type == SYNTH_HID_INITIAL_DEVICE_INFO) {
    mouse->device_info_seen = 1u;
    (void)vmbus_mouse_send_device_info_ack(mouse);
    return 3;
  }
  if (msg_type == SYNTH_HID_INPUT_REPORT) {
    if (hyperv_mouse_parse_hid_report(msg + 8u, msg_size,
                                      &mouse->pending_report)) {
      mouse->pending_ready = 1u;
      return 1;
    }
  }
  return 0;
}

static int vmbus_mouse_process_packet(struct vmbus_mouse *mouse,
                                      const uint8_t *packet,
                                      uint32_t packet_len) {
  const uint8_t *payload = NULL;
  uint32_t payload_len = 0;
  int rc = 0;

  if (!mouse || !packet) return 0;
  rc = vmbus_packet_extract_inband(packet, packet_len, &payload, &payload_len);
  if (rc <= 0) return rc < 0 ? 0 : rc;
  return vmbus_mouse_process_payload(mouse, payload, payload_len);
}

static int vmbus_mouse_drain(struct vmbus_mouse *mouse,
                             uint32_t max_packets) {
  uint8_t packet[1024];
  int last = 0;

  if (!mouse) return -1;
  for (uint32_t i = 0; i < max_packets; ++i) {
    uint32_t packet_len = 0;
    int rc = vmbus_channel_runtime_read(&mouse->channel, packet,
                                        (uint32_t)sizeof(packet),
                                        &packet_len);
    if (rc < 0) return -1;
    if (rc == 0) return last;
    last = vmbus_mouse_process_packet(mouse, packet, packet_len);
    if (mouse->pending_ready) return 1;
  }
  return last;
}

int hyperv_mouse_init(void) {
  struct hv_guid guid;
  struct vmbus_offer_info offer;

  if (!hyperv_detect()) return -1;
  if (g_mouse.initialized && g_mouse.connected) return 0;
  if (vmbus_runtime_connect() != 0) return -2;
  mouse_guid(&guid);
  if (vmbus_query_offer(&guid, &offer) != 0 &&
      vmbus_query_offer_by_data1(HV_MOUSE_GUID_DATA1, &offer) != 0) {
    return -3;
  }

  vmbus_mouse_reset(&g_mouse);
  vmbus_mouse_channel_from_offer(&offer, &g_mouse.channel);
  if (vmbus_channel_runtime_open(&g_mouse.channel) != 0) {
    vmbus_mouse_reset(&g_mouse);
    return -4;
  }
  if (vmbus_mouse_send_protocol_request(&g_mouse) != 0) {
    vmbus_mouse_reset(&g_mouse);
    return -5;
  }
  for (uint32_t i = 0; i < 256u; ++i) {
    int rc = vmbus_mouse_drain(&g_mouse, 8u);
    if (g_mouse.protocol_accepted && g_mouse.device_info_seen) {
      g_mouse.initialized = 1u;
      g_mouse.connected = 1u;
      mouse_log("[vmbus] mouse SynthHID configurado.\n");
      return 0;
    }
    if (rc < 0) {
      vmbus_mouse_reset(&g_mouse);
      return -6;
    }
    vmbus_transport_drain_simp();
    cpu_relax();
  }
  if (g_mouse.protocol_accepted) {
    g_mouse.initialized = 1u;
    g_mouse.connected = 1u;
    return 0;
  }
  vmbus_mouse_reset(&g_mouse);
  return -7;
}

int hyperv_mouse_available(void) {
  return g_mouse.initialized && g_mouse.connected;
}

int hyperv_mouse_poll(struct hyperv_mouse_report *out) {
  if (!out || !g_mouse.initialized || !g_mouse.connected) return 0;
  if (vmbus_mouse_drain(&g_mouse, 16u) < 0) {
    vmbus_mouse_reset(&g_mouse);
    return 0;
  }
  if (!g_mouse.pending_ready) return 0;
  *out = g_mouse.pending_report;
  g_mouse.pending_ready = 0u;
  return 1;
}
