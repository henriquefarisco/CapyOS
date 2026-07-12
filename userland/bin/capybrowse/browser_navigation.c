#include "browser_navigation.h"

/* Freestanding bounded helpers. No host libc dependency is required. */
static size_t bn_len(const char *s) {
  size_t n = 0u;
  if (!s) return 0u;
  while (s[n]) n++;
  return n;
}

static int bn_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static char bn_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int bn_prefix_ci(const char *s, size_t len, const char *prefix) {
  size_t i = 0u;
  while (prefix[i]) {
    if (i >= len || bn_lower(s[i]) != bn_lower(prefix[i])) return 0;
    i++;
  }
  return 1;
}

static int bn_equal(const char *a, const char *b) {
  size_t i = 0u;
  if (!a || !b) return 0;
  while (a[i] && b[i] && a[i] == b[i]) i++;
  return a[i] == '\0' && b[i] == '\0';
}

static int bn_copy(char *dst, size_t cap, const char *src) {
  size_t i = 0u;
  if (!dst || cap == 0u || !src) return 0;
  while (src[i]) {
    if (i + 1u >= cap) {
      dst[0] = '\0';
      return 0;
    }
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return 1;
}

static int bn_append_char(char *out, size_t cap, size_t *pos, char c) {
  if (!out || !pos || *pos + 1u >= cap) return 0;
  out[(*pos)++] = c;
  return 1;
}

static int bn_append_range(char *out, size_t cap, size_t *pos, const char *s,
                           size_t len) {
  size_t i;
  for (i = 0u; i < len; ++i) {
    if (!bn_append_char(out, cap, pos, s[i])) return 0;
  }
  return 1;
}

static int bn_valid_host(const char *s, size_t len, size_t *host_len_out) {
  size_t host_len = len;
  size_t i;
  size_t label_start = 0u;
  unsigned long port = 0ul;
  int colon_seen = 0;

  if (!s || len == 0u) return 0;
  for (i = 0u; i < len; ++i) {
    char c = s[i];
    if (c == ':') {
      if (colon_seen || i == 0u || i + 1u >= len) return 0; /* no IPv6 yet */
      colon_seen = 1;
      host_len = i;
      continue;
    }
    if (colon_seen) {
      if (c < '0' || c > '9') return 0;
      port = port * 10ul + (unsigned long)(c - '0');
      if (port > 65535ul) return 0;
      continue;
    }
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '.'))
      return 0;
  }
  if (colon_seen && port == 0ul) return 0;
  if (host_len == 0u || s[0] == '.' || s[host_len - 1u] == '.') return 0;
  for (i = 0u; i <= host_len; ++i) {
    if (i == host_len || s[i] == '.') {
      size_t label_len = i - label_start;
      if (label_len == 0u || label_len > 63u || s[label_start] == '-' ||
          s[i - 1u] == '-')
        return 0;
      label_start = i + 1u;
    }
  }
  if (host_len_out) *host_len_out = host_len;
  return 1;
}

/* Append a normalized absolute path and its optional query. `raw` starts with
 * '/' or '?'. Dot segments and duplicate separators are collapsed. */
static int bn_append_path(char *out, size_t cap, size_t *pos, const char *raw,
                          size_t len) {
  size_t q = 0u;
  size_t i;
  size_t path_base;
  int trailing_slash = 0;

  while (q < len && raw[q] != '?') q++;
  path_base = *pos;
  if (!bn_append_char(out, cap, pos, '/')) return 0;

  i = (q > 0u && raw[0] == '/') ? 1u : 0u;
  while (i < q) {
    size_t start;
    size_t seg_len;
    while (i < q && raw[i] == '/') i++;
    start = i;
    while (i < q && raw[i] != '/') {
      unsigned char c = (unsigned char)raw[i];
      if (c <= 0x20u || c == 0x7fu || c == '\\' || c == '#') return 0;
      i++;
    }
    seg_len = i - start;
    if (seg_len == 0u) continue;
    if (seg_len == 1u && raw[start] == '.') continue;
    if (seg_len == 2u && raw[start] == '.' && raw[start + 1u] == '.') {
      if (*pos > path_base + 1u) {
        (*pos)--;
        while (*pos > path_base + 1u && out[*pos - 1u] != '/') (*pos)--;
      }
      continue;
    }
    if (*pos > path_base + 1u && out[*pos - 1u] != '/' &&
        !bn_append_char(out, cap, pos, '/'))
      return 0;
    if (!bn_append_range(out, cap, pos, raw + start, seg_len)) return 0;
  }
  trailing_slash = q > 1u && raw[q - 1u] == '/';
  if (trailing_slash && *pos > path_base + 1u && out[*pos - 1u] != '/' &&
      !bn_append_char(out, cap, pos, '/'))
    return 0;

  if (q < len) {
    for (i = q; i < len; ++i) {
      unsigned char c = (unsigned char)raw[i];
      if (c <= 0x20u || c == 0x7fu || c == '\\' || c == '#') return 0;
    }
    if (!bn_append_range(out, cap, pos, raw + q, len - q)) return 0;
  }
  return 1;
}

/* Canonicalize a raw absolute http(s) URL. */
static int bn_absolute(const char *raw, size_t raw_len, char *out,
                       size_t out_cap) {
  size_t scheme_len;
  size_t authority_start;
  size_t authority_end;
  size_t fragment;
  size_t host_len = 0u;
  size_t pos = 0u;
  size_t i;
  int secure;

  if (!out || out_cap == 0u) return -1;
  out[0] = '\0';
  if (!raw || raw_len == 0u) return -1;
  if (bn_prefix_ci(raw, raw_len, "https://")) {
    secure = 1;
    scheme_len = 8u;
  } else if (bn_prefix_ci(raw, raw_len, "http://")) {
    secure = 0;
    scheme_len = 7u;
  } else {
    return -1;
  }
  authority_start = scheme_len;
  fragment = raw_len;
  for (i = authority_start; i < raw_len; ++i) {
    unsigned char c = (unsigned char)raw[i];
    if (c == '#') {
      fragment = i;
      break;
    }
    if (c <= 0x20u || c == 0x7fu || c == '\\' || c == '@') return -1;
  }
  authority_end = authority_start;
  while (authority_end < fragment && raw[authority_end] != '/' &&
         raw[authority_end] != '?')
    authority_end++;
  if (!bn_valid_host(raw + authority_start, authority_end - authority_start,
                     &host_len))
    return -1;

  if (!bn_append_range(out, out_cap, &pos,
                       secure ? "https://" : "http://", scheme_len))
    goto fail;
  for (i = authority_start; i < authority_end; ++i) {
    if (!bn_append_char(out, out_cap, &pos, bn_lower(raw[i]))) goto fail;
  }
  (void)host_len;
  if (authority_end == fragment) {
    if (!bn_append_char(out, out_cap, &pos, '/')) goto fail;
  } else if (!bn_append_path(out, out_cap, &pos, raw + authority_end,
                             fragment - authority_end)) {
    goto fail;
  }
  out[pos] = '\0';
  return (int)pos;

fail:
  out[0] = '\0';
  return -1;
}

int browser_navigation_normalize_input(const char *input, char *out,
                                       size_t out_cap) {
  char raw[BROWSER_NAVIGATION_URL_MAX];
  size_t start = 0u;
  size_t end;
  size_t len;
  size_t pos = 0u;
  size_t colon = (size_t)-1;
  size_t delimiter = (size_t)-1;
  size_t i;

  if (!out || out_cap == 0u) return -1;
  out[0] = '\0';
  if (!input) return -1;
  end = bn_len(input);
  while (start < end && bn_is_space(input[start])) start++;
  while (end > start && bn_is_space(input[end - 1u])) end--;
  len = end - start;
  if (len == 0u) return -1;
  for (i = 0u; i < len; ++i) {
    if (input[start + i] == ':' && colon == (size_t)-1) colon = i;
    if ((input[start + i] == '/' || input[start + i] == '?' ||
         input[start + i] == '#') &&
        delimiter == (size_t)-1)
      delimiter = i;
  }

  if (bn_prefix_ci(input + start, len, "http://") ||
      bn_prefix_ci(input + start, len, "https://")) {
    if (!bn_append_range(raw, sizeof(raw), &pos, input + start, len)) return -1;
  } else if (len >= 2u && input[start] == '/' && input[start + 1u] == '/') {
    if (!bn_append_range(raw, sizeof(raw), &pos, "https:", 6u) ||
        !bn_append_range(raw, sizeof(raw), &pos, input + start, len))
      return -1;
  } else {
    /* A colon followed by a decimal port is valid in a bare authority
     * (`localhost:8080/`). Any other colon before path/query is an unsupported
     * explicit scheme (ftp:, javascript:, ...), not a hostname. */
    if (colon != (size_t)-1 &&
        (delimiter == (size_t)-1 || colon < delimiter)) {
      size_t port_end = delimiter == (size_t)-1 ? len : delimiter;
      size_t p = colon + 1u;
      if (p >= port_end) return -1;
      while (p < port_end && input[start + p] >= '0' &&
             input[start + p] <= '9')
        p++;
      if (p != port_end) return -1;
    }
    if (!bn_append_range(raw, sizeof(raw), &pos, "https://", 8u) ||
        !bn_append_range(raw, sizeof(raw), &pos, input + start, len))
      return -1;
  }
  raw[pos] = '\0';
  return bn_absolute(raw, pos, out, out_cap);
}

static int bn_url_secure(const char *url) {
  size_t len = bn_len(url);
  return bn_prefix_ci(url, len, "https://");
}

static size_t bn_authority_end(const char *url) {
  size_t i = bn_prefix_ci(url, bn_len(url), "https://") ? 8u : 7u;
  while (url[i] && url[i] != '/' && url[i] != '?') i++;
  return i;
}

int browser_navigation_resolve_redirect(const char *base_url,
                                        const char *location, char *out,
                                        size_t out_cap) {
  char base[BROWSER_NAVIGATION_URL_MAX];
  char raw[BROWSER_NAVIGATION_URL_MAX];
  size_t ls = 0u, le, llen;
  size_t pos = 0u;
  size_t ae;
  size_t path_end;
  size_t i;

  if (!out || out_cap == 0u) return -1;
  out[0] = '\0';
  if (!base_url || !location ||
      browser_navigation_normalize_input(base_url, base, sizeof(base)) < 0)
    return -1;
  le = bn_len(location);
  while (ls < le && bn_is_space(location[ls])) ls++;
  while (le > ls && bn_is_space(location[le - 1u])) le--;
  llen = le - ls;
  if (llen == 0u) return -1;

  if (bn_prefix_ci(location + ls, llen, "http://") ||
      bn_prefix_ci(location + ls, llen, "https://"))
    return bn_absolute(location + ls, llen, out, out_cap);
  /* Reject another explicit scheme instead of treating it as a relative path. */
  for (i = 0u; i < llen && location[ls + i] != '/'; ++i) {
    if (location[ls + i] == ':') return -1;
  }

  ae = bn_authority_end(base);
  if (llen >= 2u && location[ls] == '/' && location[ls + 1u] == '/') {
    size_t scheme_len = bn_url_secure(base) ? 6u : 5u; /* "https:" / "http:" */
    if (!bn_append_range(raw, sizeof(raw), &pos, base, scheme_len) ||
        !bn_append_range(raw, sizeof(raw), &pos, location + ls, llen))
      return -1;
  } else if (location[ls] == '/') {
    if (!bn_append_range(raw, sizeof(raw), &pos, base, ae) ||
        !bn_append_range(raw, sizeof(raw), &pos, location + ls, llen))
      return -1;
  } else {
    path_end = ae;
    while (base[path_end] && base[path_end] != '?') path_end++;
    if (location[ls] == '?') {
      if (!bn_append_range(raw, sizeof(raw), &pos, base, path_end) ||
          !bn_append_range(raw, sizeof(raw), &pos, location + ls, llen))
        return -1;
    } else if (location[ls] == '#') {
      /* The canonical base already has no fragment. Preserve its query. */
      if (!bn_append_range(raw, sizeof(raw), &pos, base, bn_len(base))) return -1;
    } else {
      size_t slash_pos = path_end;
      while (slash_pos > ae && base[slash_pos - 1u] != '/') slash_pos--;
      if (!bn_append_range(raw, sizeof(raw), &pos, base, slash_pos) ||
          !bn_append_range(raw, sizeof(raw), &pos, location + ls, llen))
        return -1;
    }
  }
  raw[pos] = '\0';
  return bn_absolute(raw, pos, out, out_cap);
}

static int bn_is_redirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

static void bn_copy_content_type(struct browser_navigation *nav,
                                 const char *content_type) {
  size_t i = 0u;
  nav->content_type[0] = '\0';
  if (!content_type) return;
  while (content_type[i] && i + 1u < sizeof(nav->content_type)) {
    unsigned char c = (unsigned char)content_type[i];
    if (c < 0x20u || c == 0x7fu) break;
    nav->content_type[i] = content_type[i];
    i++;
  }
  nav->content_type[i] = '\0';
}

static void bn_commit_document(struct browser_navigation *nav, const char *url,
                               const struct browser_navigation_response *resp) {
  (void)bn_copy(nav->current_url, sizeof(nav->current_url), url);
  nav->body = resp->body;
  nav->body_len = resp->body_len;
  bn_copy_content_type(nav, resp->content_type);
  nav->state = BROWSER_NAVIGATION_READY;
  nav->document_generation++;
}

/* Load one URL transactionally. On success response/url are committed to the
 * current document, but history movement is left to the caller. */
static int bn_load(struct browser_navigation *nav, const char *start_url,
                   int force_reload, browser_navigation_fetch_fn fetch,
                   void *fetch_ctx, char *final_url) {
  char next[BROWSER_NAVIGATION_URL_MAX];
  unsigned int visited_count = 0u;
  unsigned int redirects = 0u;

  if (!nav || !start_url || !fetch || !final_url) return -1;
  if (!bn_copy(next, sizeof(next), start_url)) {
    nav->state = BROWSER_NAVIGATION_INVALID_URL;
    return -1;
  }
  nav->state = BROWSER_NAVIGATION_LOADING;
  nav->last_http_status = 0;
  nav->last_fetch_error = 0;
  nav->last_redirect_count = 0u;

  for (;;) {
    struct browser_navigation_response resp;
    unsigned int i;
    int rc;
    resp.status_code = 0;
    resp.body = NULL;
    resp.body_len = 0u;
    resp.content_type = NULL;
    resp.location = NULL;
    resp.truncated = 0;

    for (i = 0u; i < visited_count; ++i) {
      if (bn_equal(nav->redirect_visited[i], next)) {
        nav->state = BROWSER_NAVIGATION_REDIRECT_LOOP;
        return -1;
      }
    }
    if (visited_count >= BROWSER_NAVIGATION_REDIRECT_MAX + 1u) {
      nav->state = BROWSER_NAVIGATION_REDIRECT_LIMIT;
      return -1;
    }
    (void)bn_copy(nav->redirect_visited[visited_count++],
                  sizeof(nav->redirect_visited[0]), next);
    (void)bn_copy(nav->attempted_url, sizeof(nav->attempted_url), next);

    rc = fetch(fetch_ctx, next, force_reload, &resp);
    if (rc != 0) {
      nav->last_fetch_error = rc;
      nav->state = BROWSER_NAVIGATION_FETCH_ERROR;
      return -1;
    }
    nav->last_http_status = resp.status_code;
    if (resp.truncated) {
      nav->state = BROWSER_NAVIGATION_TOO_LARGE;
      return -1;
    }
    if (bn_is_redirect(resp.status_code)) {
      char resolved[BROWSER_NAVIGATION_URL_MAX];
      if (redirects >= BROWSER_NAVIGATION_REDIRECT_MAX) {
        nav->last_redirect_count = redirects;
        nav->state = BROWSER_NAVIGATION_REDIRECT_LIMIT;
        return -1;
      }
      if (!resp.location ||
          browser_navigation_resolve_redirect(next, resp.location, resolved,
                                              sizeof(resolved)) < 0) {
        nav->state = BROWSER_NAVIGATION_REDIRECT_INVALID;
        return -1;
      }
      if (bn_url_secure(next) && !bn_url_secure(resolved)) {
        nav->state = BROWSER_NAVIGATION_DOWNGRADE_BLOCKED;
        return -1;
      }
      if (!bn_copy(next, sizeof(next), resolved)) {
        nav->state = BROWSER_NAVIGATION_REDIRECT_INVALID;
        return -1;
      }
      redirects++;
      nav->last_redirect_count = redirects;
      continue;
    }
    if (resp.status_code < 200 || resp.status_code >= 300) {
      nav->state = BROWSER_NAVIGATION_HTTP_ERROR;
      return -1;
    }
    if (!bn_copy(final_url, BROWSER_NAVIGATION_URL_MAX, next)) {
      nav->state = BROWSER_NAVIGATION_INVALID_URL;
      return -1;
    }
    bn_commit_document(nav, next, &resp);
    nav->last_redirect_count = redirects;
    return 0;
  }
}

void browser_navigation_init(struct browser_navigation *nav) {
  size_t i;
  if (!nav) return;
  nav->state = BROWSER_NAVIGATION_IDLE;
  nav->current_url[0] = '\0';
  nav->attempted_url[0] = '\0';
  nav->content_type[0] = '\0';
  for (i = 0u; i < BROWSER_NAVIGATION_HISTORY_MAX; ++i)
    nav->history[i][0] = '\0';
  for (i = 0u; i < BROWSER_NAVIGATION_REDIRECT_MAX + 1u; ++i)
    nav->redirect_visited[i][0] = '\0';
  nav->history_count = 0u;
  nav->history_index = 0u;
  nav->body = NULL;
  nav->body_len = 0u;
  nav->last_http_status = 0;
  nav->last_fetch_error = 0;
  nav->last_redirect_count = 0u;
  nav->document_generation = 0u;
}

static void bn_history_push(struct browser_navigation *nav, const char *url) {
  size_t i;
  if (nav->history_count > 0u &&
      bn_equal(nav->history[nav->history_index], url))
    return; /* same effective document: do not create a phantom back entry */
  if (nav->history_count > 0u)
    nav->history_count = nav->history_index + 1u; /* drop the forward branch */
  if (nav->history_count == BROWSER_NAVIGATION_HISTORY_MAX) {
    for (i = 1u; i < nav->history_count; ++i)
      (void)bn_copy(nav->history[i - 1u], sizeof(nav->history[0]),
                    nav->history[i]);
    nav->history_count--;
  }
  (void)bn_copy(nav->history[nav->history_count], sizeof(nav->history[0]), url);
  nav->history_index = nav->history_count;
  nav->history_count++;
}

int browser_navigation_navigate(struct browser_navigation *nav,
                                const char *input,
                                browser_navigation_fetch_fn fetch,
                                void *fetch_ctx) {
  char normalized[BROWSER_NAVIGATION_URL_MAX];
  char final_url[BROWSER_NAVIGATION_URL_MAX];
  if (!nav || !fetch ||
      browser_navigation_normalize_input(input, normalized,
                                         sizeof(normalized)) < 0) {
    if (nav) nav->state = BROWSER_NAVIGATION_INVALID_URL;
    return -1;
  }
  if (bn_load(nav, normalized, 0, fetch, fetch_ctx, final_url) != 0) return -1;
  bn_history_push(nav, final_url);
  return 0;
}

static int bn_history_move(struct browser_navigation *nav, size_t target,
                           browser_navigation_fetch_fn fetch, void *fetch_ctx) {
  char final_url[BROWSER_NAVIGATION_URL_MAX];
  if (bn_load(nav, nav->history[target], 0, fetch, fetch_ctx, final_url) != 0)
    return -1;
  /* A redirect while revisiting history replaces that entry with its current
   * effective URL but does not append or destroy the surrounding history. */
  (void)bn_copy(nav->history[target], sizeof(nav->history[0]), final_url);
  nav->history_index = target;
  return 0;
}

int browser_navigation_back(struct browser_navigation *nav,
                            browser_navigation_fetch_fn fetch,
                            void *fetch_ctx) {
  if (!nav || !fetch || nav->history_count == 0u || nav->history_index == 0u) {
    if (nav) nav->state = BROWSER_NAVIGATION_NO_HISTORY;
    return -1;
  }
  return bn_history_move(nav, nav->history_index - 1u, fetch, fetch_ctx);
}

int browser_navigation_forward(struct browser_navigation *nav,
                               browser_navigation_fetch_fn fetch,
                               void *fetch_ctx) {
  if (!nav || !fetch || nav->history_count == 0u ||
      nav->history_index + 1u >= nav->history_count) {
    if (nav) nav->state = BROWSER_NAVIGATION_NO_HISTORY;
    return -1;
  }
  return bn_history_move(nav, nav->history_index + 1u, fetch, fetch_ctx);
}

int browser_navigation_reload(struct browser_navigation *nav,
                              browser_navigation_fetch_fn fetch,
                              void *fetch_ctx) {
  char final_url[BROWSER_NAVIGATION_URL_MAX];
  if (!nav || !fetch || nav->current_url[0] == '\0') {
    if (nav) nav->state = BROWSER_NAVIGATION_NO_HISTORY;
    return -1;
  }
  if (bn_load(nav, nav->current_url, 1, fetch, fetch_ctx, final_url) != 0)
    return -1;
  if (nav->history_count > 0u)
    (void)bn_copy(nav->history[nav->history_index], sizeof(nav->history[0]),
                  final_url);
  return 0;
}

int browser_navigation_can_back(const struct browser_navigation *nav) {
  return nav && nav->history_count > 0u && nav->history_index > 0u;
}

int browser_navigation_can_forward(const struct browser_navigation *nav) {
  return nav && nav->history_count > 0u &&
         nav->history_index + 1u < nav->history_count;
}

const char *browser_navigation_state_name(enum browser_navigation_state state) {
  switch (state) {
    case BROWSER_NAVIGATION_IDLE: return "IDLE";
    case BROWSER_NAVIGATION_LOADING: return "LOADING";
    case BROWSER_NAVIGATION_READY: return "READY";
    case BROWSER_NAVIGATION_INVALID_URL: return "INVALID_URL";
    case BROWSER_NAVIGATION_FETCH_ERROR: return "FETCH_ERROR";
    case BROWSER_NAVIGATION_HTTP_ERROR: return "HTTP_ERROR";
    case BROWSER_NAVIGATION_TOO_LARGE: return "TOO_LARGE";
    case BROWSER_NAVIGATION_UNSUPPORTED_CONTENT: return "UNSUPPORTED_CONTENT";
    case BROWSER_NAVIGATION_RENDER_ERROR: return "RENDER_ERROR";
    case BROWSER_NAVIGATION_REDIRECT_INVALID: return "REDIRECT_INVALID";
    case BROWSER_NAVIGATION_REDIRECT_LOOP: return "REDIRECT_LOOP";
    case BROWSER_NAVIGATION_REDIRECT_LIMIT: return "REDIRECT_LIMIT";
    case BROWSER_NAVIGATION_DOWNGRADE_BLOCKED: return "DOWNGRADE_BLOCKED";
    case BROWSER_NAVIGATION_NO_HISTORY: return "NO_HISTORY";
  }
  return "UNKNOWN";
}
