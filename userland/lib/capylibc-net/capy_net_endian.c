/*
 * userland/lib/capylibc-net/capy_net_endian.c (F4 seção c parte 2/2)
 *
 * Host order ↔ network order helpers, implemented as pure C byte
 * shuffles. Both x86_64 (production target) and arm64 (host test
 * harness) are little-endian, so all four functions unconditionally
 * swap bytes. A future port to a big-endian target would either
 * add a `#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__` no-op branch
 * or, more cleanly, drop these wrappers in favour of compiler
 * builtins -- for now the explicit shuffle keeps the surface
 * portable and trivial to audit.
 */

#include "capylibc-net/capy_net.h"

uint16_t capy_htons(uint16_t v) {
  return (uint16_t)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}

uint32_t capy_htonl(uint32_t v) {
  return ((v & 0x000000FFu) << 24) |
         ((v & 0x0000FF00u) << 8)  |
         ((v & 0x00FF0000u) >> 8)  |
         ((v & 0xFF000000u) >> 24);
}

uint16_t capy_ntohs(uint16_t v) {
  return capy_htons(v);
}

uint32_t capy_ntohl(uint32_t v) {
  return capy_htonl(v);
}
