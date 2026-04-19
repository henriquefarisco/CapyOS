#ifndef NET_STACK_UTILS_H
#define NET_STACK_UTILS_H

#include <stddef.h>
#include <stdint.h>

void net_stack_mem_zero(void *ptr, size_t len);
void net_stack_mem_copy(void *dst, const void *src, size_t len);
uint16_t net_stack_htons16(uint16_t v);
uint32_t net_stack_htonl32(uint32_t v);
uint16_t net_stack_ntohs16(uint16_t v);
uint32_t net_stack_ntohl32(uint32_t v);
uint16_t net_stack_checksum16(const uint8_t *data, size_t len);
void net_stack_delay_approx_1ms(void);
void net_stack_set_yield_hook(void (*hook)(void));
void net_ipv4_format(uint32_t ip, char out[16]);

#endif /* NET_STACK_UTILS_H */
