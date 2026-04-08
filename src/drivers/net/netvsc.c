#include "drivers/net/netvsc.h"

#include "drivers/net/rndis.h"

static const struct hv_guid k_hyperv_nic_guid = {
    .data1 = HV_NIC_GUID_DATA1,
    .data2 = HV_NIC_GUID_DATA2,
    .data3 = HV_NIC_GUID_DATA3,
    .data4 = {HV_NIC_GUID_DATA4_0, HV_NIC_GUID_DATA4_1, HV_NIC_GUID_DATA4_2,
              HV_NIC_GUID_DATA4_3, HV_NIC_GUID_DATA4_4, HV_NIC_GUID_DATA4_5,
              HV_NIC_GUID_DATA4_6, HV_NIC_GUID_DATA4_7}};

struct netvsc_runtime {
  uint8_t offer_ready;
  uint8_t channel_ready;
  struct vmbus_offer_info offer;
};

static struct netvsc_runtime g_netvsc;

static size_t netvsc_control_build_query_mtu(struct netvsc_control_state *state,
                                             uint8_t *out, size_t cap);
static size_t netvsc_control_build_query_mac(struct netvsc_control_state *state,
                                             uint8_t *out, size_t cap);
static size_t netvsc_control_build_set_filter(struct netvsc_control_state *state,
                                              uint8_t *out, size_t cap);
static uint32_t netvsc_read_u32_le(const uint8_t *buf);

static void netvsc_memzero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void netvsc_memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (!d || !s) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static uint32_t netvsc_read_u32_le(const uint8_t *buf) {
  if (!buf) {
    return 0u;
  }
  return ((uint32_t)buf[0]) | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static void netvsc_copy_status(struct netvsc_runtime_status *out) {
  if (!out) {
    return;
  }
  out->offer_ready = g_netvsc.offer_ready;
  out->channel_ready = g_netvsc.channel_ready;
  out->offer = g_netvsc.offer;
}

int netvsc_query_offer(struct vmbus_offer_info *out) {
  if (!out) {
    return -1;
  }
  return vmbus_query_offer(&k_hyperv_nic_guid, out);
}

int netvsc_runtime_status(struct netvsc_runtime_status *out) {
  if (!out) {
    return -1;
  }
  netvsc_copy_status(out);
  return 0;
}

int netvsc_refresh_runtime(struct netvsc_runtime_status *out) {
  int advanced = 0;

  if (!hyperv_detect()) {
    netvsc_copy_status(out);
    return -1;
  }

  if (!g_netvsc.offer_ready) {
    if (netvsc_query_offer(&g_netvsc.offer) != 0) {
      netvsc_copy_status(out);
      return -2;
    }
    g_netvsc.offer_ready = 1;
    advanced = 1;
  }

  netvsc_copy_status(out);
  if (g_netvsc.offer_ready) {
    return advanced ? 1 : 0;
  }
  return -4;
}

void netvsc_control_init(struct netvsc_control_state *state) {
  if (!state) {
    return;
  }
  netvsc_memzero(state, sizeof(*state));
  state->phase = NETVSC_CONTROL_IDLE;
  state->next_request_id = 1u;
  state->packet_filter = NETVSC_PACKET_FILTER_DEFAULT;
}

size_t netvsc_control_build_next_request(struct netvsc_control_state *state,
                                         uint8_t *out, size_t cap) {
  uint32_t request_id = 0;

  if (!state || !out || cap == 0u) {
    return 0u;
  }
  if (state->phase == NETVSC_CONTROL_WAIT_INITIALIZE ||
      state->phase == NETVSC_CONTROL_WAIT_QUERY_MTU ||
      state->phase == NETVSC_CONTROL_WAIT_QUERY_MAC ||
      state->phase == NETVSC_CONTROL_WAIT_SET_FILTER ||
      state->phase == NETVSC_CONTROL_READY ||
      state->phase == NETVSC_CONTROL_FAILED) {
    return 0u;
  }

  request_id = state->next_request_id++;
  state->last_request_id = request_id;

  if (!state->initialized) {
    state->phase = NETVSC_CONTROL_WAIT_INITIALIZE;
    return rndis_build_initialize_request(out, cap, request_id, 1u, 0u, 4096u);
  }
  if (state->mtu == 0u) {
    return netvsc_control_build_query_mtu(state, out, cap);
  }
  if (!state->mac_valid) {
    return netvsc_control_build_query_mac(state, out, cap);
  }
  if (!state->filter_set) {
    return netvsc_control_build_set_filter(state, out, cap);
  }
  state->phase = NETVSC_CONTROL_READY;
  return 0u;
}

size_t netvsc_control_build_next_transport(
    struct netvsc_control_state *state,
    struct netvsc_control_transport *transport, uint8_t *out, size_t cap) {
  struct netvsc_control_state next_state;
  uint8_t rndis_buf[128];
  size_t rndis_len = 0u;
  size_t total_len = 0u;

  if (transport) {
    netvsc_memzero(transport, sizeof(*transport));
  }
  if (!state || !out || cap == 0u) {
    return 0u;
  }

  next_state = *state;
  rndis_len =
      netvsc_control_build_next_request(&next_state, rndis_buf, sizeof(rndis_buf));
  if (rndis_len == 0u) {
    *state = next_state;
    return 0u;
  }

  total_len = netvsp_build_rndis_control_message(out, cap, rndis_buf, rndis_len);
  if (total_len == 0u) {
    return 0u;
  }

  *state = next_state;
  if (transport) {
    transport->request_id = state->last_request_id;
    transport->rndis_message_type = netvsc_read_u32_le(rndis_buf);
    transport->netvsp_message_type = NETVSP_MSG_SEND_RNDIS_CONTROL;
    transport->payload_len = (uint32_t)rndis_len;
  }
  return total_len;
}

static size_t netvsc_control_build_query_mtu(struct netvsc_control_state *state,
                                             uint8_t *out, size_t cap) {
  state->phase = NETVSC_CONTROL_WAIT_QUERY_MTU;
  return rndis_build_query_request(out, cap, state->last_request_id,
                                   NETVSC_RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE,
                                   NULL, 0u);
}

static size_t netvsc_control_build_query_mac(struct netvsc_control_state *state,
                                             uint8_t *out, size_t cap) {
  state->phase = NETVSC_CONTROL_WAIT_QUERY_MAC;
  return rndis_build_query_request(out, cap, state->last_request_id,
                                   NETVSC_RNDIS_OID_802_3_CURRENT_ADDRESS,
                                   NULL, 0u);
}

static size_t netvsc_control_build_set_filter(struct netvsc_control_state *state,
                                              uint8_t *out, size_t cap) {
  state->phase = NETVSC_CONTROL_WAIT_SET_FILTER;
  return rndis_build_set_request(out, cap, state->last_request_id,
                                 NETVSC_RNDIS_OID_GEN_CURRENT_PACKET_FILTER,
                                 &state->packet_filter,
                                 sizeof(state->packet_filter));
}

static int netvsc_control_handle_query_mac(struct netvsc_control_state *state,
                                           const uint8_t *buf, size_t len) {
  const uint8_t *payload = NULL;
  size_t payload_len = 0u;

  if (rndis_parse_query_complete(buf, len, state->last_request_id, &payload,
                                 &payload_len) != 0) {
    return -1;
  }
  if (!payload || payload_len < 6u) {
    return -2;
  }

  netvsc_memcpy(state->mac, payload, 6u);
  state->mac_valid = 1u;
  state->last_status = RNDIS_STATUS_SUCCESS;
  return 1;
}

int netvsc_control_handle_response(struct netvsc_control_state *state,
                                   const uint8_t *buf, size_t len) {
  uint32_t mtu = 0u;
  struct rndis_initialize_complete init_complete;
  struct rndis_set_complete set_complete;
  int rc = 0;

  if (!state || !buf || len < sizeof(uint32_t) * 2u) {
    return -1;
  }

  if (state->phase == NETVSC_CONTROL_WAIT_INITIALIZE) {
    if (rndis_parse_initialize_complete(buf, len, &init_complete) != 0 ||
        init_complete.request_id != state->last_request_id ||
        init_complete.status != RNDIS_STATUS_SUCCESS) {
      state->phase = NETVSC_CONTROL_FAILED;
      return -2;
    }
    state->initialized = 1u;
    state->last_status = init_complete.status;
    state->phase = NETVSC_CONTROL_IDLE;
    return 1;
  }

  if (state->phase == NETVSC_CONTROL_WAIT_QUERY_MTU) {
    if (rndis_parse_query_complete_u32(buf, len, state->last_request_id, &mtu) !=
        0) {
      state->phase = NETVSC_CONTROL_FAILED;
      return -3;
    }
    state->mtu = mtu;
    state->last_status = RNDIS_STATUS_SUCCESS;
    state->phase = NETVSC_CONTROL_IDLE;
    return 1;
  }

  if (state->phase == NETVSC_CONTROL_WAIT_QUERY_MAC) {
    rc = netvsc_control_handle_query_mac(state, buf, len);
    if (rc <= 0) {
      state->phase = NETVSC_CONTROL_FAILED;
      return -4;
    }
    state->phase = NETVSC_CONTROL_IDLE;
    return 1;
  }

  if (state->phase == NETVSC_CONTROL_WAIT_SET_FILTER) {
    if (rndis_parse_set_complete(buf, len, state->last_request_id,
                                 &set_complete) != 0 ||
        set_complete.status != RNDIS_STATUS_SUCCESS) {
      state->phase = NETVSC_CONTROL_FAILED;
      return -5;
    }
    state->filter_set = 1u;
    state->last_status = set_complete.status;
    state->phase = NETVSC_CONTROL_READY;
    return 1;
  }

  return 0;
}

int netvsc_control_handle_transport_response(
    struct netvsc_control_state *state, const uint8_t *buf, size_t len) {
  struct netvsp_transport_info transport_info;
  const uint8_t *payload = NULL;
  size_t payload_len = 0u;

  if (!state || !buf) {
    return -1;
  }
  if (netvsp_parse_rndis_control_message(buf, len, &transport_info, &payload,
                                         &payload_len) != 0) {
    return -2;
  }
  if (!payload || payload_len == 0u) {
    return -3;
  }
  return netvsc_control_handle_response(state, payload, payload_len);
}

int netvsc_control_is_ready(const struct netvsc_control_state *state) {
  return state && state->phase == NETVSC_CONTROL_READY;
}
