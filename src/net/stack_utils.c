#include "stack_utils.h"

#include <stddef.h>
#include <stdint.h>

void net_stack_mem_zero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

void net_stack_mem_copy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

uint16_t net_stack_htons16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

uint32_t net_stack_htonl32(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

uint16_t net_stack_ntohs16(uint16_t v) { return net_stack_htons16(v); }

uint32_t net_stack_ntohl32(uint32_t v) { return net_stack_htonl32(v); }

uint16_t net_stack_checksum16(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  while (len >= 2) {
    uint16_t word = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    sum += word;
    data += 2;
    len -= 2;
  }
  if (len) {
    sum += (uint16_t)((uint16_t)data[0] << 8);
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFFu) + (sum >> 16);
  }
  return (uint16_t)(~sum);
}

void net_stack_delay_approx_1ms(void) {
  for (volatile uint32_t i = 0; i < 60000u; ++i) {
    __asm__ volatile("pause");
  }
}

void net_ipv4_format(uint32_t ip, char out[16]) {
  if (!out) {
    return;
  }
  {
    uint8_t a = (uint8_t)((ip >> 24) & 0xFFu);
    uint8_t b = (uint8_t)((ip >> 16) & 0xFFu);
    uint8_t c = (uint8_t)((ip >> 8) & 0xFFu);
    uint8_t d = (uint8_t)(ip & 0xFFu);
    char *p = out;
    uint8_t octets[4] = {a, b, c, d};
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t v = octets[i];
      if (v >= 100u) {
        *p++ = (char)('0' + (v / 100u));
        v %= 100u;
        *p++ = (char)('0' + (v / 10u));
        *p++ = (char)('0' + (v % 10u));
      } else if (v >= 10u) {
        *p++ = (char)('0' + (v / 10u));
        *p++ = (char)('0' + (v % 10u));
      } else {
        *p++ = (char)('0' + v);
      }
      if (i != 3u) {
        *p++ = '.';
      }
    }
    *p = '\0';
  }
}
