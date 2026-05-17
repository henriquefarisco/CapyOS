#include <stdio.h>
#include <string.h>

#include "drivers/net/rndis.h"

int run_rndis_tests(void) {
    int fails = 0;
    uint8_t buffer[128];

    memset(buffer, 0, sizeof(buffer));
    {
        size_t len = rndis_build_initialize_request(buffer, sizeof(buffer), 7u, 1u, 0u, 4096u);
        const uint32_t *words = (const uint32_t *)buffer;
        if (len != 24u || words[0] != RNDIS_MSG_INITIALIZE || words[1] != 24u ||
            words[2] != 7u || words[3] != 1u || words[4] != 0u || words[5] != 4096u) {
            printf("[rndis] initialize request builder produced invalid frame\n");
            fails++;
        }
    }

    memset(buffer, 0, sizeof(buffer));
    {
        uint32_t query_payload = 0x11223344u;
        size_t len = rndis_build_query_request(buffer, sizeof(buffer), 9u, 0x01020304u,
                                               &query_payload, sizeof(query_payload));
        const uint32_t *words = (const uint32_t *)buffer;
        if (len != 32u || words[0] != RNDIS_MSG_QUERY || words[1] != 32u ||
            words[2] != 9u || words[3] != 0x01020304u || words[4] != 4u ||
            words[5] != 20u || words[7] != 0x11223344u) {
            printf("[rndis] query request builder produced invalid frame\n");
            fails++;
        }
    }

    memset(buffer, 0, sizeof(buffer));
    {
        uint32_t filter = 0x0000000Bu;
        size_t len = rndis_build_set_request(buffer, sizeof(buffer), 13u, 0x0101010Eu,
                                             &filter, sizeof(filter));
        const uint32_t *words = (const uint32_t *)buffer;
        if (len != 32u || words[0] != RNDIS_MSG_SET || words[2] != 13u ||
            words[3] != 0x0101010Eu || words[4] != 4u || words[5] != 20u ||
            words[7] != filter) {
            printf("[rndis] set request builder produced invalid frame\n");
            fails++;
        }
    }

    {
        uint8_t init_complete[] = {
            0x02, 0x00, 0x00, 0x80, 0x34, 0x00, 0x00, 0x00,
            0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x20, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        struct rndis_initialize_complete out;
        memset(&out, 0, sizeof(out));
        if (rndis_parse_initialize_complete(init_complete, sizeof(init_complete), &out) != 0 ||
            out.request_id != 7u || out.status != RNDIS_STATUS_SUCCESS ||
            out.max_packets_per_transfer != 32u || out.max_transfer_size != 4096u) {
            printf("[rndis] initialize complete parser rejected a valid reply\n");
            fails++;
        }
    }

    {
        uint8_t query_complete[] = {
            0x04, 0x00, 0x00, 0x80, 0x1c, 0x00, 0x00, 0x00,
            0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
            0x78, 0x56, 0x34, 0x12
        };
        uint32_t value = 0;
        if (rndis_parse_query_complete_u32(query_complete, sizeof(query_complete), 9u, &value) != 0 ||
            value != 0x12345678u) {
            printf("[rndis] query complete parser rejected a valid u32 payload\n");
            fails++;
        }
    }

    {
        uint8_t bad_query_complete[] = {
            0x04, 0x00, 0x00, 0x80, 0x18, 0x00, 0x00, 0x00,
            0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x08, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00
        };
        const uint8_t *payload = NULL;
        size_t payload_len = 0;
        if (rndis_parse_query_complete(bad_query_complete, sizeof(bad_query_complete), 9u,
                                       &payload, &payload_len) == 0) {
            printf("[rndis] malformed query complete should have failed\n");
            fails++;
        }
    }

    {
        uint8_t set_complete[] = {
            0x05, 0x00, 0x00, 0x80, 0x10, 0x00, 0x00, 0x00,
            0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        struct rndis_set_complete out;
        memset(&out, 0, sizeof(out));
        if (rndis_parse_set_complete(set_complete, sizeof(set_complete), 13u, &out) != 0 ||
            out.status != RNDIS_STATUS_SUCCESS) {
            printf("[rndis] set complete parser rejected a valid reply\n");
            fails++;
        }
    }

    if (fails == 0) {
        printf("[tests] rndis OK\n");
    }
    return fails;
}
