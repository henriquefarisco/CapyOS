/*
 * src/net/services/http/http_cache.c — bounded HTTP response cache (RFC 7234
 * subset) for the CapyOS browser/fetch path (Etapa 7 / Slice 7.5). See
 * include/net/http_cache.h for the contract and scope.
 *
 * Self-contained: its own case-insensitive header scan, integer/date parsers
 * and Cache-Control directive scanner, so the host test links only this TU.
 * Pure, deterministic (clock injected), fail-closed; the store is caller-owned.
 */
#include "net/http_cache.h"

#include <stddef.h>
#include <stdint.h>

/* ---- small string helpers (no libc dependency) -------------------------- */

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* Case-insensitive equality of NUL-terminated a with NUL-terminated b. */
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

static void s_copy(char *dst, const char *src, size_t cap) {
  size_t i = 0;
  if (cap == 0) return;
  while (i < cap - 1 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/* Find response header `name` (case-insensitive); returns its value or NULL. */
static const char *resp_header(const struct http_response *resp,
                               const char *name) {
  uint32_t i;
  if (!resp) return NULL;
  for (i = 0; i < resp->header_count && i < HTTP_MAX_HEADERS; i++) {
    if (ci_eq(resp->headers[i].name, name)) return resp->headers[i].value;
  }
  return NULL;
}

/* Find request header `name` (case-insensitive); returns its value or NULL. */
static const char *req_header(const struct http_request *req,
                              const char *name) {
  uint32_t i;
  if (!req) return NULL;
  for (i = 0; i < req->header_count && i < HTTP_MAX_HEADERS; i++) {
    if (ci_eq(req->headers[i].name, name)) return req->headers[i].value;
  }
  return NULL;
}

/* Parse a leading run of decimal digits as a non-negative long; -1 if none. */
static long parse_uint(const char *s) {
  long v = 0;
  int n = 0;
  if (!s) return -1;
  while (*s == ' ') s++;
  while (is_digit(*s)) {
    v = v * 10 + (*s - '0');
    if (v > 0x7FFFFFFFL) v = 0x7FFFFFFFL; /* clamp, avoid overflow */
    s++;
    n++;
  }
  return n ? v : -1;
}

/* ---- HTTP IMF-fixdate parser -------------------------------------------- */

static long days_from_civil(long y, long m, long d) {
  /* Howard Hinnant's days-from-civil (proleptic Gregorian, epoch 1970-01-01). */
  long era, doy, doe, yoe;
  unsigned mm = (unsigned)m;
  y -= (mm <= 2);
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int month_index(const char *p) {
  static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int i;
  for (i = 0; i < 12; i++) {
    const char *m = mon + i * 3;
    if (lc(p[0]) == lc(m[0]) && lc(p[1]) == lc(m[1]) && lc(p[2]) == lc(m[2]))
      return i + 1;
  }
  return 0;
}

static int two_digits(const char *p, int *out) {
  if (!is_digit(p[0]) || !is_digit(p[1])) return 0;
  *out = (p[0] - '0') * 10 + (p[1] - '0');
  return 1;
}

long http_cache_parse_date(const char *s) {
  const char *p;
  const char *q;
  const char *comma = NULL;
  int day = 0, n = 0, month, year = 0, hh, mm, ss;
  long days;

  if (!s || !*s) return -1;
  for (q = s; *q && (q - s) < 12; ++q) {
    if (*q == ',') {
      comma = q;
      break;
    }
  }
  p = comma ? comma + 1 : s;
  while (*p == ' ') ++p;

  while (is_digit(*p)) {
    day = day * 10 + (*p - '0');
    ++p;
    ++n;
  }
  if (n < 1 || n > 2 || day < 1 || day > 31) return -1;
  while (*p == ' ') ++p;

  month = month_index(p);
  if (!month) return -1;
  p += 3;
  while (*p == ' ') ++p;

  n = 0;
  while (is_digit(*p)) {
    year = year * 10 + (*p - '0');
    ++p;
    ++n;
  }
  if (n != 4) return -1;
  while (*p == ' ') ++p;

  if (!two_digits(p, &hh) || p[2] != ':') return -1;
  p += 3;
  if (!two_digits(p, &mm) || p[2] != ':') return -1;
  p += 3;
  if (!two_digits(p, &ss)) return -1;
  if (hh > 23 || mm > 59 || ss > 60) return -1;

  days = days_from_civil((long)year, (long)month, (long)day);
  return days * 86400L + (long)hh * 3600L + (long)mm * 60L + (long)ss;
}

/* ---- Cache-Control directive scan --------------------------------------- */

struct cc_flags {
  int no_store;
  int no_cache;
  int has_max_age;
  long max_age;
};

/* Match directive `name` at the start of token `t` (case-insensitive); returns
 * a pointer just past it (to '=' or end) if matched, else NULL. */
static const char *cc_token_is(const char *t, const char *name) {
  while (*name) {
    if (lc(*t) != lc(*name)) return NULL;
    t++;
    name++;
  }
  return t; /* points at '=' or ',' or ' ' or '\0' */
}

static void cc_scan(const char *cc, struct cc_flags *f) {
  const char *p = cc;
  f->no_store = f->no_cache = f->has_max_age = 0;
  f->max_age = 0;
  if (!cc) return;
  while (*p) {
    const char *rest;
    while (*p == ' ' || *p == ',') p++;
    if (!*p) break;
    if ((rest = cc_token_is(p, "no-store")) && (*rest == '\0' || *rest == ',' ||
                                                *rest == ' ')) {
      f->no_store = 1;
    } else if ((rest = cc_token_is(p, "no-cache")) &&
               (*rest == '\0' || *rest == ',' || *rest == ' ')) {
      f->no_cache = 1;
    } else if ((rest = cc_token_is(p, "max-age")) && *rest == '=') {
      long v = parse_uint(rest + 1);
      if (v >= 0) {
        f->has_max_age = 1;
        f->max_age = v;
      }
    }
    /* advance to next comma */
    while (*p && *p != ',') p++;
  }
}

/* ---- cache key ----------------------------------------------------------- */

static void key_append(char *buf, size_t cap, size_t *len, const char *s) {
  while (*s && *len < cap - 1) buf[(*len)++] = *s++;
  buf[*len] = '\0';
}

static void build_key(const struct http_request *req, char *buf, size_t cap) {
  size_t len = 0;
  char portbuf[8];
  uint16_t defport;
  buf[0] = '\0';
  if (cap == 0) return;
  switch (req->method) {
    case HTTP_GET: key_append(buf, cap, &len, "GET "); break;
    case HTTP_HEAD: key_append(buf, cap, &len, "HEAD "); break;
    default: key_append(buf, cap, &len, "OTHER "); break;
  }
  key_append(buf, cap, &len, req->use_tls ? "https://" : "http://");
  key_append(buf, cap, &len, req->host);
  defport = req->use_tls ? 443u : 80u;
  if (req->port != 0u && req->port != defport) {
    int i = 0;
    uint16_t v = req->port;
    char tmp[8];
    int j = 0;
    if (v == 0) tmp[j++] = '0';
    while (v > 0 && j < 6) {
      tmp[j++] = (char)('0' + (v % 10u));
      v /= 10u;
    }
    portbuf[i++] = ':';
    while (j > 0) portbuf[i++] = tmp[--j];
    portbuf[i] = '\0';
    key_append(buf, cap, &len, portbuf);
  }
  key_append(buf, cap, &len, req->path);
}

/* ---- public API ---------------------------------------------------------- */

void http_cache_init(struct http_cache *c) {
  uint32_t i;
  if (!c) return;
  for (i = 0; i < HTTP_CACHE_MAX_ENTRIES; i++) {
    c->entries[i].valid = 0;
    c->entries[i].body_len = 0;
    c->entries[i].lru = 0;
    c->entries[i].key[0] = '\0';
  }
  c->clock = 0;
  c->hits = c->misses = c->stores = c->evictions = c->revalidations = 0;
}

static int status_is_cacheable(int code) {
  return code == 200 || code == 203 || code == 301 || code == 308 ||
         code == 404;
}

int http_cache_is_cacheable(const struct http_request *req,
                            const struct http_response *resp) {
  struct cc_flags rcc, qcc;
  const char *cc;
  if (!req || !resp) return 0;
  if (req->method != HTTP_GET) return 0;
  if (!status_is_cacheable(resp->status_code)) return 0;

  cc_scan(resp_header(resp, "Cache-Control"), &rcc);
  if (rcc.no_store) return 0;
  cc_scan(req_header(req, "Cache-Control"), &qcc);
  if (qcc.no_store) return 0;

  /* Conservative: do not cache variant-bearing responses. */
  cc = resp_header(resp, "Vary");
  if (cc && cc[0] != '\0') return 0;

  /* Need a freshness signal or a validator to be worth storing. */
  if (rcc.has_max_age) return 1;
  if (resp_header(resp, "Expires")) return 1;
  if (resp_header(resp, "ETag")) return 1;
  if (resp_header(resp, "Last-Modified")) return 1;
  return 0;
}

long http_cache_freshness_lifetime(const struct http_response *resp) {
  struct cc_flags rcc;
  const char *expires, *date;
  if (!resp) return 0;
  cc_scan(resp_header(resp, "Cache-Control"), &rcc);
  if (rcc.has_max_age) return rcc.max_age;
  expires = resp_header(resp, "Expires");
  date = resp_header(resp, "Date");
  if (expires && date) {
    long e = http_cache_parse_date(expires);
    long d = http_cache_parse_date(date);
    if (e >= 0 && d >= 0 && e >= d) return e - d;
  }
  return 0;
}

long http_cache_entry_age(const struct http_cache_entry *e, long now) {
  long apparent, corrected, resident;
  if (!e) return 0;
  apparent = e->response_time - e->date_value;
  if (apparent < 0) apparent = 0;
  corrected = e->age_value + apparent;
  resident = now - e->response_time;
  if (resident < 0) resident = 0;
  return corrected + resident;
}

static int find_slot(struct http_cache *c, const char *key) {
  uint32_t i;
  int free_idx = -1, lru_idx = 0;
  uint64_t lru_min = (uint64_t)-1;
  for (i = 0; i < HTTP_CACHE_MAX_ENTRIES; i++) {
    if (c->entries[i].valid && ci_eq(c->entries[i].key, key)) return (int)i;
    if (!c->entries[i].valid && free_idx < 0) free_idx = (int)i;
    if (c->entries[i].lru <= lru_min) {
      lru_min = c->entries[i].lru;
      lru_idx = (int)i;
    }
  }
  if (free_idx >= 0) return free_idx;
  c->evictions++;
  return lru_idx;
}

static void fill_meta(struct http_cache_entry *e,
                      const struct http_response *resp, long now) {
  struct cc_flags rcc;
  const char *date, *age, *etag, *lm;
  cc_scan(resp_header(resp, "Cache-Control"), &rcc);
  e->response_time = now;
  date = resp_header(resp, "Date");
  if (date) {
    long d = http_cache_parse_date(date);
    e->date_value = (d >= 0) ? d : now;
  } else {
    e->date_value = now;
  }
  age = resp_header(resp, "Age");
  {
    long a = parse_uint(age);
    e->age_value = (a >= 0) ? a : 0;
  }
  e->freshness_lifetime = http_cache_freshness_lifetime(resp);
  e->no_cache = rcc.no_cache;
  etag = resp_header(resp, "ETag");
  e->etag[0] = '\0';
  if (etag) s_copy(e->etag, etag, HTTP_CACHE_VALIDATOR_MAX);
  lm = resp_header(resp, "Last-Modified");
  e->last_modified[0] = '\0';
  if (lm) s_copy(e->last_modified, lm, HTTP_CACHE_VALIDATOR_MAX);
}

int http_cache_store(struct http_cache *c, const struct http_request *req,
                     const struct http_response *resp, long now) {
  char key[HTTP_CACHE_KEY_MAX];
  int idx;
  struct http_cache_entry *e;
  size_t i;
  if (!c || !req || !resp) return 0;
  if (!http_cache_is_cacheable(req, resp)) return 0;
  if (resp->body_len > HTTP_CACHE_BODY_MAX) return 0; /* too big -> skip */

  build_key(req, key, sizeof(key));
  idx = find_slot(c, key);
  e = &c->entries[idx];

  s_copy(e->key, key, HTTP_CACHE_KEY_MAX);
  e->status_code = resp->status_code;
  fill_meta(e, resp, now);
  e->body_len = resp->body_len;
  for (i = 0; i < resp->body_len && i < HTTP_CACHE_BODY_MAX; i++)
    e->body[i] = resp->body[i];
  e->valid = 1;
  e->lru = ++c->clock;
  c->stores++;
  return 1;
}

enum http_cache_status http_cache_lookup(struct http_cache *c,
                                          const struct http_request *req,
                                          long now,
                                          struct http_cache_entry **out) {
  char key[HTTP_CACHE_KEY_MAX];
  uint32_t i;
  if (out) *out = NULL;
  if (!c || !req) return HTTP_CACHE_MISS;
  build_key(req, key, sizeof(key));
  for (i = 0; i < HTTP_CACHE_MAX_ENTRIES; i++) {
    struct http_cache_entry *e = &c->entries[i];
    if (!e->valid || !ci_eq(e->key, key)) continue;
    e->lru = ++c->clock;
    if (out) *out = e;
    if (!e->no_cache && http_cache_entry_age(e, now) < e->freshness_lifetime) {
      c->hits++;
      return HTTP_CACHE_FRESH;
    }
    c->revalidations++;
    return HTTP_CACHE_STALE;
  }
  c->misses++;
  return HTTP_CACHE_MISS;
}

static int add_header(struct http_request *req, const char *name,
                      const char *value) {
  uint32_t n = req->header_count;
  if (n >= HTTP_MAX_HEADERS) return 0;
  s_copy(req->headers[n].name, name, sizeof(req->headers[n].name));
  s_copy(req->headers[n].value, value, sizeof(req->headers[n].value));
  req->header_count = n + 1;
  return 1;
}

int http_cache_add_conditional_headers(const struct http_cache_entry *e,
                                        struct http_request *req) {
  int added = 0;
  if (!e || !req) return 0;
  if (e->etag[0] != '\0') added += add_header(req, "If-None-Match", e->etag);
  if (e->last_modified[0] != '\0')
    added += add_header(req, "If-Modified-Since", e->last_modified);
  return added;
}

void http_cache_refresh_on_304(struct http_cache *c, struct http_cache_entry *e,
                               const struct http_response *resp_304, long now) {
  const char *etag, *lm;
  if (!e || !resp_304) return;
  fill_meta(e, resp_304, now); /* re-evaluate Date/Age/freshness/no-cache */
  /* A 304 may carry refreshed validators; keep the old one if absent. */
  etag = resp_header(resp_304, "ETag");
  if (etag) s_copy(e->etag, etag, HTTP_CACHE_VALIDATOR_MAX);
  lm = resp_header(resp_304, "Last-Modified");
  if (lm) s_copy(e->last_modified, lm, HTTP_CACHE_VALIDATOR_MAX);
  e->lru = c ? ++c->clock : e->lru;
}
