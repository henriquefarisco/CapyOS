#include <stdio.h>

#include "drivers/storage/storvsc_session.h"

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

int run_storvsc_session_tests(void) {
  int fails = 0;
  struct storvsc_session_state state;
  uint8_t buffer[STORVSP_PACKET_SIZE];
  struct storvsp_packet_header_info header;
  size_t len = 0u;

  storvsc_session_init(&state, STORVSP_PROTO_WIN10);
  if (state.phase != STORVSC_SESSION_WAIT_BEGIN ||
      state.requested_major_minor != STORVSP_PROTO_WIN10) {
    printf("[storvsc_session] init failed\n");
    fails++;
  }

  len = storvsc_session_build_next(&state, buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_BEGIN_INITIALIZATION ||
      state.last_request_operation != STORVSP_OPERATION_BEGIN_INITIALIZATION ||
      state.request_sent != 1u) {
    printf("[storvsc_session] begin step failed\n");
    fails++;
  }
  set_u32(&buffer[0], STORVSP_OPERATION_COMPLETE_IO);
  if (storvsc_session_handle_response(&state, buffer, len) != 1 ||
      state.phase != STORVSC_SESSION_WAIT_VERSION) {
    printf("[storvsc_session] begin response did not advance\n");
    fails++;
  }

  len = storvsc_session_build_next(&state, buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_QUERY_PROTOCOL_VERSION) {
    printf("[storvsc_session] version step failed\n");
    fails++;
  }
  set_u32(&buffer[0], STORVSP_OPERATION_COMPLETE_IO);
  set_u16(&buffer[12], STORVSP_PROTO_WIN8_1);
  set_u16(&buffer[14], 0u);
  if (storvsc_session_handle_response(&state, buffer, len) != 1 ||
      state.phase != STORVSC_SESSION_WAIT_PROPERTIES ||
      state.negotiated_major_minor != STORVSP_PROTO_WIN8_1) {
    printf("[storvsc_session] version response failed\n");
    fails++;
  }

  len = storvsc_session_build_next(&state, buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_QUERY_PROPERTIES) {
    printf("[storvsc_session] properties step failed\n");
    fails++;
  }
  set_u32(&buffer[0], STORVSP_OPERATION_COMPLETE_IO);
  set_u16(&buffer[16], 4u);
  set_u16(&buffer[18], 0u);
  set_u32(&buffer[20], STORVSP_CHANNEL_SUPPORTS_MULTI_CHANNEL);
  set_u32(&buffer[24], 512u * 1024u);
  if (storvsc_session_handle_response(&state, buffer, len) != 1 ||
      state.phase != STORVSC_SESSION_WAIT_END ||
      state.properties.max_channel_count != 4u ||
      state.properties.max_transfer_bytes != 512u * 1024u) {
    printf("[storvsc_session] properties response failed\n");
    fails++;
  }

  len = storvsc_session_build_next(&state, buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_END_INITIALIZATION) {
    printf("[storvsc_session] end init step failed\n");
    fails++;
  }
  set_u32(&buffer[0], STORVSP_OPERATION_COMPLETE_IO);
  if (storvsc_session_handle_response(&state, buffer, len) != 1 ||
      state.phase != STORVSC_SESSION_READY) {
    printf("[storvsc_session] end init response failed\n");
    fails++;
  }
  len = storvsc_session_build_next(&state, buffer, sizeof(buffer));
  if (len != 0u || !storvsc_session_is_ready(&state)) {
    printf("[storvsc_session] ready state should not send extra control\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] storvsc_session OK\n");
  }
  return fails;
}
