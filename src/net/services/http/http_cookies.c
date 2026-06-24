/*
 * src/net/services/http/http_cookies.c — bounded per-domain cookie jar
 * (RFC 6265 subset) for the CapyOS browser/fetch path (Etapa 7 / Slice 7.5).
 * See include/net/http_cookies.h for the contract and scope.
 *
 * Self-contained string handling; reuses the shared HTTP IMF-fixdate parser
 * (http_cache_parse_date) for cookie Expires. Pure, deterministic (clock
 * injected), fail-closed; the jar is caller-owned.
 */
#include "net/http_cookies.h"

#include "net/http_cache.h" /* http_cache_parse_date */

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

/* Case-insensitive equality of a NUL-terminated `a` with a [b,b+n) range. */
static int ci_eq_range(const char *a, const char *b, size_t n) {
  size_t i = 0;
  for (i = 0; i < n; i++) {
    if (a[i] == '\0' || lc(a[i]) != lc(b[i])) return 0;
  }
  return a[n] == '\0';
}

static size_t s_len(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}

/* Copy [src,src+n) into dst (cap incl. NUL), lower-casing when `lower`. */
static void copy_range(char *dst, size_t cap, const char *src, size_t n,
                       int lower) {
  size_t i = 0;
  if (cap == 0) return;
  while (i < n && i < cap - 1) {
    dst[i] = lower ? lc(src[i]) : src[i];
    i++;
  }
  dst[i] = '\0';
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

static int is_space(char c) { return c == ' ' || c == '\t'; }

/* Trim leading/trailing spaces of [*=start, *=end); end is exclusive. */
static void trim(const char **start, const char **end) {
  const char *s = *start;
  const char *e = *end;
  while (s < e && is_space(*s)) s++;
  while (e > s && is_space(e[-1])) e--;
  *start = s;
  *end = e;
}

static long parse_long_signed(const char *s, const char *end) {
  long v = 0;
  int neg = 0;
  int any = 0;
  if (s < end && (*s == '-' || *s == '+')) {
    neg = (*s == '-');
    s++;
  }
  while (s < end && *s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    if (v > 0x7FFFFFFFL) v = 0x7FFFFFFFL;
    s++;
    any = 1;
  }
  if (!any) return 0x7FFFFFFFL; /* sentinel: caller treats as "no value" via flag */
  return neg ? -v : v;
}

static int host_is_ip_literal(const char *host) {
  /* IPv4 dotted-quad (only digits and dots) or a bracketed IPv6 literal. */
  const char *p = host;
  int has_digit = 0, only_digits_dots = 1;
  if (*host == '[') return 1;
  for (; *p; p++) {
    if (*p >= '0' && *p <= '9')
      has_digit = 1;
    else if (*p != '.')
      only_digits_dots = 0;
  }
  return has_digit && only_digits_dots;
}

/* ---- matching ------------------------------------------------------------ */

int http_cookie_domain_match(const char *host, const char *domain) {
  size_t lh, ld;
  if (!host || !domain || !*host || !*domain) return 0;
  if (ci_eq(host, domain)) return 1;
  if (host_is_ip_literal(host)) return 0; /* IP: only exact */
  lh = s_len(host);
  ld = s_len(domain);
  if (lh <= ld) return 0;
  /* host must end with domain, with a '.' immediately before the suffix. */
  {
    size_t off = lh - ld;
    size_t i;
    if (host[off - 1] != '.') return 0;
    for (i = 0; i < ld; i++)
      if (lc(host[off + i]) != lc(domain[i])) return 0;
    return 1;
  }
}

int http_cookie_path_match(const char *req_path, const char *cookie_path) {
  size_t lr, lc_;
  if (!req_path || !cookie_path) return 0;
  if (req_path[0] == '\0') req_path = "/";
  if (cookie_path[0] == '\0') cookie_path = "/";
  lr = s_len(req_path);
  lc_ = s_len(cookie_path);
  {
    size_t i;
    for (i = 0; i < lr && i < lc_; i++)
      if (req_path[i] != cookie_path[i]) return 0;
  }
  if (lr < lc_) return 0;          /* cookie-path longer -> not a prefix */
  if (lr == lc_) return 1;         /* identical */
  /* cookie_path is a proper prefix of req_path: ok if it ends with '/' or the
   * next char in req_path is '/'. */
  if (cookie_path[lc_ - 1] == '/') return 1;
  return req_path[lc_] == '/';
}

/* ---- default path (RFC 6265 5.1.4) --------------------------------------- */

static void default_path(const char *req_path, char *out, size_t cap) {
  size_t i, last_slash = 0;
  int seen = 0;
  if (!req_path || req_path[0] != '/') {
    s_copy(out, "/", cap);
    return;
  }
  for (i = 0; req_path[i]; i++) {
    if (req_path[i] == '/') {
      last_slash = i;
      seen = 1;
    }
  }
  if (!seen || last_slash == 0) {
    s_copy(out, "/", cap);
    return;
  }
  copy_range(out, cap, req_path, last_slash, 0);
}

/* ---- Set-Cookie parsing -------------------------------------------------- */

int http_cookie_parse_set_cookie(const char *set_cookie, const char *req_host,
                                 const char *req_path, long now,
                                 struct http_cookie *out) {
  const char *p, *semi, *eq;
  const char *ns, *ne, *vs, *ve;
  int have_maxage = 0;

  if (!set_cookie || !req_host || !req_path || !out) return 0;
  /* zero */
  {
    size_t i;
    char *z = (char *)out;
    for (i = 0; i < sizeof(*out); i++) z[i] = 0;
  }

  /* first segment: name=value (up to the first ';'). */
  p = set_cookie;
  semi = p;
  while (*semi && *semi != ';') semi++;
  eq = p;
  while (eq < semi && *eq != '=') eq++;
  if (eq >= semi) return 0; /* no '=' -> malformed */
  ns = p;
  ne = eq;
  vs = eq + 1;
  ve = semi;
  trim(&ns, &ne);
  trim(&vs, &ve);
  if (ne <= ns) return 0; /* empty name */
  copy_range(out->name, sizeof(out->name), ns, (size_t)(ne - ns), 0);
  copy_range(out->value, sizeof(out->value), vs, (size_t)(ve - vs), 0);

  /* defaults */
  s_copy(out->domain, req_host, sizeof(out->domain));
  /* lower-case the default domain */
  {
    size_t i;
    for (i = 0; out->domain[i]; i++) out->domain[i] = lc(out->domain[i]);
  }
  out->host_only = 1;
  default_path(req_path, out->path, sizeof(out->path));
  out->expires = 0; /* session */
  out->secure = 0;
  out->http_only = 0;

  /* attributes */
  p = (*semi == ';') ? semi + 1 : semi;
  while (*p) {
    const char *as, *ae, *avs, *ave;
    const char *a_eq;
    /* one attribute up to the next ';' */
    semi = p;
    while (*semi && *semi != ';') semi++;
    a_eq = p;
    while (a_eq < semi && *a_eq != '=') a_eq++;
    as = p;
    ae = a_eq;
    trim(&as, &ae);
    if (a_eq < semi) {
      avs = a_eq + 1;
      ave = semi;
    } else {
      avs = semi; /* no value */
      ave = semi;
    }
    trim(&avs, &ave);

    if (ci_eq_range("Max-Age", as, (size_t)(ae - as))) {
      long v = parse_long_signed(avs, ave);
      if (!(avs == ave)) { /* had a value */
        have_maxage = 1;
        out->expires = (v <= 0) ? (now - 1) : (now + v); /* <=0 -> expired */
      }
    } else if (ci_eq_range("Expires", as, (size_t)(ae - as)) && !have_maxage) {
      char datebuf[64];
      copy_range(datebuf, sizeof(datebuf), avs, (size_t)(ave - avs), 0);
      {
        long d = http_cache_parse_date(datebuf);
        if (d >= 0) out->expires = d; /* else leave as session */
      }
    } else if (ci_eq_range("Domain", as, (size_t)(ae - as))) {
      /* strip a single leading '.' */
      if (avs < ave && *avs == '.') avs++;
      if (avs < ave) {
        copy_range(out->domain, sizeof(out->domain), avs,
                   (size_t)(ave - avs), 1 /* lower */);
        out->host_only = 0;
      }
    } else if (ci_eq_range("Path", as, (size_t)(ae - as))) {
      if (avs < ave && *avs == '/')
        copy_range(out->path, sizeof(out->path), avs, (size_t)(ave - avs), 0);
    } else if (ci_eq_range("Secure", as, (size_t)(ae - as))) {
      out->secure = 1;
    } else if (ci_eq_range("HttpOnly", as, (size_t)(ae - as))) {
      out->http_only = 1;
    }
    /* SameSite and unknown attributes are ignored. */

    if (*semi == '\0') break;
    p = semi + 1;
  }

  /* RFC 6265 5.3: a Set-Cookie with an explicit Domain must domain-match the
   * request host, else the whole cookie is rejected (anti-injection). */
  if (!out->host_only) {
    if (out->domain[0] == '\0') return 0;
    if (!http_cookie_domain_match(req_host, out->domain)) return 0;
  }
  out->valid = 1;
  return 1;
}

/* ---- jar ----------------------------------------------------------------- */

void http_cookie_jar_init(struct http_cookie_jar *j) {
  uint32_t i;
  if (!j) return;
  for (i = 0; i < HTTP_COOKIE_MAX; i++) {
    j->cookies[i].valid = 0;
    j->cookies[i].lru = 0;
  }
  j->clock = 0;
  j->set_count = j->deleted = j->rejected = j->evictions = 0;
}

static int cookie_expired(const struct http_cookie *c, long now) {
  return c->expires != 0 && c->expires <= now;
}

/* Same (name, domain, path) identity per RFC 6265 5.3. */
static int same_identity(const struct http_cookie *a, const struct http_cookie *b) {
  size_t i;
  for (i = 0; a->name[i] || b->name[i]; i++)
    if (a->name[i] != b->name[i]) return 0;
  if (!ci_eq(a->domain, b->domain)) return 0;
  for (i = 0; a->path[i] || b->path[i]; i++)
    if (a->path[i] != b->path[i]) return 0;
  return 1;
}

static int find_identity(struct http_cookie_jar *j, const struct http_cookie *c) {
  uint32_t i;
  for (i = 0; i < HTTP_COOKIE_MAX; i++)
    if (j->cookies[i].valid && same_identity(&j->cookies[i], c)) return (int)i;
  return -1;
}

static int alloc_slot(struct http_cookie_jar *j) {
  uint32_t i;
  int lru_idx = 0;
  uint64_t lru_min = (uint64_t)-1;
  for (i = 0; i < HTTP_COOKIE_MAX; i++) {
    if (!j->cookies[i].valid) return (int)i;
    if (j->cookies[i].lru <= lru_min) {
      lru_min = j->cookies[i].lru;
      lru_idx = (int)i;
    }
  }
  j->evictions++;
  return lru_idx;
}

static void store_one(struct http_cookie_jar *j, const struct http_cookie *c,
                      long now) {
  int idx;
  if (cookie_expired(c, now)) {
    /* an expiring Set-Cookie deletes a matching stored cookie. */
    idx = find_identity(j, c);
    if (idx >= 0) {
      j->cookies[idx].valid = 0;
      j->deleted++;
    }
    return;
  }
  idx = find_identity(j, c);
  if (idx < 0) idx = alloc_slot(j);
  j->cookies[idx] = *c;
  j->cookies[idx].valid = 1;
  j->cookies[idx].lru = ++j->clock;
  j->set_count++;
}

static const char *resp_get_header_at(const struct http_response *resp,
                                      uint32_t *idx) {
  while (*idx < resp->header_count && *idx < HTTP_MAX_HEADERS) {
    uint32_t i = (*idx)++;
    if (ci_eq(resp->headers[i].name, "Set-Cookie")) return resp->headers[i].value;
  }
  return NULL;
}

int http_cookie_jar_set_from_response(struct http_cookie_jar *j,
                                      const char *req_host,
                                      const char *req_path,
                                      const struct http_response *resp,
                                      long now) {
  uint32_t idx = 0;
  const char *sc;
  int accepted = 0;
  if (!j || !req_host || !req_path || !resp) return 0;
  while ((sc = resp_get_header_at(resp, &idx)) != NULL) {
    struct http_cookie c;
    if (!http_cookie_parse_set_cookie(sc, req_host, req_path, now, &c)) {
      j->rejected++;
      continue;
    }
    {
      uint32_t before = j->set_count + j->deleted;
      store_one(j, &c, now);
      if (j->set_count + j->deleted > before) accepted++;
    }
  }
  return accepted;
}

static int cookie_matches_request(const struct http_cookie *c,
                                  const char *host, const char *path,
                                  int secure, long now) {
  if (!c->valid || cookie_expired(c, now)) return 0;
  if (c->secure && !secure) return 0;
  if (c->host_only) {
    if (!ci_eq(host, c->domain)) return 0;
  } else if (!http_cookie_domain_match(host, c->domain)) {
    return 0;
  }
  return http_cookie_path_match(path, c->path);
}

size_t http_cookie_jar_header(struct http_cookie_jar *j, const char *req_host,
                              const char *req_path, int secure, long now,
                              char *out, size_t cap) {
  int order[HTTP_COOKIE_MAX];
  int n = 0, i, k;
  size_t len = 0;
  if (out && cap > 0) out[0] = '\0';
  if (!j || !req_host || !req_path || !out || cap == 0) return 0;
  if (req_path[0] == '\0') req_path = "/";

  for (i = 0; i < (int)HTTP_COOKIE_MAX; i++)
    if (cookie_matches_request(&j->cookies[i], req_host, req_path, secure, now))
      order[n++] = i;

  /* RFC 6265 5.4: longer path first; stable for equal paths (selection sort). */
  for (i = 0; i < n; i++) {
    int best = i;
    for (k = i + 1; k < n; k++) {
      size_t lk = s_len(j->cookies[order[k]].path);
      size_t lb = s_len(j->cookies[order[best]].path);
      if (lk > lb) best = k;
    }
    if (best != i) {
      int t = order[i];
      order[i] = order[best];
      order[best] = t;
    }
  }

  for (i = 0; i < n; i++) {
    const struct http_cookie *c = &j->cookies[order[i]];
    size_t nn = s_len(c->name);
    size_t vv = s_len(c->value);
    size_t need = nn + 1 + vv + (len > 0 ? 2 : 0); /* "; " + name=value */
    if (len + need >= cap) break; /* don't partial-write a cookie */
    if (len > 0) {
      out[len++] = ';';
      out[len++] = ' ';
    }
    {
      size_t t;
      for (t = 0; t < nn; t++) out[len++] = c->name[t];
      out[len++] = '=';
      for (t = 0; t < vv; t++) out[len++] = c->value[t];
    }
  }
  out[len] = '\0';
  return len;
}

void http_cookie_jar_gc(struct http_cookie_jar *j, long now) {
  uint32_t i;
  if (!j) return;
  for (i = 0; i < HTTP_COOKIE_MAX; i++)
    if (j->cookies[i].valid && cookie_expired(&j->cookies[i], now))
      j->cookies[i].valid = 0;
}
