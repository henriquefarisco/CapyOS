#include <stdio.h>
#include <string.h>

#include "drivers/storage/storvsc_backend.h"

struct mock_storvsc_backend {
  uint8_t pending[STORVSP_PACKET_SIZE];
  size_t pending_len;
  uint32_t stage;
  uint32_t query_calls;
  uint32_t open_calls;
  uint32_t send_calls;
  uint32_t transient_recv_errors;
};

static struct mock_storvsc_backend g_mock;

static void mock_zero(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  for (size_t i = 0; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static void set_u16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void set_u32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t get_u32(const uint8_t *src) {
  return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static int mock_query_offer(struct vmbus_offer_info *out) {
  if (!out) {
    return -1;
  }
  g_mock.query_calls++;
  out->child_relid = 11u;
  out->connection_id = 13u;
  out->is_dedicated_interrupt = 0u;
  return 0;
}

static int mock_open_channel(struct vmbus_channel_runtime *channel) {
  if (!channel || channel->child_relid != 11u || channel->connection_id != 13u ||
      channel->open_id == 0u || channel->gpadl_handle == 0u ||
      channel->send_ring_size != STORVSC_BACKEND_RING_BYTES ||
      channel->recv_ring_size != STORVSC_BACKEND_RING_BYTES) {
    return -1;
  }
  g_mock.open_calls++;
  return 0;
}

static int queue_header_response(uint32_t operation, uint32_t status) {
  mock_zero(g_mock.pending, sizeof(g_mock.pending));
  set_u32(&g_mock.pending[0], operation);
  set_u32(&g_mock.pending[4], STORVSP_FLAG_REQUEST_COMPLETION);
  set_u32(&g_mock.pending[8], status);
  g_mock.pending_len = STORVSP_PACKET_SIZE;
  return 0;
}

static int mock_send_control(struct vmbus_channel_runtime *channel,
                             const uint8_t *buf, size_t len) {
  (void)channel;
  if (!buf || len != STORVSP_PACKET_SIZE) {
    return -1;
  }
  g_mock.send_calls++;

  if (g_mock.stage == 0u &&
      get_u32(buf) == STORVSP_OPERATION_BEGIN_INITIALIZATION) {
    g_mock.stage = 1u;
    return queue_header_response(STORVSP_OPERATION_COMPLETE_IO,
                                 STORVSP_STATUS_SUCCESS);
  }
  if (g_mock.stage == 1u &&
      get_u32(buf) == STORVSP_OPERATION_QUERY_PROTOCOL_VERSION) {
    g_mock.stage = 2u;
    queue_header_response(STORVSP_OPERATION_COMPLETE_IO,
                          STORVSP_STATUS_SUCCESS);
    set_u16(&g_mock.pending[12], STORVSP_PROTO_WIN10);
    set_u16(&g_mock.pending[14], 0u);
    return 0;
  }
  if (g_mock.stage == 2u &&
      get_u32(buf) == STORVSP_OPERATION_QUERY_PROPERTIES) {
    g_mock.stage = 3u;
    queue_header_response(STORVSP_OPERATION_COMPLETE_IO,
                          STORVSP_STATUS_SUCCESS);
    set_u16(&g_mock.pending[16], 4u);
    set_u16(&g_mock.pending[18], 0u);
    set_u32(&g_mock.pending[20], STORVSP_CHANNEL_SUPPORTS_MULTI_CHANNEL);
    set_u32(&g_mock.pending[24], 512u * 1024u);
    return 0;
  }
  if (g_mock.stage == 3u &&
      get_u32(buf) == STORVSP_OPERATION_END_INITIALIZATION) {
    g_mock.stage = 4u;
    return queue_header_response(STORVSP_OPERATION_COMPLETE_IO,
                                 STORVSP_STATUS_SUCCESS);
  }
  return -1;
}

static int mock_recv_control(struct vmbus_channel_runtime *channel, uint8_t *buf,
                             size_t cap, size_t *out_len,
                             struct storvsc_control_diag *diag) {
  (void)channel;
  if (!buf || !out_len) {
    return -1;
  }
  if (diag) {
    memset(diag, 0, sizeof(*diag));
  }
  if (g_mock.transient_recv_errors != 0u) {
    g_mock.transient_recv_errors--;
    if (diag) {
      diag->read_rc = -7;
    }
    *out_len = 0u;
    return -7;
  }
  if (g_mock.pending_len == 0u) {
    *out_len = 0u;
    return 0;
  }
  if (cap < g_mock.pending_len) {
    return -1;
  }
  memcpy(buf, g_mock.pending, g_mock.pending_len);
  *out_len = g_mock.pending_len;
  g_mock.pending_len = 0u;
  return 1;
}

int run_storvsc_backend_tests(void) {
  int fails = 0;
  struct storvsc_backend_state state;
  struct storvsc_backend_ops ops = {
      .query_offer = mock_query_offer,
      .open_channel = mock_open_channel,
      .send_control = mock_send_control,
      .recv_control = mock_recv_control,
  };

  mock_zero(&g_mock, sizeof(g_mock));
  storvsc_backend_init(&state);
  if (state.phase != STORVSC_BACKEND_PROBE) {
    printf("[storvsc_backend] init failed\n");
    fails++;
  }

  for (int i = 0; i < 16 && !storvsc_backend_is_ready(&state); ++i) {
    int rc = storvsc_backend_step(&state, &ops);
    if (rc < 0) {
      printf("[storvsc_backend] backend step failed unexpectedly\n");
      fails++;
      break;
    }
  }

  if (!storvsc_backend_is_ready(&state) || !state.offer_ready ||
      !state.channel_ready || !state.channel.opened ||
      state.session.negotiated_major_minor != STORVSP_PROTO_WIN10 ||
      state.session.properties.max_channel_count != 4u ||
      state.session.properties.max_transfer_bytes != 512u * 1024u ||
      g_mock.query_calls != 1u || g_mock.open_calls != 1u ||
      g_mock.send_calls != 4u) {
    printf("[storvsc_backend] backend did not reach ready state correctly\n");
    fails++;
  }

  mock_zero(&g_mock, sizeof(g_mock));
  storvsc_backend_init(&state);
  g_mock.transient_recv_errors = 2u;
  for (int i = 0; i < 24 && !storvsc_backend_is_ready(&state); ++i) {
    int rc = storvsc_backend_step(&state, &ops);
    if (rc < 0) {
      printf("[storvsc_backend] transient recv error should not fail early\n");
      fails++;
      break;
    }
  }
  if (!storvsc_backend_is_ready(&state) || state.last_error != 0 ||
      state.control_wait_budget != 0u) {
    printf("[storvsc_backend] control fallback did not recover cleanly\n");
    fails++;
  }

  mock_zero(&g_mock, sizeof(g_mock));
  storvsc_backend_init(&state);
  ops.open_channel = NULL;
  if (storvsc_backend_step(&state, &ops) >= 0) {
    printf("[storvsc_backend] invalid ops were not rejected\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] storvsc_backend OK\n");
  }
  return fails;
}
