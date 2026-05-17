/*
 * tests/userland/test_capylibc_net.c
 *
 * Host-side regression test for libcapy-net (the userland TCP client
 * façade in `userland/lib/capylibc-net/`). The tests run on the host
 * (arm64 / x86_64 macOS / Linux) and never go near the real syscall
 * path -- instead this file provides C definitions of `capy_socket`,
 * `capy_connect`, `capy_send`, `capy_recv`, `capy_close` and
 * `capy_dns_resolve` that the libcapy-net `.c` files link against.
 * That lets us exercise the full state machine (rollback on connect
 * failure, send_all loop, recv_until terminator handling, errno-style
 * discrimination, DNS resolver fallback) without booting the kernel.
 *
 * This file owns the entry point `test_capylibc_net_run` and the
 * sockets/DNS-tier coverage: endian helpers, inet_pton4/inet_ntoa4,
 * tcp_connect_ip4/tcp_connect_str, send_all, recv_all, recv_until,
 * resolve_host_ip4, tcp_connect_host, last_error_resets.
 *
 * URL parser + HTTP request builder + HTTP status line parser + HTTP
 * header parser tests live in
 * `tests/userland/test_capylibc_net_url.c`. The full `capy_http_get`
 * end-to-end coverage lives in
 * `tests/userland/test_capylibc_net_http.c`. Shared TEST/PASS/FAIL
 * macros, run/pass counter externs and the recording `fake_state` /
 * `g_fake` / `fake_reset` come from
 * `tests/userland/test_capylibc_net_internal.h`.
 *
 * Carved out of the historical single-file `tests/test_capylibc_net.c`
 * (2025 LOC) at the 2026-05-15 monolith refactor so each host-test
 * translation unit stays under the 900-line layout limit.
 */
#include <string.h>

#include "test_capylibc_net_internal.h"

/* === Recording fake stubs ==================================== */

struct fake_state g_fake;

void fake_reset(void) {
  memset(&g_fake, 0, sizeof(g_fake));
}

int capy_socket(int domain, int type, int protocol) {
  (void)domain; (void)type; (void)protocol;
  g_fake.socket_calls++;
  if (g_fake.socket_should_fail) return -1;
  return FAKE_FD;
}

int capy_connect(int fd, const struct capy_sockaddr_in *addr,
                  unsigned int addrlen) {
  (void)addrlen;
  g_fake.connect_calls++;
  g_fake.last_connect_fd = fd;
  if (addr) {
    g_fake.last_connect_port_net = addr->sin_port;
    g_fake.last_connect_addr_net = addr->sin_addr;
  }
  if (g_fake.connect_should_fail) return -1;
  return 0;
}

long capy_send(int fd, const void *buf, size_t len, int flags) {
  (void)fd; (void)flags;
  g_fake.send_calls++;
  if (g_fake.next_send_err_at == g_fake.send_calls) return -1;
  if (g_fake.next_send_zero_at == g_fake.send_calls) return 0;

  size_t to_log = len;
  if (g_fake.next_send_chunk > 0 && (size_t)g_fake.next_send_chunk < to_log) {
    to_log = (size_t)g_fake.next_send_chunk;
  }
  if (g_fake.send_log_len + to_log > sizeof(g_fake.send_log)) {
    to_log = sizeof(g_fake.send_log) - g_fake.send_log_len;
  }
  memcpy(g_fake.send_log + g_fake.send_log_len, buf, to_log);
  g_fake.send_log_len += to_log;
  return (long)to_log;
}

long capy_recv(int fd, void *buf, size_t cap, int flags) {
  (void)fd; (void)flags;
  g_fake.recv_calls++;
  if (cap == 0) return 0;

  /* Bulk mode (preferred for HTTP-style tests). */
  if (g_fake.recv_canned_buf && g_fake.recv_canned_len > 0) {
    size_t remaining = g_fake.recv_canned_len - g_fake.recv_canned_pos;
    if (remaining == 0) return 0;
    size_t want = remaining;
    if (g_fake.recv_chunk_size > 0 && want > g_fake.recv_chunk_size) {
      want = g_fake.recv_chunk_size;
    }
    if (want > cap) want = cap;
    memcpy(buf, g_fake.recv_canned_buf + g_fake.recv_canned_pos, want);
    g_fake.recv_canned_pos += want;
    return (long)want;
  }

  /* Drip mode (legacy: 1 byte per call until terminator/eof). */
  if (g_fake.recv_eof_after > 0 &&
      g_fake.recv_drip_emitted >= g_fake.recv_eof_after) {
    return 0;
  }
  if (g_fake.recv_drip_count == 0) return 0;
  if (g_fake.recv_drip_emitted >= g_fake.recv_drip_count) return 0;

  uint8_t emit;
  int idx = g_fake.recv_drip_emitted;
  if (idx == g_fake.recv_drip_count - 1) {
    emit = (uint8_t)g_fake.recv_terminator;
  } else {
    emit = (uint8_t)('A' + (idx % 26));
  }
  ((uint8_t *)buf)[0] = emit;
  g_fake.recv_drip_emitted++;
  return 1;
}

int capy_close(int fd) {
  g_fake.close_calls++;
  g_fake.last_close_fd = fd;
  return 0;
}

int capy_dns_resolve(const char *name, uint32_t *out_ip, int flags) {
  g_fake.dns_calls++;
  if (flags != 0) return -1;
  if (name) {
    size_t copy = strlen(name);
    if (copy >= sizeof(g_fake.last_dns_name)) {
      copy = sizeof(g_fake.last_dns_name) - 1;
    }
    memcpy(g_fake.last_dns_name, name, copy);
    g_fake.last_dns_name[copy] = '\0';
  }
  if (g_fake.dns_should_miss) return -1;
  if (out_ip) *out_ip = g_fake.dns_canned_ip;
  return 0;
}

/* === Test harness ============================================ */

int test_capylibc_net_runs = 0;
int test_capylibc_net_passes = 0;

/* === Endian helpers ========================================== */

static void test_endian_helpers(void) {
  TEST("htons/ntohs round-trip");
  if (capy_htons(0x1234) == 0x3412 && capy_ntohs(0x3412) == 0x1234) PASS();
  else FAIL("byte swap mismatch");

  TEST("htonl/ntohl round-trip");
  if (capy_htonl(0x12345678u) == 0x78563412u &&
      capy_ntohl(0x78563412u) == 0x12345678u) PASS();
  else FAIL("byte swap mismatch");

  TEST("htons of zero is zero");
  if (capy_htons(0) == 0) PASS();
  else FAIL("zero corruption");
}

/* === inet_pton4 / inet_ntoa4 ================================= */

static void test_inet_pton4(void) {
  uint32_t ip = 0;

  TEST("inet_pton4 \"1.2.3.4\"");
  if (capy_inet_pton4("1.2.3.4", &ip) == 0 && ip == 0x01020304u) PASS();
  else FAIL("expected 0x01020304");

  TEST("inet_pton4 \"0.0.0.0\"");
  if (capy_inet_pton4("0.0.0.0", &ip) == 0 && ip == 0) PASS();
  else FAIL("expected zero");

  TEST("inet_pton4 \"255.255.255.255\"");
  if (capy_inet_pton4("255.255.255.255", &ip) == 0 && ip == 0xFFFFFFFFu) PASS();
  else FAIL("expected ~0");

  TEST("inet_pton4 \"127.0.0.1\"");
  if (capy_inet_pton4("127.0.0.1", &ip) == 0 && ip == 0x7F000001u) PASS();
  else FAIL("expected 0x7F000001");

  TEST("inet_pton4 rejects empty string");
  if (capy_inet_pton4("", &ip) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("did not reject empty");

  TEST("inet_pton4 rejects \"1.2.3\"");
  if (capy_inet_pton4("1.2.3", &ip) == -1) PASS();
  else FAIL("3 octets accepted");

  TEST("inet_pton4 rejects \"1.2.3.4.5\"");
  if (capy_inet_pton4("1.2.3.4.5", &ip) == -1) PASS();
  else FAIL("5 octets accepted");

  TEST("inet_pton4 rejects \"1.2.3.256\"");
  if (capy_inet_pton4("1.2.3.256", &ip) == -1) PASS();
  else FAIL("octet > 255 accepted");

  TEST("inet_pton4 rejects leading dot");
  if (capy_inet_pton4(".1.2.3.4", &ip) == -1) PASS();
  else FAIL("leading dot accepted");

  TEST("inet_pton4 rejects trailing dot");
  if (capy_inet_pton4("1.2.3.4.", &ip) == -1) PASS();
  else FAIL("trailing dot accepted");

  TEST("inet_pton4 rejects non-digit");
  if (capy_inet_pton4("1.2.3.4a", &ip) == -1) PASS();
  else FAIL("trailing junk accepted");

  TEST("inet_pton4 rejects \"1.2.3.0001\"");
  if (capy_inet_pton4("1.2.3.0001", &ip) == -1) PASS();
  else FAIL("4-digit octet accepted");

  TEST("inet_pton4 rejects NULL string");
  if (capy_inet_pton4(NULL, &ip) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL str accepted");
}

static void test_inet_ntoa4(void) {
  char buf[32];

  TEST("inet_ntoa4 0x01020304 -> \"1.2.3.4\"");
  int n = capy_inet_ntoa4(0x01020304u, buf, sizeof(buf));
  if (n == 7 && strcmp(buf, "1.2.3.4") == 0) PASS();
  else FAIL("format mismatch");

  TEST("inet_ntoa4 max -> \"255.255.255.255\" (15 chars)");
  n = capy_inet_ntoa4(0xFFFFFFFFu, buf, sizeof(buf));
  if (n == 15 && strcmp(buf, "255.255.255.255") == 0) PASS();
  else FAIL("format mismatch");

  TEST("inet_ntoa4 zero -> \"0.0.0.0\"");
  n = capy_inet_ntoa4(0u, buf, sizeof(buf));
  if (n == 7 && strcmp(buf, "0.0.0.0") == 0) PASS();
  else FAIL("format mismatch");

  TEST("inet_ntoa4 rejects cap < 16");
  n = capy_inet_ntoa4(0xFFFFFFFFu, buf, 15);
  if (n == -1 && capy_net_last_error() == CAPY_NET_EBUF) PASS();
  else FAIL("undersized buf accepted");

  TEST("inet_ntoa4 rejects NULL out");
  n = capy_inet_ntoa4(0u, NULL, 32);
  if (n == -1 && capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL out accepted");

  TEST("inet_ntoa4 then inet_pton4 round-trips");
  uint32_t ip = 0;
  capy_inet_ntoa4(0xC0A80001u, buf, sizeof(buf));
  if (capy_inet_pton4(buf, &ip) == 0 && ip == 0xC0A80001u) PASS();
  else FAIL("round-trip mismatch");
}

/* === tcp_connect ============================================= */

static void test_tcp_connect_ip4_happy(void) {
  fake_reset();

  TEST("tcp_connect_ip4 returns fake fd on success");
  int fd = capy_tcp_connect_ip4(0x7F000001u, 8080);
  if (fd == FAKE_FD && g_fake.socket_calls == 1 &&
      g_fake.connect_calls == 1 && g_fake.close_calls == 0) PASS();
  else FAIL("unexpected call counts");

  TEST("tcp_connect_ip4 byte-swaps port and IP into network order");
  if (g_fake.last_connect_port_net == 0x901F &&
      g_fake.last_connect_addr_net == 0x0100007Fu) PASS();
  else FAIL("net-order conversion wrong");
}

static void test_tcp_connect_ip4_socket_fail(void) {
  fake_reset();
  g_fake.socket_should_fail = 1;

  TEST("tcp_connect_ip4 returns -1 if capy_socket fails");
  int fd = capy_tcp_connect_ip4(0x7F000001u, 80);
  if (fd == -1 && g_fake.connect_calls == 0 && g_fake.close_calls == 0 &&
      capy_net_last_error() == CAPY_NET_ESOCK) PASS();
  else FAIL("did not short-circuit");
}

static void test_tcp_connect_ip4_connect_fail_rolls_back(void) {
  fake_reset();
  g_fake.connect_should_fail = 1;

  TEST("tcp_connect_ip4 closes fd if capy_connect fails");
  int fd = capy_tcp_connect_ip4(0x7F000001u, 80);
  if (fd == -1 && g_fake.close_calls == 1 &&
      g_fake.last_close_fd == FAKE_FD &&
      capy_net_last_error() == CAPY_NET_ECONNECT) PASS();
  else FAIL("rollback missing or wrong fd");
}

static void test_tcp_connect_str(void) {
  fake_reset();
  TEST("tcp_connect_str parses dotted-decimal");
  int fd = capy_tcp_connect_str("10.20.30.40", 443);
  if (fd == FAKE_FD &&
      g_fake.last_connect_addr_net == 0x281E140Au) PASS();
  else FAIL("dotted-decimal not converted");

  TEST("tcp_connect_str rejects bad IP without calling capy_socket");
  fake_reset();
  fd = capy_tcp_connect_str("10.20.30", 443);
  if (fd == -1 && g_fake.socket_calls == 0 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("did not short-circuit on parse fail");

  TEST("tcp_connect_str rejects NULL string");
  fake_reset();
  fd = capy_tcp_connect_str(NULL, 80);
  if (fd == -1 && g_fake.socket_calls == 0 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("did not short-circuit on NULL");
}

/* === send_all ================================================ */

static void test_send_all_short_writes(void) {
  fake_reset();
  g_fake.next_send_chunk = 3;

  TEST("send_all loops across short writes");
  const char *payload = "HELLO-WORLD!";
  long n = capy_send_all(7, payload, strlen(payload));
  if (n == (long)strlen(payload) &&
      g_fake.send_log_len == strlen(payload) &&
      memcmp(g_fake.send_log, payload, strlen(payload)) == 0) PASS();
  else FAIL("short-write loop broken");
}

static void test_send_all_zero_stops(void) {
  fake_reset();
  g_fake.next_send_chunk = 4;
  g_fake.next_send_zero_at = 2;

  TEST("send_all stops on zero return, reports partial");
  long n = capy_send_all(7, "ABCDEFGH", 8);
  if (n == 4 && g_fake.send_log_len == 4 &&
      capy_net_last_error() == CAPY_NET_ESEND) PASS();
  else FAIL("partial count wrong");
}

static void test_send_all_err_propagates(void) {
  fake_reset();
  g_fake.next_send_chunk = 4;
  g_fake.next_send_err_at = 2;

  TEST("send_all returns partial > 0 on mid-stream error");
  long n = capy_send_all(7, "ABCDEFGH", 8);
  if (n == 4 && g_fake.send_log_len == 4 &&
      capy_net_last_error() == CAPY_NET_ESEND) PASS();
  else FAIL("partial-on-err count wrong");

  fake_reset();
  g_fake.next_send_err_at = 1;
  TEST("send_all returns -1 if first send errors");
  n = capy_send_all(7, "X", 1);
  if (n == -1 && capy_net_last_error() == CAPY_NET_ESEND) PASS();
  else FAIL("expected -1");
}

static void test_send_all_zero_len(void) {
  fake_reset();
  TEST("send_all with len=0 returns 0 without calling capy_send");
  long n = capy_send_all(7, "X", 0);
  if (n == 0 && g_fake.send_calls == 0) PASS();
  else FAIL("called capy_send with len=0");
}

static void test_send_all_invalid_args(void) {
  fake_reset();
  TEST("send_all rejects fd < 0");
  if (capy_send_all(-1, "x", 1) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("negative fd accepted");

  TEST("send_all rejects NULL buf with len > 0");
  if (capy_send_all(3, NULL, 1) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL buf accepted");
}

/* === recv_all ================================================ */

static void test_recv_all_passthrough(void) {
  fake_reset();
  g_fake.recv_terminator = '\n';
  g_fake.recv_drip_count = 3;

  TEST("recv_all returns N on success (single drip call here)");
  uint8_t buf[16] = {0};
  long n = capy_recv_all(7, buf, sizeof(buf));
  if (n == 1 && buf[0] == 'A') PASS();
  else FAIL("expected 1 byte");
}

/* === recv_until ============================================== */

static void test_recv_until_terminator(void) {
  fake_reset();
  g_fake.recv_terminator = '\n';
  g_fake.recv_drip_count = 5;

  TEST("recv_until stops at terminator, includes it");
  uint8_t buf[16] = {0};
  long n = capy_recv_until(7, buf, sizeof(buf), '\n');
  if (n == 5 && buf[0] == 'A' && buf[1] == 'B' &&
      buf[2] == 'C' && buf[3] == 'D' && buf[4] == '\n') PASS();
  else FAIL("terminator handling wrong");
}

static void test_recv_until_cap_exhaust(void) {
  fake_reset();
  g_fake.recv_terminator = '\n';
  g_fake.recv_drip_count = 100;

  TEST("recv_until returns cap when cap exhausted before terminator");
  uint8_t buf[8] = {0};
  long n = capy_recv_until(7, buf, sizeof(buf), '\n');
  if (n == (long)sizeof(buf)) PASS();
  else FAIL("cap not enforced");
}

static void test_recv_until_eof(void) {
  fake_reset();
  g_fake.recv_terminator = '\n';
  g_fake.recv_drip_count = 10;
  g_fake.recv_eof_after = 3;

  TEST("recv_until returns partial count on clean EOF");
  uint8_t buf[16] = {0};
  long n = capy_recv_until(7, buf, sizeof(buf), '\n');
  if (n == 3) PASS();
  else FAIL("EOF accounting wrong");
}

static void test_recv_until_invalid_args(void) {
  fake_reset();
  TEST("recv_until rejects cap=0");
  uint8_t buf[4];
  if (capy_recv_until(7, buf, 0, '\n') == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("cap=0 accepted");

  TEST("recv_until rejects NULL buf");
  if (capy_recv_until(7, NULL, 16, '\n') == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL buf accepted");
}

/* === resolve + tcp_connect_host (F4 seção c parte 3/3) ======= */

static void test_resolve_host_literal_short_circuits(void) {
  fake_reset();
  TEST("resolve_host_ip4 literal IPv4 short-circuits without DNS");
  uint32_t ip = 0;
  int rc = capy_resolve_host_ip4("192.168.1.1", &ip);
  if (rc == 0 && ip == 0xC0A80101u && g_fake.dns_calls == 0 &&
      capy_net_last_error() == CAPY_NET_OK) PASS();
  else FAIL("dns_resolve was called or ip wrong");
}

static void test_resolve_host_dns_fallback_hit(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x08080808u;
  TEST("resolve_host_ip4 DNS fallback hit populates ip");
  uint32_t ip = 0;
  int rc = capy_resolve_host_ip4("dns.example", &ip);
  if (rc == 0 && ip == 0x08080808u && g_fake.dns_calls == 1 &&
      strcmp(g_fake.last_dns_name, "dns.example") == 0 &&
      capy_net_last_error() == CAPY_NET_OK) PASS();
  else FAIL("DNS fallback didn't populate ip");
}

static void test_resolve_host_dns_miss(void) {
  fake_reset();
  g_fake.dns_should_miss = 1;
  TEST("resolve_host_ip4 DNS miss reports CAPY_NET_EDNS");
  uint32_t ip = 0xDEADBEEFu;
  int rc = capy_resolve_host_ip4("missing.example", &ip);
  if (rc == -1 && g_fake.dns_calls == 1 &&
      capy_net_last_error() == CAPY_NET_EDNS) PASS();
  else FAIL("did not surface EDNS");
}

static void test_resolve_host_invalid_args(void) {
  fake_reset();
  uint32_t ip;
  TEST("resolve_host_ip4 rejects NULL host");
  if (capy_resolve_host_ip4(NULL, &ip) == -1 &&
      g_fake.dns_calls == 0 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL host accepted");

  TEST("resolve_host_ip4 rejects NULL out_ip");
  if (capy_resolve_host_ip4("x", NULL) == -1 &&
      g_fake.dns_calls == 0 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL out_ip accepted");
}

static void test_tcp_connect_host_literal(void) {
  fake_reset();
  TEST("tcp_connect_host literal IPv4 connects without DNS");
  int fd = capy_tcp_connect_host("10.0.0.42", 80);
  if (fd == FAKE_FD && g_fake.dns_calls == 0 &&
      g_fake.connect_calls == 1 &&
      g_fake.last_connect_addr_net == 0x2A00000Au &&
      g_fake.last_connect_port_net == 0x5000) PASS();
  else FAIL("literal path wrong");
}

static void test_tcp_connect_host_dns(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x01020304u;
  TEST("tcp_connect_host hostname goes through DNS then connect");
  int fd = capy_tcp_connect_host("example.com", 443);
  if (fd == FAKE_FD && g_fake.dns_calls == 1 &&
      strcmp(g_fake.last_dns_name, "example.com") == 0 &&
      g_fake.connect_calls == 1 &&
      g_fake.last_connect_addr_net == 0x04030201u &&
      g_fake.last_connect_port_net == 0xBB01) PASS();
  else FAIL("DNS->connect path wrong");
}

static void test_tcp_connect_host_dns_miss(void) {
  fake_reset();
  g_fake.dns_should_miss = 1;
  TEST("tcp_connect_host with DNS miss returns -1, no socket allocated");
  int fd = capy_tcp_connect_host("missing.example", 80);
  if (fd == -1 && g_fake.dns_calls == 1 &&
      g_fake.socket_calls == 0 &&
      capy_net_last_error() == CAPY_NET_EDNS) PASS();
  else FAIL("did not short-circuit on DNS miss");
}

static void test_tcp_connect_host_null(void) {
  fake_reset();
  TEST("tcp_connect_host rejects NULL host");
  int fd = capy_tcp_connect_host(NULL, 80);
  if (fd == -1 && g_fake.dns_calls == 0 &&
      g_fake.socket_calls == 0 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL host accepted");
}

/* === Last error reset on success ============================= */

static void test_last_error_resets(void) {
  uint32_t ip = 0;
  fake_reset();
  /* Establish an error state first. */
  capy_inet_pton4("bad", &ip);
  if (capy_net_last_error() == CAPY_NET_OK) {
    TEST("last_error setup");
    FAIL("error not set");
    return;
  }
  TEST("last_error clears on successful inet_pton4");
  if (capy_inet_pton4("1.2.3.4", &ip) == 0 &&
      capy_net_last_error() == CAPY_NET_OK) PASS();
  else FAIL("error not cleared");
}

/* === Entry point ============================================= */

int test_capylibc_net_run(void) {
  printf("[test_capylibc_net]\n");
  test_capylibc_net_runs = 0;
  test_capylibc_net_passes = 0;

  test_endian_helpers();
  test_inet_pton4();
  test_inet_ntoa4();
  test_tcp_connect_ip4_happy();
  test_tcp_connect_ip4_socket_fail();
  test_tcp_connect_ip4_connect_fail_rolls_back();
  test_tcp_connect_str();
  test_send_all_short_writes();
  test_send_all_zero_stops();
  test_send_all_err_propagates();
  test_send_all_zero_len();
  test_send_all_invalid_args();
  test_recv_all_passthrough();
  test_recv_until_terminator();
  test_recv_until_cap_exhaust();
  test_recv_until_eof();
  test_recv_until_invalid_args();
  test_resolve_host_literal_short_circuits();
  test_resolve_host_dns_fallback_hit();
  test_resolve_host_dns_miss();
  test_resolve_host_invalid_args();
  test_tcp_connect_host_literal();
  test_tcp_connect_host_dns();
  test_tcp_connect_host_dns_miss();
  test_tcp_connect_host_null();

  test_capylibc_net_url_cases();
  test_capylibc_net_http_cases();

  test_last_error_resets();

  printf("  -> %d/%d passed\n",
         test_capylibc_net_passes, test_capylibc_net_runs);
  return test_capylibc_net_runs - test_capylibc_net_passes;
}
