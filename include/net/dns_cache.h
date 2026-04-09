#ifndef NET_DNS_CACHE_H
#define NET_DNS_CACHE_H

#include <stdint.h>
#include <stddef.h>

#define DNS_CACHE_MAX_ENTRIES 64
#define DNS_CACHE_NAME_MAX 128
#define DNS_CACHE_TTL_DEFAULT 300

struct dns_cache_entry {
  char name[DNS_CACHE_NAME_MAX];
  uint32_t ip;
  uint32_t ttl;
  uint64_t created_tick;
  int valid;
};

struct dns_cache_stats {
  uint32_t entries;
  uint64_t hits;
  uint64_t misses;
  uint64_t evictions;
  uint64_t expired;
};

void dns_cache_init(void);
int dns_cache_lookup(const char *name, uint32_t *out_ip);
void dns_cache_insert(const char *name, uint32_t ip, uint32_t ttl);
void dns_cache_invalidate(const char *name);
void dns_cache_flush(void);
void dns_cache_tick(uint64_t now);
void dns_cache_stats_get(struct dns_cache_stats *out);

#endif /* NET_DNS_CACHE_H */
