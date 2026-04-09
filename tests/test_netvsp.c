#include <stdio.h>
#include <string.h>

#include "drivers/net/netvsp.h"

static void write_u32_le(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

int run_netvsp_tests(void) {
  int fails = 0;
  uint8_t payload[8] = {0x02u, 0x00u, 0x00u, 0x00u,
                        0x18u, 0x00u, 0x00u, 0x00u};
  uint8_t buffer[64];
  const uint8_t *parsed_payload = NULL;
  size_t parsed_payload_len = 0u;
  struct netvsp_transport_info info;
  size_t len = 0u;
  struct netvsp_init_complete init_complete;

  len = netvsp_build_init_message(buffer, sizeof(buffer), 5u, 1u);
  if (len != 16u) {
    printf("[netvsp] init envelope length mismatch\n");
    fails++;
  }
  {
    uint32_t *words = (uint32_t *)buffer;
    if (words[0] != NETVSP_MSG_INIT || words[2] != 5u || words[3] != 1u) {
      printf("[netvsp] init envelope was not encoded correctly\n");
      fails++;
    }
  }
  memset(buffer, 0, sizeof(buffer));
  write_u32_le(&buffer[0], NETVSP_MSG_INIT_COMPLETE);
  write_u32_le(&buffer[4], 20u);
  write_u32_le(&buffer[8], NETVSP_STATUS_SUCCESS);
  write_u32_le(&buffer[12], 6u);
  write_u32_le(&buffer[16], 1u);
  if (netvsp_parse_init_complete(buffer, 20u, &init_complete) != 0 ||
      init_complete.status != NETVSP_STATUS_SUCCESS ||
      init_complete.protocol_major != 6u ||
      init_complete.protocol_minor != 1u) {
    printf("[netvsp] init completion parser failed\n");
    fails++;
  }

  len = netvsp_build_rndis_control_message(buffer, sizeof(buffer), payload,
                                           sizeof(payload));
  if (len != sizeof(payload) + 20u) {
    printf("[netvsp] control envelope length mismatch\n");
    fails++;
  }
  if (netvsp_parse_rndis_control_message(buffer, len, &info, &parsed_payload,
                                         &parsed_payload_len) != 0 ||
      info.message_type != NETVSP_MSG_SEND_RNDIS_CONTROL ||
      info.channel_type != NETVSP_CHANNEL_TYPE_CONTROL ||
      parsed_payload_len != sizeof(payload) ||
      memcmp(parsed_payload, payload, sizeof(payload)) != 0) {
    printf("[netvsp] control envelope round-trip failed\n");
    fails++;
  }

  if (netvsp_build_rndis_control_message(buffer, 8u, payload, sizeof(payload)) !=
      0u) {
    printf("[netvsp] short buffer should reject control envelope\n");
    fails++;
  }

  memset(buffer, 0, sizeof(buffer));
  write_u32_le(&buffer[0], 0xDEADBEEFu);
  write_u32_le(&buffer[4], 20u);
  write_u32_le(&buffer[8], NETVSP_CHANNEL_TYPE_CONTROL);
  write_u32_le(&buffer[12], 0u);
  if (netvsp_parse_rndis_control_message(buffer, 20u, &info, &parsed_payload,
                                         &parsed_payload_len) == 0) {
    printf("[netvsp] parser accepted an unexpected message type\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] netvsp OK\n");
  }
  return fails;
}
