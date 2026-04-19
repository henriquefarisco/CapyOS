#pragma GCC optimize("O0")
#include "net/dns_cache.h"
#ifndef UNIT_TEST
#include "drivers/timer/pit.h"
#endif
#include <stddef.h>

static struct dns_cache_entry cache[DNS_CACHE_MAX_ENTRIES];
static struct dns_cache_stats cache_stats;

static int dns_streq(const char *a, const char *b) {
  while (*a && *b) { if (*a != *b) return 0; a++; b++; }
  return *a == *b;
}

static void dns_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

void dns_cache_init(void) {
  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    cache[i].valid = 0;
    cache[i].name[0] = '\0';
    cache[i].ip = 0;
    cache[i].ttl = 0;
    cache[i].created_tick = 0;
  }
  cache_stats.entries = 0;
  cache_stats.hits = 0;
  cache_stats.misses = 0;
  cache_stats.evictions = 0;
  cache_stats.expired = 0;
}

static uint64_t dns_cache_current_tick(void) {
#ifdef UNIT_TEST
  return 0;
#else
  return pit_ticks();
#endif
}

int dns_cache_lookup(const char *name, uint32_t *out_ip) {
  if (!name || !out_ip) return -1;
  uint64_t now = dns_cache_current_tick();
  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    if (cache[i].valid && dns_streq(cache[i].name, name)) {
      if (cache[i].ttl > 0 && cache[i].created_tick > 0 &&
          now > cache[i].created_tick &&
          (now - cache[i].created_tick) > (uint64_t)cache[i].ttl) {
        cache[i].valid = 0;
        if (cache_stats.entries > 0) cache_stats.entries--;
        cache_stats.expired++;
        cache_stats.misses++;
        return -1;
      }
      *out_ip = cache[i].ip;
      cache_stats.hits++;
      return 0;
    }
  }
  cache_stats.misses++;
  return -1;
}

void dns_cache_insert(const char *name, uint32_t ip, uint32_t ttl) {
  if (!name) return;

  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    if (cache[i].valid && dns_streq(cache[i].name, name)) {
      cache[i].ip = ip;
      cache[i].ttl = ttl > 0 ? ttl : DNS_CACHE_TTL_DEFAULT;
      cache[i].created_tick = 0;
      return;
    }
  }

  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    if (!cache[i].valid) {
      dns_strcpy(cache[i].name, name, DNS_CACHE_NAME_MAX);
      cache[i].ip = ip;
      cache[i].ttl = ttl > 0 ? ttl : DNS_CACHE_TTL_DEFAULT;
      cache[i].created_tick = 0;
      cache[i].valid = 1;
      cache_stats.entries++;
      return;
    }
  }

  uint64_t oldest_tick = 0;
  int oldest_idx = 0;
  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    if (cache[i].created_tick >= oldest_tick) {
      oldest_tick = cache[i].created_tick;
      oldest_idx = i;
    }
  }
  dns_strcpy(cache[oldest_idx].name, name, DNS_CACHE_NAME_MAX);
  cache[oldest_idx].ip = ip;
  cache[oldest_idx].ttl = ttl > 0 ? ttl : DNS_CACHE_TTL_DEFAULT;
  cache[oldest_idx].created_tick = 0;
  cache[oldest_idx].valid = 1;
  cache_stats.evictions++;
}

void dns_cache_invalidate(const char *name) {
  if (!name) return;
  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    if (cache[i].valid && dns_streq(cache[i].name, name)) {
      cache[i].valid = 0;
      if (cache_stats.entries > 0) cache_stats.entries--;
      return;
    }
  }
}

void dns_cache_flush(void) {
  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) cache[i].valid = 0;
  cache_stats.entries = 0;
}

void dns_cache_tick(uint64_t now) {
  for (int i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
    if (!cache[i].valid) continue;
    if (cache[i].created_tick == 0) {
      cache[i].created_tick = now;
      continue;
    }
    uint64_t age = now - cache[i].created_tick;
    if (age > (uint64_t)cache[i].ttl * 100) {
      cache[i].valid = 0;
      if (cache_stats.entries > 0) cache_stats.entries--;
      cache_stats.expired++;
    }
  }
}

void dns_cache_stats_get(struct dns_cache_stats *out) {
  if (out) *out = cache_stats;
}
