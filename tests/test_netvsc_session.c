#include <stdio.h>
#include <string.h>

#include "drivers/net/netvsc_session.h"
#include "drivers/net/netvsp.h"
#include "drivers/net/rndis.h"

static void write_u32_le(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static int wrap_rndis(uint8_t *dst, size_t cap, const uint8_t *payload,
                      size_t payload_len) {
  return (int)netvsp_build_rndis_control_message(dst, cap, payload, payload_len);
}

int run_netvsc_session_tests(void) {
  int fails = 0;
  struct netvsc_session_state state;
  struct netvsc_control_transport transport;
  uint8_t buffer[192];
  size_t len = 0u;

  netvsc_session_init(&state, 6u, 1u);
  if (state.phase != NETVSC_SESSION_WAIT_INIT || state.requested_major != 6u ||
      state.requested_minor != 1u) {
    printf("[netvsc_session] init did not seed handshake state\n");
    fails++;
  }

  len = netvsc_session_build_next(&state, &transport, buffer, sizeof(buffer));
  if (len == 0u || transport.netvsp_message_type != NETVSP_MSG_INIT ||
      state.init_sent != 1u) {
    printf("[netvsc_session] init request was not generated\n");
    fails++;
  }

  {
    uint8_t init_complete[20];
    memset(init_complete, 0, sizeof(init_complete));
    write_u32_le(&init_complete[0], NETVSP_MSG_INIT_COMPLETE);
    write_u32_le(&init_complete[4], (uint32_t)sizeof(init_complete));
    write_u32_le(&init_complete[8], NETVSP_STATUS_SUCCESS);
    write_u32_le(&init_complete[12], 6u);
    write_u32_le(&init_complete[16], 1u);
    if (netvsc_session_handle_response(&state, init_complete,
                                       sizeof(init_complete)) != 1 ||
        state.phase != NETVSC_SESSION_CONTROL ||
        state.negotiated_major != 6u || state.negotiated_minor != 1u) {
      printf("[netvsc_session] init completion did not advance session\n");
      fails++;
    }
  }

  len = netvsc_session_build_next(&state, &transport, buffer, sizeof(buffer));
  if (len == 0u || transport.netvsp_message_type != NETVSP_MSG_SEND_RNDIS_CONTROL ||
      transport.rndis_message_type != RNDIS_MSG_INITIALIZE) {
    printf("[netvsc_session] control stage did not emit wrapped initialize\n");
    fails++;
  }

  {
    uint8_t init_complete[] = {
        0x02, 0x00, 0x00, 0x80, 0x34, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    uint8_t response[128];
    write_u32_le(&init_complete[8], transport.request_id);
    if (wrap_rndis(response, sizeof(response), init_complete,
                   sizeof(init_complete)) <= 0 ||
        netvsc_session_handle_response(&state, response, 72u) != 1 ||
        !state.control.initialized) {
      printf("[netvsc_session] wrapped initialize completion failed\n");
      fails++;
    }
  }

  {
    uint8_t response[128];
    uint8_t query_complete[28];

    len = netvsc_session_build_next(&state, &transport, buffer, sizeof(buffer));
    memset(query_complete, 0, sizeof(query_complete));
    write_u32_le(&query_complete[0], RNDIS_MSG_QUERY_COMPLETE);
    write_u32_le(&query_complete[4], (uint32_t)sizeof(query_complete));
    write_u32_le(&query_complete[8], transport.request_id);
    write_u32_le(&query_complete[12], RNDIS_STATUS_SUCCESS);
    write_u32_le(&query_complete[16], 4u);
    write_u32_le(&query_complete[20], 16u);
    write_u32_le(&query_complete[24], 1500u);
    if (len == 0u || transport.rndis_message_type != RNDIS_MSG_QUERY ||
        wrap_rndis(response, sizeof(response), query_complete,
                   sizeof(query_complete)) <= 0 ||
        netvsc_session_handle_response(&state, response, 48u) != 1 ||
        state.control.mtu != 1500u) {
      printf("[netvsc_session] wrapped mtu query stage failed\n");
      fails++;
    }
  }

  {
    uint8_t response[128];
    uint8_t mac_complete[30];

    len = netvsc_session_build_next(&state, &transport, buffer, sizeof(buffer));
    memset(mac_complete, 0, sizeof(mac_complete));
    write_u32_le(&mac_complete[0], RNDIS_MSG_QUERY_COMPLETE);
    write_u32_le(&mac_complete[4], (uint32_t)sizeof(mac_complete));
    write_u32_le(&mac_complete[8], transport.request_id);
    write_u32_le(&mac_complete[12], RNDIS_STATUS_SUCCESS);
    write_u32_le(&mac_complete[16], 6u);
    write_u32_le(&mac_complete[20], 16u);
    mac_complete[24] = 0x02u;
    mac_complete[25] = 0x15u;
    mac_complete[26] = 0xCAu;
    mac_complete[27] = 0xFEu;
    mac_complete[28] = 0x00u;
    mac_complete[29] = 0x01u;
    if (len == 0u || transport.rndis_message_type != RNDIS_MSG_QUERY ||
        wrap_rndis(response, sizeof(response), mac_complete, sizeof(mac_complete)) <=
            0 ||
        netvsc_session_handle_response(&state, response, 50u) != 1 ||
        !state.control.mac_valid || state.control.mac[5] != 0x01u) {
      printf("[netvsc_session] wrapped mac query stage failed\n");
      fails++;
    }
  }

  {
    uint8_t response[128];
    uint8_t set_complete[] = {
        0x05, 0x00, 0x00, 0x80, 0x10, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    len = netvsc_session_build_next(&state, &transport, buffer, sizeof(buffer));
    write_u32_le(&set_complete[8], transport.request_id);
    if (len == 0u || transport.rndis_message_type != RNDIS_MSG_SET ||
        wrap_rndis(response, sizeof(response), set_complete, sizeof(set_complete)) <=
            0 ||
        netvsc_session_handle_response(&state, response, 36u) != 1 ||
        !netvsc_session_is_ready(&state)) {
      printf("[netvsc_session] wrapped set filter stage failed\n");
      fails++;
    }
  }

  if (fails == 0) {
    printf("[tests] netvsc_session OK\n");
  }
  return fails;
}
