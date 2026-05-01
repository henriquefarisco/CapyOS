/*
 * test_browser_ipc.c (F3.3a)
 *
 * Cobertura de regressao para o codec de IPC do browser. Valida:
 *   - encode + decode round-trip de cada kind
 *   - rejeicao de magic incorreto
 *   - rejeicao de payload_len > MAX
 *   - rejeicao de kind invalido
 *   - rejeicao de buffer curto
 *   - rejeicao de NULL
 *   - introspeccao de kind (request/event/known) e min_payload
 *
 * Tests register-themselves via the unit_tests binary (UNIT_TEST define).
 */

#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_browser_ipc_passed = 0;
static int g_browser_ipc_failed = 0;

#define BIPC_CHECK(cond, label)                                          \
    do {                                                                 \
        if (cond) {                                                      \
            ++g_browser_ipc_passed;                                      \
        } else {                                                         \
            ++g_browser_ipc_failed;                                      \
            printf("    [FAIL] %s:%d %s\n", __FILE__, __LINE__, label);  \
        }                                                                \
    } while (0)

static void test_browser_ipc_encode_decode_roundtrip(void) {
    static const uint16_t kinds[] = {
        BROWSER_IPC_NAVIGATE,
        BROWSER_IPC_CANCEL,
        BROWSER_IPC_BACK,
        BROWSER_IPC_FORWARD,
        BROWSER_IPC_RELOAD,
        BROWSER_IPC_SCROLL,
        BROWSER_IPC_RESIZE,
        BROWSER_IPC_CLICK,
        BROWSER_IPC_KEY,
        BROWSER_IPC_PING,
        BROWSER_IPC_SHUTDOWN,
        BROWSER_IPC_EVENT_TITLE,
        BROWSER_IPC_EVENT_NAV_STARTED,
        BROWSER_IPC_EVENT_NAV_PROGRESS,
        BROWSER_IPC_EVENT_NAV_READY,
        BROWSER_IPC_EVENT_NAV_FAILED,
        BROWSER_IPC_EVENT_NAV_CANCELLED,
        BROWSER_IPC_EVENT_FRAME,
        BROWSER_IPC_EVENT_CURSOR,
        BROWSER_IPC_EVENT_PONG,
        BROWSER_IPC_EVENT_LOG
    };
    const size_t n = sizeof(kinds) / sizeof(kinds[0]);

    for (size_t i = 0; i < n; ++i) {
        struct browser_ipc_header in = {
            .magic       = BROWSER_IPC_MAGIC,
            .kind        = kinds[i],
            .seq         = (uint32_t)(0x1234ABCDu + (uint32_t)i),
            .payload_len = (uint32_t)(i * 17u)
        };
        uint8_t buf[BROWSER_IPC_HEADER_SIZE];
        BIPC_CHECK(browser_ipc_header_encode(&in, buf, sizeof(buf)) == BROWSER_IPC_OK,
                   "encode ok");

        struct browser_ipc_header out;
        memset(&out, 0xAA, sizeof(out));
        BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_OK,
                   "decode ok");
        BIPC_CHECK(out.magic == in.magic, "magic preserved");
        BIPC_CHECK(out.kind == in.kind, "kind preserved");
        BIPC_CHECK(out.seq == in.seq, "seq preserved");
        BIPC_CHECK(out.payload_len == in.payload_len, "payload_len preserved");
    }
}

static void test_browser_ipc_be_byte_order(void) {
    /* Confirma layout big-endian no wire. */
    struct browser_ipc_header in = {
        .magic       = 0xCB1Bu,
        .kind        = 0x0102u,
        .seq         = 0xDEADBEEFu,
        .payload_len = 0x12345678u
    };
    uint8_t buf[BROWSER_IPC_HEADER_SIZE];
    BIPC_CHECK(browser_ipc_header_encode(&in, buf, sizeof(buf)) == BROWSER_IPC_OK,
               "encode BE ok");
    BIPC_CHECK(buf[0] == 0xCBu && buf[1] == 0x1Bu, "magic BE");
    BIPC_CHECK(buf[2] == 0x01u && buf[3] == 0x02u, "kind BE");
    BIPC_CHECK(buf[4] == 0xDEu && buf[5] == 0xADu && buf[6] == 0xBEu && buf[7] == 0xEFu,
               "seq BE");
    BIPC_CHECK(buf[8] == 0x12u && buf[9] == 0x34u && buf[10] == 0x56u && buf[11] == 0x78u,
               "payload_len BE");
}

static void test_browser_ipc_decode_rejects_bad_magic(void) {
    uint8_t buf[BROWSER_IPC_HEADER_SIZE] = {0};
    /* magic 0x0000 != 0xCB1B */
    buf[2] = 0x00; buf[3] = (uint8_t)BROWSER_IPC_NAVIGATE;
    struct browser_ipc_header out;
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_ERR_MAGIC,
               "rejects bad magic");

    /* magic 0xCB1C (versao futura) tambem deve falhar como magic */
    buf[0] = 0xCB; buf[1] = 0x1C;
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_ERR_MAGIC,
               "rejects future magic");
}

static void test_browser_ipc_decode_rejects_bad_kind(void) {
    struct browser_ipc_header in = {
        .magic       = BROWSER_IPC_MAGIC,
        .kind        = 0x00FFu, /* nao mapeado */
        .seq         = 1,
        .payload_len = 0
    };
    uint8_t buf[BROWSER_IPC_HEADER_SIZE];
    BIPC_CHECK(browser_ipc_header_encode(&in, buf, sizeof(buf)) == BROWSER_IPC_OK,
               "encode unknown kind ok");

    struct browser_ipc_header out;
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_ERR_KIND,
               "rejects unknown kind");
}

static void test_browser_ipc_decode_rejects_payload_too_large(void) {
    struct browser_ipc_header in = {
        .magic       = BROWSER_IPC_MAGIC,
        .kind        = BROWSER_IPC_NAVIGATE,
        .seq         = 1,
        .payload_len = BROWSER_IPC_MAX_PAYLOAD + 1u
    };
    uint8_t buf[BROWSER_IPC_HEADER_SIZE];
    BIPC_CHECK(browser_ipc_header_encode(&in, buf, sizeof(buf)) == BROWSER_IPC_OK,
               "encode oversize ok");

    struct browser_ipc_header out;
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_ERR_PAYLOAD,
               "rejects oversize payload");

    /* MAX exatamente deve passar */
    in.payload_len = BROWSER_IPC_MAX_PAYLOAD;
    BIPC_CHECK(browser_ipc_header_encode(&in, buf, sizeof(buf)) == BROWSER_IPC_OK,
               "encode MAX ok");
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_OK,
               "accepts MAX payload");
}

static void test_browser_ipc_short_buffer(void) {
    uint8_t buf[BROWSER_IPC_HEADER_SIZE - 1] = {0};
    struct browser_ipc_header out;
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), &out) == BROWSER_IPC_ERR_SHORT,
               "decode short buffer rejects");

    struct browser_ipc_header in = {
        .magic = BROWSER_IPC_MAGIC,
        .kind  = BROWSER_IPC_PING,
    };
    uint8_t small[BROWSER_IPC_HEADER_SIZE - 1];
    BIPC_CHECK(browser_ipc_header_encode(&in, small, sizeof(small)) == BROWSER_IPC_ERR_SHORT,
               "encode small buffer rejects");
}

static void test_browser_ipc_null_args(void) {
    uint8_t buf[BROWSER_IPC_HEADER_SIZE];
    struct browser_ipc_header hdr = {
        .magic = BROWSER_IPC_MAGIC,
        .kind  = BROWSER_IPC_PING
    };
    BIPC_CHECK(browser_ipc_header_encode(NULL, buf, sizeof(buf)) == BROWSER_IPC_ERR_INVAL,
               "encode NULL hdr");
    BIPC_CHECK(browser_ipc_header_encode(&hdr, NULL, sizeof(buf)) == BROWSER_IPC_ERR_INVAL,
               "encode NULL out");
    BIPC_CHECK(browser_ipc_header_decode(NULL, sizeof(buf), &hdr) == BROWSER_IPC_ERR_INVAL,
               "decode NULL in");
    BIPC_CHECK(browser_ipc_header_decode(buf, sizeof(buf), NULL) == BROWSER_IPC_ERR_INVAL,
               "decode NULL out");
}

static void test_browser_ipc_kind_classification(void) {
    BIPC_CHECK(browser_ipc_kind_is_request(BROWSER_IPC_NAVIGATE), "NAVIGATE is request");
    BIPC_CHECK(browser_ipc_kind_is_request(BROWSER_IPC_PING), "PING is request");
    BIPC_CHECK(!browser_ipc_kind_is_request(BROWSER_IPC_EVENT_TITLE), "TITLE is not request");
    BIPC_CHECK(browser_ipc_kind_is_event(BROWSER_IPC_EVENT_TITLE), "TITLE is event");
    BIPC_CHECK(browser_ipc_kind_is_event(BROWSER_IPC_EVENT_PONG), "PONG is event");
    BIPC_CHECK(!browser_ipc_kind_is_event(BROWSER_IPC_NAVIGATE), "NAVIGATE is not event");
    BIPC_CHECK(browser_ipc_kind_is_known(BROWSER_IPC_PING), "PING is known");
    BIPC_CHECK(browser_ipc_kind_is_known(BROWSER_IPC_EVENT_FRAME), "FRAME is known");
    BIPC_CHECK(!browser_ipc_kind_is_known(0xFFFFu), "0xFFFF unknown");
    BIPC_CHECK(!browser_ipc_kind_is_known(0x0000u), "0x0000 unknown");
    BIPC_CHECK(!browser_ipc_kind_is_known(0x00FFu), "0x00FF unknown");
}

static void test_browser_ipc_min_payload(void) {
    BIPC_CHECK(browser_ipc_kind_min_payload(BROWSER_IPC_CANCEL) == 0u,
               "CANCEL has 0 payload");
    BIPC_CHECK(browser_ipc_kind_min_payload(BROWSER_IPC_PING) == 4u,
               "PING has 4-byte payload (nonce u32)");
    BIPC_CHECK(browser_ipc_kind_min_payload(BROWSER_IPC_CLICK) == 5u,
               "CLICK has 5-byte payload (x+y+button)");
    BIPC_CHECK(browser_ipc_kind_min_payload(BROWSER_IPC_NAVIGATE) == 2u,
               "NAVIGATE min has 2-byte payload (url_len)");
    BIPC_CHECK(browser_ipc_kind_min_payload(BROWSER_IPC_EVENT_FRAME) == 12u,
               "EVENT_FRAME min has 12-byte payload (nav+w+h+stride)");
    BIPC_CHECK(browser_ipc_kind_min_payload(0xFFFFu) == (uint32_t)-1,
               "unknown kind returns -1");
}

int test_browser_ipc_run(void) {
    printf("[test_browser_ipc]\n");
    test_browser_ipc_encode_decode_roundtrip();
    test_browser_ipc_be_byte_order();
    test_browser_ipc_decode_rejects_bad_magic();
    test_browser_ipc_decode_rejects_bad_kind();
    test_browser_ipc_decode_rejects_payload_too_large();
    test_browser_ipc_short_buffer();
    test_browser_ipc_null_args();
    test_browser_ipc_kind_classification();
    test_browser_ipc_min_payload();
    printf("  -> %d passed, %d failed\n",
           g_browser_ipc_passed, g_browser_ipc_failed);
    return g_browser_ipc_failed;
}
