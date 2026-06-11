/*
 * tests/net/test_http_chunked.c
 *
 * Host-side coverage for the HTTP/1.1 chunked transfer-encoding parser
 * (src/net/services/http/http_chunked.c). Pure buffer logic — no
 * transport, allocation or global state — so this test links only that
 * TU and forward-declares its functions (declared in the module-internal
 * http_internal.h). Chunk parsing is a classic request-smuggling /
 * buffer-overrun vector, so the cases lock the size parser (incl. the
 * ";ext" skip), the completeness probe and the in-place decode, plus the
 * fail-closed paths on malformed / truncated input.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Defined in http_chunked.c (declared in internal/http_internal.h). */
int http_hex_digit(char ch);
int http_parse_chunk_size(const uint8_t *buf, size_t len,
                          size_t *header_len, size_t *chunk_size);
int http_chunked_complete(const uint8_t *buf, size_t len);
int http_decode_chunked_body(uint8_t *body, size_t *body_len);

static int g_failures = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            printf("[FAIL] http_chunked: %s\n", (msg));                    \
        }                                                                  \
    } while (0)

static void test_hex_digit(void) {
    CHECK(http_hex_digit('0') == 0 && http_hex_digit('9') == 9, "decimal digits");
    CHECK(http_hex_digit('a') == 10 && http_hex_digit('f') == 15, "lowercase hex");
    CHECK(http_hex_digit('A') == 10 && http_hex_digit('F') == 15, "uppercase hex");
    CHECK(http_hex_digit('g') == -1 && http_hex_digit(' ') == -1, "non-hex rejected");
}

static void test_parse_chunk_size_basic(void) {
    size_t hlen = 0;
    size_t csize = 0;
    CHECK(http_parse_chunk_size((const uint8_t *)"5\r\n", 3, &hlen, &csize) == 0 &&
              csize == 5 && hlen == 3,
          "decimal chunk size");
    hlen = csize = 0;
    CHECK(http_parse_chunk_size((const uint8_t *)"ff\r\n", 4, &hlen, &csize) == 0 &&
              csize == 255 && hlen == 4,
          "hex chunk size 0xff");
    hlen = csize = 0;
    CHECK(http_parse_chunk_size((const uint8_t *)"1a\r\n", 4, &hlen, &csize) == 0 &&
              csize == 26 && hlen == 4,
          "hex chunk size 0x1a");
}

static void test_parse_chunk_size_skips_extension(void) {
    size_t hlen = 0;
    size_t csize = 0;
    /* "5;name=val\r\n" — the chunk extension must be skipped, leaving the
     * decoded size 5 and the header length spanning the whole line. */
    const char *line = "5;name=val\r\n";
    CHECK(http_parse_chunk_size((const uint8_t *)line, 12, &hlen, &csize) == 0 &&
              csize == 5 && hlen == 12,
          "chunk extension skipped, size preserved");
}

static void test_parse_chunk_size_rejects_malformed(void) {
    size_t hlen = 0;
    size_t csize = 0;
    CHECK(http_parse_chunk_size((const uint8_t *)"x\r\n", 3, &hlen, &csize) != 0,
          "no hex digit must be rejected");
    CHECK(http_parse_chunk_size((const uint8_t *)"5", 1, &hlen, &csize) != 0,
          "size line without CRLF must be rejected");
    CHECK(http_parse_chunk_size(NULL, 3, &hlen, &csize) != 0, "NULL buf rejected");
}

static void test_chunked_complete(void) {
    CHECK(http_chunked_complete((const uint8_t *)"5\r\nhello\r\n0\r\n\r\n", 15) == 1,
          "complete chunked body detected");
    CHECK(http_chunked_complete((const uint8_t *)"0\r\n\r\n", 5) == 1,
          "empty body terminator is complete");
    CHECK(http_chunked_complete((const uint8_t *)"5\r\nhel", 6) == 0,
          "truncated chunked body is not complete");
    CHECK(http_chunked_complete((const uint8_t *)"5\r\nhello\r\n", 10) == 0,
          "missing terminator chunk is not complete");
}

static void test_decode_single_chunk(void) {
    char body[] = "5\r\nhello\r\n0\r\n\r\n";
    size_t len = (size_t)(sizeof(body) - 1u); /* 15, excludes the NUL */
    int rc = http_decode_chunked_body((uint8_t *)body, &len);
    CHECK(rc == 0, "single chunk decodes");
    CHECK(len == 5, "decoded length is the payload length");
    CHECK(memcmp(body, "hello", 5) == 0, "decoded payload matches");
}

static void test_decode_multi_chunk(void) {
    char body[] = "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
    size_t len = (size_t)(sizeof(body) - 1u);
    int rc = http_decode_chunked_body((uint8_t *)body, &len);
    CHECK(rc == 0, "multi chunk decodes");
    CHECK(len == 5, "concatenated chunk length");
    CHECK(memcmp(body, "abcde", 5) == 0, "chunks concatenated in order");
}

static void test_decode_rejects_malformed(void) {
    char bad[] = "xyz";
    size_t len = 3u;
    CHECK(http_decode_chunked_body((uint8_t *)bad, &len) != 0,
          "malformed chunk header fails closed");
    {
        char trunc[] = "5\r\nhel";
        size_t tlen = (size_t)(sizeof(trunc) - 1u);
        CHECK(http_decode_chunked_body((uint8_t *)trunc, &tlen) != 0,
              "chunk longer than the buffer fails closed");
    }
}

/* ---- security: oversized chunk-size integer-overflow defense ---- */

static void test_parse_chunk_size_rejects_overflow(void) {
    size_t hlen = 0;
    size_t csize = 0;
    /* A chunk-size line with far more hex digits than size_t can hold must
     * fail closed, not wrap to a small/garbage size that bypasses the bounds
     * checks in complete/decode. 17 'f' nibbles overflow a 64-bit size_t (and
     * fewer overflow a 32-bit one), so this is rejected on any platform. */
    CHECK(http_parse_chunk_size((const uint8_t *)"fffffffffffffffff\r\n", 19,
                                &hlen, &csize) != 0,
          "overflowing chunk size must be rejected");
}

static void test_decode_rejects_oversized_chunk(void) {
    /* 16 'f's parse to SIZE_MAX on a 64-bit host. The decode bound must
     * reject the chunk (chunk_size > len - src) WITHOUT `src + chunk_size`
     * wrapping below len and letting http_memcpy run past the buffer. */
    char body[] = "ffffffffffffffff\r\nx\r\n0\r\n\r\n";
    size_t len = (size_t)(sizeof(body) - 1u);
    CHECK(http_decode_chunked_body((uint8_t *)body, &len) != 0,
          "oversized chunk must fail closed without overflow");
}

static void test_chunked_complete_rejects_oversized_chunk(void) {
    /* The completeness probe must also treat a SIZE_MAX-scale chunk as
     * not-complete without wrapping its bound or reading out of range. */
    const char buf[] = "fffffffffffffff\r\nx\r\n0\r\n\r\n";
    CHECK(http_chunked_complete((const uint8_t *)buf,
                                (size_t)(sizeof(buf) - 1u)) == 0,
          "oversized chunk must not be reported complete");
}

int run_http_chunked_tests(void) {
    g_failures = 0;
    test_hex_digit();
    test_parse_chunk_size_basic();
    test_parse_chunk_size_skips_extension();
    test_parse_chunk_size_rejects_malformed();
    test_chunked_complete();
    test_decode_single_chunk();
    test_decode_multi_chunk();
    test_decode_rejects_malformed();
    test_parse_chunk_size_rejects_overflow();
    test_decode_rejects_oversized_chunk();
    test_chunked_complete_rejects_oversized_chunk();
    if (g_failures == 0) printf("[PASS] http_chunked\n");
    return g_failures;
}
