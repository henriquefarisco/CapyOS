/*
 * tests/test_capylibc_net.c (2026-05-08, F4 seção c parte 2/2)
 *
 * Host-side regression test for libcapy-net (the userland TCP
 * client façade in `userland/lib/capylibc-net/`). The tests run
 * on the host (arm64 / x86_64 macOS / Linux) and never go near
 * the real syscall path -- instead they provide C definitions of
 * `capy_socket`, `capy_connect`, `capy_send`, `capy_recv` and
 * `capy_close` that the libcapy-net `.c` files link against. This
 * lets us exercise the full state machine (rollback on connect
 * failure, send_all loop, recv_until terminator handling, errno-
 * style discrimination) without booting the kernel.
 *
 * What's covered:
 *   1. Endian helpers (htons / htonl / ntohs / ntohl) round-trip
 *      through known constants.
 *   2. inet_pton4: canonical "1.2.3.4", boundary "0.0.0.0" /
 *      "255.255.255.255", reject malformed (empty, leading dot,
 *      trailing dot, non-digit, octet > 255, too few/many octets,
 *      4-digit octet "1234").
 *   3. inet_ntoa4: round-trip with inet_pton4; reject too-small
 *      buffer; verify exact length of "255.255.255.255" (15).
 *   4. tcp_connect_ip4 happy path: capy_socket succeeds, capy_connect
 *      succeeds, returns the fake fd.
 *   5. tcp_connect_ip4 rollback: capy_connect fails, capy_close is
 *      invoked exactly once with the same fake fd.
 *   6. tcp_connect_str: dotted-decimal parses, then delegates to
 *      tcp_connect_ip4 with the right host-order IP.
 *   7. send_all: loops across multiple short writes; stops cleanly
 *      on send returning 0; reports partial total on send returning
 *      -1 mid-stream.
 *   8. recv_until: stops at the terminator, INCLUDES the terminator
 *      in the count; falls back to cap-exhaustion; clean EOF.
 *   9. last_error code surface matches the failure mode.
 */

#include "capylibc-net/capy_net.h"
#include "capylibc-tls/capy_tls.h"
#include "capylibc/capylibc.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

capy_net_err_t capy_net_internal_tls_error_to_net(capy_tls_err_t err);
int capy_net_internal_https_fail_closed(
    const struct capy_url_parts *url);

/* === Recording fake stubs ==================================== */

#define FAKE_FD 42

struct fake_state {
  /* Knobs the test sets BEFORE calling. */
  int socket_should_fail;
  int connect_should_fail;
  int next_send_chunk;     /* if > 0, capy_send returns at most this */
  int next_send_zero_at;   /* on the Nth call (>=1) capy_send returns 0 */
  int next_send_err_at;    /* on the Nth call (>=1) capy_send returns -1 */
  int recv_terminator;     /* byte that recv_drip emits as terminator */
  int recv_drip_count;     /* total bytes recv_drip will emit (incl. term) */
  int recv_eof_after;      /* after this many bytes, recv returns 0 */
  int dns_should_miss;     /* capy_dns_resolve returns -1 */
  uint32_t dns_canned_ip;  /* host-order IP returned on hit */
  /* HTTP-mode (F4 c parte 5): if recv_canned_len > 0, capy_recv
   * delivers from this buffer in `recv_chunk_size` chunks (or all
   * remaining when chunk_size == 0). Once drained, returns 0
   * (clean EOF). Takes precedence over the drip mode. */
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
  uint8_t  send_log[4096];   /* big enough for HTTP request */
  size_t   send_log_len;
  int      recv_drip_emitted;
};

static struct fake_state g_fake;

static void fake_reset(void) {
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
  /* Cap the log buffer so silly tests don't smash the array. */
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

  /* Emit one byte: terminator if we're at the last drip slot,
   * else 'A' + index for distinctness. */
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

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

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

  /* Rejection cases */
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

  /* Round-trip */
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
  int fd = capy_tcp_connect_ip4(0x7F000001u /*127.0.0.1*/, 8080);
  if (fd == FAKE_FD && g_fake.socket_calls == 1 &&
      g_fake.connect_calls == 1 && g_fake.close_calls == 0) PASS();
  else FAIL("unexpected call counts");

  TEST("tcp_connect_ip4 byte-swaps port and IP into network order");
  /* Port 8080 = 0x1F90 host -> 0x901F network. */
  /* IP 127.0.0.1 = 0x7F000001 host -> 0x0100007F network. */
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
  /* IP 0x0A141E28 host -> 0x281E140A network. */
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
  g_fake.next_send_chunk = 3;  /* each capy_send returns at most 3 */

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
  g_fake.next_send_zero_at = 2; /* second send returns 0 */

  TEST("send_all stops on zero return, reports partial");
  long n = capy_send_all(7, "ABCDEFGH", 8);
  /* First send: 4 bytes. Second send: returns 0 -> total = 4. */
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
  g_fake.next_send_err_at = 1; /* error on FIRST send */
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
  g_fake.recv_drip_count = 5;     /* "ABCD\n" */

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
  g_fake.recv_drip_count = 100;   /* never reaches terminator */

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
  g_fake.recv_eof_after = 3;      /* recv returns 0 after 3 bytes drained */

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
  /* IP 192.168.1.1 = 0xC0A80101 host-order. */
  if (rc == 0 && ip == 0xC0A80101u && g_fake.dns_calls == 0 &&
      capy_net_last_error() == CAPY_NET_OK) PASS();
  else FAIL("dns_resolve was called or ip wrong");
}

static void test_resolve_host_dns_fallback_hit(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x08080808u;  /* 8.8.8.8 */
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
  /* IP 10.0.0.42 host = 0x0A00002A → net = 0x2A00000A. Port 80 net = 0x5000. */
  if (fd == FAKE_FD && g_fake.dns_calls == 0 &&
      g_fake.connect_calls == 1 &&
      g_fake.last_connect_addr_net == 0x2A00000Au &&
      g_fake.last_connect_port_net == 0x5000) PASS();
  else FAIL("literal path wrong");
}

static void test_tcp_connect_host_dns(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x01020304u;  /* 1.2.3.4 */
  TEST("tcp_connect_host hostname goes through DNS then connect");
  int fd = capy_tcp_connect_host("example.com", 443);
  /* 1.2.3.4 host = 0x01020304 → net = 0x04030201. Port 443 net = 0xBB01. */
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

/* === URL parser (F4 seção c parte 5) ========================= */

static void test_url_parse_http_minimal(void) {
  fake_reset();
  TEST("url_parse: http://example.com");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://example.com", &u);
  if (rc == 0 && u.is_https == 0 && u.port == 80 &&
      strcmp(u.host, "example.com") == 0 &&
      strcmp(u.path, "/") == 0) PASS();
  else FAIL("minimal parse wrong");
}

static void test_url_parse_https_with_port_and_path(void) {
  fake_reset();
  TEST("url_parse: https://host.tld:8443/foo/bar");
  struct capy_url_parts u;
  int rc = capy_url_parse("https://host.tld:8443/foo/bar", &u);
  if (rc == 0 && u.is_https == 1 && u.port == 8443 &&
      strcmp(u.host, "host.tld") == 0 &&
      strcmp(u.path, "/foo/bar") == 0) PASS();
  else FAIL("full parse wrong");
}

static void test_url_parse_http_with_query(void) {
  fake_reset();
  TEST("url_parse: query string preserved in path");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example/api?key=1&x=y", &u);
  if (rc == 0 && strcmp(u.path, "/api?key=1&x=y") == 0) PASS();
  else FAIL("query lost");
}

static void test_url_parse_http_no_path_with_query(void) {
  fake_reset();
  TEST("url_parse: synthesises leading '/' when only query");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example?z=1", &u);
  if (rc == 0 && strcmp(u.path, "/?z=1") == 0) PASS();
  else FAIL("synthesised path wrong");
}


static void test_url_parse_strips_fragment_from_path(void) {
  fake_reset();
  TEST("url_parse strips fragment from path");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example/docs/page#private", &u);
  if (rc == 0 && strcmp(u.path, "/docs/page") == 0) PASS();
  else FAIL("fragment leaked into path");
}

static void test_url_parse_strips_fragment_after_query(void) {
  fake_reset();
  TEST("url_parse preserves query but strips fragment");
  struct capy_url_parts u;
  int rc = capy_url_parse("http://x.example/search?q=capy#secret", &u);
  if (rc == 0 && strcmp(u.path, "/search?q=capy") == 0) PASS();
  else FAIL("query/fragment split wrong");
}

static void test_url_parse_https_default_port(void) {
  fake_reset();
  TEST("url_parse: https://x.example defaults to port 443");
  struct capy_url_parts u;
  int rc = capy_url_parse("https://x.example", &u);
  if (rc == 0 && u.is_https == 1 && u.port == 443) PASS();
  else FAIL("default https port wrong");
}

static void test_url_parse_rejects_unknown_scheme(void) {
  fake_reset();
  TEST("url_parse rejects ftp://...");
  struct capy_url_parts u;
  if (capy_url_parse("ftp://example.com", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("ftp accepted");
}

static void test_url_parse_rejects_userinfo(void) {
  fake_reset();
  TEST("url_parse rejects userinfo (CVE-class footgun)");
  struct capy_url_parts u;
  if (capy_url_parse("http://user:pass@example.com/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("userinfo accepted");
}


static void test_url_parse_rejects_backslash_in_host(void) {
  fake_reset();
  TEST("url_parse rejects backslash in host");
  struct capy_url_parts u;
  if (capy_url_parse("http://good.example\\evil/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("backslash host accepted");
}

static void test_url_parse_rejects_percent_in_host(void) {
  fake_reset();
  TEST("url_parse rejects percent-encoded authority in host");
  struct capy_url_parts u;
  if (capy_url_parse("http://good.example%2fevil/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent host accepted");
}


static void test_url_parse_rejects_empty_host_label(void) {
  fake_reset();
  TEST("url_parse rejects empty host label");
  struct capy_url_parts u;
  if (capy_url_parse("http://good..example/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("empty host label accepted");
}

static void test_url_parse_rejects_hyphen_edge_host_label(void) {
  fake_reset();
  TEST("url_parse rejects hyphen-edge host label");
  struct capy_url_parts u;
  if (capy_url_parse("http://-bad.example/path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("hyphen-edge host label accepted");
}

static void test_url_parse_rejects_ipv6(void) {
  fake_reset();
  TEST("url_parse rejects IPv6 literal [::1]");
  struct capy_url_parts u;
  if (capy_url_parse("http://[::1]/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("ipv6 accepted");
}

static void test_url_parse_rejects_empty_host(void) {
  fake_reset();
  TEST("url_parse rejects empty host (http:///path)");
  struct capy_url_parts u;
  if (capy_url_parse("http:///path", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("empty host accepted");
}

static void test_url_parse_rejects_zero_port(void) {
  fake_reset();
  TEST("url_parse rejects port 0");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example:0/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("port 0 accepted");
}

static void test_url_parse_rejects_overflow_port(void) {
  fake_reset();
  TEST("url_parse rejects port > 65535");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example:99999/", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("overflow port accepted");
}

static void test_url_parse_rejects_null(void) {
  fake_reset();
  TEST("url_parse rejects NULL url");
  struct capy_url_parts u;
  if (capy_url_parse(NULL, &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("NULL url accepted");
}

static void test_url_parse_rejects_raw_crlf_in_path(void) {
  fake_reset();
  TEST("url_parse rejects raw CRLF in path");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/a\r\nHost:evil", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw CRLF accepted");
}

static void test_url_parse_rejects_raw_tab_in_query(void) {
  fake_reset();
  TEST("url_parse rejects raw tab in query");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/search?q=ok\tbad", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw tab accepted");
}

static void test_url_parse_rejects_backslash_in_path(void) {
  fake_reset();
  TEST("url_parse rejects backslash in request target");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/a\\b", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("backslash path accepted");
}

static void test_url_parse_rejects_percent_nul_in_path(void) {
  fake_reset();
  TEST("url_parse rejects %00 in path");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/a%00b", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent NUL path accepted");
}

static void test_url_parse_rejects_percent_nul_in_query(void) {
  fake_reset();
  TEST("url_parse rejects %00 in query");
  struct capy_url_parts u;
  if (capy_url_parse("http://x.example/search?q=%00", &u) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent NUL query accepted");
}

/* === HTTP request builder ==================================== */

static void test_http_build_get_default_port(void) {
  fake_reset();
  TEST("http_build_get default port 80 → no :port in Host");
  char buf[512];
  int n = capy_http_build_get_request("example.com", 80, "/", buf, sizeof(buf));
  if (n > 0 && strstr(buf, "GET / HTTP/1.1\r\n") != NULL &&
      strstr(buf, "Host: example.com\r\n") != NULL &&
      strstr(buf, "Host: example.com:80\r\n") == NULL &&
      strstr(buf, "Connection: close\r\n\r\n") != NULL) PASS();
  else FAIL("request format wrong");
}

static void test_http_build_get_custom_port(void) {
  fake_reset();
  TEST("http_build_get custom port 8080 → Host: host:8080");
  char buf[512];
  int n = capy_http_build_get_request("x.example", 8080, "/api", buf, sizeof(buf));
  if (n > 0 && strstr(buf, "GET /api HTTP/1.1\r\n") != NULL &&
      strstr(buf, "Host: x.example:8080\r\n") != NULL) PASS();
  else FAIL("custom port not in Host");
}


static void test_http_build_get_rejects_zero_port(void) {
  fake_reset();
  TEST("http_build_get rejects port 0");
  char buf[512];
  if (capy_http_build_get_request("x.example", 0, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("port 0 accepted");
}

static void test_http_build_get_buffer_too_small(void) {
  fake_reset();
  TEST("http_build_get returns -1 when buf overflows during build");
  /* 40 bytes passes the `buf_cap < 32` initial guard but is too
   * small to fit the full request (~100 bytes after User-Agent
   * and Connection: close). */
  char buf[40];
  if (capy_http_build_get_request("x.example", 80, "/some/path", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EBUF) PASS();
  else FAIL("did not detect overflow");
}

static void test_http_build_get_rejects_raw_crlf_host(void) {
  fake_reset();
  TEST("http_build_get rejects raw CRLF in host");
  char buf[512];
  if (capy_http_build_get_request("x.example\r\nX:evil", 80, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw CRLF host accepted");
}


static void test_http_build_get_rejects_ambiguous_host_char(void) {
  fake_reset();
  TEST("http_build_get rejects ambiguous host char");
  char buf[512];
  if (capy_http_build_get_request("x.example\\evil", 80, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("ambiguous host accepted");
}


static void test_http_build_get_rejects_bad_host_label_boundary(void) {
  fake_reset();
  TEST("http_build_get rejects bad host label boundary");
  char buf[512];
  if (capy_http_build_get_request("bad-.example", 80, "/", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("bad host label boundary accepted");
}

static void test_http_build_get_rejects_raw_tab_path(void) {
  fake_reset();
  TEST("http_build_get rejects raw tab in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/ok\tbad", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw tab path accepted");
}


static void test_http_build_get_rejects_fragment_in_path(void) {
  fake_reset();
  TEST("http_build_get rejects fragment in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/path#secret", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("fragment path accepted");
}

static void test_http_build_get_rejects_backslash_path(void) {
  fake_reset();
  TEST("http_build_get rejects backslash in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/a\\b", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("backslash path accepted");
}

static void test_http_build_get_rejects_percent_nul_path(void) {
  fake_reset();
  TEST("http_build_get rejects %00 in path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "/a%00b", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("percent NUL path accepted");
}

static void test_http_build_get_rejects_relative_path(void) {
  fake_reset();
  TEST("http_build_get rejects non-empty relative path");
  char buf[512];
  if (capy_http_build_get_request("x.example", 80, "relative", buf, sizeof(buf)) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("relative path accepted");
}

/* === HTTP status line parser ================================= */

static void test_http_parse_status_200(void) {
  fake_reset();
  TEST("parse_status_line: HTTP/1.1 200 OK");
  const char *s = "HTTP/1.1 200 OK\r\nrest";
  int code = 0;
  int after = capy_http_parse_status_line(s, strlen(s), &code);
  /* "HTTP/1.1 200 OK\r\n" is 17 chars. */
  if (after == 17 && code == 200) PASS();
  else FAIL("status 200 parse wrong");
}

static void test_http_parse_status_404(void) {
  fake_reset();
  TEST("parse_status_line: HTTP/1.0 404 Not Found");
  const char *s = "HTTP/1.0 404 Not Found\nbody";
  int code = 0;
  int after = capy_http_parse_status_line(s, strlen(s), &code);
  if (after == 23 && code == 404) PASS();
  else FAIL("status 404 parse wrong");
}

static void test_http_parse_status_rejects_bad_prefix(void) {
  fake_reset();
  TEST("parse_status_line rejects HTTP/2.0 prefix");
  int code = 0;
  if (capy_http_parse_status_line("HTTP/2.0 200 OK\r\n", 17, &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("HTTP/2.0 accepted");
}

static void test_http_parse_status_rejects_non_digit_code(void) {
  fake_reset();
  TEST("parse_status_line rejects non-digit status code");
  int code = 0;
  if (capy_http_parse_status_line("HTTP/1.1 X00 Bad\r\n", 18, &code) == -1) PASS();
  else FAIL("non-digit code accepted");
}


static void test_http_parse_status_rejects_below_range(void) {
  fake_reset();
  TEST("parse_status_line rejects status below HTTP range");
  const char *s = "HTTP/1.1 099 Weird\r\n";
  int code = 0;
  if (capy_http_parse_status_line(s, strlen(s), &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("status 099 accepted");
}

static void test_http_parse_status_rejects_above_range(void) {
  fake_reset();
  TEST("parse_status_line rejects status above HTTP range");
  const char *s = "HTTP/1.1 600 Weird\r\n";
  int code = 0;
  if (capy_http_parse_status_line(s, strlen(s), &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("status 600 accepted");
}

static void test_http_parse_status_rejects_missing_reason_separator(void) {
  fake_reset();
  TEST("parse_status_line rejects missing reason separator");
  int code = 0;
  if (capy_http_parse_status_line("HTTP/1.1 200OK\r\n", 16, &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("status line without separator accepted");
}

static void test_http_parse_status_rejects_ctl_reason(void) {
  fake_reset();
  TEST("parse_status_line rejects raw control in reason phrase");
  const char *s = "HTTP/1.1 200 O" "\x01" "K\r\n";
  int code = 0;
  if (capy_http_parse_status_line(s, strlen(s), &code) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("raw control in reason phrase accepted");
}

/* === HTTP header parser ====================================== */

static void test_http_parse_headers_basic(void) {
  fake_reset();
  TEST("parse_headers: 2 headers + empty terminator");
  const char *block =
      "Content-Length: 42\r\n"
      "Server: nginx\r\n"
      "\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  int n = capy_http_parse_headers(block, strlen(block), &r);
  if (n == 2 &&
      strcmp(r.headers[0].name, "Content-Length") == 0 &&
      strcmp(r.headers[0].value, "42") == 0 &&
      strcmp(r.headers[1].name, "Server") == 0 &&
      strcmp(r.headers[1].value, "nginx") == 0) PASS();
  else FAIL("header values wrong");
}

static void test_http_parse_headers_strips_ows(void) {
  fake_reset();
  TEST("parse_headers strips leading/trailing OWS");
  const char *block = "X-Custom:    spaced value   \r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  capy_http_parse_headers(block, strlen(block), &r);
  if (r.header_count == 1 &&
      strcmp(r.headers[0].value, "spaced value") == 0) PASS();
  else FAIL("OWS not stripped");
}



static void test_http_parse_headers_rejects_unterminated_line(void) {
  fake_reset();
  TEST("parse_headers rejects unterminated header line");
  const char *block = "X-Test: ok";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("unterminated header line accepted");
}

static void test_http_parse_headers_rejects_missing_empty_terminator(void) {
  fake_reset();
  TEST("parse_headers rejects missing empty terminator");
  const char *block = "X-Test: ok\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("header block without empty terminator accepted");
}

static void test_http_parse_headers_rejects_missing_separator(void) {
  fake_reset();
  TEST("parse_headers rejects header without separator");
  const char *block = "X-Broken-Header\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("header without separator accepted");
}

static void test_http_parse_headers_rejects_bad_name_space(void) {
  fake_reset();
  TEST("parse_headers rejects space in header name");
  const char *block = "Bad Name: value\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("bad header name accepted");
}

static void test_http_parse_headers_rejects_ctl_value(void) {
  fake_reset();
  TEST("parse_headers rejects raw control in header value");
  const char *block = "X-Test: ok\x01" "bad\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("bad header value accepted");
}


static void test_http_parse_headers_rejects_obs_fold(void) {
  fake_reset();
  TEST("parse_headers rejects folded continuation lines");
  const char *block = "X-Test: ok\r\n folded\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(block, strlen(block), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("folded header continuation accepted");
}


static const char k_header_block_bad_name_after_cap[] =
    "X-Fill-00: a\r\n"
    "X-Fill-01: b\r\n"
    "X-Fill-02: c\r\n"
    "X-Fill-03: d\r\n"
    "X-Fill-04: e\r\n"
    "X-Fill-05: f\r\n"
    "X-Fill-06: g\r\n"
    "X-Fill-07: h\r\n"
    "X-Fill-08: i\r\n"
    "X-Fill-09: j\r\n"
    "X-Fill-10: k\r\n"
    "X-Fill-11: l\r\n"
    "X-Fill-12: m\r\n"
    "X-Fill-13: n\r\n"
    "X-Fill-14: o\r\n"
    "X-Fill-15: p\r\n"
    "Bad Name: value\r\n"
    "\r\n";

static void test_http_parse_headers_rejects_bad_name_after_header_cap(void) {
  fake_reset();
  TEST("parse_headers validates names beyond stored header cap");
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  if (capy_http_parse_headers(k_header_block_bad_name_after_cap,
                              strlen(k_header_block_bad_name_after_cap), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EPARSE && r.header_count == 0) PASS();
  else FAIL("bad header name beyond cap accepted");
}

static void test_http_parse_headers_keeps_internal_tab_value(void) {
  fake_reset();
  TEST("parse_headers allows HTAB inside header value");
  const char *block = "X-Test: ok\tvalue\r\n\r\n";
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  int n = capy_http_parse_headers(block, strlen(block), &r);
  if (n == 1 && strcmp(r.headers[0].value, "ok\tvalue") == 0) PASS();
  else FAIL("HTAB value rejected");
}

static void test_http_response_find_header_ci(void) {
  fake_reset();
  TEST("find_header is case-insensitive");
  struct capy_http_response r;
  memset(&r, 0, sizeof(r));
  r.header_count = 1;
  strcpy(r.headers[0].name, "Content-Type");
  strcpy(r.headers[0].value, "text/html");
  const char *v1 = capy_http_response_find_header(&r, "content-type");
  const char *v2 = capy_http_response_find_header(&r, "CONTENT-TYPE");
  const char *v3 = capy_http_response_find_header(&r, "Other");
  if (v1 && strcmp(v1, "text/html") == 0 &&
      v2 && strcmp(v2, "text/html") == 0 &&
      v3 == NULL) PASS();
  else FAIL("CI lookup broken");
}

/* === capy_http_get end-to-end (with fakes) =================== */

static const char k_canned_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_happy_path(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x01020304u; /* 1.2.3.4 */
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;
  g_fake.recv_chunk_size = 0; /* deliver whole thing in one capy_recv */

  TEST("http_get happy path: parse status + headers + body");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.header_count == 2 &&
      r.content_length == 13 && r.body_len == 13 && r.truncated == 0 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("end-to-end parse failed");
}


static const char k_canned_lf_only_response[] =
    "HTTP/1.1 200 OK\n"
    "Content-Length: 13\n"
    "Content-Type: text/plain\n"
    "\n"
    "Hello, world!";

static void test_http_get_lf_only_head(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_lf_only_response;
  g_fake.recv_canned_len = sizeof(k_canned_lf_only_response) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get accepts LF-only response head terminator");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/lf-only", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.header_count == 2 &&
      r.content_length == 13 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("LF-only head terminator rejected");
}

static void test_http_get_lf_only_head_split_recv(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_lf_only_response;
  g_fake.recv_canned_len = sizeof(k_canned_lf_only_response) - 1;
  g_fake.recv_chunk_size = 7;

  TEST("http_get reassembles LF-only head split across recv calls");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/lf-only-split", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("split LF-only head terminator rejected");
}

static void test_http_get_chunked_recv(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;
  g_fake.recv_chunk_size = 7; /* force capy_recv to deliver in 7-byte slices */

  TEST("http_get reassembles head split across multiple recv calls");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 200 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("chunked recv didn't reassemble");
}

static void test_http_get_body_truncated(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;

  TEST("http_get marks truncated when body_buf too small");
  uint8_t body[5];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  /* Body is "Hello, world!" (13 bytes); buf is 5. We expect the
   * first 5 bytes ("Hello") in body, body_len == 5, truncated == 1. */
  if (rc == 0 && r.body_len == 5 && r.truncated == 1 &&
      memcmp(body, "Hello", 5) == 0) PASS();
  else FAIL("truncation not flagged");
}


static void test_http_get_truncated_known_length_stops_without_eof_recv(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get stops at known Content-Length after truncation");
  uint8_t body[5];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.body_len == 5 && r.truncated == 1 &&
      g_fake.recv_calls == 1 && memcmp(body, "Hello", 5) == 0) PASS();
  else FAIL("known Content-Length waited for EOF after truncation");
}


static const char k_canned_short_content_length_body[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello";

static void test_http_get_rejects_short_content_length_body(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_short_content_length_body;
  g_fake.recv_canned_len = sizeof(k_canned_short_content_length_body) - 1;

  TEST("http_get rejects EOF before Content-Length body is complete");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("short Content-Length body accepted");
}

static void test_http_get_https_unsupported(void) {
  fake_reset();
  TEST("http_get rejects https:// before DNS/socket I/O");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("https://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED &&
      g_fake.dns_calls == 0 && g_fake.socket_calls == 0 &&
      g_fake.connect_calls == 0 && g_fake.send_calls == 0 &&
      g_fake.recv_calls == 0 && g_fake.close_calls == 0) PASS();
  else FAIL("https touched network or was accepted");
}

static void test_https_tls_error_mapping(void) {
  TEST("https adapter maps libcapy-tls errors to libcapy-net errors");
  if (capy_net_internal_tls_error_to_net(CAPY_TLS_OK) == CAPY_NET_OK &&
      capy_net_internal_tls_error_to_net(CAPY_TLS_EINVAL) == CAPY_NET_EINVAL &&
      capy_net_internal_tls_error_to_net(CAPY_TLS_EUNSUPPORTED) ==
          CAPY_NET_EUNSUPPORTED &&
      capy_net_internal_tls_error_to_net(CAPY_TLS_ESTATE) ==
          CAPY_NET_EUNSUPPORTED &&
      capy_net_internal_tls_error_to_net((capy_tls_err_t)-99) ==
          CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("unexpected TLS error mapping");
}

static void test_https_adapter_rejects_invalid_state(void) {
  fake_reset();
  TEST("https adapter rejects invalid URL state");
  if (capy_net_internal_https_fail_closed(0) == -1 &&
      capy_net_last_error() == CAPY_NET_EINVAL) PASS();
  else FAIL("invalid https adapter state accepted");
}

static const char k_canned_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_404_no_body(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_404;
  g_fake.recv_canned_len = sizeof(k_canned_404) - 1;

  TEST("http_get parses 404 with empty body");
  uint8_t body[8];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/missing", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 404 && r.body_len == 0 &&
      r.content_length == 0) PASS();
  else FAIL("404 not parsed");
}




static const char k_canned_100_then_200[] =
    "HTTP/1.1 100 Continue\r\n"
    "\r\n"
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "Hello";

static void test_http_get_rejects_100_continue(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_100_then_200;
  g_fake.recv_canned_len = sizeof(k_canned_100_then_200) - 1;

  TEST("http_get rejects informational 100 response");
  uint8_t body[16];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/continue", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("100 Continue accepted as final response");
}

static const char k_canned_103_then_200[] =
    "HTTP/1.1 103 Early Hints\r\n"
    "Link: </style.css>\r\n"
    "\r\n"
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "Hello";

static void test_http_get_rejects_103_early_hints(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_103_then_200;
  g_fake.recv_canned_len = sizeof(k_canned_103_then_200) - 1;

  TEST("http_get rejects informational 103 response");
  uint8_t body[16];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/early-hints", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("103 Early Hints accepted as final response");
}

static const char k_canned_204_with_tail[] =
    "HTTP/1.1 204 No Content\r\n"
    "\r\n"
    "Ignored";

static void test_http_get_204_ignores_body_tail(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_204_with_tail;
  g_fake.recv_canned_len = sizeof(k_canned_204_with_tail) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get treats 204 as known empty body");
  uint8_t body[16];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/no-content", body, sizeof(body), &r);
  if (rc == 0 && r.status_code == 204 && r.content_length == 0 &&
      r.body_len == 0 && r.truncated == 0 && g_fake.recv_calls == 1) PASS();
  else FAIL("204 response exposed body tail");
}

static const char k_canned_304_with_nonzero_content_length[] =
    "HTTP/1.1 304 Not Modified\r\n"
    "Content-Length: 1\r\n"
    "\r\n"
    "X";

static void test_http_get_rejects_304_nonzero_content_length(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_304_with_nonzero_content_length;
  g_fake.recv_canned_len = sizeof(k_canned_304_with_nonzero_content_length) - 1;

  TEST("http_get rejects non-zero Content-Length on 304");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/not-modified", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("304 with non-zero Content-Length accepted");
}

static const char k_canned_content_length_zero_with_tail[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 0\r\n"
    "\r\n"
    "Ignored";

static void test_http_get_content_length_zero_ignores_tail(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_content_length_zero_with_tail;
  g_fake.recv_canned_len = sizeof(k_canned_content_length_zero_with_tail) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get treats Content-Length zero as known empty body");
  uint8_t body[16];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/empty", body, sizeof(body), &r);
  if (rc == 0 && r.content_length == 0 && r.body_len == 0 &&
      r.truncated == 0 && g_fake.recv_calls == 1) PASS();
  else FAIL("Content-Length zero consumed tail bytes as body");
}



static const char k_canned_identity_content_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding: identity\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_accepts_identity_content_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_identity_content_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_identity_content_encoding) - 1;

  TEST("http_get accepts identity Content-Encoding");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/identity", body, sizeof(body), &r);
  if (rc == 0 && r.content_length == 13 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("identity Content-Encoding rejected");
}

static const char k_canned_gzip_content_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding: gzip\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_gzip_content_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_gzip_content_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_gzip_content_encoding) - 1;

  TEST("http_get rejects unsupported Content-Encoding");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/gzip", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("gzip Content-Encoding accepted without decoder");
}


static const char k_canned_empty_content_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Encoding:   \r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_empty_content_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_empty_content_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_empty_content_encoding) - 1;

  TEST("http_get rejects empty Content-Encoding");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/empty-encoding", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("empty Content-Encoding accepted");
}


static const char k_canned_header_without_separator[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Broken-Header\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_header_without_separator(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_header_without_separator;
  g_fake.recv_canned_len = sizeof(k_canned_header_without_separator) - 1;

  TEST("http_get rejects header without separator");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/no-header-separator", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("header without separator accepted by http_get");
}

static const char k_canned_obs_fold_header[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Test: ok\r\n"
    " folded\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_obs_fold_header(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_obs_fold_header;
  g_fake.recv_canned_len = sizeof(k_canned_obs_fold_header) - 1;

  TEST("http_get rejects folded response headers");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/folded", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("folded response header accepted");
}

static const char k_canned_chunked[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "5\r\nHello\r\n0\r\n\r\n";

static void test_http_get_rejects_chunked(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_chunked;
  g_fake.recv_canned_len = sizeof(k_canned_chunked) - 1;

  TEST("http_get rejects chunked transfer-encoding");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("chunked accepted");
}

static const char k_canned_identity_transfer_encoding[] =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: identity\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_identity_transfer_encoding(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_identity_transfer_encoding;
  g_fake.recv_canned_len = sizeof(k_canned_identity_transfer_encoding) - 1;

  TEST("http_get rejects non-chunked Transfer-Encoding");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EUNSUPPORTED) PASS();
  else FAIL("non-chunked Transfer-Encoding accepted");
}

static const char k_canned_bad_content_length_suffix[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13x\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_bad_content_length_suffix(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_bad_content_length_suffix;
  g_fake.recv_canned_len = sizeof(k_canned_bad_content_length_suffix) - 1;

  TEST("http_get rejects Content-Length with suffix");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("bad Content-Length suffix accepted");
}

static const char k_canned_bad_content_length_overflow[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 184467440737095516160\r\n"
    "\r\n";

static void test_http_get_rejects_content_length_overflow(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_bad_content_length_overflow;
  g_fake.recv_canned_len = sizeof(k_canned_bad_content_length_overflow) - 1;

  TEST("http_get rejects Content-Length overflow");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("Content-Length overflow accepted");
}

static const char k_canned_duplicate_content_length_same[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_accepts_duplicate_content_length_same(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_duplicate_content_length_same;
  g_fake.recv_canned_len = sizeof(k_canned_duplicate_content_length_same) - 1;

  TEST("http_get accepts duplicate matching Content-Length");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/", body, sizeof(body), &r);
  if (rc == 0 && r.content_length == 13 && r.body_len == 13 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("duplicate matching Content-Length rejected");
}

static const char k_canned_duplicate_content_length_conflict[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Content-Length: 12\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_duplicate_content_length_conflict(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_duplicate_content_length_conflict;
  g_fake.recv_canned_len = sizeof(k_canned_duplicate_content_length_conflict) - 1;

  TEST("http_get rejects conflicting duplicate Content-Length");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("conflicting duplicate Content-Length accepted");
}


static const char k_canned_content_length_after_header_cap[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Fill-00: a\r\n"
    "X-Fill-01: b\r\n"
    "X-Fill-02: c\r\n"
    "X-Fill-03: d\r\n"
    "X-Fill-04: e\r\n"
    "X-Fill-05: f\r\n"
    "X-Fill-06: g\r\n"
    "X-Fill-07: h\r\n"
    "X-Fill-08: i\r\n"
    "X-Fill-09: j\r\n"
    "X-Fill-10: k\r\n"
    "X-Fill-11: l\r\n"
    "X-Fill-12: m\r\n"
    "X-Fill-13: n\r\n"
    "X-Fill-14: o\r\n"
    "X-Fill-15: p\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_uses_content_length_after_header_cap(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_content_length_after_header_cap;
  g_fake.recv_canned_len = sizeof(k_canned_content_length_after_header_cap) - 1;
  g_fake.recv_chunk_size = 0;

  TEST("http_get resolves Content-Length beyond stored header cap");
  uint8_t body[64];
  struct capy_http_response r;
  int rc = capy_http_get("http://example.com/late-cl", body, sizeof(body), &r);
  if (rc == 0 && r.header_count == CAPY_HTTP_MAX_HEADERS &&
      r.content_length == 13 && r.body_len == 13 && g_fake.recv_calls == 1 &&
      memcmp(body, "Hello, world!", 13) == 0) PASS();
  else FAIL("Content-Length beyond stored cap ignored");
}

static const char k_canned_content_length_conflict_after_header_cap[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Fill-00: a\r\n"
    "X-Fill-01: b\r\n"
    "X-Fill-02: c\r\n"
    "X-Fill-03: d\r\n"
    "X-Fill-04: e\r\n"
    "X-Fill-05: f\r\n"
    "X-Fill-06: g\r\n"
    "X-Fill-07: h\r\n"
    "X-Fill-08: i\r\n"
    "X-Fill-09: j\r\n"
    "X-Fill-10: k\r\n"
    "X-Fill-11: l\r\n"
    "X-Fill-12: m\r\n"
    "X-Fill-13: n\r\n"
    "X-Fill-14: o\r\n"
    "Content-Length: 13\r\n"
    "Content-Length: 12\r\n"
    "\r\n"
    "Hello, world!";

static void test_http_get_rejects_content_length_conflict_after_header_cap(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_content_length_conflict_after_header_cap;
  g_fake.recv_canned_len = sizeof(k_canned_content_length_conflict_after_header_cap) - 1;

  TEST("http_get rejects Content-Length conflict beyond stored header cap");
  uint8_t body[64];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/conflict-late-cl", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("late conflicting Content-Length accepted");
}


static const char k_canned_bad_header_value_after_cap[] =
    "HTTP/1.1 200 OK\r\n"
    "X-Fill-00: a\r\n"
    "X-Fill-01: b\r\n"
    "X-Fill-02: c\r\n"
    "X-Fill-03: d\r\n"
    "X-Fill-04: e\r\n"
    "X-Fill-05: f\r\n"
    "X-Fill-06: g\r\n"
    "X-Fill-07: h\r\n"
    "X-Fill-08: i\r\n"
    "X-Fill-09: j\r\n"
    "X-Fill-10: k\r\n"
    "X-Fill-11: l\r\n"
    "X-Fill-12: m\r\n"
    "X-Fill-13: n\r\n"
    "X-Fill-14: o\r\n"
    "X-Fill-15: p\r\n"
    "X-Late: ok\x01" "bad\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static void test_http_get_rejects_bad_header_value_after_header_cap(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_bad_header_value_after_cap;
  g_fake.recv_canned_len = sizeof(k_canned_bad_header_value_after_cap) - 1;

  TEST("http_get validates header values beyond stored header cap");
  uint8_t body[8];
  struct capy_http_response r;
  if (capy_http_get("http://example.com/bad-late-header", body, sizeof(body), &r) == -1 &&
      capy_net_last_error() == CAPY_NET_EHTTP) PASS();
  else FAIL("bad header value beyond cap accepted");
}


static void test_http_get_strips_fragment_from_request_target(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;

  TEST("http_get strips URL fragment from request target");
  uint8_t body[64];
  struct capy_http_response r;
  capy_http_get("http://example.com/path?x=1#secret", body, sizeof(body), &r);
  g_fake.send_log[g_fake.send_log_len] = '\0';
  if (strstr((char *)g_fake.send_log, "GET /path?x=1 HTTP/1.1\r\n") != NULL &&
      strstr((char *)g_fake.send_log, "#secret") == NULL) PASS();
  else FAIL("fragment leaked into request target");
}

static void test_http_get_request_format(void) {
  fake_reset();
  g_fake.dns_canned_ip = 0x7F000001u;
  g_fake.recv_canned_buf = (const uint8_t *)k_canned_response;
  g_fake.recv_canned_len = sizeof(k_canned_response) - 1;

  TEST("http_get sends well-formed GET request on the wire");
  uint8_t body[64];
  struct capy_http_response r;
  capy_http_get("http://example.com:8080/path?x=1", body, sizeof(body), &r);
  /* The fake send_log captures bytes pushed via capy_send_all. */
  g_fake.send_log[g_fake.send_log_len] = '\0';
  if (strstr((char *)g_fake.send_log, "GET /path?x=1 HTTP/1.1\r\n") != NULL &&
      strstr((char *)g_fake.send_log, "Host: example.com:8080\r\n") != NULL &&
      strstr((char *)g_fake.send_log, "Connection: close\r\n\r\n") != NULL) PASS();
  else FAIL("request line / Host header malformed");
}

/* === Last error reset on success ============================= */

static void test_last_error_resets(void) {
  uint32_t ip = 0;
  /* Provoke an error first. */
  (void)capy_inet_pton4("bad", &ip);
  TEST("last_error reflects most recent failure (CAPY_NET_EPARSE)");
  if (capy_net_last_error() == CAPY_NET_EPARSE) PASS();
  else FAIL("no error recorded");

  /* Now a successful call should reset. */
  (void)capy_inet_pton4("1.2.3.4", &ip);
  TEST("successful call resets last_error to CAPY_NET_OK");
  if (capy_net_last_error() == CAPY_NET_OK) PASS();
  else FAIL("error not cleared");
}

/* === Entry point ============================================= */

int test_capylibc_net_run(void) {
  printf("[test_capylibc_net]\n");
  tests_run = 0;
  tests_passed = 0;

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
  test_url_parse_http_minimal();
  test_url_parse_https_with_port_and_path();
  test_url_parse_http_with_query();
  test_url_parse_http_no_path_with_query();
  test_url_parse_strips_fragment_from_path();
  test_url_parse_strips_fragment_after_query();
  test_url_parse_https_default_port();
  test_url_parse_rejects_unknown_scheme();
  test_url_parse_rejects_userinfo();
  test_url_parse_rejects_backslash_in_host();
  test_url_parse_rejects_percent_in_host();
  test_url_parse_rejects_empty_host_label();
  test_url_parse_rejects_hyphen_edge_host_label();
  test_url_parse_rejects_ipv6();
  test_url_parse_rejects_empty_host();
  test_url_parse_rejects_zero_port();
  test_url_parse_rejects_overflow_port();
  test_url_parse_rejects_null();
  test_url_parse_rejects_raw_crlf_in_path();
  test_url_parse_rejects_raw_tab_in_query();
  test_url_parse_rejects_backslash_in_path();
  test_url_parse_rejects_percent_nul_in_path();
  test_url_parse_rejects_percent_nul_in_query();
  test_http_build_get_default_port();
  test_http_build_get_custom_port();
  test_http_build_get_rejects_zero_port();
  test_http_build_get_buffer_too_small();
  test_http_build_get_rejects_raw_crlf_host();
  test_http_build_get_rejects_ambiguous_host_char();
  test_http_build_get_rejects_bad_host_label_boundary();
  test_http_build_get_rejects_raw_tab_path();
  test_http_build_get_rejects_fragment_in_path();
  test_http_build_get_rejects_backslash_path();
  test_http_build_get_rejects_percent_nul_path();
  test_http_build_get_rejects_relative_path();
  test_http_parse_status_200();
  test_http_parse_status_404();
  test_http_parse_status_rejects_bad_prefix();
  test_http_parse_status_rejects_non_digit_code();
  test_http_parse_status_rejects_below_range();
  test_http_parse_status_rejects_above_range();
  test_http_parse_status_rejects_missing_reason_separator();
  test_http_parse_status_rejects_ctl_reason();
  test_http_parse_headers_basic();
  test_http_parse_headers_strips_ows();
  test_http_parse_headers_rejects_unterminated_line();
  test_http_parse_headers_rejects_missing_empty_terminator();
  test_http_parse_headers_rejects_missing_separator();
  test_http_parse_headers_rejects_bad_name_space();
  test_http_parse_headers_rejects_ctl_value();
  test_http_parse_headers_rejects_obs_fold();
  test_http_parse_headers_rejects_bad_name_after_header_cap();
  test_http_parse_headers_keeps_internal_tab_value();
  test_http_response_find_header_ci();
  test_http_get_happy_path();
  test_http_get_lf_only_head();
  test_http_get_lf_only_head_split_recv();
  test_http_get_chunked_recv();
  test_http_get_body_truncated();
  test_http_get_truncated_known_length_stops_without_eof_recv();
  test_http_get_rejects_short_content_length_body();
  test_http_get_https_unsupported();
  test_https_tls_error_mapping();
  test_https_adapter_rejects_invalid_state();
  test_http_get_404_no_body();
  test_http_get_rejects_100_continue();
  test_http_get_rejects_103_early_hints();
  test_http_get_204_ignores_body_tail();
  test_http_get_rejects_304_nonzero_content_length();
  test_http_get_content_length_zero_ignores_tail();
  test_http_get_accepts_identity_content_encoding();
  test_http_get_rejects_gzip_content_encoding();
  test_http_get_rejects_empty_content_encoding();
  test_http_get_rejects_header_without_separator();
  test_http_get_rejects_obs_fold_header();
  test_http_get_rejects_chunked();
  test_http_get_rejects_identity_transfer_encoding();
  test_http_get_rejects_bad_content_length_suffix();
  test_http_get_rejects_content_length_overflow();
  test_http_get_accepts_duplicate_content_length_same();
  test_http_get_rejects_duplicate_content_length_conflict();
  test_http_get_uses_content_length_after_header_cap();
  test_http_get_rejects_content_length_conflict_after_header_cap();
  test_http_get_rejects_bad_header_value_after_header_cap();
  test_http_get_strips_fragment_from_request_target();
  test_http_get_request_format();
  test_last_error_resets();

  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
