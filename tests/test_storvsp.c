#include <stdio.h>

#include "drivers/storage/storvsp.h"

int run_storvsp_tests(void) {
  int fails = 0;
  uint8_t buffer[STORVSP_PACKET_SIZE];
  struct storvsp_packet_header_info header;
  struct storvsp_protocol_response version;
  struct storvsp_channel_properties properties;
  uint32_t status = 1u;
  size_t len = 0u;

  len = storvsp_build_begin_init(buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_BEGIN_INITIALIZATION ||
      header.flags != STORVSP_FLAG_REQUEST_COMPLETION) {
    printf("[storvsp] begin init packet failed\n");
    fails++;
  }

  len = storvsp_build_query_protocol(buffer, sizeof(buffer), STORVSP_PROTO_WIN10);
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_protocol_response(buffer, len, &version) != 0 ||
      version.negotiated_major_minor != STORVSP_PROTO_WIN10) {
    printf("[storvsp] protocol packet round-trip failed\n");
    fails++;
  }

  len = storvsp_build_query_properties(buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_properties_response(buffer, len, &properties, &status) != 0 ||
      status != STORVSP_STATUS_SUCCESS) {
    printf("[storvsp] properties packet round-trip failed\n");
    fails++;
  }

  len = storvsp_build_end_init(buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_END_INITIALIZATION) {
    printf("[storvsp] end init packet failed\n");
    fails++;
  }

  len = storvsp_build_enumerate_bus(buffer, sizeof(buffer));
  if (len != STORVSP_PACKET_SIZE ||
      storvsp_parse_header(buffer, len, &header) != 0 ||
      header.operation != STORVSP_OPERATION_ENUMERATE_BUS) {
    printf("[storvsp] enumerate bus packet failed\n");
    fails++;
  }

  if (storvsp_build_begin_init(buffer, 8u) != 0u) {
    printf("[storvsp] short buffer should fail\n");
    fails++;
  }

  if (fails == 0) {
    printf("[tests] storvsp OK\n");
  }
  return fails;
}
