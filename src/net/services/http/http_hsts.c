/*
 * src/net/services/http/http_hsts.c — HTTP Strict-Transport-Security store
 * (RFC 6797 subset) for the CapyOS browser/fetch path (Etapa 7 / Slice 7.6
 * hardening). See net/http_hsts.h. Pure, deterministic (clock injected),
 * fail-closed, freestanding; the store is caller-owned.
 */
#include "net/http_hsts.h"

#include <stddef.h>
#include <stdint.h>

/* ---- string helpers ------------------------------------------------------ */

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static int ci_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (lc(*a) != lc(*b)) return 0;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static size_t s_len(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}

static void copy_lower(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  if (cap == 0) return;
  while (i < cap - 1 && src[i]) {
    dst[i] = lc(src[i]);
    i++;
  }
  dst[i] = '\0';
}

static int is_space(char c) { return c == ' ' || c == '\t'; }

static int host_is_ip_literal(const char *host) {
  const char *p = host;
  int has_digit = 0, only_digits_dots = 1;
  if (*host == '[') return 1; /* bracketed IPv6 */
  for (; *p; p++) {
    if (*p >= '0' && *p <= '9')
      has_digit = 1;
    else if (*p != '.')
      only_digits_dots = 0;
  }
  return has_digit && only_digits_dots;
}

/* domain-suffix match: 1 if host == domain or host is a subdomain of domain. */
static int domain_covers(const char *domain, const char *host, int subdomains) {
  size_t lh, ld;
  if (ci_eq(host, domain)) return 1;
  if (!subdomains) return 0;
  lh = s_len(host);
  ld = s_len(domain);
  if (lh <= ld) return 0;
  {
    size_t off = lh - ld, i;
    if (host[off - 1] != '.') return 0;
    for (i = 0; i < ld; i++)
      if (lc(host[off + i]) != lc(domain[i])) return 0;
    return 1;
  }
}

/* ---- Strict-Transport-Security directive parse --------------------------- */

struct sts_parsed {
  int has_max_age;
  long max_age;
  int include_subdomains;
};

static long parse_uint(const char *s, const char *end) {
  long v = 0;
  int any = 0;
  /* a quoted-string value ("31536000") is tolerated by skipping quotes. */
  while (s < end && (*s == '"' || is_space(*s))) s++;
  while (s < end && *s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    if (v > 0x7FFFFFFFL) v = 0x7FFFFFFFL;
    s++;
    any = 1;
  }
  return any ? v : -1;
}

/* Match directive name in [s,e) (case-insensitive) against NUL-term `name`. */
static int dir_name_is(const char *s, const char *e, const char *name) {
  size_t i = 0;
  while (s + i < e && name[i]) {
    if (lc(s[i]) != lc(name[i])) return 0;
    i++;
  }
  return (s + i == e) && name[i] == '\0';
}

static void sts_parse(const char *value, struct sts_parsed *out) {
  const char *p = value;
  out->has_max_age = 0;
  out->max_age = 0;
  out->include_subdomains = 0;
  if (!value) return;
  while (*p) {
    const char *tok, *semi, *eq, *ns, *ne;
    while (*p == ';' || is_space(*p)) p++;
    if (!*p) break;
    tok = p;
    semi = p;
    while (*semi && *semi != ';') semi++;
    /* split token on '=' */
    eq = tok;
    while (eq < semi && *eq != '=') eq++;
    ns = tok;
    ne = eq;
    while (ne > ns && is_space(ne[-1])) ne--;
    if (dir_name_is(ns, ne, "max-age")) {
      if (eq < semi) {
        long v = parse_uint(eq + 1, semi);
        if (v >= 0) {
          out->has_max_age = 1;
          out->max_age = v;
        }
      }
    } else if (dir_name_is(ns, ne, "includeSubDomains")) {
      out->include_subdomains = 1;
    }
    /* "preload" and unknown directives are ignored. */
    p = (*semi == ';') ? semi + 1 : semi;
  }
}

/* ---- store --------------------------------------------------------------- */

void http_hsts_init(struct http_hsts_store *s) {
  uint32_t i;
  if (!s) return;
  for (i = 0; i < HTTP_HSTS_MAX_ENTRIES; i++) {
    s->entries[i].valid = 0;
    s->entries[i].lru = 0;
  }
  s->clock = 0;
  s->set_count = s->deleted = s->evictions = 0;
}

static int hsts_expired(const struct http_hsts_entry *e, long now) {
  return e->expires <= now;
}

static int find_exact(struct http_hsts_store *s, const char *host) {
  uint32_t i;
  for (i = 0; i < HTTP_HSTS_MAX_ENTRIES; i++)
    if (s->entries[i].valid && ci_eq(s->entries[i].domain, host)) return (int)i;
  return -1;
}

static int alloc_slot(struct http_hsts_store *s) {
  uint32_t i;
  int lru_idx = 0;
  uint64_t lru_min = (uint64_t)-1;
  for (i = 0; i < HTTP_HSTS_MAX_ENTRIES; i++) {
    if (!s->entries[i].valid) return (int)i;
    if (s->entries[i].lru <= lru_min) {
      lru_min = s->entries[i].lru;
      lru_idx = (int)i;
    }
  }
  s->evictions++;
  return lru_idx;
}

int http_hsts_process_header(struct http_hsts_store *s, const char *host,
                             const char *value, int secure, long now) {
  struct sts_parsed p;
  int idx;
  if (!s || !host || !value || host[0] == '\0') return 0;
  if (!secure) return 0;                /* RFC 6797 8.1: ignore over http */
  if (host_is_ip_literal(host)) return 0; /* RFC 6797 8.1.1 */
  if (s_len(host) >= HTTP_HSTS_DOMAIN_MAX) return 0;

  sts_parse(value, &p);
  if (!p.has_max_age) return 0; /* max-age required */

  idx = find_exact(s, host);
  if (p.max_age == 0) {
    /* remove an existing policy. */
    if (idx >= 0) {
      s->entries[idx].valid = 0;
      s->deleted++;
      return 1;
    }
    return 0;
  }
  if (idx < 0) idx = alloc_slot(s);
  copy_lower(s->entries[idx].domain, HTTP_HSTS_DOMAIN_MAX, host);
  s->entries[idx].expires = now + p.max_age;
  s->entries[idx].include_subdomains = p.include_subdomains;
  s->entries[idx].valid = 1;
  s->entries[idx].lru = ++s->clock;
  s->set_count++;
  return 1;
}

int http_hsts_should_upgrade(struct http_hsts_store *s, const char *host,
                             long now) {
  uint32_t i;
  if (!s || !host || host[0] == '\0') return 0;
  for (i = 0; i < HTTP_HSTS_MAX_ENTRIES; i++) {
    struct http_hsts_entry *e = &s->entries[i];
    if (!e->valid || hsts_expired(e, now)) continue;
    if (domain_covers(e->domain, host, e->include_subdomains)) return 1;
  }
  return 0;
}

void http_hsts_gc(struct http_hsts_store *s, long now) {
  uint32_t i;
  if (!s) return;
  for (i = 0; i < HTTP_HSTS_MAX_ENTRIES; i++)
    if (s->entries[i].valid && hsts_expired(&s->entries[i], now))
      s->entries[i].valid = 0;
}
