#include "internal/html_viewer_internal.h"

int hv_strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
  }
  return 0;
}

static char hv_tolower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

int hv_is_space(char ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\f';
}

static int hv_is_alnum(char ch) {
  return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9'));
}

int hv_streq_ci(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] || b[i]) {
    if (hv_tolower(a[i]) != hv_tolower(b[i])) return 0;
    i++;
  }
  return 1;
}

static int hv_match_token_ci(const char *src, size_t len, const char *token) {
  size_t i = 0;
  if (!src || !token) return 0;
  while (i < len && token[i]) {
    if (hv_tolower(src[i]) != hv_tolower(token[i])) return 0;
    i++;
  }
  return token[i] == '\0' && i == len;
}

int hv_contains_ci(const char *text, const char *needle) {
  size_t i = 0;
  size_t needle_len = kstrlen(needle);
  if (!text || !needle || needle_len == 0) return 0;
  while (text[i]) {
    size_t j = 0;
    while (j < needle_len && text[i + j] &&
           hv_tolower(text[i + j]) == hv_tolower(needle[j])) {
      j++;
    }
    if (j == needle_len) return 1;
    i++;
  }
  return 0;
}

int hv_has_scheme(const char *url) {
  size_t i = 0;
  if (!url || !url[0]) return 0;
  while (url[i]) {
    char ch = url[i];
    if (ch == ':') return i > 0;
    if (ch == '/' || ch == '?' || ch == '#') return 0;
    if (!hv_is_alnum(ch) && ch != '+' && ch != '-' && ch != '.') return 0;
    i++;
  }
  return 0;
}

static void hv_parse_enter(void) {
  while (__sync_lock_test_and_set(&g_hv_parse_lock, 1u) != 0u) {
  }
}

static void hv_parse_leave(void) {
  __sync_lock_release(&g_hv_parse_lock);
}

int hv_parse_locked(const char *html, size_t len, struct html_document *doc) {
  /* Backward-compatible entry: parses without enforcing the parse budget.
   * Used for internal scaffolding and tests. Production navigations should
   * call hv_parse_locked_with_app to keep parse work bounded. */
  int rc = 0;
  hv_doc_release_assets(doc);
  hv_parse_enter();
  hv_parse_app_set(NULL);
  rc = html_parse(html, len, doc);
  hv_parse_app_set(NULL);
  hv_parse_leave();
  return rc;
}

int hv_parse_locked_with_app(struct html_viewer_app *app, const char *html,
                              size_t len, struct html_document *doc) {
  int rc = 0;
  hv_doc_release_assets(doc);
  hv_parse_enter();
  if (app) {
    hv_parse_budget_reset(app);
  }
  hv_parse_app_set(app);
  rc = html_parse(html, len, doc);
  hv_parse_app_set(NULL);
  hv_parse_leave();
  return rc;
}

void hv_copy_prefix(char *dst, size_t dst_len, const char *src, size_t src_len) {
  size_t i = 0;
  if (!dst || dst_len == 0) return;
  while (i < src_len && i + 1 < dst_len && src && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static size_t hv_path_query_offset(const char *path) {
  size_t i = 0;
  if (!path) return 0;
  while (path[i] && path[i] != '?' && path[i] != '#') i++;
  return i;
}

static size_t hv_path_fragment_offset(const char *path) {
  size_t i = 0;
  if (!path) return 0;
  while (path[i] && path[i] != '#') i++;
  return i;
}

size_t hv_path_directory_length(const char *path) {
  size_t len = hv_path_query_offset(path);
  while (len > 1 && path[len - 1] != '/') len--;
  return len;
}

static void hv_normalize_path(const char *path, char *out, size_t out_len) {
  size_t marks[64];
  int depth = 0;
  size_t i = 0;
  size_t suffix_offset = hv_path_query_offset(path);
  const char *suffix = path ? path + suffix_offset : NULL;
  if (!out || out_len == 0) return;
  out[0] = '/';
  if (out_len > 1) out[1] = '\0';
  if (!path) return;
  while (i < suffix_offset && path[i] == '/') i++;
  while (i < suffix_offset) {
    size_t start = i;
    size_t seg_len = 0;
    size_t out_pos = 0;
    while (i < suffix_offset && path[i] != '/') i++;
    seg_len = i - start;
    if (seg_len == 0 || (seg_len == 1 && path[start] == '.')) {
    } else if (seg_len == 2 && path[start] == '.' && path[start + 1] == '.') {
      if (depth > 0) out[marks[--depth]] = '\0';
    } else {
      out_pos = kstrlen(out);
      if (out_pos > 1 && out_pos < out_len - 1) {
        out[out_pos++] = '/';
        out[out_pos] = '\0';
      }
      if (depth < 64) marks[depth++] = out_pos > 1 ? out_pos - 1 : 1;
      for (size_t j = 0; j < seg_len && out_pos < out_len - 1; j++) {
        out[out_pos++] = path[start + j];
      }
      out[out_pos] = '\0';
    }
    while (i < suffix_offset && path[i] == '/') i++;
  }
  if (suffix && suffix[0]) kbuf_append(out, out_len, suffix);
}

static void hv_build_absolute_url(char *out, size_t out_len, int use_tls,
                                  const char *host, uint16_t port,
                                  const char *path) {
  if (!out || out_len == 0) return;
  out[0] = '\0';
  kstrcpy(out, out_len, use_tls ? "https://" : "http://");
  kbuf_append(out, out_len, host ? host : "");
  if ((use_tls && port != 443) || (!use_tls && port != 80)) {
    kbuf_append(out, out_len, ":");
    kbuf_append_u32(out, out_len, (uint32_t)port);
  }
  kbuf_append(out, out_len, path && path[0] ? path : "/");
}

int hv_resolve_url(const char *base_url, const char *ref,
                   char *out, size_t out_len) {
  static char host[HTTP_MAX_HOST];
  static char base_path[HTTP_MAX_PATH];
  static char joined[HTML_URL_MAX];
  static char normalized[HTML_URL_MAX];
  uint16_t port = 0;
  int use_tls = 0;
  if (!ref || !ref[0] || !out || out_len == 0) return -1;
  out[0] = '\0';
  if (hv_strncmp(ref, "about:", 6) == 0 || hv_strncmp(ref, "http://", 7) == 0 ||
      hv_strncmp(ref, "https://", 8) == 0 || hv_has_scheme(ref)) {
    kstrcpy(out, out_len, ref);
    return 0;
  }
  if (!base_url || http_parse_url(base_url, host, sizeof(host), base_path,
                                  sizeof(base_path), &port, &use_tls) != 0) {
    return -1;
  }
  if (ref[0] == '/' && ref[1] == '/') {
    kstrcpy(out, out_len, use_tls ? "https:" : "http:");
    kbuf_append(out, out_len, ref);
    return 0;
  }
  joined[0] = '\0';
  if (ref[0] == '/') {
    kstrcpy(joined, sizeof(joined), ref);
  } else if (ref[0] == '?') {
    hv_copy_prefix(joined, sizeof(joined), base_path,
                   hv_path_query_offset(base_path));
    kbuf_append(joined, sizeof(joined), ref);
  } else if (ref[0] == '#') {
    hv_copy_prefix(joined, sizeof(joined), base_path,
                   hv_path_fragment_offset(base_path));
    kbuf_append(joined, sizeof(joined), ref);
  } else {
    hv_copy_prefix(joined, sizeof(joined), base_path,
                   hv_path_directory_length(base_path));
    if (!joined[0]) kstrcpy(joined, sizeof(joined), "/");
    kbuf_append(joined, sizeof(joined), ref);
  }
  hv_normalize_path(joined, normalized, sizeof(normalized));
  hv_build_absolute_url(out, out_len, use_tls, host, port, normalized);
  return 0;
}

uint8_t hv_parse_form_method(const char *method) {
  return hv_streq_ci(method, "post") ? HTML_FORM_METHOD_POST
                                     : HTML_FORM_METHOD_GET;
}

int hv_form_action_matches(const struct html_node *a,
                           const struct html_node *b) {
  if (!a || !b) return 0;
  if (!a->href[0] && !b->href[0]) return 1;
  return kstreq(a->href, b->href);
}

static void hv_urlencode_append_char(char *out, size_t out_len, char ch) {
  static const char hex[] = "0123456789ABCDEF";
  char encoded[4];
  if (hv_is_alnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
    encoded[0] = ch;
    encoded[1] = '\0';
  } else if (ch == ' ') {
    encoded[0] = '+';
    encoded[1] = '\0';
  } else {
    encoded[0] = '%';
    encoded[1] = hex[((unsigned char)ch >> 4) & 0x0F];
    encoded[2] = hex[(unsigned char)ch & 0x0F];
    encoded[3] = '\0';
  }
  kbuf_append(out, out_len, encoded);
}

void hv_urlencode_append(char *out, size_t out_len, const char *text) {
  size_t i = 0;
  if (!out || out_len == 0 || !text) return;
  while (text[i]) {
    hv_urlencode_append_char(out, out_len, text[i]);
    i++;
  }
}

const char *html_viewer_find_header(const struct http_response *resp,
                                    const char *name) {
  if (!resp || !name) return NULL;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (hv_streq_ci(resp->headers[i].name, name)) return resp->headers[i].value;
  }
  return NULL;
}

void hv_trim_text(char *text) {
  size_t len = kstrlen(text);
  size_t start = 0;
  if (!text[0]) return;
  while (text[start] == ' ' || text[start] == '\n') start++;
  while (len > start && (text[len - 1] == ' ' || text[len - 1] == '\n')) len--;
  if (start > 0 && len > start) kmemmove(text, text + start, len - start);
  else if (start >= len) {
    text[0] = '\0';
    return;
  }
  text[len - start] = '\0';
}

void hv_text_append_char(char *dst, size_t dst_len, size_t *dst_pos,
                         char ch, int *last_space) {
  if (!dst || !dst_pos || !last_space || dst_len == 0) return;
  if (ch == '\r' || ch == '\t' || ch == '\f') ch = ' ';
  if (ch == '\n') {
    while (*dst_pos > 0 && dst[*dst_pos - 1] == ' ') (*dst_pos)--;
    if (*dst_pos > 0 && dst[*dst_pos - 1] == '\n') {
      dst[*dst_pos] = '\0';
      *last_space = 1;
      return;
    }
    if (*dst_pos + 1 < dst_len) {
      dst[(*dst_pos)++] = '\n';
      dst[*dst_pos] = '\0';
    }
    *last_space = 1;
    return;
  }
  if (hv_is_space(ch)) {
    if (*dst_pos == 0 || *last_space ||
        (*dst_pos > 0 && dst[*dst_pos - 1] == '\n')) {
      dst[*dst_pos] = '\0';
      return;
    }
    ch = ' ';
    *last_space = 1;
  } else {
    *last_space = 0;
  }
  if (*dst_pos + 1 < dst_len) {
    dst[(*dst_pos)++] = ch;
    dst[*dst_pos] = '\0';
  }
}

int hv_decode_entity_value(const char *html, size_t len,
                           size_t *consumed, char *out_char) {
  size_t i = 1;
  char decoded = 0;
  if (!html || len < 3 || html[0] != '&' || !consumed || !out_char) return 0;
  while (i < len && i < 12 && html[i] != ';') i++;
  if (i >= len || i >= 12 || html[i] != ';') return 0;
  if (html[1] == '#') {
    uint32_t value = 0;
    size_t pos = 2;
    int base = 10;
    if (pos < i && (html[pos] == 'x' || html[pos] == 'X')) {
      base = 16;
      pos++;
    }
    while (pos < i) {
      char ch = html[pos++];
      uint32_t digit = 0;
      if (ch >= '0' && ch <= '9') digit = (uint32_t)(ch - '0');
      else if (base == 16 && ch >= 'a' && ch <= 'f') digit = 10u + (uint32_t)(ch - 'a');
      else if (base == 16 && ch >= 'A' && ch <= 'F') digit = 10u + (uint32_t)(ch - 'A');
      else return 0;
      value = value * (uint32_t)base + digit;
    }
    if (value == 160u) decoded = ' ';
    else if (value == 0u || value > 255u) decoded = '?';
    else decoded = (char)(uint8_t)value;
  } else {
    size_t name_len = i - 1;
    const char *name = html + 1;
    if (hv_match_token_ci(name, name_len, "amp")) decoded = '&';
    else if (hv_match_token_ci(name, name_len, "lt")) decoded = '<';
    else if (hv_match_token_ci(name, name_len, "gt")) decoded = '>';
    else if (hv_match_token_ci(name, name_len, "quot")) decoded = '"';
    else if (hv_match_token_ci(name, name_len, "apos")) decoded = '\'';
    else if (hv_match_token_ci(name, name_len, "nbsp")) decoded = ' ';
    else if (hv_match_token_ci(name, name_len, "copy")) decoded = (char)0xA9;
    else if (hv_match_token_ci(name, name_len, "reg")) decoded = (char)0xAE;
    else if (hv_match_token_ci(name, name_len, "deg")) decoded = (char)0xB0;
    else if (hv_match_token_ci(name, name_len, "plusmn")) decoded = (char)0xB1;
    else if (hv_match_token_ci(name, name_len, "laquo")) decoded = (char)0xAB;
    else if (hv_match_token_ci(name, name_len, "raquo")) decoded = (char)0xBB;
    else if (hv_match_token_ci(name, name_len, "mdash") ||
             hv_match_token_ci(name, name_len, "ndash")) decoded = '-';
    else if (hv_match_token_ci(name, name_len, "hellip")) decoded = '.';
    else if (hv_match_token_ci(name, name_len, "ldquo") ||
             hv_match_token_ci(name, name_len, "rdquo") ||
             hv_match_token_ci(name, name_len, "lsquo") ||
             hv_match_token_ci(name, name_len, "rsquo")) decoded = '\'';
    else if (hv_match_token_ci(name, name_len, "trade")) decoded = '*';
    else if (hv_match_token_ci(name, name_len, "times")) decoded = 'x';
    else if (hv_match_token_ci(name, name_len, "divide")) decoded = '/';
    else if (hv_match_token_ci(name, name_len, "middot")) decoded = '.';
    else if (hv_match_token_ci(name, name_len, "bull")) decoded = '*';
    else if (hv_match_token_ci(name, name_len, "euro")) decoded = 'E';
    else if (hv_match_token_ci(name, name_len, "pound")) decoded = (char)0xA3;
    else if (hv_match_token_ci(name, name_len, "yen")) decoded = (char)0xA5;
    else if (hv_match_token_ci(name, name_len, "sect")) decoded = (char)0xA7;
    else return 0;
  }
  *consumed = i + 1;
  *out_char = decoded;
  return 1;
}

void hv_append_decoded_text(char *dst, size_t dst_len, size_t *dst_pos,
                            const char *src, size_t src_len,
                            int *last_space) {
  size_t pos = 0;
  while (pos < src_len && src[pos]) {
    size_t consumed = 0;
    char decoded = 0;
    if (src[pos] == '&' &&
        hv_decode_entity_value(src + pos, src_len - pos, &consumed, &decoded)) {
      hv_text_append_char(dst, dst_len, dst_pos, decoded, last_space);
      pos += consumed;
      continue;
    }
    hv_text_append_char(dst, dst_len, dst_pos, src[pos], last_space);
    pos++;
  }
}

void hv_read_tag_name(const char *html, size_t len,
                      size_t *pos, char *tag, size_t tag_len) {
  size_t i = 0;
  if (!pos || !tag || tag_len == 0) return;
  while (*pos < len && hv_is_space(html[*pos])) (*pos)++;
  while (*pos < len && i + 1 < tag_len) {
    char ch = html[*pos];
    if (!hv_is_alnum(ch) && ch != '-' && ch != ':' && ch != '_') break;
    tag[i++] = hv_tolower(ch);
    (*pos)++;
  }
  tag[i] = '\0';
  while (*pos < len) {
    char ch = html[*pos];
    if (hv_is_alnum(ch) || ch == '-' || ch == ':' || ch == '_') (*pos)++;
    else break;
  }
}

size_t hv_scan_tag_end(const char *html, size_t len, size_t pos,
                       size_t *tag_end, int *self_closing) {
  char quote = 0;
  size_t last_non_space = pos;
  while (pos < len) {
    char ch = html[pos];
    if (quote) {
      if (ch == quote) quote = 0;
      pos++;
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quote = ch;
      pos++;
      continue;
    }
    if (!hv_is_space(ch) && ch != '>') last_non_space = pos;
    if (ch == '>') {
      if (tag_end) *tag_end = pos;
      if (self_closing) {
        *self_closing = (last_non_space < pos && html[last_non_space] == '/');
      }
      return pos + 1;
    }
    pos++;
  }
  if (tag_end) *tag_end = len;
  if (self_closing) *self_closing = 0;
  return len;
}

size_t hv_skip_special_tag(const char *html, size_t len, size_t pos) {
  if (pos + 2 < len && html[pos] == '!' &&
      html[pos + 1] == '-' && html[pos + 2] == '-') {
    pos += 3;
    while (pos + 2 < len) {
      if (html[pos] == '-' && html[pos + 1] == '-' && html[pos + 2] == '>') {
        return pos + 3;
      }
      pos++;
    }
    return len;
  }
  while (pos < len && html[pos] != '>') pos++;
  return pos < len ? pos + 1 : len;
}

int hv_extract_attr_value(const char *attrs, size_t len,
                          const char *name, char *out,
                          size_t out_len) {
  size_t pos = 0;
  if (!out || out_len == 0) return 0;
  out[0] = '\0';
  if (!attrs || !name) return 0;
  while (pos < len) {
    size_t name_start = 0;
    size_t name_end = 0;
    size_t value_start = 0;
    size_t value_end = 0;
    char quote = 0;
    while (pos < len && hv_is_space(attrs[pos])) pos++;
    if (pos >= len || attrs[pos] == '/' || attrs[pos] == '>') break;
    name_start = pos;
    while (pos < len && !hv_is_space(attrs[pos]) &&
           attrs[pos] != '=' && attrs[pos] != '/' && attrs[pos] != '>') {
      pos++;
    }
    name_end = pos;
    while (pos < len && hv_is_space(attrs[pos])) pos++;
    if (pos >= len || attrs[pos] == '/' || attrs[pos] == '>') break;
    if (attrs[pos] != '=') {
      if (pos == name_end) pos++;
      continue;
    }
    pos++;
    while (pos < len && hv_is_space(attrs[pos])) pos++;
    if (pos < len && (attrs[pos] == '"' || attrs[pos] == '\'')) {
      quote = attrs[pos++];
    }
    value_start = pos;
    if (quote) {
      while (pos < len && attrs[pos] != quote) pos++;
      value_end = pos;
      if (pos < len) pos++;
    } else {
      while (pos < len && !hv_is_space(attrs[pos]) &&
             attrs[pos] != '/' && attrs[pos] != '>') {
        pos++;
      }
      value_end = pos;
    }
    if (hv_match_token_ci(attrs + name_start, name_end - name_start, name)) {
      hv_copy_prefix(out, out_len, attrs + value_start, value_end - value_start);
      return out[0] != '\0';
    }
  }
  return 0;
}

static const char *html_viewer_effective_base_url(const struct html_viewer_app *app,
                                                  char *scratch,
                                                  size_t scratch_len) {
  if (!app) return "";
  if (!app->doc.base_url[0]) return app->url;
  if (hv_has_scheme(app->doc.base_url)) return app->doc.base_url;
  if (scratch && scratch_len > 0 &&
      hv_resolve_url(app->url, app->doc.base_url, scratch, scratch_len) == 0) {
    return scratch;
  }
  return app->url;
}

int html_viewer_resolve_document_url(const struct html_viewer_app *app,
                                     const char *ref,
                                     char *out,
                                     size_t out_len) {
  static char base_scratch[HTML_URL_MAX];
  const char *base = html_viewer_effective_base_url(app, base_scratch,
                                                    sizeof(base_scratch));
  return hv_resolve_url(base, ref, out, out_len);
}

int hv_has_boolean_attr(const char *attrs, size_t len,
                        const char *name) {
  size_t pos = 0;
  size_t name_len = 0;
  if (!attrs || !name || !name[0]) return 0;
  name_len = kstrlen(name);
  while (pos < len) {
    size_t attr_start = 0;
    size_t attr_end = 0;
    char quote = 0;
    while (pos < len && hv_is_space(attrs[pos])) pos++;
    if (pos >= len || attrs[pos] == '>') break;
    if (attrs[pos] == '/') {
      pos++;
      continue;
    }
    attr_start = pos;
    while (pos < len && !hv_is_space(attrs[pos]) &&
           attrs[pos] != '=' && attrs[pos] != '/' && attrs[pos] != '>') {
      pos++;
    }
    attr_end = pos;
    if (hv_match_token_ci(attrs + attr_start, attr_end - attr_start, name) &&
        attr_end - attr_start == name_len) {
      return 1;
    }
    while (pos < len && hv_is_space(attrs[pos])) pos++;
    if (pos < len && attrs[pos] == '=') {
      pos++;
      while (pos < len && hv_is_space(attrs[pos])) pos++;
      if (pos < len && (attrs[pos] == '"' || attrs[pos] == '\'')) {
        quote = attrs[pos++];
        while (pos < len && attrs[pos] != quote) pos++;
        if (pos < len) pos++;
      } else {
        while (pos < len && !hv_is_space(attrs[pos]) && attrs[pos] != '>') pos++;
      }
    }
  }
  return 0;
}

int hv_token_list_contains_ci(const char *list, const char *needle) {
  size_t pos = 0;
  size_t needle_len = 0;
  if (!list || !needle || !needle[0]) return 0;
  needle_len = kstrlen(needle);
  while (list[pos]) {
    size_t start = 0;
    size_t end = 0;
    while (list[pos] == ' ' || list[pos] == '\t' ||
           list[pos] == '\r' || list[pos] == '\n' ||
           list[pos] == ',') {
      pos++;
    }
    start = pos;
    while (list[pos] && list[pos] != ',' &&
           list[pos] != ' ' && list[pos] != '\t' &&
           list[pos] != '\r' && list[pos] != '\n') {
      pos++;
    }
    end = pos;
    if (end > start &&
        end - start == needle_len &&
        hv_match_token_ci(list + start, end - start, needle)) {
      return 1;
    }
  }
  return 0;
}

int hv_extract_srcset_first_url(const char *srcset,
                                char *out,
                                size_t out_len) {
  size_t pos = 0;
  size_t out_pos = 0;
  if (!srcset || !out || out_len == 0) return 0;
  out[0] = '\0';
  while (srcset[pos] && hv_is_space(srcset[pos])) pos++;
  while (srcset[pos] && srcset[pos] != ',' &&
         !hv_is_space(srcset[pos]) && out_pos + 1 < out_len) {
    out[out_pos++] = srcset[pos++];
  }
  out[out_pos] = '\0';
  hv_trim_text(out);
  return out[0] != '\0';
}

int hv_image_type_supported_by_decoder(const char *type) {
  if (!type || !type[0]) return 1;
  return hv_contains_ci(type, "image/png") ||
         hv_contains_ci(type, "image/jpeg") ||
         hv_contains_ci(type, "image/jpg");
}

int hv_image_type_is_known_unsupported(const char *type) {
  if (!type || !type[0]) return 0;
  return hv_contains_ci(type, "image/webp") ||
         hv_contains_ci(type, "image/avif");
}

int hv_image_body_is_known_unsupported(const uint8_t *body,
                                       size_t body_len) {
  if (!body || body_len < 12) return 0;
  if (body_len >= 12 &&
      body[0] == 'R' && body[1] == 'I' && body[2] == 'F' && body[3] == 'F' &&
      body[8] == 'W' && body[9] == 'E' && body[10] == 'B' && body[11] == 'P') {
    return 1;
  }
  if (body_len >= 16 &&
      body[4] == 'f' && body[5] == 't' && body[6] == 'y' && body[7] == 'p' &&
      body[8] == 'a' && body[9] == 'v' && body[10] == 'i' && body[11] == 'f') {
    return 1;
  }
  return 0;
}
