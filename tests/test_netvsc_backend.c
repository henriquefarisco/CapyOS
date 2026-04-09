#include <stdio.h>
#include <string.h>

#include "drivers/net/netvsc_backend.h"
#include "drivers/net/netvsp.h"
#include "drivers/net/rndis.h"

struct mock_netvsc_backend {
  uint8_t pending_response[256];
  size_t pending_len;
  uint32_t stage;
  uint32_t query_calls;
  uint32_t open_calls;
  uint32_t send_calls;
};

static struct mock_netvsc_backend g_mock;

static void write_u32_le(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t read_u32_le(const uint8_t *src) {
  return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void mock_zero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static int mock_query_offer(struct vmbus_offer_info *out) {
  if (!out) {
    return -1;
  }
  g_mock.query_calls++;
  out->child_relid = 7u;
  out->connection_id = 9u;
  out->is_dedicated_interrupt = 1u;
  return 0;
}

static int mock_open_channel(struct vmbus_channel_runtime *channel) {
  if (!channel || channel->child_relid != 7u || channel->connection_id != 9u ||
      channel->open_id == 0u || channel->gpadl_handle == 0u ||
      channel->send_ring_size != NETVSC_BACKEND_RING_BYTES ||
      channel->recv_ring_size != NETVSC_BACKEND_RING_BYTES) {
    return -1;
  }
  g_mock.open_calls++;
  return 0;
}

static int mock_queue_rndis_response(const uint8_t *payload, size_t payload_len) {
  g_mock.pending_len = netvsp_build_rndis_control_message(
      g_mock.pending_response, sizeof(g_mock.pending_response), payload,
      payload_len);
  return g_mock.pending_len ? 0 : -1;
}

static int mock_send_control(struct vmbus_channel_runtime *channel,
                             const uint8_t *buf, size_t len) {
  struct netvsp_transport_info info;
  const uint8_t *payload = NULL;
  size_t payload_len = 0u;

  if (!channel || !buf || len == 0u) {
    return -1;
  }
  g_mock.send_calls++;

  if (g_mock.stage == 0u) {
    if (len < 16u || read_u32_le(buf) != NETVSP_MSG_INIT) {
      return -1;
    }
    mock_zero(g_mock.pending_response, sizeof(g_mock.pending_response));
    write_u32_le(&g_mock.pending_response[0], NETVSP_MSG_INIT_COMPLETE);
    write_u32_le(&g_mock.pending_response[4], 20u);
    write_u32_le(&g_mock.pending_response[8], NETVSP_STATUS_SUCCESS);
    write_u32_le(&g_mock.pending_response[12], NETVSC_BACKEND_VSP_MAJOR);
    write_u32_le(&g_mock.pending_response[16], NETVSC_BACKEND_VSP_MINOR);
    g_mock.pending_len = 20u;
    g_mock.stage = 1u;
    return 0;
  }

  if (netvsp_parse_rndis_control_message(buf, len, &info, &payload,
                                         &payload_len) != 0 ||
      !payload || payload_len < 16u) {
    return -1;
  }

  if (g_mock.stage == 1u && read_u32_le(payload) == RNDIS_MSG_INITIALIZE) {
    uint8_t init_complete[] = {
        0x02, 0x00, 0x00, 0x80, 0x34, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    write_u32_le(&init_complete[8], read_u32_le(payload + 8u));
    g_mock.stage = 2u;
    return mock_queue_rndis_response(init_complete, sizeof(init_complete));
  }

  if (g_mock.stage == 2u && read_u32_le(payload) == RNDIS_MSG_QUERY &&
      read_u32_le(payload + 12u) == NETVSC_RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE) {
    uint8_t query_complete[28];
    mock_zero(query_complete, sizeof(query_complete));
    write_u32_le(&query_complete[0], RNDIS_MSG_QUERY_COMPLETE);
    write_u32_le(&query_complete[4], (uint32_t)sizeof(query_complete));
    write_u32_le(&query_complete[8], read_u32_le(payload + 8u));
    write_u32_le(&query_complete[12], RNDIS_STATUS_SUCCESS);
    write_u32_le(&query_complete[16], 4u);
    write_u32_le(&query_complete[20], 16u);
    write_u32_le(&query_complete[24], 1500u);
    g_mock.stage = 3u;
    return mock_queue_rndis_response(query_complete, sizeof(query_complete));
  }

  if (g_mock.stage == 3u && read_u32_le(payload) == RNDIS_MSG_QUERY &&
      read_u32_le(payload + 12u) == NETVSC_RNDIS_OID_802_3_CURRENT_ADDRESS) {
    uint8_t mac_complete[30];
    mock_zero(mac_complete, sizeof(mac_complete));
    write_u32_le(&mac_complete[0], RNDIS_MSG_QUERY_COMPLETE);
    write_u32_le(&mac_complete[4], (uint32_t)sizeof(mac_complete));
    write_u32_le(&mac_complete[8], read_u32_le(payload + 8u));
    write_u32_le(&mac_complete[12], RNDIS_STATUS_SUCCESS);
    write_u32_le(&mac_complete[16], 6u);
    write_u32_le(&mac_complete[20], 16u);
    mac_complete[24] = 0x02u;
    mac_complete[25] = 0x15u;
    mac_complete[26] = 0xCAu;
    mac_complete[27] = 0xFEu;
    mac_complete[28] = 0x00u;
    mac_complete[29] = 0x01u;
    g_mock.stage = 4u;
    return mock_queue_rndis_response(mac_complete, sizeof(mac_complete));
  }

  if (g_mock.stage == 4u && read_u32_le(payload) == RNDIS_MSG_SET &&
      read_u32_le(payload + 12u) == NETVSC_RNDIS_OID_GEN_CURRENT_PACKET_FILTER) {
    uint8_t set_complete[] = {
        0x05, 0x00, 0x00, 0x80, 0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    write_u32_le(&set_complete[8], read_u32_le(payload + 8u));
    g_mock.stage = 5u;
    return mock_queue_rndis_response(set_complete, sizeof(set_complete));
  }

  return -1;
}

static int mock_recv_control(struct vmbus_channel_runtime *channel, uint8_t *buf,
                             size_t cap, size_t *out_len) {
  (void)channel;
  if (!buf || !out_len) {
    return -1;
  }
  if (g_mock.pending_len == 0u) {
    *out_len = 0u;
    return 0;
  }
  if (cap < g_mock.pending_len) {
    return -1;
  }
  memcpy(buf, g_mock.pending_response, g_mock.pending_len);
  *out_len = g_mock.pending_len;
  g_mock.pending_len = 0u;
  return 1;
}

int run_netvsc_backend_tests(void) {
  int fails = 0;
  struct netvsc_backend_state state;
  struct netvsc_backend_ops ops = {
      .query_offer = mock_query_offer,
      .open_channel = mock_open_channel,
      .send_control = mock_send_control,
      .recv_control = mock_recv_control,
  };

  mock_zero(&g_mock, sizeof(g_mock));
  netvsc_backend_init(&state);
  if (state.phase != NETVSC_BACKEND_PROBE) {
    printf("[netvsc_backend] init did not reset probe phase\n");
    fails++;
  }

  for (int i = 0; i < 16 && !netvsc_backend_is_ready(&state); ++i) {
    int rc = netvsc_backend_step(&state, &ops);
    if (rc < 0) {
      printf("[netvsc_backend] backend step failed unexpectedly\n");
      fails++;
      break;
    }
  }

  if (!netvsc_backend_is_ready(&state) || !state.offer_ready ||
      !state.channel_ready || !state.channel.opened ||
      !state.session.control.filter_set || state.session.control.mtu != 1500u ||
      state.session.control.mac[5] != 0x01u || g_mock.query_calls != 1u ||
      g_mock.open_calls != 1u || g_mock.send_calls != 5u) {
    printf("[netvsc_backend] backend did not reach ready state correctly\n");
    fails++;
  }

  mock_zero(&g_mock, sizeof(g_mock));
  netvsc_backend_init(&state);
  ops.open_channel = NULL;
  if (netvsc_backend_step(&state, &ops) >= 0) {
    printf("[netvsc_backend] invalid ops were not rejected\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] netvsc_backend OK\n");
  }
  return fails;
}
