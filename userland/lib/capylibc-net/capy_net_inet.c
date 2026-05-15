/*
 * userland/lib/capylibc-net/capy_net_inet.c (F4 seção c parte 2/2)
 *
 * Dotted-decimal IPv4 parser/formatter.
 *
 * Returns a host-order 32-bit IP -- e.g. "1.2.3.4" -> 0x01020304
 * (the high byte of the result is the FIRST octet of the dotted
 * address). The function deliberately accepts ONLY the canonical
 * form: four decimal octets separated by '.', each 0..255, no
 * leading '+'/'-', no whitespace, no octal/hex tolerance.
 *
 * inet_aton(3) traditionally accepts shorter forms ("a", "a.b",
 * "a.b.c") and hex/octal -- we don't, both because parsing those
 * is a known historical attack surface (CVE-2014-9263 style) and
 * because the only callers in the F4 surface (capy_tcp_connect_str
 * + future libcapy-net DNS fallback) hand IPs straight from a
 * resolver, so the strict form is sufficient.
 */

#include "capylibc-net/capy_net.h"

#include <stddef.h>
#include <stdint.h>

extern void capy_net_internal_set_error(capy_net_err_t err);
extern void capy_net_internal_reset_error(void);

/* Parse one decimal octet from `*pp`, advancing `*pp` past the
 * digits. Returns 0 on success and writes the value to `*out`,
 * -1 on no digits / value > 255 / leading non-digit. */
static int parse_octet(const char **pp, uint32_t *out) {
  const char *p = *pp;
  if (*p < '0' || *p > '9') return -1;
  uint32_t v = 0;
  int digits = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (uint32_t)(*p - '0');
    if (v > 255) return -1;
    p++;
    digits++;
    if (digits > 3) return -1;  /* "0001" rejected */
  }
  *pp = p;
  *out = v;
  return 0;
}

int capy_inet_pton4(const char *str, uint32_t *out_host_order) {
  capy_net_internal_reset_error();
  if (!str || !out_host_order) {
    capy_net_internal_set_error(CAPY_NET_EINVAL);
    return -1;
  }

  uint32_t octets[4];
  const char *p = str;

  for (int i = 0; i < 4; i++) {
    if (parse_octet(&p, &octets[i]) != 0) {
      capy_net_internal_set_error(CAPY_NET_EPARSE);
      return -1;
    }
    if (i < 3) {
      if (*p != '.') {
        capy_net_internal_set_error(CAPY_NET_EPARSE);
        return -1;
      }
      p++;
    }
  }
  if (*p != '\0') {
    capy_net_internal_set_error(CAPY_NET_EPARSE);
    return -1;
  }

  *out_host_order = (octets[0] << 24) | (octets[1] << 16) |
                    (octets[2] << 8)  |  octets[3];
  return 0;
}

/* Format a 0..255 octet as decimal into `dst`, returning the
 * number of bytes written (1, 2 or 3). The caller guarantees
 * dst has at least 3 bytes available. No NUL is written. */
static int format_octet(uint32_t v, char *dst) {
  if (v >= 100) {
    dst[0] = (char)('0' + v / 100);
    dst[1] = (char)('0' + (v / 10) % 10);
    dst[2] = (char)('0' + v % 10);
    return 3;
  }
  if (v >= 10) {
    dst[0] = (char)('0' + v / 10);
    dst[1] = (char)('0' + v % 10);
    return 2;
  }
  dst[0] = (char)('0' + v);
  return 1;
}

int capy_inet_ntoa4(uint32_t ip_host_order, char *out, size_t cap) {
  capy_net_internal_reset_error();
  if (!out || cap < 16) {
    capy_net_internal_set_error(out ? CAPY_NET_EBUF : CAPY_NET_EINVAL);
    return -1;
  }
  uint32_t a = (ip_host_order >> 24) & 0xFFu;
  uint32_t b = (ip_host_order >> 16) & 0xFFu;
  uint32_t c = (ip_host_order >>  8) & 0xFFu;
  uint32_t d =  ip_host_order        & 0xFFu;

  int written = 0;
  written += format_octet(a, out + written);
  out[written++] = '.';
  written += format_octet(b, out + written);
  out[written++] = '.';
  written += format_octet(c, out + written);
  out[written++] = '.';
  written += format_octet(d, out + written);
  out[written] = '\0';
  return written;
}
