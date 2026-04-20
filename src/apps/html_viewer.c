#include "apps/html_viewer.h"
#include "apps/css_parser.h"
#include "drivers/input/keyboard_layout.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "gui/png_loader.h"
#include "memory/kmem.h"
#include "net/http.h"
#include "security/tls.h"
#include "util/kstring.h"
#include <stddef.h>

static struct html_viewer_app g_viewer;
static int g_viewer_open = 0;

/* ------------------------------------------------------------------ */
/* Image cache (per-page, cleared on each navigation)                   */
/* ------------------------------------------------------------------ */

#define HV_IMG_CACHE_MAX 8
struct hv_img_cache_entry {
    char     url[HTML_URL_MAX];
    uint32_t *pixels;  /* kalloc'd ARGB32; NULL means slot unused */
    uint32_t width;
    uint32_t height;
};
static struct hv_img_cache_entry hv_img_cache[HV_IMG_CACHE_MAX];

static void hv_img_cache_clear(void) {
    for (int i = 0; i < HV_IMG_CACHE_MAX; i++) {
        if (hv_img_cache[i].pixels) {
            kfree(hv_img_cache[i].pixels);
            hv_img_cache[i].pixels = NULL;
        }
        hv_img_cache[i].url[0] = '\0';
        hv_img_cache[i].width  = 0;
        hv_img_cache[i].height = 0;
    }
}

static struct hv_img_cache_entry *hv_img_cache_find(const char *url) {
    for (int i = 0; i < HV_IMG_CACHE_MAX; i++) {
        if (hv_img_cache[i].pixels && kstreq(hv_img_cache[i].url, url))
            return &hv_img_cache[i];
    }
    return NULL;
}

static struct hv_img_cache_entry *hv_img_cache_slot(void) {
    for (int i = 0; i < HV_IMG_CACHE_MAX; i++) {
        if (!hv_img_cache[i].pixels) return &hv_img_cache[i];
    }
    /* All slots full: evict slot 0 */
    kfree(hv_img_cache[0].pixels);
    hv_img_cache[0].pixels = NULL;
    return &hv_img_cache[0];
}

/* ------------------------------------------------------------------ */
/* HTTP response body cache (GET only, 200 responses, max-age based)    */
/* ------------------------------------------------------------------ */

#define HV_HTTP_CACHE_MAX  8
#define HV_HTTP_CACHE_BODY_MAX (256 * 1024)  /* 256 KB per entry */

struct hv_http_cache_entry {
    char     url[HTTP_MAX_URL];
    uint8_t *body;       /* kalloc'd; NULL = slot unused */
    size_t   body_len;
    char     content_type[128];
    uint32_t max_age;    /* max-age in "ticks" (navigations); 0 = session-only */
    uint32_t age;        /* how many navigations since cached */
};

static struct hv_http_cache_entry hv_http_cache[HV_HTTP_CACHE_MAX];

static void hv_http_cache_clear(void) {
    for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
        if (hv_http_cache[i].body) {
            kfree(hv_http_cache[i].body);
            hv_http_cache[i].body = NULL;
        }
        hv_http_cache[i].url[0] = '\0';
        hv_http_cache[i].body_len = 0;
        hv_http_cache[i].age = 0;
    }
}

static struct hv_http_cache_entry *hv_http_cache_find(const char *url) {
    for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
        if (hv_http_cache[i].body && kstreq(hv_http_cache[i].url, url))
            return &hv_http_cache[i];
    }
    return NULL;
}

static void hv_http_cache_tick(void) {
    for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
        if (!hv_http_cache[i].body) continue;
        hv_http_cache[i].age++;
        if (hv_http_cache[i].max_age > 0 &&
            hv_http_cache[i].age > hv_http_cache[i].max_age) {
            kfree(hv_http_cache[i].body);
            hv_http_cache[i].body = NULL;
            hv_http_cache[i].url[0] = '\0';
        }
    }
}

static struct hv_http_cache_entry *hv_http_cache_slot(void) {
    for (int i = 0; i < HV_HTTP_CACHE_MAX; i++)
        if (!hv_http_cache[i].body) return &hv_http_cache[i];
    /* Evict oldest */
    int oldest = 0;
    for (int i = 1; i < HV_HTTP_CACHE_MAX; i++)
        if (hv_http_cache[i].age > hv_http_cache[oldest].age) oldest = i;
    kfree(hv_http_cache[oldest].body);
    hv_http_cache[oldest].body = NULL;
    return &hv_http_cache[oldest];
}

static uint32_t hv_parse_cache_control_max_age(const char *cc) {
    if (!cc) return 0;
    const char *p = cc;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if ((p[0]|32)=='n' && (p[1]|32)=='o' && (p[2]=='-'||p[2]==' ') &&
            ((p[3]|32)=='s'||(p[3]|32)=='c')) return 0; /* no-store / no-cache */
        if (p[0]=='m' && p[1]=='a' && p[2]=='x' && p[3]=='-' &&
            p[4]=='a' && p[5]=='g' && p[6]=='e' && p[7]=='=') {
            uint32_t v = 0;
            p += 8;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; }
            return v / 60; /* convert seconds → navigation-ticks (≈60s per nav) */
        }
        while (*p && *p != ',') p++;
    }
    return 1; /* default: keep for 1 navigation */
}

static void hv_http_cache_store(const char *url, const struct http_response *resp) {
    const char *cc = NULL;
    const char *ct = NULL;
    uint32_t max_age;
    struct hv_http_cache_entry *slot;
    uint8_t *body_copy;
    int i;
    if (!url || !resp || resp->status_code != 200 || !resp->body || resp->body_len == 0)
        return;
    if (resp->body_len > HV_HTTP_CACHE_BODY_MAX) return;
    /* Check cache-control */
    for (i = 0; i < (int)resp->header_count; i++) {
        if ((resp->headers[i].name[0]|32) == 'c' &&
            kstreq(resp->headers[i].name, "Cache-Control"))
            cc = resp->headers[i].value;
        if ((resp->headers[i].name[0]|32) == 'c' &&
            kstreq(resp->headers[i].name, "Content-Type"))
            ct = resp->headers[i].value;
    }
    max_age = hv_parse_cache_control_max_age(cc);
    if (max_age == 0 && cc) return; /* explicit no-store */
    body_copy = (uint8_t *)kalloc(resp->body_len);
    if (!body_copy) return;
    kmemcpy(body_copy, resp->body, resp->body_len);
    slot = hv_http_cache_slot();
    kstrcpy(slot->url, sizeof(slot->url), url);
    slot->body = body_copy;
    slot->body_len = resp->body_len;
    slot->max_age = max_age;
    slot->age = 0;
    if (ct) kstrcpy(slot->content_type, sizeof(slot->content_type), ct);
    else slot->content_type[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Navigation history (back / forward)                                  */
/* ------------------------------------------------------------------ */

#define HV_HISTORY_MAX 16
static char hv_history[HV_HISTORY_MAX][HTML_URL_MAX];
static int  hv_history_count = 0;
static int  hv_history_cur   = -1;
static int  hv_navigating_history = 0; /* suppress push during back/fwd */

/* Set by TR renderer, read by TD renderer for side-by-side column layout */
static int32_t hv_table_row_y = 28;
static int32_t hv_table_row_h = 24;

static void hv_history_push(const char *url) {
    if (hv_navigating_history || !url || !url[0]) return;
    /* Avoid duplicate consecutive entries */
    if (hv_history_cur >= 0 && kstreq(hv_history[hv_history_cur], url)) return;
    /* Truncate forward history */
    hv_history_count = hv_history_cur + 1;
    if (hv_history_count >= HV_HISTORY_MAX) {
        /* Shift left, discard oldest */
        for (int i = 0; i < HV_HISTORY_MAX - 1; i++)
            kmemcpy(hv_history[i], hv_history[i + 1], HTML_URL_MAX);
        hv_history_cur = HV_HISTORY_MAX - 2;
        hv_history_count = HV_HISTORY_MAX - 1;
    }
    kstrcpy(hv_history[hv_history_count], sizeof(hv_history[0]), url);
    hv_history_cur = hv_history_count;
    hv_history_count++;
}

int http_last_error(void);
const char *http_error_string(int error);

static void html_viewer_set_error(struct html_viewer_app *app,
                                  const char *title,
                                  const char *message);
static void html_viewer_load_text_document(struct html_viewer_app *app,
                                           const char *title,
                                           const char *text,
                                           size_t len,
                                           uint32_t color);
static void html_viewer_request_internal(struct html_viewer_app *app,
                                         const char *url,
                                         enum http_method method,
                                         const uint8_t *body,
                                         size_t body_len,
                                         int depth);
static void html_viewer_submit_form(struct html_viewer_app *app, int node_index);
static void hv_http_cache_store(const char *url, const struct http_response *resp);
static void hv_fetch_external_css(struct html_viewer_app *app);
static void hv_fetch_page_images(struct html_viewer_app *app);

enum { HTML_VIEWER_HTTP_ERR_TLS = 6 };
enum { HTML_FORM_METHOD_GET = 0, HTML_FORM_METHOD_POST = 1 };
enum {
  HTML_INPUT_TYPE_TEXT = 0,
  HTML_INPUT_TYPE_SEARCH,
  HTML_INPUT_TYPE_TEXTAREA,
  HTML_INPUT_TYPE_HIDDEN,
  HTML_INPUT_TYPE_SUBMIT,
  HTML_INPUT_TYPE_BUTTON
};

static int hv_strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
  }
  return 0;
}

static char hv_tolower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

static int hv_is_space(char ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\f';
}

static int hv_is_alnum(char ch) {
  return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9'));
}

static int hv_streq_ci(const char *a, const char *b) {
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

static int hv_contains_ci(const char *text, const char *needle) {
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

static int hv_has_scheme(const char *url) {
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

static void hv_copy_prefix(char *dst, size_t dst_len,
                           const char *src, size_t src_len) {
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

static size_t hv_path_directory_length(const char *path) {
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

static int hv_resolve_url(const char *base_url, const char *ref,
                          char *out, size_t out_len) {
  char host[HTTP_MAX_HOST];
  char base_path[HTTP_MAX_PATH];
  char joined[HTML_URL_MAX];
  char normalized[HTML_URL_MAX];
  uint16_t port = 0;
  int use_tls = 0;
  if (!ref || !ref[0] || !out || out_len == 0) return -1;
  out[0] = '\0';
  if (hv_strncmp(ref, "about:", 6) == 0 ||
      hv_strncmp(ref, "http://", 7) == 0 ||
      hv_strncmp(ref, "https://", 8) == 0 ||
      hv_has_scheme(ref)) {
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
    hv_copy_prefix(joined, sizeof(joined), base_path, hv_path_query_offset(base_path));
    kbuf_append(joined, sizeof(joined), ref);
  } else if (ref[0] == '#') {
    hv_copy_prefix(joined, sizeof(joined), base_path, hv_path_fragment_offset(base_path));
    kbuf_append(joined, sizeof(joined), ref);
  } else {
    hv_copy_prefix(joined, sizeof(joined), base_path, hv_path_directory_length(base_path));
    if (!joined[0]) kstrcpy(joined, sizeof(joined), "/");
    kbuf_append(joined, sizeof(joined), ref);
  }
  hv_normalize_path(joined, normalized, sizeof(normalized));
  hv_build_absolute_url(out, out_len, use_tls, host, port, normalized);
  return 0;
}

static uint8_t hv_parse_form_method(const char *method) {
  return hv_streq_ci(method, "post") ? HTML_FORM_METHOD_POST : HTML_FORM_METHOD_GET;
}

static int hv_form_action_matches(const struct html_node *a, const struct html_node *b) {
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

static void hv_urlencode_append(char *out, size_t out_len, const char *text) {
  size_t i = 0;
  if (!out || out_len == 0 || !text) return;
  while (text[i]) {
    hv_urlencode_append_char(out, out_len, text[i]);
    i++;
  }
}

static const char *html_viewer_find_header(const struct http_response *resp,
                                           const char *name) {
  if (!resp || !name) return NULL;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (hv_streq_ci(resp->headers[i].name, name)) return resp->headers[i].value;
  }
  return NULL;
}

static void hv_trim_text(char *text) {
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

static void hv_text_append_char(char *dst, size_t dst_len,
                                size_t *dst_pos, char ch, int *last_space) {
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

static int hv_decode_entity_value(const char *html, size_t len,
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
    else return 0;
  }
  *consumed = i + 1;
  *out_char = decoded;
  return 1;
}

static void hv_append_decoded_text(char *dst, size_t dst_len,
                                   size_t *dst_pos, const char *src,
                                   size_t src_len, int *last_space) {
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

static void hv_read_tag_name(const char *html, size_t len,
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

static size_t hv_scan_tag_end(const char *html, size_t len, size_t pos,
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

static size_t hv_skip_special_tag(const char *html, size_t len, size_t pos) {
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

static int hv_extract_attr_value(const char *attrs, size_t len,
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
    if (pos >= len || attrs[pos] != '=') continue;
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

static int hv_tag_is_void(const char *tag) {
  return hv_streq_ci(tag, "br") || hv_streq_ci(tag, "img") ||
         hv_streq_ci(tag, "meta") || hv_streq_ci(tag, "link") ||
         hv_streq_ci(tag, "input") || hv_streq_ci(tag, "hr");
}

static size_t hv_skip_block(const char *html, size_t len, size_t pos,
                            const char *tag) {
  int depth = 1;
  while (pos < len) {
    char inner[32];
    size_t tag_end = pos;
    int closing = 0;
    int self_closing = 0;
    if (html[pos] != '<') {
      pos++;
      continue;
    }
    pos++;
    if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
      pos = hv_skip_special_tag(html, len, pos);
      continue;
    }
    if (pos < len && html[pos] == '/') {
      closing = 1;
      pos++;
    }
    hv_read_tag_name(html, len, &pos, inner, sizeof(inner));
    pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
    if (!inner[0]) continue;
    if (closing && hv_streq_ci(inner, tag)) {
      depth--;
      if (depth == 0) return pos;
      continue;
    }
    if (!closing && !self_closing && hv_streq_ci(inner, tag)) depth++;
  }
  return len;
}

static size_t hv_collect_text_until_tag(const char *html, size_t len,
                                        size_t pos, const char *tag,
                                        char *out, size_t out_len) {
  size_t out_pos = 0;
  int last_space = 1;
  int depth = 1;
  if (!out || out_len == 0) return pos;
  out[0] = '\0';
  while (pos < len) {
    if (html[pos] == '<') {
      char inner[32];
      char alt[HTML_TEXT_MAX];
      size_t attr_start = 0;
      size_t tag_end = pos;
      int closing = 0;
      int self_closing = 0;
      pos++;
      if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
        pos = hv_skip_special_tag(html, len, pos);
        continue;
      }
      if (pos < len && html[pos] == '/') {
        closing = 1;
        pos++;
      }
      hv_read_tag_name(html, len, &pos, inner, sizeof(inner));
      attr_start = pos;
      pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
      if (!inner[0]) continue;
      if (closing && hv_streq_ci(inner, tag)) {
        depth--;
        if (depth == 0) break;
        continue;
      }
      if (!closing && !self_closing && hv_streq_ci(inner, tag)) {
        depth++;
        continue;
      }
      if (!closing &&
          (hv_streq_ci(inner, "script") || hv_streq_ci(inner, "style") ||
           hv_streq_ci(inner, "svg") || hv_streq_ci(inner, "canvas") ||
           hv_streq_ci(inner, "iframe"))) {
        pos = hv_skip_block(html, len, pos, inner);
        continue;
      }
      if (!closing &&
          (hv_streq_ci(inner, "br") || hv_streq_ci(inner, "p") ||
           hv_streq_ci(inner, "div") || hv_streq_ci(inner, "section") ||
           hv_streq_ci(inner, "article") || hv_streq_ci(inner, "li") ||
           hv_streq_ci(inner, "tr") || hv_streq_ci(inner, "table"))) {
        hv_text_append_char(out, out_len, &out_pos, '\n', &last_space);
      }
      if (!closing && hv_streq_ci(inner, "img") &&
          hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                "alt", alt, sizeof(alt))) {
        hv_append_decoded_text(out, out_len, &out_pos, alt, kstrlen(alt), &last_space);
      }
      continue;
    }
    if (html[pos] == '&') {
      size_t consumed = 0;
      char decoded = 0;
      if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
        hv_text_append_char(out, out_len, &out_pos, decoded, &last_space);
        pos += consumed;
        continue;
      }
    }
    hv_text_append_char(out, out_len, &out_pos, html[pos], &last_space);
    pos++;
  }
  hv_trim_text(out);
  return pos;
}

static void hv_apply_node_attrs(struct html_node *node, const char *attrs,
                               size_t len) {
  char style_attr[256];
  style_attr[0] = '\0';
  if (!node || !attrs) return;
  (void)hv_extract_attr_value(attrs, len, "id", node->id, sizeof(node->id));
  (void)hv_extract_attr_value(attrs, len, "class", node->class_list,
                              sizeof(node->class_list));
  if (hv_extract_attr_value(attrs, len, "style", style_attr,
                             sizeof(style_attr)))
    css_apply_inline(style_attr, node);
}

static struct html_node *html_push_node(struct html_document *doc) {
  struct html_node *node = NULL;
  if (!doc || doc->node_count >= HTML_MAX_NODES) return NULL;
  node = &doc->nodes[doc->node_count++];
  kmemzero(node, sizeof(*node));
  node->type = HTML_NODE_TEXT;
  node->color = 0xCDD6F4;
  node->font_size = 16;
  return node;
}

static uint8_t hv_form_input_type(const char *type) {
  if (hv_streq_ci(type, "search")) return HTML_INPUT_TYPE_SEARCH;
  if (hv_streq_ci(type, "textarea")) return HTML_INPUT_TYPE_TEXTAREA;
  if (hv_streq_ci(type, "hidden")) return HTML_INPUT_TYPE_HIDDEN;
  if (hv_streq_ci(type, "submit")) return HTML_INPUT_TYPE_SUBMIT;
  if (hv_streq_ci(type, "button")) return HTML_INPUT_TYPE_BUTTON;
  return HTML_INPUT_TYPE_TEXT;
}

static int hv_body_looks_html(const uint8_t *body, size_t len) {
  char sample[257];
  size_t sample_len = len;
  if (!body || len == 0) return 0;
  if (sample_len > 256) sample_len = 256;
  for (size_t i = 0; i < sample_len; i++) {
    sample[i] = (char)body[i];
  }
  sample[sample_len] = '\0';
  return hv_contains_ci(sample, "<!doctype html") ||
         hv_contains_ci(sample, "<html") ||
         hv_contains_ci(sample, "<body") ||
         hv_contains_ci(sample, "<head");
}

static int hv_body_looks_textual(const uint8_t *body, size_t len) {
  size_t sample_len = len;
  size_t printable = 0;
  if (!body || len == 0) return 0;
  if (sample_len > 1024) sample_len = 1024;
  for (size_t i = 0; i < sample_len; i++) {
    uint8_t ch = body[i];
    if (ch == 0) return 0;
    if (ch == '\t' || ch == '\n' || ch == '\r' ||
        (ch >= 32 && ch < 127) || ch >= 128) {
      printable++;
    }
  }
  return printable * 100 >= sample_len * 85;
}

static int hv_content_type_is_html(const char *content_type) {
  return content_type && content_type[0] &&
         (hv_contains_ci(content_type, "text/html") ||
          hv_contains_ci(content_type, "application/xhtml+xml"));
}

static int hv_content_type_is_textual(const char *content_type) {
  if (!content_type || !content_type[0]) return 0;
  if (hv_contains_ci(content_type, "text/") ||
      hv_contains_ci(content_type, "application/json") ||
      hv_contains_ci(content_type, "application/xml") ||
      hv_contains_ci(content_type, "text/xml") ||
      hv_contains_ci(content_type, "+json") ||
      hv_contains_ci(content_type, "+xml") ||
      hv_contains_ci(content_type, "application/javascript") ||
      hv_contains_ci(content_type, "text/javascript") ||
      hv_contains_ci(content_type, "application/x-javascript")) {
    return 1;
  }
  return 0;
}

static void hv_cookie_trim(char *text) {
  size_t len = kstrlen(text);
  size_t start = 0;
  while (text[start] == ' ') start++;
  while (len > start && text[len - 1] == ' ') len--;
  if (start > 0 && len > start) kmemmove(text, text + start, len - start);
  else if (start >= len) {
    text[0] = '\0';
    return;
  }
  text[len - start] = '\0';
}

static int hv_cookie_domain_matches(const char *cookie_domain,
                                    const char *host, int host_only) {
  size_t host_len = kstrlen(host);
  size_t domain_len = kstrlen(cookie_domain);
  if (!cookie_domain[0] || !host[0]) return 0;
  if (host_only) return hv_streq_ci(cookie_domain, host);
  if (hv_streq_ci(cookie_domain, host)) return 1;
  if (host_len <= domain_len) return 0;
  return hv_streq_ci(host + (host_len - domain_len), cookie_domain) &&
         host[host_len - domain_len - 1] == '.';
}

static int hv_cookie_path_matches(const char *cookie_path, const char *path) {
  size_t i = 0;
  if (!cookie_path[0]) return 1;
  while (cookie_path[i]) {
    if (path[i] != cookie_path[i]) return 0;
    i++;
  }
  return 1;
}

static int hv_cookie_find_slot(struct html_viewer_app *app,
                               const char *name,
                               const char *domain,
                               const char *path) {
  if (!app) return -1;
  for (uint32_t i = 0; i < app->cookie_count; i++) {
    if (hv_streq_ci(app->cookies[i].name, name) &&
        hv_streq_ci(app->cookies[i].domain, domain) &&
        hv_streq_ci(app->cookies[i].path, path)) {
      return (int)i;
    }
  }
  return -1;
}

static void hv_cookie_remove_index(struct html_viewer_app *app, uint32_t index) {
  if (!app || index >= app->cookie_count) return;
  for (uint32_t i = index; i + 1 < app->cookie_count; i++) {
    app->cookies[i] = app->cookies[i + 1];
  }
  if (app->cookie_count > 0) app->cookie_count--;
}

static void hv_cookie_default_path(const char *request_path,
                                   char *out, size_t out_len) {
  size_t len = hv_path_directory_length(request_path);
  if (!out || out_len == 0) return;
  if (!request_path || !request_path[0] || request_path[0] != '/') {
    kstrcpy(out, out_len, "/");
    return;
  }
  hv_copy_prefix(out, out_len, request_path, len);
  if (!out[0]) kstrcpy(out, out_len, "/");
}

static void hv_store_cookie_from_header(struct html_viewer_app *app,
                                        const char *host,
                                        const char *path,
                                        int use_tls,
                                        const char *header_value) {
  char segment[384];
  char name[HTML_COOKIE_NAME_MAX];
  char value[HTML_COOKIE_VALUE_MAX];
  char domain[HTML_COOKIE_DOMAIN_MAX];
  char cookie_path[HTML_COOKIE_PATH_MAX];
  size_t pos = 0;
  uint8_t secure = 0;
  uint8_t host_only = 1;
  int delete_cookie = 0;
  int existing = -1;
  if (!app || !host || !path || !header_value || !header_value[0]) return;

  kstrcpy(domain, sizeof(domain), host);
  hv_cookie_default_path(path, cookie_path, sizeof(cookie_path));
  name[0] = '\0';
  value[0] = '\0';

  while (header_value[pos]) {
    size_t seg_len = 0;
    while (header_value[pos + seg_len] && header_value[pos + seg_len] != ';') seg_len++;
    hv_copy_prefix(segment, sizeof(segment), header_value + pos, seg_len);
    hv_cookie_trim(segment);
    if (!name[0]) {
      size_t i = 0;
      while (segment[i] && segment[i] != '=' && i + 1 < sizeof(name)) {
        name[i] = segment[i];
        i++;
      }
      name[i] = '\0';
      if (segment[i] == '=') kstrcpy(value, sizeof(value), segment + i + 1);
      hv_cookie_trim(name);
      hv_cookie_trim(value);
      if (!name[0]) return;
    } else if (hv_contains_ci(segment, "=")) {
      char attr_name[32];
      char attr_value[HTML_COOKIE_PATH_MAX];
      size_t i = 0;
      size_t j = 0;
      while (segment[i] && segment[i] != '=' && i + 1 < sizeof(attr_name)) {
        attr_name[i] = segment[i];
        i++;
      }
      attr_name[i] = '\0';
      if (segment[i] == '=') kstrcpy(attr_value, sizeof(attr_value), segment + i + 1);
      else attr_value[0] = '\0';
      hv_cookie_trim(attr_name);
      hv_cookie_trim(attr_value);
      if (hv_streq_ci(attr_name, "Domain")) {
        if (attr_value[0] == '.') {
          while (attr_value[j + 1]) {
            attr_value[j] = attr_value[j + 1];
            j++;
          }
          attr_value[j] = '\0';
        }
        if (!hv_cookie_domain_matches(attr_value, host, 0)) return;
        kstrcpy(domain, sizeof(domain), attr_value);
        host_only = 0;
      } else if (hv_streq_ci(attr_name, "Path")) {
        kstrcpy(cookie_path, sizeof(cookie_path), attr_value[0] ? attr_value : "/");
      } else if (hv_streq_ci(attr_name, "Max-Age") &&
                 attr_value[0] == '0' && attr_value[1] == '\0') {
        delete_cookie = 1;
      }
    } else if (hv_streq_ci(segment, "Secure")) {
      secure = 1;
    }
    pos += seg_len;
    if (header_value[pos] == ';') pos++;
    while (header_value[pos] == ' ') pos++;
  }

  if (secure && !use_tls) return;
  existing = hv_cookie_find_slot(app, name, domain, cookie_path);
  if (delete_cookie) {
    if (existing >= 0) hv_cookie_remove_index(app, (uint32_t)existing);
    return;
  }
  if (existing < 0) {
    if (app->cookie_count >= HTML_MAX_COOKIES) hv_cookie_remove_index(app, 0);
    existing = (int)app->cookie_count++;
  }
  kmemzero(&app->cookies[existing], sizeof(app->cookies[existing]));
  kstrcpy(app->cookies[existing].name, sizeof(app->cookies[existing].name), name);
  kstrcpy(app->cookies[existing].value, sizeof(app->cookies[existing].value), value);
  kstrcpy(app->cookies[existing].domain, sizeof(app->cookies[existing].domain), domain);
  kstrcpy(app->cookies[existing].path, sizeof(app->cookies[existing].path), cookie_path);
  app->cookies[existing].secure = secure;
  app->cookies[existing].host_only = host_only;
}

static int hv_build_cookie_header(struct html_viewer_app *app,
                                  const char *host,
                                  const char *path,
                                  int use_tls,
                                  char *out, size_t out_len) {
  int wrote_any = 0;
  if (!out || out_len == 0) return 0;
  out[0] = '\0';
  if (!app || !host || !path) return 0;
  for (uint32_t i = 0; i < app->cookie_count; i++) {
    const struct html_cookie *cookie = &app->cookies[i];
    if (cookie->secure && !use_tls) continue;
    if (!hv_cookie_domain_matches(cookie->domain, host, cookie->host_only)) continue;
    if (!hv_cookie_path_matches(cookie->path, path)) continue;
    if (wrote_any) kbuf_append(out, out_len, "; ");
    kbuf_append(out, out_len, cookie->name);
    kbuf_append(out, out_len, "=");
    kbuf_append(out, out_len, cookie->value);
    wrote_any = 1;
  }
  return wrote_any;
}

static void html_viewer_set_transport_error(struct html_viewer_app *app) {
  char message[160];
  int err = http_last_error();
  message[0] = '\0';
  kstrcpy(message, sizeof(message), http_error_string(err));
  if (err == HTML_VIEWER_HTTP_ERR_TLS) {
    struct tls_security_info info;
    kbuf_append(message, sizeof(message), " (");
    kbuf_append(message, sizeof(message), tls_state_name(tls_last_state()));
    if (tls_last_error() != 0) {
      kbuf_append(message, sizeof(message), ", ");
      kbuf_append(message, sizeof(message), tls_alert_name(tls_last_error()));
    }
    if (tls_get_last_security_info(&info) == 0 && info.protocol_version != 0) {
      kbuf_append(message, sizeof(message), ", ");
      kbuf_append(message, sizeof(message), tls_version_name(info.protocol_version));
      if (info.hostname_validated) {
        kbuf_append(message, sizeof(message), ", host-ok");
      }
      if (info.alpn[0]) {
        kbuf_append(message, sizeof(message), ", ");
        kbuf_append(message, sizeof(message), info.alpn);
      }
    }
    kbuf_append(message, sizeof(message), ")");
  }
  html_viewer_set_error(app, "Navigation Error", message);
}

static void html_viewer_set_status_error(struct html_viewer_app *app, int status_code) {
  char message[128];
  message[0] = '\0';
  kstrcpy(message, sizeof(message), "HTTP status ");
  kbuf_append_u32(message, sizeof(message), (uint32_t)status_code);
  html_viewer_set_error(app, "Navigation Error", message);
}

static void html_viewer_set_error(struct html_viewer_app *app,
                                  const char *title,
                                  const char *message) {
  if (!app) return;
  kmemzero(&app->doc, sizeof(app->doc));
  app->doc.node_count = 0;
  if (title && title[0]) {
    struct html_node *title_node = html_push_node(&app->doc);
    if (title_node) {
      title_node->type = HTML_NODE_TAG_H1;
      title_node->color = 0xF38BA8;
      kstrcpy(title_node->text, sizeof(title_node->text), title);
    }
  }
  if (message && message[0]) {
    struct html_node *body_node = html_push_node(&app->doc);
    if (body_node) {
      body_node->type = HTML_NODE_TAG_P;
      body_node->color = 0xCDD6F4;
      kstrcpy(body_node->text, sizeof(body_node->text), message);
    }
  }
  kstrcpy(app->doc.title, sizeof(app->doc.title),
          title && title[0] ? title : "Navigation Error");
  if (app->window) compositor_set_title(app->window->id, app->doc.title);
}

static void html_viewer_load_builtin(struct html_viewer_app *app, const char *url) {
  const char *html = NULL;
  size_t len = 0;
  if (!app || !url) return;
  if (hv_strncmp(url, "about:blank", 11) == 0) {
    html = "<html><head><title>New Tab</title></head><body></body></html>";
    url = "about:blank";
  } else if (hv_strncmp(url, "about:version", 13) == 0) {
    html =
      "<html><head><title>About CapyBrowser</title></head><body>"
      "<h1>CapyBrowser</h1>"
      "<p>HTTP/1.1 client with verified HTTPS transport.</p>"
      "<p>TLS 1.2 certificate validation, cookies, redirects, gzip/deflate decoding and basic HTML forms are enabled in this build.</p>"
      "<p>Pages that depend on heavy JavaScript or advanced CSS fall back to simplified document mode.</p>"
      "<a href=\"about:home\">Back to home</a>"
      "</body></html>";
  } else {
    html =
      "<html><head><title>CapyBrowser</title></head><body>"
      "<h1>CapyBrowser</h1>"
      "<p>Open secure sites directly from the address bar.</p>"
      "<p>Pages with heavy CSS or JavaScript are rendered in simplified mode.</p>"
      "<a href=\"https://www.google.com\">Open Google</a>"
      "<br><a href=\"about:version\">About this build</a>"
      "</body></html>";
    url = "about:home";
  }
  while (html[len]) len++;
  html_parse(html, len, &app->doc);
  kstrcpy(app->url, sizeof(app->url), url);
  if (app->doc.title[0] && app->window) {
    compositor_set_title(app->window->id, app->doc.title);
  }
}

static void html_viewer_cleanup(void) {
  g_viewer.window = NULL;
  g_viewer_open = 0;
}

static void html_viewer_on_close(struct gui_window *win) {
  (void)win;
  html_viewer_cleanup();
}

static void html_viewer_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  html_viewer_paint((struct html_viewer_app *)win->user_data);
}

static void html_viewer_window_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  html_viewer_scroll((struct html_viewer_app *)win->user_data, -delta);
}

static void html_viewer_window_key(struct gui_window *win, uint32_t keycode,
                                   uint8_t mods) {
  (void)mods;
  if (!win || !win->user_data) return;
  struct html_viewer_app *app = (struct html_viewer_app *)win->user_data;
  char ch = (keycode < 0x80) ? (char)keycode : 0;

  /* URL bar cursor movement */
  if (app->url_editing) {
    if (keycode == KEY_LEFT) {
      if (app->url_cursor > 0) app->url_cursor--;
      compositor_invalidate(win->id);
      return;
    }
    if (keycode == KEY_RIGHT) {
      int len = (int)kstrlen(app->url);
      if (app->url_cursor < len) app->url_cursor++;
      compositor_invalidate(win->id);
      return;
    }
    if (keycode == KEY_HOME) {
      app->url_cursor = 0;
      compositor_invalidate(win->id);
      return;
    }
    if (keycode == KEY_END) {
      app->url_cursor = (int)kstrlen(app->url);
      compositor_invalidate(win->id);
      return;
    }
  }

  /* Back / forward (only in browsing mode, not when editing URL or focused on input) */
  if (!app->url_editing && app->focused_node_index < 0) {
    if (keycode == KEY_LEFT) {
      if (hv_history_cur > 0) {
        hv_history_cur--;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
      compositor_invalidate(win->id);
      return;
    }
    if (keycode == KEY_RIGHT) {
      if (hv_history_cur < hv_history_count - 1) {
        hv_history_cur++;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
      compositor_invalidate(win->id);
      return;
    }
  }

  if (!app->url_editing &&
      app->focused_node_index >= 0 &&
      app->focused_node_index < app->doc.node_count) {
    struct html_node *node = &app->doc.nodes[app->focused_node_index];
    if (node->type == HTML_NODE_TAG_INPUT && !node->hidden) {
      if (ch == '\n' || ch == '\r') {
        if (node->input_type == HTML_INPUT_TYPE_TEXTAREA) {
          size_t len = kstrlen(node->text);
          if (len + 1 < sizeof(node->text)) {
            node->text[len] = '\n';
            node->text[len + 1] = '\0';
          }
          compositor_invalidate(win->id);
          return;
        }
        html_viewer_submit_form(app, app->focused_node_index);
        compositor_invalidate(win->id);
        return;
      }
      if (ch == 0x1B) {
        app->focused_node_index = -1;
        compositor_invalidate(win->id);
        return;
      }
      if (ch == '\b') {
        size_t len = kstrlen(node->text);
        if (len > 0) node->text[len - 1] = '\0';
        compositor_invalidate(win->id);
        return;
      }
      if (ch >= 32 && ch < 127) {
        size_t len = kstrlen(node->text);
        if (len + 1 < sizeof(node->text)) {
          node->text[len] = ch;
          node->text[len + 1] = '\0';
        }
        compositor_invalidate(win->id);
        return;
      }
    }
  }

  if (!app->url_editing) {
    /* Arrow key scroll */
    if (keycode == KEY_UP) {
      html_viewer_scroll(app, -1);
      return;
    }
    if (keycode == KEY_DOWN) {
      html_viewer_scroll(app, 1);
      return;
    }
    if (keycode == KEY_PGUP) {
      int page = app->window ? (int)app->window->surface.height / 20 : 10;
      html_viewer_scroll(app, -page);
      return;
    }
    if (keycode == KEY_PGDN) {
      int page = app->window ? (int)app->window->surface.height / 20 : 10;
      html_viewer_scroll(app, page);
      return;
    }
    /* F5: reload current page */
    if (keycode == KEY_F5) {
      html_viewer_navigate(app, app->url);
      return;
    }
    /* Tab: cycle focus through form input elements */
    if (ch == '\t') {
      int start = app->focused_node_index + 1;
      int found = -1;
      int i;
      for (i = start; i < app->doc.node_count; i++) {
        if ((app->doc.nodes[i].type == HTML_NODE_TAG_INPUT ||
             app->doc.nodes[i].type == HTML_NODE_TAG_BUTTON) &&
            !app->doc.nodes[i].hidden) {
          found = i; break;
        }
      }
      if (found < 0) {
        for (i = 0; i < app->doc.node_count; i++) {
          if ((app->doc.nodes[i].type == HTML_NODE_TAG_INPUT ||
               app->doc.nodes[i].type == HTML_NODE_TAG_BUTTON) &&
              !app->doc.nodes[i].hidden) {
            found = i; break;
          }
        }
      }
      if (found >= 0) app->focused_node_index = found;
      compositor_invalidate(win->id);
      return;
    }
    if (ch >= 32 && ch < 127) {
      app->url_editing = 1;
      app->url[0] = '\0';
      app->url_cursor = 0;
    } else {
      return;
    }
  }

  if (ch == '\n' || ch == '\r') {
    app->url_editing = 0;
    html_viewer_navigate(app, app->url);
    compositor_invalidate(win->id);
    return;
  }
  if (ch == 0x1B) {
    app->url_editing = 0;
    compositor_invalidate(win->id);
    return;
  }
  if (ch == '\b') {
    if (app->url_cursor > 0) {
      int len = (int)kstrlen(app->url);
      for (int i = app->url_cursor - 1; i < len; i++) app->url[i] = app->url[i + 1];
      app->url_cursor--;
    }
    compositor_invalidate(win->id);
    return;
  }
  if (ch >= 32 && ch < 127) {
    int len = (int)kstrlen(app->url);
    if (len < HTML_URL_MAX - 1) {
      for (int i = len; i >= app->url_cursor; i--) app->url[i + 1] = app->url[i];
      app->url[app->url_cursor] = ch;
      app->url_cursor++;
    }
    compositor_invalidate(win->id);
  }
}

/* Draw a single bitmap glyph scaled by an integer factor (nearest-neighbor). */
static void hv_draw_char_scaled(struct gui_surface *surface, const struct font *f,
                                int32_t x, int32_t y, char c, uint32_t color, int scale) {
  uint32_t idx = (uint32_t)(uint8_t)c;
  uint32_t glyph_offset;
  const uint8_t *glyph;
  if (!surface || !f || !f->data || scale < 1) return;
  if (idx < f->first_char || idx > f->last_char) return;
  glyph_offset = (idx - f->first_char) * f->bytes_per_glyph;
  glyph = f->data + glyph_offset;
  for (uint32_t row = 0; row < f->glyph_height; row++) {
    uint8_t bits = glyph[row];
    for (int sy = 0; sy < scale; sy++) {
      int32_t py = y + (int32_t)(row * (uint32_t)scale + (uint32_t)sy);
      if (py < 0 || (uint32_t)py >= surface->height) continue;
      uint32_t *line = (uint32_t *)((uint8_t *)surface->pixels + (uint32_t)py * surface->pitch);
      for (uint32_t col = 0; col < f->glyph_width; col++) {
        if (!(bits & (0x80 >> col))) continue;
        for (int sx = 0; sx < scale; sx++) {
          int32_t px = x + (int32_t)(col * (uint32_t)scale + (uint32_t)sx);
          if (px >= 0 && (uint32_t)px < surface->width) line[(uint32_t)px] = color;
        }
      }
    }
  }
}

/* Word-wrap text using a scaled bitmap font; returns total height in pixels. */
static int hv_wrap_text_scaled(struct gui_surface *surface, const struct font *f,
                               int32_t x, int32_t y, int32_t max_width,
                               const char *text, uint32_t color, int scale) {
  int32_t char_w  = (int32_t)(f->glyph_width  * (uint32_t)scale);
  int32_t char_h  = (int32_t)(f->glyph_height * (uint32_t)scale) + 2;
  int32_t line_y  = y;
  int32_t cursor_x = x;
  size_t  i = 0;
  int drew = 0;
  if (!f || !text || !text[0]) return char_h;
  while (text[i]) {
    size_t word_start, word_len = 0;
    if (text[i] == '\n') { cursor_x = x; line_y += char_h; i++; continue; }
    while (text[i] == ' ') {
      if (cursor_x != x) {
        if (cursor_x - x + char_w > max_width) { cursor_x = x; line_y += char_h; }
        else cursor_x += char_w;
      }
      i++;
    }
    word_start = i;
    while (text[i] && text[i] != ' ' && text[i] != '\n') { word_len++; i++; }
    if (!word_len) break;
    if (cursor_x != x && cursor_x - x + (int32_t)word_len * char_w > max_width) {
      cursor_x = x; line_y += char_h;
    }
    for (size_t j = 0; j < word_len; j++) {
      if (surface)
        hv_draw_char_scaled(surface, f, cursor_x, line_y, text[word_start + j], color, scale);
      cursor_x += char_w;
    }
    drew = 1;
  }
  return drew ? (int)(line_y - y + char_h) : char_h;
}

static int html_viewer_wrap_text(struct gui_surface *surface, const struct font *f,
                                 int32_t x, int32_t y, int32_t max_width,
                                 const char *text, uint32_t color,
                                 int underline) {
  int32_t line_y = y;
  int32_t cursor_x = x;
  int32_t line_height = (int32_t)f->glyph_height + 2;
  size_t i = 0;
  int drew = 0;
  if (!f) return 0;
  if (!text || !text[0]) return line_height;
  if (max_width < (int32_t)f->glyph_width) max_width = (int32_t)f->glyph_width;
  while (text[i]) {
    if (text[i] == '\n') {
      cursor_x = x;
      line_y += line_height;
      i++;
      continue;
    }
    while (text[i] == ' ') {
      if (cursor_x != x) {
        if (cursor_x - x + (int32_t)f->glyph_width > max_width) {
          cursor_x = x;
          line_y += line_height;
        } else {
          cursor_x += (int32_t)f->glyph_width;
        }
      }
      i++;
    }
    if (!text[i]) break;
    {
      size_t word_start = i;
      size_t word_len = 0;
      int32_t word_width = 0;
      while (text[i] && text[i] != ' ' && text[i] != '\n') i++;
      word_len = i - word_start;
      word_width = (int32_t)word_len * (int32_t)f->glyph_width;
      if (cursor_x != x && word_width <= max_width &&
          cursor_x - x + word_width > max_width) {
        cursor_x = x;
        line_y += line_height;
      }
      if (word_width > max_width) {
        for (size_t j = 0; j < word_len; j++) {
          if (cursor_x != x &&
              cursor_x - x + (int32_t)f->glyph_width > max_width) {
            cursor_x = x;
            line_y += line_height;
          }
          if (surface) {
            font_draw_char(surface, f, cursor_x, line_y,
                           text[word_start + j], color);
            if (underline) {
              int32_t uy = line_y + (int32_t)f->glyph_height;
              if (uy >= 0 && (uint32_t)uy < surface->height) {
                uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                             (uint32_t)uy * surface->pitch);
                for (uint32_t ux = 0; ux < f->glyph_width &&
                                     (uint32_t)(cursor_x + (int32_t)ux) < surface->width; ux++) {
                  if (cursor_x + (int32_t)ux >= 0) {
                    row[(uint32_t)(cursor_x + (int32_t)ux)] = color;
                  }
                }
              }
            }
          }
          cursor_x += (int32_t)f->glyph_width;
          drew = 1;
        }
      } else {
        int32_t start_x = cursor_x;
        if (surface) {
          for (size_t j = 0; j < word_len; j++) {
            font_draw_char(surface, f, cursor_x, line_y, text[word_start + j], color);
            cursor_x += (int32_t)f->glyph_width;
          }
          if (underline) {
            int32_t uy = line_y + (int32_t)f->glyph_height;
            if (uy >= 0 && (uint32_t)uy < surface->height) {
              uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                           (uint32_t)uy * surface->pitch);
              for (int32_t ux = start_x; ux < cursor_x; ux++) {
                if (ux >= 0 && (uint32_t)ux < surface->width) row[(uint32_t)ux] = color;
              }
            }
          }
        } else {
          cursor_x += word_width;
        }
        drew = 1;
      }
    }
  }
  return drew ? (int)(line_y - y + line_height) : line_height;
}

static int html_viewer_node_margin_top(enum html_node_type type) {
  if (type == HTML_NODE_TAG_H1) return 8;
  if (type == HTML_NODE_TAG_H2) return 6;
  if (type == HTML_NODE_TAG_H3) return 4;
  if (type == HTML_NODE_TAG_H4) return 4;
  if (type == HTML_NODE_TAG_H5) return 3;
  if (type == HTML_NODE_TAG_H6) return 2;
  if (type == HTML_NODE_TAG_HR) return 6;
  if (type == HTML_NODE_TAG_BLOCKQUOTE) return 6;
  if (type == HTML_NODE_TAG_PRE) return 4;
  if (type == HTML_NODE_TAG_TR) return 2;
  return 2;
}

static int html_viewer_node_margin_bottom(enum html_node_type type) {
  if (type == HTML_NODE_TAG_H1) return 10;
  if (type == HTML_NODE_TAG_H2) return 8;
  if (type == HTML_NODE_TAG_H3) return 6;
  if (type == HTML_NODE_TAG_H4) return 6;
  if (type == HTML_NODE_TAG_H5) return 4;
  if (type == HTML_NODE_TAG_H6) return 4;
  if (type == HTML_NODE_TAG_BR) return 6;
  if (type == HTML_NODE_TAG_HR) return 6;
  if (type == HTML_NODE_TAG_BLOCKQUOTE) return 6;
  if (type == HTML_NODE_TAG_PRE) return 4;
  if (type == HTML_NODE_TAG_TR) return 2;
  if (type == HTML_NODE_TAG_TD) return 1;
  return 4;
}

static uint32_t html_viewer_node_color(const struct gui_theme_palette *theme,
                                       const struct html_node *node) {
  if (!theme || !node) return 0xCDD6F4;
  /* CSS inline/stylesheet color takes priority */
  if (node->css_color) return node->css_color;
  if (node->type == HTML_NODE_TAG_H1) return theme->accent;
  if (node->type == HTML_NODE_TAG_H2) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_H3) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_H4) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_H5) return theme->text;
  if (node->type == HTML_NODE_TAG_H6) return theme->text_muted;
  if (node->type == HTML_NODE_TAG_A) return theme->accent;
  if (node->type == HTML_NODE_TAG_BUTTON) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_INPUT) return theme->text;
  if (node->type == HTML_NODE_TAG_PRE) return theme->terminal_fg;
  if (node->type == HTML_NODE_TAG_CODE) return theme->terminal_fg;
  if (node->type == HTML_NODE_TAG_BLOCKQUOTE) return theme->text_muted;
  if (node->type == HTML_NODE_TAG_MARK) return 0xF9E2AF;
  if (node->type == HTML_NODE_TAG_TD) return node->bold ? theme->accent_alt : theme->text;
  if (node->type == HTML_NODE_TAG_FIGCAPTION) return theme->text_muted;
  if (node->type == HTML_NODE_TAG_DETAILS) return theme->accent;
  if (node->type == HTML_NODE_TAG_MEDIA) return theme->text_muted;
  return node->color ? node->color : theme->text;
}

static int html_viewer_render_node(struct gui_surface *surface, const struct font *f,
                                   const struct gui_theme_palette *theme,
                                   const struct html_node *node,
                                   int32_t y, int draw) {
  char display[HTML_TEXT_MAX + 16];
  int32_t margin = 12;
  int32_t max_width = 0;
  int32_t top = y + html_viewer_node_margin_top(node->type);
  int32_t height = 0;
  uint32_t color = html_viewer_node_color(theme, node);
  if (!surface || !f || !theme || !node) return y;
  if (node->hidden) return y;
  /* CSS background color: fill the node's row before drawing text */
  if (draw && node->css_bg_color) {
    int32_t row_h = (int32_t)f->glyph_height + 4;
    for (int32_t dy = 0; dy < row_h; dy++) {
      int32_t hy = top + dy;
      if (hy >= 0 && (uint32_t)hy < surface->height) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                     (uint32_t)hy * surface->pitch);
        for (uint32_t px = 0; px < surface->width; px++)
          row[px] = node->css_bg_color;
      }
    }
  }
  if (node->type == HTML_NODE_TAG_BR) return y + html_viewer_node_margin_bottom(node->type);
  /* Horizontal rule: draw a 2-px line across the viewport */
  if (node->type == HTML_NODE_TAG_HR) {
    if (draw) {
      for (int32_t dy = 0; dy < 2; dy++) {
        int32_t hy = top + dy;
        if (hy >= 0 && (uint32_t)hy < surface->height) {
          uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                       (uint32_t)hy * surface->pitch);
          for (uint32_t px = (uint32_t)margin;
               px < surface->width - (uint32_t)margin; px++) {
            row[px] = theme->text_muted;
          }
        }
      }
    }
    return top + 2 + html_viewer_node_margin_bottom(node->type);
  }
  /* Table row: separator line + reserve space for side-by-side cells */
  if (node->type == HTML_NODE_TAG_TR) {
    hv_table_row_h = (int32_t)f->glyph_height + 8;
    hv_table_row_y = top + 1;
    if (draw) {
      int32_t hy = top;
      if (hy >= 0 && (uint32_t)hy < surface->height) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                     (uint32_t)hy * surface->pitch);
        for (uint32_t px = (uint32_t)margin;
             px < surface->width - (uint32_t)margin; px++) {
          row[px] = theme->window_border;
        }
      }
    }
    /* Advance y past separator + full row height so TD nodes don't shift y */
    return top + 1 + hv_table_row_h + html_viewer_node_margin_bottom(node->type);
  }
  /* Scaled heading rendering: H1 at 3×, H2/H3 at 2× (or CSS font-size derived) */
  if (node->type == HTML_NODE_TAG_H1 || node->type == HTML_NODE_TAG_H2 ||
      node->type == HTML_NODE_TAG_H3) {
    int scale;
    int32_t max_w = (int32_t)surface->width - margin * 2;
    int32_t start_x = margin;
    int32_t h;
    if (node->font_size > 0)
      scale = (int)(node->font_size / f->glyph_height);
    else
      scale = (node->type == HTML_NODE_TAG_H1) ? 3 : 2;
    if (scale < 1) scale = 1;
    /* Apply text-align: center */
    if (node->text_align == 1) {
      int32_t tw = (int32_t)(kstrlen(node->text) * f->glyph_width * (uint32_t)scale);
      if (tw < max_w) start_x = margin + (max_w - tw) / 2;
    } else if (node->text_align == 2) {
      int32_t tw = (int32_t)(kstrlen(node->text) * f->glyph_width * (uint32_t)scale);
      if (tw < max_w) start_x = margin + max_w - tw;
    }
    h = hv_wrap_text_scaled(draw ? surface : NULL, f, start_x, top, max_w,
                    node->text, color, scale);
    return top + h + html_viewer_node_margin_bottom(node->type);
  }

  /* IMG node: blit cached pixels or fall back to alt text */
  if (node->type == HTML_NODE_TAG_IMG) {
    struct hv_img_cache_entry *img = node->href[0] ? hv_img_cache_find(node->href) : NULL;
    if (img && img->pixels && img->width > 0 && img->height > 0) {
      uint32_t max_w = surface->width > (uint32_t)(margin * 2)
                       ? surface->width - (uint32_t)(margin * 2) : 1;
      uint32_t dst_w = img->width < max_w ? img->width : max_w;
      uint32_t dst_h = (dst_w == img->width) ? img->height
                     : (uint32_t)((uint64_t)img->height * dst_w / img->width);
      if (dst_h == 0) dst_h = 1;
      if (draw) {
        for (uint32_t dy = 0; dy < dst_h; dy++) {
          int32_t sy_row = top + (int32_t)dy;
          if (sy_row < 0 || (uint32_t)sy_row >= surface->height) continue;
          uint32_t *dst_row = (uint32_t *)((uint8_t *)surface->pixels +
                                           (uint32_t)sy_row * surface->pitch);
          uint32_t src_y = dy * img->height / dst_h;
          const uint32_t *src_row = img->pixels + src_y * img->width;
          for (uint32_t dx = 0; dx < dst_w; dx++) {
            uint32_t src_x = dx * img->width / dst_w;
            uint32_t px = src_row[src_x];
            uint32_t alpha = px >> 24;
            if (alpha == 0xFF) {
              dst_row[(uint32_t)margin + dx] = px;
            } else if (alpha > 0) {
              uint32_t bg = theme->window_bg;
              uint32_t r = ((px >> 16) & 0xFF) * alpha / 255
                         + ((bg >> 16) & 0xFF) * (255 - alpha) / 255;
              uint32_t g = ((px >> 8)  & 0xFF) * alpha / 255
                         + ((bg >> 8)  & 0xFF) * (255 - alpha) / 255;
              uint32_t b = (px & 0xFF) * alpha / 255
                         + (bg & 0xFF) * (255 - alpha) / 255;
              dst_row[(uint32_t)margin + dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
          }
        }
      }
      return top + (int32_t)dst_h + html_viewer_node_margin_bottom(node->type);
    }
    /* fallback */
    {
      char img_display[HTML_TEXT_MAX + 16];
      kstrcpy(img_display, sizeof(img_display), "[image] ");
      if (node->text[0]) kbuf_append(img_display, sizeof(img_display), node->text);
      int32_t h = html_viewer_wrap_text(draw ? surface : NULL, f, margin, top,
                      (int32_t)surface->width - margin * 2,
                      img_display, color, 0);
      return top + h + html_viewer_node_margin_bottom(node->type);
    }
  }
  /* Table cell: render at column x offset, y fixed by prior TR, no y advance */
  if (node->type == HTML_NODE_TAG_TD) {
    int32_t ncols  = node->col_count > 0 ? (int32_t)(uint8_t)node->col_count : 1;
    int32_t cidx   = (int32_t)(uint8_t)node->col_index;
    int32_t total_w = (int32_t)surface->width - 8;
    int32_t col_w  = total_w / ncols;
    int32_t cell_x = 4 + cidx * col_w;
    int32_t cell_y = hv_table_row_y + 2;
    uint32_t cell_color = node->bold ? theme->accent_text : color;
    if (draw) {
      /* th: accent background */
      if (node->bold) {
        for (int32_t dy = 0; dy < hv_table_row_h; dy++) {
          int32_t hy = hv_table_row_y + dy;
          if (hy < 0 || (uint32_t)hy >= surface->height) continue;
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          for (int32_t px = cell_x; px < cell_x + col_w &&
               (uint32_t)px < surface->width; px++)
            rp[(uint32_t)px] = theme->accent_alt;
        }
      }
      /* Left vertical border between columns */
      if (cidx > 0 && (uint32_t)cell_x < surface->width) {
        for (int32_t dy = 0; dy < hv_table_row_h; dy++) {
          int32_t hy = hv_table_row_y + dy;
          if (hy < 0 || (uint32_t)hy >= surface->height) continue;
          uint32_t *rp = (uint32_t *)((uint8_t *)surface->pixels +
                                      (uint32_t)hy * surface->pitch);
          rp[(uint32_t)cell_x] = theme->window_border;
        }
      }
    }
    html_viewer_wrap_text(draw ? surface : NULL, f, cell_x + 2, cell_y,
                          col_w - 4, node->text, cell_color, 0);
    return y; /* y already advanced by TR — do not shift again */
  }
  display[0] = '\0';
  if (node->type == HTML_NODE_TAG_LI) kstrcpy(display, sizeof(display), "* ");
  else if (node->type == HTML_NODE_TAG_PRE ||
           node->type == HTML_NODE_TAG_CODE) {
    kstrcpy(display, sizeof(display), "  ");
    kbuf_append(display, sizeof(display), node->text);
  }
  else if (node->type == HTML_NODE_TAG_BLOCKQUOTE) {
    kstrcpy(display, sizeof(display), "> ");
    kbuf_append(display, sizeof(display), node->text);
  }
  else if (node->type == HTML_NODE_TAG_DETAILS) {
    kstrcpy(display, sizeof(display), "[+] ");
    kbuf_append(display, sizeof(display), node->text);
  }
  else if (node->type == HTML_NODE_TAG_FIGCAPTION) {
    kstrcpy(display, sizeof(display), "  ");
    kbuf_append(display, sizeof(display), node->text);
  }
  else if (node->type == HTML_NODE_TAG_INPUT) {
    if (node->name[0]) {
      kstrcpy(display, sizeof(display), node->name);
      kbuf_append(display, sizeof(display), ": ");
    }
    kbuf_append(display, sizeof(display), "[");
    kbuf_append(display, sizeof(display), node->text);
    kbuf_append(display, sizeof(display), "]");
  } else if (node->type == HTML_NODE_TAG_BUTTON) {
    kstrcpy(display, sizeof(display), "[ ");
    kbuf_append(display, sizeof(display), node->text);
    kbuf_append(display, sizeof(display), " ]");
  }
  else kbuf_append(display, sizeof(display), node->text);
  if (!display[0] && node->type == HTML_NODE_TAG_A && node->href[0]) {
    kstrcpy(display, sizeof(display), node->href);
  }
  max_width = (int32_t)surface->width - margin * 2;
  height = html_viewer_wrap_text(draw ? surface : NULL, f, margin, top, max_width,
                                 display, color,
                                 node->type == HTML_NODE_TAG_A ||
                                 node->type == HTML_NODE_TAG_INPUT ||
                                 node->type == HTML_NODE_TAG_BUTTON);
  return top + height + html_viewer_node_margin_bottom(node->type);
}

static int html_viewer_node_hit_test(struct html_viewer_app *app, const struct font *f,
                                     const struct html_node *node,
                                     int32_t start_y, int32_t *top_out,
                                     int32_t *bottom_out) {
  struct gui_surface *surface = NULL;
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t end_y = 0;
  if (!app || !app->window || !f || !node || !theme) return start_y;
  surface = &app->window->surface;
  if (top_out) *top_out = start_y + html_viewer_node_margin_top(node->type);
  end_y = html_viewer_render_node(surface, f, theme, node, start_y, 0);
  if (bottom_out) *bottom_out = end_y;
  return end_y;
}

static void hv_scroll_to_anchor(struct html_viewer_app *app, const char *id) {
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t y = 28;
  int viewport = 0;
  int max_scroll = 0;
  if (!app || !id || !id[0] || !f || !theme || !app->window) return;
  for (int i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    if (node->id[0] && kstreq(node->id, id)) {
      app->scroll_offset = y - 28;
      if (app->scroll_offset < 0) app->scroll_offset = 0;
      viewport = (int)app->window->surface.height - 32;
      max_scroll = app->content_height - viewport;
      if (max_scroll < 0) max_scroll = 0;
      if (app->scroll_offset > max_scroll) app->scroll_offset = max_scroll;
      compositor_invalidate(app->window->id);
      return;
    }
    y = html_viewer_render_node(&app->window->surface, f, theme, node, y, 0);
  }
}

static void html_viewer_window_mouse(struct gui_window *win, int32_t x, int32_t y,
                                     uint8_t buttons) {
  if (!win || !win->user_data || !(buttons & 1)) return;
  struct html_viewer_app *app = (struct html_viewer_app *)win->user_data;
  const struct font *f = font_default();

  if (y < 24) {
    if (x < 24) {
      /* Back button */
      if (hv_history_cur > 0) {
        hv_history_cur--;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
    } else if (x < 50) {
      /* Forward button */
      if (hv_history_cur < hv_history_count - 1) {
        hv_history_cur++;
        hv_navigating_history = 1;
        html_viewer_navigate(app, hv_history[hv_history_cur]);
        hv_navigating_history = 0;
      }
    } else {
      /* URL bar click: enter editing, set cursor position by click x */
      app->url_editing = 1;
      {
        int click_pos = (x - 54) / (int32_t)font_default()->glyph_width;
        int url_len = (int)kstrlen(app->url);
        app->url_cursor = click_pos < 0 ? 0 : click_pos > url_len ? url_len : click_pos;
      }
    }
    compositor_invalidate(win->id);
    return;
  }
  if (!f) return;

  {
    int32_t node_y = 28 - app->scroll_offset;
    for (int i = 0; i < app->doc.node_count; i++) {
      int32_t top = node_y;
      int32_t bottom = node_y;
      struct html_node *node = &app->doc.nodes[i];
      node_y = html_viewer_node_hit_test(app, f, node, node_y, &top, &bottom);
      if (node->type == HTML_NODE_TAG_INPUT && !node->hidden &&
          y >= top && y < bottom && x >= 12) {
        app->focused_node_index = i;
        app->url_editing = 0;
        compositor_invalidate(win->id);
        return;
      }
      if (node->type == HTML_NODE_TAG_BUTTON &&
          y >= top && y < bottom && x >= 12) {
        app->focused_node_index = i;
        app->url_editing = 0;
        html_viewer_submit_form(app, i);
        compositor_invalidate(win->id);
        return;
      }
      if (node->type == HTML_NODE_TAG_A && node->href[0] &&
          y >= top && y < bottom && x >= 12) {
        char resolved[HTML_URL_MAX];
        if (node->href[0] == '#') {
          hv_scroll_to_anchor(app, node->href + 1);
          compositor_invalidate(win->id);
          return;
        }
        if (hv_resolve_url(app->url, node->href, resolved, sizeof(resolved)) == 0) {
          html_viewer_navigate(app, resolved);
        } else {
          html_viewer_navigate(app, node->href);
        }
        compositor_invalidate(win->id);
        return;
      }
    }
  }
  app->focused_node_index = -1;
  compositor_invalidate(win->id);
}

int html_parse(const char *html, size_t len, struct html_document *doc) {
  size_t pos = 0;
  char current_form_action[HTML_URL_MAX];
  uint8_t current_form_method = HTML_FORM_METHOD_GET;
  int list_ordered = 0;  /* inside <ol>? */
  int list_num = 1;      /* next ordered-list item number */
  if (!html || !doc) return -1;
  kmemzero(doc, sizeof(*doc));
  current_form_action[0] = '\0';
  while (pos < len && doc->node_count < HTML_MAX_NODES) {
    if (hv_is_space(html[pos])) {
      pos++;
      continue;
    }
    if (html[pos] == '<') {
      char tag[32];
      char href[HTML_URL_MAX];
      char src[HTML_URL_MAX];
      char text[HTML_TEXT_MAX];
      char name[HTML_COOKIE_NAME_MAX];
      char type_attr[24];
      char method_attr[16];
      size_t attr_start = 0;
      size_t tag_end = pos;
      int closing = 0;
      int self_closing = 0;
      pos++;
      if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
        pos = hv_skip_special_tag(html, len, pos);
        continue;
      }
      if (pos < len && html[pos] == '/') {
        closing = 1;
        pos++;
      }
      hv_read_tag_name(html, len, &pos, tag, sizeof(tag));
      attr_start = pos;
      pos = hv_scan_tag_end(html, len, pos, &tag_end, &self_closing);
      if (!tag[0]) continue;
      if (closing) {
        if (hv_streq_ci(tag, "form")) {
          current_form_action[0] = '\0';
          current_form_method = HTML_FORM_METHOD_GET;
        } else if (hv_streq_ci(tag, "ol")) {
          list_ordered = 0;
          list_num = 1;
        }
        continue;
      }
      if (!self_closing && hv_tag_is_void(tag)) self_closing = 1;

      /* Collect <style> block into doc->style_text for CSS processing */
      if (hv_streq_ci(tag, "style")) {
        if (!self_closing) {
          size_t used = kstrlen(doc->style_text);
          size_t cap = HTML_STYLE_BUF_MAX - 1;
          size_t end = pos;
          while (end < len) {
            if (html[end] == '<') {
              size_t j = end + 1;
              if (j < len && html[j] == '/') j++;
              size_t k = j;
              while (k < len && html[k] != '>') k++;
              char inner[12];
              size_t copy = k - j;
              if (copy >= sizeof(inner)) copy = sizeof(inner) - 1;
              size_t m;
              for (m = 0; m < copy; m++)
                inner[m] = (char)(html[j + m] | 32);
              inner[m] = '\0';
              if (kstreq(inner, "style")) { pos = k + 1; break; }
            }
            if (used < cap) doc->style_text[used++] = html[end];
            end++;
          }
          doc->style_text[used] = '\0';
          if (pos == end) pos = end;
        }
        continue;
      }
      /* <link rel="stylesheet" href="..."> — queue for external CSS fetch */
      if (hv_streq_ci(tag, "link")) {
        char rel[32];
        char link_href[HTML_URL_MAX];
        rel[0] = '\0'; link_href[0] = '\0';
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "rel", rel, sizeof(rel));
        hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                              "href", link_href, sizeof(link_href));
        if (link_href[0] && doc->css_count < HTML_MAX_PENDING_CSS) {
          char rel_lower[32];
          size_t ri;
          for (ri = 0; ri < sizeof(rel_lower) - 1 && rel[ri]; ri++)
            rel_lower[ri] = (char)(rel[ri] | 32);
          rel_lower[ri] = '\0';
          if (kstreq(rel_lower, "stylesheet"))
            kstrcpy(doc->pending_css[doc->css_count++], HTML_URL_MAX, link_href);
        }
        continue;
      }
      if (hv_streq_ci(tag, "script") ||
          hv_streq_ci(tag, "svg") || hv_streq_ci(tag, "template") ||
          hv_streq_ci(tag, "object") || hv_streq_ci(tag, "embed") ||
          hv_streq_ci(tag, "iframe")) {
        if (!self_closing) pos = hv_skip_block(html, len, pos, tag);
        continue;
      }
      if (hv_streq_ci(tag, "form")) {
        method_attr[0] = '\0';
        if (!hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                   "action", current_form_action,
                                   sizeof(current_form_action))) {
          current_form_action[0] = '\0';
        }
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "method", method_attr, sizeof(method_attr));
        current_form_method = hv_parse_form_method(method_attr);
        continue;
      }
      if (hv_streq_ci(tag, "title")) {
        pos = hv_collect_text_until_tag(html, len, pos, tag,
                                        doc->title, sizeof(doc->title));
        continue;
      }
      if (hv_streq_ci(tag, "hr")) {
        struct html_node *node = html_push_node(doc);
        if (node) {
          node->type = HTML_NODE_TAG_HR;
          hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        }
        continue;
      }
      if (hv_streq_ci(tag, "br")) {
        struct html_node *node = html_push_node(doc);
        if (node) node->type = HTML_NODE_TAG_BR;
        continue;
      }
      if (hv_streq_ci(tag, "ol")) { list_ordered = 1; list_num = 1; continue; }
      if (hv_streq_ci(tag, "html") || hv_streq_ci(tag, "body") ||
          hv_streq_ci(tag, "head") || hv_streq_ci(tag, "main") ||
          hv_streq_ci(tag, "header") || hv_streq_ci(tag, "footer") ||
          hv_streq_ci(tag, "nav") || hv_streq_ci(tag, "aside") ||
          hv_streq_ci(tag, "section") || hv_streq_ci(tag, "article") ||
          hv_streq_ci(tag, "figure") || hv_streq_ci(tag, "picture") ||
          hv_streq_ci(tag, "ul") || hv_streq_ci(tag, "menu") ||
          hv_streq_ci(tag, "table") || hv_streq_ci(tag, "tbody") ||
          hv_streq_ci(tag, "thead") || hv_streq_ci(tag, "tfoot") ||
          hv_streq_ci(tag, "colgroup") || hv_streq_ci(tag, "col") ||
          hv_streq_ci(tag, "caption") ||
          hv_streq_ci(tag, "div") || hv_streq_ci(tag, "span") ||
          hv_streq_ci(tag, "map") || hv_streq_ci(tag, "area")) {
        continue;
      }
      /* Table row separator */
      if (hv_streq_ci(tag, "tr")) {
        struct html_node *node = html_push_node(doc);
        if (node) {
          node->type = HTML_NODE_TAG_TR;
          hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        }
        continue;
      }
      /* Table cells */
      if (hv_streq_ci(tag, "td") || hv_streq_ci(tag, "th")) {
        char cell_text[HTML_TEXT_MAX];
        struct html_node *node;
        cell_text[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, cell_text,
                                        sizeof(cell_text));
        if (!cell_text[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_TD;
        node->bold = hv_streq_ci(tag, "th") ? 1 : 0;
        kstrcpy(node->text, sizeof(node->text), cell_text);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Sub-headings h4-h6 */
      if (hv_streq_ci(tag, "h4") || hv_streq_ci(tag, "h5") ||
          hv_streq_ci(tag, "h6")) {
        char htext[HTML_TEXT_MAX];
        struct html_node *node;
        htext[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, htext,
                                        sizeof(htext));
        if (!htext[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = hv_streq_ci(tag, "h4") ? HTML_NODE_TAG_H4 :
                     hv_streq_ci(tag, "h5") ? HTML_NODE_TAG_H5 :
                                              HTML_NODE_TAG_H6;
        kstrcpy(node->text, sizeof(node->text), htext);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Preformatted text and inline code */
      if (hv_streq_ci(tag, "pre") || hv_streq_ci(tag, "code") ||
          hv_streq_ci(tag, "samp") || hv_streq_ci(tag, "kbd")) {
        char ptext[HTML_TEXT_MAX];
        struct html_node *node;
        ptext[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, ptext,
                                        sizeof(ptext));
        if (!ptext[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = hv_streq_ci(tag, "pre") ? HTML_NODE_TAG_PRE
                                              : HTML_NODE_TAG_CODE;
        kstrcpy(node->text, sizeof(node->text), ptext);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Blockquote */
      if (hv_streq_ci(tag, "blockquote")) {
        char btext[HTML_TEXT_MAX];
        struct html_node *node;
        btext[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, btext,
                                        sizeof(btext));
        if (!btext[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_BLOCKQUOTE;
        node->indent = 1;
        kstrcpy(node->text, sizeof(node->text), btext);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Inline emphasis: strong, b, em, i, mark, abbr, cite, small, s, del */
      if (hv_streq_ci(tag, "strong") || hv_streq_ci(tag, "b") ||
          hv_streq_ci(tag, "em") || hv_streq_ci(tag, "i") ||
          hv_streq_ci(tag, "mark") || hv_streq_ci(tag, "abbr") ||
          hv_streq_ci(tag, "cite") || hv_streq_ci(tag, "small") ||
          hv_streq_ci(tag, "s") || hv_streq_ci(tag, "del") ||
          hv_streq_ci(tag, "ins") || hv_streq_ci(tag, "u")) {
        char itext[HTML_TEXT_MAX];
        struct html_node *node;
        itext[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, itext,
                                        sizeof(itext));
        if (!itext[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = hv_streq_ci(tag, "mark") ? HTML_NODE_TAG_MARK
                                               : HTML_NODE_TEXT;
        node->bold = (hv_streq_ci(tag, "strong") || hv_streq_ci(tag, "b")) ? 1
                                                                             : 0;
        kstrcpy(node->text, sizeof(node->text), itext);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Figcaption */
      if (hv_streq_ci(tag, "figcaption")) {
        char ftext[HTML_TEXT_MAX];
        struct html_node *node;
        ftext[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, ftext,
                                        sizeof(ftext));
        if (!ftext[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_FIGCAPTION;
        kstrcpy(node->text, sizeof(node->text), ftext);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Details/summary collapsible */
      if (hv_streq_ci(tag, "details") || hv_streq_ci(tag, "summary")) {
        char dtext[HTML_TEXT_MAX];
        struct html_node *node;
        dtext[0] = '\0';
        pos = hv_collect_text_until_tag(html, len, pos, tag, dtext,
                                        sizeof(dtext));
        if (!dtext[0]) continue;
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_DETAILS;
        kstrcpy(node->text, sizeof(node->text), dtext);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }
      /* Video and audio: skip block content, show placeholder */
      if (hv_streq_ci(tag, "video") || hv_streq_ci(tag, "audio")) {
        struct html_node *node;
        if (!self_closing) pos = hv_skip_block(html, len, pos, tag);
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_MEDIA;
        kstrcpy(node->text, sizeof(node->text),
                hv_streq_ci(tag, "video") ? "[video]" : "[audio]");
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
        continue;
      }

      href[0] = '\0';
      src[0] = '\0';
      text[0] = '\0';
      name[0] = '\0';
      type_attr[0] = '\0';
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "href", href, sizeof(href));
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "src", src, sizeof(src));
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "name", name, sizeof(name));
      (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                  "type", type_attr, sizeof(type_attr));

      if (hv_streq_ci(tag, "input")) {
        struct html_node *node;
        uint8_t input_type = hv_form_input_type(type_attr);
        (void)hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                    "value", text, sizeof(text));
        node = html_push_node(doc);
        if (!node) break;
        node->type = (input_type == HTML_INPUT_TYPE_SUBMIT ||
                      input_type == HTML_INPUT_TYPE_BUTTON)
                         ? HTML_NODE_TAG_BUTTON
                         : HTML_NODE_TAG_INPUT;
        node->input_type = input_type;
        node->hidden = (input_type == HTML_INPUT_TYPE_HIDDEN);
        node->form_method = current_form_method;
        if (node->type == HTML_NODE_TAG_BUTTON && !text[0]) {
          kstrcpy(text, sizeof(text), "Submit");
        }
        kstrcpy(node->text, sizeof(node->text), text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        continue;
      } else if (hv_streq_ci(tag, "textarea")) {
        struct html_node *node;
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_INPUT;
        node->input_type = HTML_INPUT_TYPE_TEXTAREA;
        node->hidden = 0;
        node->form_method = current_form_method;
        kstrcpy(node->text, sizeof(node->text), text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        continue;
      } else if (hv_streq_ci(tag, "button")) {
        struct html_node *node;
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
        node = html_push_node(doc);
        if (!node) break;
        node->type = HTML_NODE_TAG_BUTTON;
        node->input_type = hv_streq_ci(type_attr, "button")
                               ? HTML_INPUT_TYPE_BUTTON
                               : HTML_INPUT_TYPE_SUBMIT;
        node->form_method = current_form_method;
        if (!text[0]) kstrcpy(text, sizeof(text), "Submit");
        kstrcpy(node->text, sizeof(node->text), text);
        kstrcpy(node->name, sizeof(node->name), name);
        kstrcpy(node->href, sizeof(node->href), current_form_action);
        continue;
      } else if (hv_streq_ci(tag, "img")) {
        if (!hv_extract_attr_value(html + attr_start, tag_end - attr_start,
                                   "alt", text, sizeof(text))) {
          kstrcpy(text, sizeof(text), src);
        }
        if (src[0] && !href[0]) kstrcpy(href, sizeof(href), src);
      } else if (!self_closing) {
        pos = hv_collect_text_until_tag(html, len, pos, tag, text, sizeof(text));
      }

      if (text[0] || href[0] || src[0]) {
        struct html_node *node = html_push_node(doc);
        if (!node) break;
        if (hv_streq_ci(tag, "h1")) node->type = HTML_NODE_TAG_H1;
        else if (hv_streq_ci(tag, "h2")) node->type = HTML_NODE_TAG_H2;
        else if (hv_streq_ci(tag, "h3")) node->type = HTML_NODE_TAG_H3;
        else if (hv_streq_ci(tag, "p")) node->type = HTML_NODE_TAG_P;
        else if (hv_streq_ci(tag, "a")) node->type = HTML_NODE_TAG_A;
        else if (hv_streq_ci(tag, "div")) node->type = HTML_NODE_TAG_DIV;
        else if (hv_streq_ci(tag, "span")) node->type = HTML_NODE_TAG_SPAN;
        else if (hv_streq_ci(tag, "ul")) node->type = HTML_NODE_TAG_UL;
        else if (hv_streq_ci(tag, "li")) {
          node->type = HTML_NODE_TAG_LI;
          if (list_ordered) {
            char prefix[16];
            char new_text[HTML_TEXT_MAX];
            prefix[0] = '\0';
            kbuf_append_u32(prefix, sizeof(prefix), (uint32_t)list_num++);
            kbuf_append(prefix, sizeof(prefix), ". ");
            kstrcpy(new_text, sizeof(new_text), prefix);
            kbuf_append(new_text, sizeof(new_text), text);
            kstrcpy(text, sizeof(text), new_text);
          }
        }
        else if (hv_streq_ci(tag, "img")) node->type = HTML_NODE_TAG_IMG;
        else if (hv_streq_ci(tag, "noscript")) node->type = HTML_NODE_TAG_DIV;
        else node->type = HTML_NODE_TEXT;
        if (node->type == HTML_NODE_TAG_A) node->color = 0x89B4FA;
        kstrcpy(node->text, sizeof(node->text), text[0] ? text : href);
        kstrcpy(node->href, sizeof(node->href), href);
        hv_apply_node_attrs(node, html + attr_start, tag_end - attr_start);
      }
      continue;
    }

    {
      struct html_node *node = html_push_node(doc);
      size_t out_pos = 0;
      int last_space = 1;
      if (!node) break;
      while (pos < len && html[pos] != '<') {
        if (html[pos] == '&') {
          size_t consumed = 0;
          char decoded = 0;
          if (hv_decode_entity_value(html + pos, len - pos, &consumed, &decoded)) {
            hv_text_append_char(node->text, sizeof(node->text), &out_pos, decoded, &last_space);
            pos += consumed;
            continue;
          }
        }
        hv_text_append_char(node->text, sizeof(node->text), &out_pos, html[pos], &last_space);
        pos++;
      }
      hv_trim_text(node->text);
      if (!node->text[0]) doc->node_count--;
    }
  }
  if (!doc->title[0]) {
    for (int i = 0; i < doc->node_count; i++) {
      if (doc->nodes[i].type == HTML_NODE_TAG_H1 && doc->nodes[i].text[0]) {
        kstrcpy(doc->title, sizeof(doc->title), doc->nodes[i].text);
        break;
      }
    }
  }
  /* Fill col_index / col_count on TD nodes for side-by-side table rendering */
  {
    int i;
    for (i = 0; i < doc->node_count; i++) {
      if (doc->nodes[i].type == HTML_NODE_TAG_TR) {
        int first_td = i + 1;
        int ncols = 0;
        int j;
        for (j = first_td; j < doc->node_count; j++) {
          if (doc->nodes[j].type == HTML_NODE_TAG_TD) ncols++;
          else break;
        }
        if (ncols > 255) ncols = 255;
        for (j = 0; j < ncols; j++) {
          doc->nodes[first_td + j].col_index = (uint8_t)j;
          doc->nodes[first_td + j].col_count = (uint8_t)ncols;
        }
      }
    }
  }
  /* Apply collected <style> block to all nodes */
  if (doc->style_text[0]) {
    struct css_stylesheet sheet;
    if (css_parse(doc->style_text, kstrlen(doc->style_text), &sheet) == 0)
      css_apply_to_doc(&sheet, doc);
  }
  return 0;
}

void html_viewer_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();

  if (g_viewer_open && g_viewer.window) {
    compositor_show_window(g_viewer.window->id);
    compositor_focus_window(g_viewer.window->id);
    return;
  }

  html_viewer_cleanup();
  kmemzero(&g_viewer, sizeof(g_viewer));
  g_viewer.focused_node_index = -1;

  g_viewer.window = compositor_create_window("CapyBrowser", 60, 40,
                                             640 + 160 * (scale - 1),
                                             480 + 140 * (scale - 1));
  if (!g_viewer.window) return;
  g_viewer.window->bg_color = theme->window_bg;
  g_viewer.window->border_color = theme->window_border;
  g_viewer.window->user_data = &g_viewer;
  g_viewer.window->on_paint = html_viewer_window_paint;
  g_viewer.window->on_scroll = html_viewer_window_scroll;
  g_viewer.window->on_key = html_viewer_window_key;
  g_viewer.window->on_mouse = html_viewer_window_mouse;
  g_viewer.window->on_close = html_viewer_on_close;
  compositor_show_window(g_viewer.window->id);
  compositor_focus_window(g_viewer.window->id);
  g_viewer_open = 1;
  html_viewer_load_builtin(&g_viewer, "about:home");
}

static int html_viewer_issue_request(struct html_viewer_app *app, const char *url,
                                     enum http_method method,
                                     const uint8_t *body, size_t body_len,
                                     struct http_request *req,
                                     struct http_response *resp) {
  char cookie_header[1024];
  if (!app || !url || !req || !resp) return -1;
  kmemzero(req, sizeof(*req));
  req->method = method;
  req->body = body;
  req->body_len = body_len;
  req->timeout_ms = 15000;
  if (http_parse_url(url, req->host, sizeof(req->host), req->path, sizeof(req->path),
                     &req->port, &req->use_tls) != 0) {
    return -1;
  }
  if (hv_build_cookie_header(app, req->host, req->path, req->use_tls,
                             cookie_header, sizeof(cookie_header)) &&
      req->header_count < HTTP_MAX_HEADERS) {
    kstrcpy(req->headers[req->header_count].name,
            sizeof(req->headers[req->header_count].name), "Cookie");
    kstrcpy(req->headers[req->header_count].value,
            sizeof(req->headers[req->header_count].value), cookie_header);
    req->header_count++;
  }
  if (body && body_len > 0 && req->header_count < HTTP_MAX_HEADERS) {
    kstrcpy(req->headers[req->header_count].name,
            sizeof(req->headers[req->header_count].name), "Content-Type");
    kstrcpy(req->headers[req->header_count].value,
            sizeof(req->headers[req->header_count].value),
            "application/x-www-form-urlencoded");
    req->header_count++;
  }
  return http_request(req, resp);
}

static void hv_form_append_pair(char *out, size_t out_len,
                                const char *name, const char *value,
                                int *wrote_any) {
  if (!out || out_len == 0 || !name || !name[0] || !wrote_any) return;
  if (*wrote_any) kbuf_append(out, out_len, "&");
  hv_urlencode_append(out, out_len, name);
  kbuf_append(out, out_len, "=");
  hv_urlencode_append(out, out_len, value ? value : "");
  *wrote_any = 1;
}

static int html_viewer_build_form_payload(struct html_viewer_app *app,
                                          const struct html_node *submit_node,
                                          char *payload, size_t payload_len) {
  int wrote_any = 0;
  if (!app || !submit_node || !payload || payload_len == 0) return 0;
  payload[0] = '\0';
  for (int i = 0; i < app->doc.node_count; i++) {
    const struct html_node *node = &app->doc.nodes[i];
    if (!hv_form_action_matches(node, submit_node) ||
        node->form_method != submit_node->form_method) {
      continue;
    }
    if (node->type == HTML_NODE_TAG_INPUT) {
      hv_form_append_pair(payload, payload_len, node->name, node->text, &wrote_any);
    } else if (node->type == HTML_NODE_TAG_BUTTON && node == submit_node) {
      hv_form_append_pair(payload, payload_len, node->name, node->text, &wrote_any);
    }
  }
  return wrote_any;
}

static void html_viewer_submit_form(struct html_viewer_app *app, int node_index) {
  char action[HTML_URL_MAX];
  char resolved_action[HTML_URL_MAX];
  char payload[HTML_URL_MAX];
  char target[HTML_URL_MAX];
  struct html_node *node;
  int has_payload;
  if (!app || node_index < 0 || node_index >= app->doc.node_count) return;
  node = &app->doc.nodes[node_index];
  if (node->type != HTML_NODE_TAG_INPUT && node->type != HTML_NODE_TAG_BUTTON) return;
  if (node->type == HTML_NODE_TAG_BUTTON &&
      node->input_type == HTML_INPUT_TYPE_BUTTON) {
    return;
  }

  if (node->href[0]) kstrcpy(action, sizeof(action), node->href);
  else kstrcpy(action, sizeof(action), app->url);

  if (hv_resolve_url(app->url, action, resolved_action, sizeof(resolved_action)) != 0) {
    kstrcpy(resolved_action, sizeof(resolved_action), action);
  }

  has_payload = html_viewer_build_form_payload(app, node, payload, sizeof(payload));
  if (node->form_method == HTML_FORM_METHOD_POST) {
    html_viewer_request_internal(app, resolved_action, HTTP_POST,
                                 has_payload ? (const uint8_t *)payload : NULL,
                                 has_payload ? kstrlen(payload) : 0, 0);
    return;
  }

  kstrcpy(target, sizeof(target), resolved_action);
  if (has_payload) {
    kbuf_append(target, sizeof(target),
                hv_contains_ci(resolved_action, "?") ? "&" : "?");
    kbuf_append(target, sizeof(target), payload);
  }
  html_viewer_request_internal(app, target, HTTP_GET, NULL, 0, 0);
}

static void html_viewer_capture_cookies(struct html_viewer_app *app,
                                        const struct http_request *req,
                                        const struct http_response *resp) {
  if (!app || !req || !resp) return;
  for (uint32_t i = 0; i < resp->header_count; i++) {
    if (hv_streq_ci(resp->headers[i].name, "Set-Cookie")) {
      hv_store_cookie_from_header(app, req->host, req->path,
                                  req->use_tls, resp->headers[i].value);
    }
  }
}

static void html_viewer_load_text_document(struct html_viewer_app *app,
                                           const char *title,
                                           const char *text,
                                           size_t len,
                                           uint32_t color) {
  size_t pos = 0;
  if (!app) return;
  kmemzero(&app->doc, sizeof(app->doc));
  kstrcpy(app->doc.title, sizeof(app->doc.title),
          title && title[0] ? title : "CapyBrowser");
  while (pos < len && app->doc.node_count < HTML_MAX_NODES) {
    struct html_node *node = html_push_node(&app->doc);
    size_t out_pos = 0;
    int last_space = 1;
    if (!node) break;
    node->type = HTML_NODE_TEXT;
    node->color = color;
    while (pos < len && text[pos] != '\n') {
      hv_text_append_char(node->text, sizeof(node->text), &out_pos, text[pos], &last_space);
      pos++;
    }
    hv_trim_text(node->text);
    if (!node->text[0]) app->doc.node_count--;
    if (text[pos] == '\n' && app->doc.node_count < HTML_MAX_NODES) {
      struct html_node *br = html_push_node(&app->doc);
      if (br) br->type = HTML_NODE_TAG_BR;
      pos++;
    }
  }
  if (app->doc.node_count == 0) html_viewer_set_error(app, title, "Empty response.");
}

static void html_viewer_apply_response(struct html_viewer_app *app,
                                       const struct http_request *req,
                                       const struct http_response *resp) {
  const char *content_type = html_viewer_find_header(resp, "Content-Type");
  if (!app || !req || !resp) return;
  html_viewer_capture_cookies(app, req, resp);
  if (resp->body && resp->body_len > 0 &&
      (hv_content_type_is_html(content_type) ||
       hv_body_looks_html(resp->body, resp->body_len))) {
    hv_img_cache_clear();
    hv_http_cache_tick();
    html_parse((const char *)resp->body, resp->body_len, &app->doc);
    if (app->doc.node_count == 0) {
      html_viewer_load_text_document(app, "Document",
                                     (const char *)resp->body, resp->body_len,
                                     0xCDD6F4);
    }
    if (app->doc.title[0] && app->window)
      compositor_set_title(app->window->id, app->doc.title);
    hv_history_push(app->url);
    hv_fetch_external_css(app);
    hv_fetch_page_images(app);
    return;
  }
  if (resp->body && resp->body_len > 0 &&
      (hv_content_type_is_textual(content_type) ||
       hv_body_looks_textual(resp->body, resp->body_len))) {
    html_viewer_load_text_document(app,
                                   content_type ? content_type : "Document",
                                   (const char *)resp->body, resp->body_len,
                                   0xCDD6F4);
    return;
  }
  if (resp->body && resp->body_len > 0) {
    html_viewer_set_error(app, "Navigation Error",
                          "Unsupported content type for the built-in renderer.");
    return;
  }
  if (resp->status_code != 200) {
    html_viewer_set_status_error(app, resp->status_code);
    return;
  }
  html_viewer_set_transport_error(app);
}

static void html_viewer_request_internal(struct html_viewer_app *app,
                                         const char *url,
                                         enum http_method method,
                                         const uint8_t *body,
                                         size_t body_len,
                                         int depth) {
  char normalized_url[HTML_URL_MAX];
  char redirect_url[HTML_URL_MAX];
  const char *target = url;
  const char *location = NULL;
  int rc = -1;
  struct http_request req;
  struct http_response resp;
  if (!app || !url) return;
  if (!url[0]) {
    html_viewer_load_builtin(app, "about:home");
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_strncmp(url, "about:home", 10) == 0 ||
      hv_strncmp(url, "about:version", 13) == 0 ||
      hv_strncmp(url, "about:blank", 11) == 0) {
    html_viewer_load_builtin(app, url);
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_strncmp(url, "about:", 6) == 0) {
    kstrcpy(app->url, sizeof(app->url), url);
    html_viewer_set_error(app, "Navigation Error", "Unknown about page.");
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_has_scheme(url) &&
      hv_strncmp(url, "http://", 7) != 0 &&
      hv_strncmp(url, "https://", 8) != 0) {
    kstrcpy(app->url, sizeof(app->url), url);
    html_viewer_set_error(app, "Navigation Error", "Unsupported URL scheme.");
    if (app->window) compositor_invalidate(app->window->id);
    return;
  }
  if (hv_strncmp(url, "http://", 7) != 0 &&
      hv_strncmp(url, "https://", 8) != 0) {
    kstrcpy(normalized_url, sizeof(normalized_url), "https://");
    kbuf_append(normalized_url, sizeof(normalized_url), url);
    target = normalized_url;
  }

  kstrcpy(app->url, sizeof(app->url), target);
  app->scroll_offset = 0;
  app->focused_node_index = -1;
  app->loading = 1;
  /* Force loading state onto screen before the blocking network call. */
  if (app->window) {
    html_viewer_paint(app);
    compositor_invalidate(app->window->id);
    compositor_render();
  }
  kmemzero(&resp, sizeof(resp));
  rc = html_viewer_issue_request(app, target, method, body, body_len, &req, &resp);
  if (rc != 0) {
    html_viewer_set_transport_error(app);
  } else if (resp.status_code >= 300 && resp.status_code < 400) {
    int redirect_method = (resp.status_code == 303 || method != HTTP_GET)
                              ? HTTP_GET
                              : method;
    const uint8_t *redirect_body = redirect_method == HTTP_GET ? NULL : body;
    size_t redirect_body_len = redirect_method == HTTP_GET ? 0 : body_len;
    html_viewer_capture_cookies(app, &req, &resp);
    location = html_viewer_find_header(&resp, "Location");
    if (location && depth >= 4) {
      html_viewer_set_error(app, "Navigation Error", "Too many redirects.");
    } else if (location &&
               hv_resolve_url(target, location, redirect_url, sizeof(redirect_url)) == 0 &&
               !kstreq(redirect_url, target)) {
      http_response_free(&resp);
      html_viewer_request_internal(app, redirect_url, redirect_method,
                                   redirect_body, redirect_body_len, depth + 1);
      return;
    } else {
      html_viewer_apply_response(app, &req, &resp);
    }
  } else {
    html_viewer_apply_response(app, &req, &resp);
  }

  if (app->doc.title[0]) {
    if (app->window) compositor_set_title(app->window->id, app->doc.title);
  } else if (app->window) {
    compositor_set_title(app->window->id, app->url);
  }
  http_response_free(&resp);
  app->loading = 0;
  if (app->window) compositor_invalidate(app->window->id);
}

static void hv_fetch_external_css(struct html_viewer_app *app) {
  int i;
  if (!app || !app->doc.css_count) return;
  for (i = 0; i < app->doc.css_count; i++) {
    char abs_url[HTML_URL_MAX];
    struct css_stylesheet sheet;
    struct hv_http_cache_entry *cached;
    if (hv_resolve_url(app->url, app->doc.pending_css[i], abs_url, sizeof(abs_url)) != 0)
      kstrcpy(abs_url, sizeof(abs_url), app->doc.pending_css[i]);
    cached = hv_http_cache_find(abs_url);
    if (cached) {
      kmemzero(&sheet, sizeof(sheet));
      if (css_parse((const char *)cached->body, cached->body_len, &sheet) == 0)
        css_apply_to_doc(&sheet, &app->doc);
    } else {
      struct http_request req;
      struct http_response resp;
      kmemzero(&resp, sizeof(resp));
      if (html_viewer_issue_request(app, abs_url, HTTP_GET, NULL, 0, &req, &resp) == 0) {
        if (resp.body && resp.body_len > 0) {
          hv_http_cache_store(abs_url, &resp);
          kmemzero(&sheet, sizeof(sheet));
          if (css_parse((const char *)resp.body, resp.body_len, &sheet) == 0)
            css_apply_to_doc(&sheet, &app->doc);
        }
      }
      http_response_free(&resp);
    }
  }
}

static void hv_fetch_page_images(struct html_viewer_app *app) {
  int i;
  if (!app) return;
  for (i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    char abs_url[HTML_URL_MAX];
    struct hv_img_cache_entry *slot;
    struct png_image img;
    const uint8_t *body = NULL;
    size_t body_len = 0;
    struct http_response resp;
    struct hv_http_cache_entry *cached;
    if (node->type != HTML_NODE_TAG_IMG || !node->href[0]) continue;
    if (hv_resolve_url(app->url, node->href, abs_url, sizeof(abs_url)) != 0)
      kstrcpy(abs_url, sizeof(abs_url), node->href);
    kstrcpy(node->href, sizeof(node->href), abs_url);
    if (hv_img_cache_find(abs_url)) continue;
    cached = hv_http_cache_find(abs_url);
    if (cached) {
      body = cached->body;
      body_len = cached->body_len;
    } else {
      struct http_request req;
      kmemzero(&resp, sizeof(resp));
      if (html_viewer_issue_request(app, abs_url, HTTP_GET, NULL, 0, &req, &resp) != 0 ||
          !resp.body || resp.body_len < 8) {
        http_response_free(&resp);
        continue;
      }
      hv_http_cache_store(abs_url, &resp);
      body = resp.body;
      body_len = resp.body_len;
    }
    kmemzero(&img, sizeof(img));
    if (body_len >= 8 && png_decode(body, body_len, &img) == 0 &&
        img.pixels && img.width > 0 && img.height > 0) {
      slot = hv_img_cache_slot();
      kstrcpy(slot->url, sizeof(slot->url), abs_url);
      slot->pixels = img.pixels;
      slot->width  = img.width;
      slot->height = img.height;
    }
    if (!cached) http_response_free(&resp);
  }
}

void html_viewer_navigate(struct html_viewer_app *app, const char *url) {
  html_viewer_request_internal(app, url, HTTP_GET, NULL, 0, 0);
}

void html_viewer_paint(struct html_viewer_app *app) {
  struct gui_surface *s = NULL;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  int32_t y = 28 - app->scroll_offset;
  if (!app || !app->window || !f || !theme) return;
  s = &app->window->surface;

  for (uint32_t py = 0; py < s->height; py++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
    for (uint32_t px = 0; px < s->width; px++) line[px] = theme->window_bg;
  }
  /* Toolbar: 24px bar, back/forward buttons on the left, URL after */
  {
    int can_back = (hv_history_cur > 0);
    int can_fwd  = (hv_history_cur < hv_history_count - 1);
    uint32_t btn_color = theme->accent_alt;
    uint32_t url_bg    = app->url_editing ? theme->terminal_bg : theme->accent_alt;
    for (uint32_t py = 0; py < 24 && py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < 50; px++) row[px] = btn_color;
      for (uint32_t px = 50; px < s->width; px++) row[px] = url_bg;
    }
    /* Draw separator line between buttons and URL area */
    for (uint32_t py = 2; py < 22 && py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      row[49] = theme->window_border;
    }
    /* Back button: "<" glyph */
    font_draw_char(s, f, 4, 4, '<',
                   can_back ? theme->accent : theme->text_muted);
    font_draw_char(s, f, 4 + (int32_t)f->glyph_width, 4, ' ', 0);
    /* Forward button: ">" glyph */
    font_draw_char(s, f, 26, 4, '>',
                   can_fwd ? theme->accent : theme->text_muted);
    /* URL text */
    font_draw_string(s, f, 54, 4, app->url,
                     app->url_editing ? theme->text : theme->text_muted);
    if (app->url_editing) {
      int32_t cx = 54 + app->url_cursor * (int32_t)f->glyph_width;
      for (uint32_t cy = 4; cy < 4 + f->glyph_height && cy < 24; cy++) {
        if ((uint32_t)cx < s->width) {
          uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + cy * s->pitch);
          row[(uint32_t)cx] = theme->accent;
        }
      }
    }
  }

  for (int i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    y = html_viewer_render_node(s, f, theme, node, y, 1);
    if (y > (int32_t)s->height + app->scroll_offset + 32) break;
  }
  app->content_height = y + 8;

  /* Scrollbar: 4px strip on the right edge */
  if (app->content_height > (int32_t)s->height && s->width > 6) {
    int viewport = (int)s->height - 28;
    int total = app->content_height;
    if (total > 0 && viewport > 0) {
      uint32_t sb_x = s->width - 4;
      uint32_t sb_top = 28;
      uint32_t sb_h = (uint32_t)s->height - 28;
      uint32_t thumb_h = (uint32_t)((int64_t)sb_h * viewport / total);
      uint32_t thumb_y = sb_top + (uint32_t)((int64_t)sb_h * app->scroll_offset / total);
      if (thumb_h < 4) thumb_h = 4;
      if (thumb_y + thumb_h > sb_top + sb_h) thumb_y = sb_top + sb_h - thumb_h;
      for (uint32_t sy = sb_top; sy < sb_top + sb_h && sy < s->height; sy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + sy * s->pitch);
        uint32_t color = (sy >= thumb_y && sy < thumb_y + thumb_h)
                         ? theme->accent : theme->window_border;
        for (uint32_t sx = sb_x; sx < sb_x + 4 && sx < s->width; sx++)
          row[sx] = color;
      }
    }
  }

  if (app->loading) {
    /* Draw a full-width loading bar at the bottom of the viewport. */
    uint32_t bar_h = f->glyph_height + 8;
    uint32_t bar_y = s->height > bar_h ? s->height - bar_h : 0;
    for (uint32_t py = bar_y; py < s->height; py++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
      for (uint32_t px = 0; px < s->width; px++) row[px] = theme->accent_alt;
    }
    char status_line[HTTP_MAX_HOST + 24];
    status_line[0] = '\0';
    kstrcpy(status_line, sizeof(status_line), "Carregando: ");
    kbuf_append(status_line, sizeof(status_line), app->url);
    font_draw_string(s, f, 4, (int32_t)(bar_y + 4), status_line, theme->accent_text);
  }
}

void html_viewer_scroll(struct html_viewer_app *app, int delta) {
  int max_scroll = 0;
  if (!app) return;
  app->scroll_offset += delta * 20;
  if (app->window) {
    int viewport = (int)app->window->surface.height - 32;
    max_scroll = app->content_height - viewport;
    if (max_scroll < 0) max_scroll = 0;
  }
  if (app->scroll_offset < 0) app->scroll_offset = 0;
  if (app->scroll_offset > max_scroll) app->scroll_offset = max_scroll;
  if (app->window) compositor_invalidate(app->window->id);
}
