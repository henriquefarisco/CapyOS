#include <stdio.h>
#include <string.h>

#include "net/http_encoding.h"
#include "memory/kmem.h"

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    printf("[FAIL] http_encoding: %s\n", msg);
    return 1;
  }
  return 0;
}

static int test_http_encoding_identity_copy(void) {
  static const uint8_t payload[] = "plain body";
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int fails = 0;
  int rc = http_encoding_decode_body("identity", payload, sizeof(payload) - 1,
                                     1024, &decoded, &decoded_len);
  fails += expect_true(rc == HTTP_ENCODING_OK, "identity should decode");
  fails += expect_true(decoded != NULL, "identity should allocate output");
  fails += expect_true(decoded_len == sizeof(payload) - 1, "identity length should match");
  fails += expect_true(decoded && memcmp(decoded, payload, sizeof(payload) - 1) == 0,
                       "identity payload should match");
  if (decoded) kfree(decoded);
  return fails;
}

static int test_http_encoding_gzip_decode(void) {
  static const uint8_t gzip_body[] = {
      31, 139, 8, 0, 0, 0, 0, 0, 2, 255, 203, 72, 205, 201, 201, 87, 72, 78,
      44, 168, 84, 72, 42, 202, 47, 47, 78, 45, 2, 0, 245, 101, 131, 58, 18,
      0, 0, 0
  };
  static const char expected[] = "hello capy browser";
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int fails = 0;
  int rc = http_encoding_decode_body("gzip", gzip_body, sizeof(gzip_body),
                                     1024, &decoded, &decoded_len);
  fails += expect_true(rc == HTTP_ENCODING_OK, "gzip should decode");
  fails += expect_true(decoded_len == strlen(expected), "gzip length should match");
  fails += expect_true(decoded && memcmp(decoded, expected, strlen(expected)) == 0,
                       "gzip payload should match");
  if (decoded) kfree(decoded);
  return fails;
}

static int test_http_encoding_deflate_decode(void) {
  static const uint8_t zlib_body[] = {
      120, 156, 203, 72, 205, 201, 201, 87, 72, 78, 44, 168, 84, 72, 42, 202,
      47, 47, 78, 45, 2, 0, 65, 67, 7, 6
  };
  static const char expected[] = "hello capy browser";
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int fails = 0;
  int rc = http_encoding_decode_body("deflate", zlib_body, sizeof(zlib_body),
                                     1024, &decoded, &decoded_len);
  fails += expect_true(rc == HTTP_ENCODING_OK, "zlib-wrapped deflate should decode");
  fails += expect_true(decoded_len == strlen(expected), "deflate length should match");
  fails += expect_true(decoded && memcmp(decoded, expected, strlen(expected)) == 0,
                       "deflate payload should match");
  if (decoded) kfree(decoded);
  return fails;
}

static int test_http_encoding_raw_deflate_decode(void) {
  static const uint8_t raw_body[] = {
      203, 72, 205, 201, 201, 87, 72, 78, 44, 168, 84, 72, 42, 202, 47, 47,
      78, 45, 2, 0
  };
  static const char expected[] = "hello capy browser";
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int fails = 0;
  int rc = http_encoding_decode_body("deflate", raw_body, sizeof(raw_body),
                                     1024, &decoded, &decoded_len);
  fails += expect_true(rc == HTTP_ENCODING_OK, "raw deflate should decode");
  fails += expect_true(decoded_len == strlen(expected), "raw deflate length should match");
  fails += expect_true(decoded && memcmp(decoded, expected, strlen(expected)) == 0,
                       "raw deflate payload should match");
  if (decoded) kfree(decoded);
  return fails;
}

static int test_http_encoding_decode_limit(void) {
  static const uint8_t gzip_body[] = {
      31, 139, 8, 0, 0, 0, 0, 0, 2, 255, 203, 72, 205, 201, 201, 87, 72, 78,
      44, 168, 84, 72, 42, 202, 47, 47, 78, 45, 2, 0, 245, 101, 131, 58, 18,
      0, 0, 0
  };
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int fails = 0;
  int rc = http_encoding_decode_body("gzip", gzip_body, sizeof(gzip_body),
                                     8, &decoded, &decoded_len);
  fails += expect_true(rc == HTTP_ENCODING_ERR_TOO_LARGE,
                       "decoded payload above the limit should fail");
  fails += expect_true(decoded == NULL && decoded_len == 0,
                       "too-large decode should not return a buffer");
  return fails;
}

static int test_http_encoding_partial_config_rejected(void) {
  static const uint8_t payload[] = {1, 2, 3, 4};
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int fails = 0;
  int rc = http_encoding_decode_body("br", payload, sizeof(payload),
                                     1024, &decoded, &decoded_len);
  fails += expect_true(rc == HTTP_ENCODING_ERR_UNSUPPORTED,
                       "unsupported content encoding should fail");
  fails += expect_true(decoded == NULL && decoded_len == 0,
                       "unsupported encoding should not allocate output");
  return fails;
}

int run_http_encoding_tests(void) {
  int fails = 0;
  fails += test_http_encoding_identity_copy();
  fails += test_http_encoding_gzip_decode();
  fails += test_http_encoding_deflate_decode();
  fails += test_http_encoding_raw_deflate_decode();
  fails += test_http_encoding_decode_limit();
  fails += test_http_encoding_partial_config_rejected();
  if (fails == 0) printf("[PASS] http_encoding\n");
  return fails;
}
