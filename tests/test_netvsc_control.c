#include <stdio.h>
#include <string.h>

#include "drivers/net/netvsc.h"
#include "drivers/net/netvsp.h"
#include "drivers/net/rndis.h"

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

int run_netvsc_control_tests(void) {
    int fails = 0;
    struct netvsc_control_state state;
    uint8_t buffer[192];
    size_t len = 0;

    netvsc_control_init(&state);
    if (state.phase != NETVSC_CONTROL_IDLE || state.packet_filter != NETVSC_PACKET_FILTER_DEFAULT) {
        printf("[netvsc] control init did not reset state correctly\n");
        fails++;
    }

    len = netvsc_control_build_next_request(&state, buffer, sizeof(buffer));
    if (len == 0u || state.phase != NETVSC_CONTROL_WAIT_INITIALIZE) {
        printf("[netvsc] initial request was not generated\n");
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
            0x00, 0x00, 0x00, 0x00
        };
        if (netvsc_control_handle_response(&state, init_complete, sizeof(init_complete)) != 1 ||
            !state.initialized || state.phase != NETVSC_CONTROL_IDLE) {
            printf("[netvsc] initialize completion was not accepted\n");
            fails++;
        }
    }

    {
        struct netvsc_control_state transport_state;
        struct netvsc_control_transport transport;
        struct netvsp_transport_info transport_info;
        const uint8_t *transport_payload = NULL;
        size_t transport_payload_len = 0u;
        uint8_t init_complete[] = {
            0x02, 0x00, 0x00, 0x80, 0x34, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x20, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        uint8_t transport_response[128];
        size_t transport_response_len = 0u;

        netvsc_control_init(&transport_state);
        len = netvsc_control_build_next_transport(&transport_state, &transport,
                                                  buffer, sizeof(buffer));
        if (len == 0u || transport_state.phase != NETVSC_CONTROL_WAIT_INITIALIZE ||
            transport.request_id != transport_state.last_request_id ||
            transport.rndis_message_type != RNDIS_MSG_INITIALIZE ||
            transport.netvsp_message_type != NETVSP_MSG_SEND_RNDIS_CONTROL) {
            printf("[netvsc] transport wrapper did not encode initialize stage\n");
            fails++;
        } else if (netvsp_parse_rndis_control_message(
                       buffer, len, &transport_info, &transport_payload,
                       &transport_payload_len) != 0 ||
                   transport_info.message_type != NETVSP_MSG_SEND_RNDIS_CONTROL ||
                   transport_info.payload_len != transport.payload_len ||
                   transport_payload_len < sizeof(uint32_t) ||
                   read_u32_le(transport_payload) != RNDIS_MSG_INITIALIZE) {
            printf("[netvsc] transport wrapper did not round-trip through netvsp\n");
            fails++;
        }

        write_u32_le(&init_complete[8], transport.request_id);
        transport_response_len = netvsp_build_rndis_control_message(
            transport_response, sizeof(transport_response), init_complete,
            sizeof(init_complete));
        if (transport_response_len == 0u ||
            netvsc_control_handle_transport_response(
                &transport_state, transport_response, transport_response_len) != 1 ||
            !transport_state.initialized ||
            transport_state.phase != NETVSC_CONTROL_IDLE) {
            printf("[netvsc] transport response did not feed control state\n");
            fails++;
        }
    }

    len = netvsc_control_build_next_request(&state, buffer, sizeof(buffer));
    if (len == 0u) {
        printf("[netvsc] mtu query was not generated after initialize\n");
        fails++;
    }
    {
        const uint32_t *words = (const uint32_t *)buffer;
        if (state.phase != NETVSC_CONTROL_WAIT_QUERY_MTU ||
            words[0] != RNDIS_MSG_QUERY || words[3] != NETVSC_RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE) {
            printf("[netvsc] mtu query stage was not encoded correctly\n");
            fails++;
        }
    }
    {
        uint8_t query_complete[28];
        memset(query_complete, 0, sizeof(query_complete));
        write_u32_le(&query_complete[0], RNDIS_MSG_QUERY_COMPLETE);
        write_u32_le(&query_complete[4], (uint32_t)sizeof(query_complete));
        write_u32_le(&query_complete[8], state.last_request_id);
        write_u32_le(&query_complete[12], RNDIS_STATUS_SUCCESS);
        write_u32_le(&query_complete[16], 4u);
        write_u32_le(&query_complete[20], 16u);
        write_u32_le(&query_complete[24], 1500u);
        if (netvsc_control_handle_response(&state, query_complete, sizeof(query_complete)) != 1 ||
            state.mtu != 1500u || state.phase != NETVSC_CONTROL_IDLE) {
            printf("[netvsc] mtu query response was not parsed correctly\n");
            fails++;
        }
    }

    len = netvsc_control_build_next_request(&state, buffer, sizeof(buffer));
    if (len == 0u) {
        printf("[netvsc] mac query was not generated after mtu\n");
        fails++;
    }
    {
        const uint32_t *words = (const uint32_t *)buffer;
        if (state.phase != NETVSC_CONTROL_WAIT_QUERY_MAC ||
            words[0] != RNDIS_MSG_QUERY || words[3] != NETVSC_RNDIS_OID_802_3_CURRENT_ADDRESS) {
            printf("[netvsc] mac query stage was not encoded correctly\n");
            fails++;
        }
    }
    {
        uint8_t mac_complete[30];
        memset(mac_complete, 0, sizeof(mac_complete));
        write_u32_le(&mac_complete[0], RNDIS_MSG_QUERY_COMPLETE);
        write_u32_le(&mac_complete[4], (uint32_t)sizeof(mac_complete));
        write_u32_le(&mac_complete[8], state.last_request_id);
        write_u32_le(&mac_complete[12], RNDIS_STATUS_SUCCESS);
        write_u32_le(&mac_complete[16], 6u);
        write_u32_le(&mac_complete[20], 16u);
        mac_complete[24] = 0x02u;
        mac_complete[25] = 0x15u;
        mac_complete[26] = 0xCAu;
        mac_complete[27] = 0xFEu;
        mac_complete[28] = 0x00u;
        mac_complete[29] = 0x01u;
        if (netvsc_control_handle_response(&state, mac_complete, sizeof(mac_complete)) != 1 ||
            !state.mac_valid || state.mac[0] != 0x02u || state.mac[5] != 0x01u ||
            state.phase != NETVSC_CONTROL_IDLE) {
            printf("[netvsc] mac query response was not parsed correctly\n");
            fails++;
        }
    }

    len = netvsc_control_build_next_request(&state, buffer, sizeof(buffer));
    if (len == 0u) {
        printf("[netvsc] packet filter set request was not generated\n");
        fails++;
    }
    {
        const uint32_t *words = (const uint32_t *)buffer;
        if (state.phase != NETVSC_CONTROL_WAIT_SET_FILTER ||
            words[0] != RNDIS_MSG_SET ||
            words[3] != NETVSC_RNDIS_OID_GEN_CURRENT_PACKET_FILTER ||
            words[7] != NETVSC_PACKET_FILTER_DEFAULT) {
            printf("[netvsc] packet filter request was not encoded correctly\n");
            fails++;
        }
    }
    {
        uint8_t set_complete[] = {
            0x05, 0x00, 0x00, 0x80, 0x10, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        write_u32_le(&set_complete[8], state.last_request_id);
        if (netvsc_control_handle_response(&state, set_complete, sizeof(set_complete)) != 1 ||
            !netvsc_control_is_ready(&state) || !state.filter_set) {
            printf("[netvsc] set filter completion did not transition to ready\n");
            fails++;
        }
    }

    netvsc_control_init(&state);
    len = netvsc_control_build_next_request(&state, buffer, sizeof(buffer));
    if (len == 0u) {
        printf("[netvsc] second init fixture was not generated\n");
        fails++;
    }
    {
        uint8_t bad_init_complete[52];
        memset(bad_init_complete, 0, sizeof(bad_init_complete));
        write_u32_le(&bad_init_complete[0], RNDIS_MSG_INITIALIZE_COMPLETE);
        write_u32_le(&bad_init_complete[4], (uint32_t)sizeof(bad_init_complete));
        write_u32_le(&bad_init_complete[8], state.last_request_id);
        write_u32_le(&bad_init_complete[12], 0xC0000001u);
        if (netvsc_control_handle_response(&state, bad_init_complete, sizeof(bad_init_complete)) >= 0 ||
            state.phase != NETVSC_CONTROL_FAILED) {
            printf("[netvsc] failed initialize response did not poison the state machine\n");
            fails++;
        }
    }

    if (fails == 0) {
        printf("[tests] netvsc_control OK\n");
    }
    return fails;
}
