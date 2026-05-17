/*
 * tests/userland/test_capylibc_net_internal.h
 *
 * Shared TEST/PASS/FAIL macros, counter externs, recording fake-stub
 * state (`fake_state`, `g_fake`, `fake_reset`) and companion entry
 * declarations for the `test_capylibc_net` host test. Created at the
 * 2026-05-15 monolith refactor when the single file
 * `tests/test_capylibc_net.c` (2025 LOC) was split into:
 *
 *   - tests/userland/test_capylibc_net.c       (sockets/DNS coverage +
 *                                                fake-stub definitions +
 *                                                entry `test_capylibc_net_run`)
 *   - tests/userland/test_capylibc_net_url.c   (URL parser + HTTP
 *                                                request builder + status
 *                                                line + header parser)
 *   - tests/userland/test_capylibc_net_http.c  (`capy_http_get`
 *                                                end-to-end)
 *
 * The actual `capy_socket` / `capy_connect` / `capy_send` / `capy_recv`
 * / `capy_close` / `capy_dns_resolve` stubs live in
 * `test_capylibc_net.c` (single linker definition). All three test
 * translation units share `g_fake`, `fake_reset`, the run/pass
 * counters and the TEST/PASS/FAIL macros through this header.
 */
#ifndef TEST_CAPYLIBC_NET_INTERNAL_H
#define TEST_CAPYLIBC_NET_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "capylibc-net/capy_net.h"
#include "capylibc-tls/capy_tls.h"
#include "capylibc/capylibc.h"

#define FAKE_FD 42

struct fake_state {
  /* Knobs the test sets BEFORE calling. */
  int socket_should_fail;
  int connect_should_fail;
  int next_send_chunk;
  int next_send_zero_at;
  int next_send_err_at;
  int recv_terminator;
  int recv_drip_count;
  int recv_eof_after;
  int dns_should_miss;
  uint32_t dns_canned_ip;
  const uint8_t *recv_canned_buf;
  size_t         recv_canned_len;
  size_t         recv_canned_pos;
  size_t         recv_chunk_size;

  /* Counters / observation. */
  int socket_calls;
  int connect_calls;
  int send_calls;
  int recv_calls;
  int close_calls;
  int dns_calls;
  int last_close_fd;
  int last_connect_fd;
  uint16_t last_connect_port_net;
  uint32_t last_connect_addr_net;
  char     last_dns_name[64];
  uint8_t  send_log[4096];
  size_t   send_log_len;
  int      recv_drip_emitted;
};

extern struct fake_state g_fake;

void fake_reset(void);

extern int test_capylibc_net_runs;
extern int test_capylibc_net_passes;

#define TEST(name)                                                             \
  do { test_capylibc_net_runs++; printf("  %-58s ", name); } while (0)
#define PASS()                                                                 \
  do { printf("OK\n"); test_capylibc_net_passes++; } while (0)
#define FAIL(m)                                                                \
  do { printf("FAIL: %s\n", m); } while (0)

/* Internal capy_net adapters surfaced by capylibc-net for tests that
 * need to drive specific error mappings. */
capy_net_err_t capy_net_internal_tls_error_to_net(capy_tls_err_t err);
int capy_net_internal_https_fail_closed(const struct capy_url_parts *url);

/* Companion-file entries. Called from `test_capylibc_net_run` after
 * the local socket/DNS tests. */
void test_capylibc_net_url_cases(void);
void test_capylibc_net_http_cases(void);

/* Macro alias preserving the original counter names so test bodies
 * stay verbatim across files. */
#define tests_run     test_capylibc_net_runs
#define tests_passed  test_capylibc_net_passes

#endif /* TEST_CAPYLIBC_NET_INTERNAL_H */
