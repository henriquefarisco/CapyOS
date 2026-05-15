#ifndef NET_DNS_CACHE_H
#define NET_DNS_CACHE_H

#include <stdint.h>
#include <stddef.h>

#define DNS_CACHE_MAX_ENTRIES 64
#define DNS_CACHE_NAME_MAX 128
#define DNS_CACHE_TTL_DEFAULT 300
/* Sessão 44 (2026-05-08): RFC 2308 §5 caps the negative TTL at
 * SOA MINIMUM, but recursors regularly serve MINIMUM in the days/
 * weeks range (3600..86400). For an embedded system we don't want
 * a typo in the URL bar to stick in the negative cache for a
 * day, so we additionally cap at 1 hour here. The clamp window
 * for negative entries is therefore [0, 3600s]. */
#define DNS_CACHE_NEGATIVE_TTL_MAX 3600

struct dns_cache_entry {
  char name[DNS_CACHE_NAME_MAX];
  uint32_t ip;
  uint32_t ttl;
  uint64_t created_tick;
  int valid;
  uint8_t is_negative;
};

struct dns_cache_stats {
  uint32_t entries;
  uint64_t hits;
  uint64_t misses;
  uint64_t evictions;
  uint64_t expired;
  uint64_t negative_hits;
};

void dns_cache_init(void);
/* Returns 0 only for non-expired POSITIVE entries (with an IP).
 * Negative entries are silently skipped so existing callers don't
 * have to know about negative caching to stay correct. The new
 * negative-aware path is `dns_cache_lookup_negative`. */
int dns_cache_lookup(const char *name, uint32_t *out_ip);
/* Returns 0 if `name` has a non-expired NEGATIVE entry; -1 if
 * there's no entry or the entry is positive/expired. Used by the
 * syscall wrapper to short-circuit active queries for names the
 * upstream said don't exist. */
int dns_cache_lookup_negative(const char *name);
void dns_cache_insert(const char *name, uint32_t ip, uint32_t ttl);
/* Inserts (or replaces) a NEGATIVE entry for `name`. The TTL is
 * stored verbatim; lookup expiration uses `dns_cache_ttl_ticks`
 * with the same DNS_CACHE_TTL_DEFAULT fallback as positive
 * entries when ttl == 0 (sentinel). Caller is expected to clamp
 * the wire-derived TTL via DNS_CACHE_NEGATIVE_TTL_MAX. */
void dns_cache_insert_negative(const char *name, uint32_t ttl);
void dns_cache_invalidate(const char *name);
void dns_cache_flush(void);
void dns_cache_tick(uint64_t now);
void dns_cache_stats_get(struct dns_cache_stats *out);

#endif /* NET_DNS_CACHE_H */
