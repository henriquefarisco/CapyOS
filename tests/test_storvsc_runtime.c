#include <stdio.h>

#include "drivers/storage/storvsc_runtime.h"

struct mock_storvsc_runtime {
  uint8_t pending[STORVSP_PACKET_SIZE];
  size_t pending_len;
  uint32_t stage;
  uint32_t open_calls;
};

static struct mock_storvsc_runtime g_mock;

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
  out->child_relid = 21u;
  out->connection_id = 34u;
  out->is_dedicated_interrupt = 1u;
  return 0;
}

static int mock_open_channel(struct vmbus_channel_runtime *channel) {
  if (!channel) {
    return -1;
  }
  g_mock.open_calls++;
  return 0;
}

static int queue_response(uint32_t operation, uint32_t status) {
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

  if (g_mock.stage == 0u &&
      get_u32(buf) == STORVSP_OPERATION_BEGIN_INITIALIZATION) {
    g_mock.stage = 1u;
    return queue_response(STORVSP_OPERATION_COMPLETE_IO,
                          STORVSP_STATUS_SUCCESS);
  }
  if (g_mock.stage == 1u &&
      get_u32(buf) == STORVSP_OPERATION_QUERY_PROTOCOL_VERSION) {
    g_mock.stage = 2u;
    queue_response(STORVSP_OPERATION_COMPLETE_IO,
                   STORVSP_STATUS_SUCCESS);
    set_u16(&g_mock.pending[12], STORVSP_PROTO_WIN10);
    return 0;
  }
  if (g_mock.stage == 2u &&
      get_u32(buf) == STORVSP_OPERATION_QUERY_PROPERTIES) {
    g_mock.stage = 3u;
    queue_response(STORVSP_OPERATION_COMPLETE_IO, STORVSP_STATUS_SUCCESS);
    set_u16(&g_mock.pending[16], 2u);
    set_u32(&g_mock.pending[20], 0u);
    set_u32(&g_mock.pending[24], 256u * 1024u);
    return 0;
  }
  if (g_mock.stage == 3u &&
      get_u32(buf) == STORVSP_OPERATION_END_INITIALIZATION) {
    g_mock.stage = 4u;
    return queue_response(STORVSP_OPERATION_COMPLETE_IO,
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
    mock_zero(diag, sizeof(*diag));
  }
  if (g_mock.pending_len == 0u) {
    *out_len = 0u;
    return 0;
  }
  if (cap < g_mock.pending_len) {
    return -1;
  }
  for (size_t i = 0; i < g_mock.pending_len; ++i) {
    buf[i] = g_mock.pending[i];
  }
  *out_len = g_mock.pending_len;
  g_mock.pending_len = 0u;
  return 1;
}

int run_storvsc_runtime_tests(void) {
  int fails = 0;
  struct storvsc_runtime_state state;
  struct storvsc_controller_status status;
  struct storvsc_backend_ops ops = {
      .query_offer = mock_query_offer,
      .open_channel = mock_open_channel,
      .send_control = mock_send_control,
      .recv_control = mock_recv_control,
  };
  mock_zero(&g_mock, sizeof(g_mock));

  storvsc_runtime_init(&state);
  if (state.phase != STORVSC_RUNTIME_UNCONFIGURED) {
    printf("[storvsc_runtime] init failed\n");
    fails++;
  }

  if (storvsc_runtime_configure(&state, 1, &ops) != 0 ||
      state.phase != STORVSC_RUNTIME_DISABLED) {
    printf("[storvsc_runtime] configure failed\n");
    fails++;
  }

  storvsc_runtime_set_enabled(&state, 1);
  if (storvsc_runtime_step_probe_only(&state) <= 0 ||
      storvsc_runtime_controller_status(&state, &status) != 0 ||
      status.phase != STORVSC_RUNTIME_CHANNEL || status.offer_ready != 1u ||
      status.channel_ready != 0u || g_mock.open_calls != 0u) {
    printf("[storvsc_runtime] probe-only advance failed\n");
    fails++;
  }

  mock_zero(&g_mock, sizeof(g_mock));
  if (storvsc_runtime_configure(&state, 1, &ops) != 0) {
    printf("[storvsc_runtime] reconfigure failed\n");
    fails++;
  }
  storvsc_runtime_set_enabled(&state, 1);
  for (int i = 0; i < 16 && state.phase != STORVSC_RUNTIME_READY; ++i) {
    if (storvsc_runtime_step(&state) < 0) {
      printf("[storvsc_runtime] step failed unexpectedly\n");
      fails++;
      break;
    }
  }

  if (storvsc_runtime_controller_status(&state, &status) != 0 ||
      status.ready != 1u || status.phase != STORVSC_RUNTIME_READY ||
      status.offer.child_relid != 21u ||
      status.properties.max_transfer_bytes != 256u * 1024u) {
    printf("[storvsc_runtime] controller status mismatch\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] storvsc_runtime OK\n");
  }
  return fails;
}
