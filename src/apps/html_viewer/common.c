#include "internal/html_viewer_internal.h"

struct html_viewer_app g_viewer;
int g_viewer_open = 0;
volatile uint32_t g_hv_parse_lock = 0;
struct hv_http_cache_entry hv_http_cache[HV_HTTP_CACHE_MAX];
struct hv_http_cache_stats hv_http_cache_stats;
char hv_history[HV_HISTORY_MAX][HTML_URL_MAX];
int hv_history_count = 0;
int hv_history_cur = -1;
int hv_navigating_history = 0;
char hv_bookmark_url[HV_BOOKMARK_MAX][HTML_URL_MAX];
char hv_bookmark_title[HV_BOOKMARK_MAX][HTML_TITLE_MAX];
int hv_bookmark_count = 0;
int32_t hv_table_row_y = 28;
int32_t hv_table_row_h = 24;

#ifndef UNIT_TEST
struct hv_browser_job g_hv_browser_job;
int g_hv_browser_worker_bootstrapped = 0;
int g_hv_browser_worker_pool = -1;
uint32_t g_hv_browser_ticket = 0;
uint32_t g_hv_browser_active_ticket = 0;
int g_hv_browser_followup_pending = 0;
enum http_method g_hv_browser_followup_method = HTTP_GET;
char g_hv_browser_followup_url[HTML_URL_MAX];
uint8_t g_hv_browser_followup_body[HTML_URL_MAX];
size_t g_hv_browser_followup_body_len = 0;
#endif

void hv_doc_release_assets(struct html_document *doc) {
  if (!doc) return;
  for (int i = 0; i < doc->node_count; i++) {
    if (doc->nodes[i].image_pixels) {
      kfree(doc->nodes[i].image_pixels);
      doc->nodes[i].image_pixels = NULL;
    }
    doc->nodes[i].image_width = 0;
    doc->nodes[i].image_height = 0;
  }
}

void hv_doc_reset(struct html_document *doc) {
  if (!doc) return;
  hv_doc_release_assets(doc);
  kmemzero(doc, sizeof(*doc));
}

static void hv_http_cache_release_entry(int idx, int evicted, int expired) {
  if (idx < 0 || idx >= HV_HTTP_CACHE_MAX || !hv_http_cache[idx].body) return;
  if (hv_http_cache_stats.total_bytes >= hv_http_cache[idx].body_len) {
    hv_http_cache_stats.total_bytes -= hv_http_cache[idx].body_len;
  } else {
    hv_http_cache_stats.total_bytes = 0;
  }
  if (hv_http_cache_stats.entries > 0) hv_http_cache_stats.entries--;
  if (evicted) hv_http_cache_stats.evictions++;
  if (expired) hv_http_cache_stats.expired++;
  kfree(hv_http_cache[idx].body);
  hv_http_cache[idx].body = NULL;
  hv_http_cache[idx].url[0] = '\0';
  hv_http_cache[idx].body_len = 0;
  hv_http_cache[idx].content_type[0] = '\0';
  hv_http_cache[idx].max_age = 0;
  hv_http_cache[idx].age = 0;
}

void hv_http_cache_clear(void) {
  for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
    hv_http_cache_release_entry(i, 0, 0);
  }
  kmemzero(&hv_http_cache_stats, sizeof(hv_http_cache_stats));
}

struct hv_http_cache_entry *hv_http_cache_find(const char *url) {
  if (!url || !url[0]) return NULL;
  for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
    if (hv_http_cache[i].body && kstreq(hv_http_cache[i].url, url)) {
      hv_http_cache[i].age = 0;
      hv_http_cache_stats.hits++;
      return &hv_http_cache[i];
    }
  }
  hv_http_cache_stats.misses++;
  return NULL;
}

void hv_http_cache_tick(void) {
  for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
    if (!hv_http_cache[i].body) continue;
    hv_http_cache[i].age++;
    if (hv_http_cache[i].max_age > 0 &&
        hv_http_cache[i].age > hv_http_cache[i].max_age) {
      hv_http_cache_release_entry(i, 0, 1);
    }
  }
}

struct hv_http_cache_entry *hv_http_cache_slot(size_t body_len) {
  if (body_len == 0 || body_len > HV_HTTP_CACHE_BODY_MAX ||
      body_len > HV_HTTP_CACHE_TOTAL_MAX) {
    hv_http_cache_stats.rejected++;
    return NULL;
  }
  while (hv_http_cache_stats.total_bytes + body_len > HV_HTTP_CACHE_TOTAL_MAX) {
    int oldest = -1;
    for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
      if (!hv_http_cache[i].body) continue;
      if (oldest < 0 || hv_http_cache[i].age > hv_http_cache[oldest].age) oldest = i;
    }
    if (oldest < 0) {
      hv_http_cache_stats.rejected++;
      return NULL;
    }
    hv_http_cache_release_entry(oldest, 1, 0);
  }
  for (int i = 0; i < HV_HTTP_CACHE_MAX; i++) {
    if (!hv_http_cache[i].body) return &hv_http_cache[i];
  }
  int oldest = 0;
  for (int i = 1; i < HV_HTTP_CACHE_MAX; i++) {
    if (hv_http_cache[i].age > hv_http_cache[oldest].age) oldest = i;
  }
  hv_http_cache_release_entry(oldest, 1, 0);
  return &hv_http_cache[oldest];
}

void hv_http_cache_stats_get(struct hv_http_cache_stats *out) {
  if (out) *out = hv_http_cache_stats;
}

uint32_t hv_parse_cache_control_max_age(const char *cc) {
  if (!cc) return 0;
  const char *p = cc;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    if ((p[0] | 32) == 'n' && (p[1] | 32) == 'o' &&
        (p[2] == '-' || p[2] == ' ') &&
        ((p[3] | 32) == 's' || (p[3] | 32) == 'c')) {
      return 0;
    }
    if (p[0] == 'm' && p[1] == 'a' && p[2] == 'x' && p[3] == '-' &&
        p[4] == 'a' && p[5] == 'g' && p[6] == 'e' && p[7] == '=') {
      uint32_t v = 0;
      p += 8;
      while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint32_t)(*p - '0');
        p++;
      }
      return v / 60;
    }
    while (*p && *p != ',') p++;
  }
  return 1;
}

void hv_http_cache_store(const char *url, const struct http_response *resp) {
  const char *cc = NULL;
  const char *ct = NULL;
  uint32_t max_age;
  struct hv_http_cache_entry *slot;
  uint8_t *body_copy;
  int i;
  if (!url || !resp || resp->status_code != 200 || !resp->body ||
      resp->body_len == 0) {
    return;
  }
  if (resp->body_len > HV_HTTP_CACHE_BODY_MAX ||
      resp->body_len > HV_HTTP_CACHE_TOTAL_MAX) {
    hv_http_cache_stats.rejected++;
    return;
  }
  for (i = 0; i < (int)resp->header_count; i++) {
    if (hv_streq_ci(resp->headers[i].name, "Cache-Control")) {
      cc = resp->headers[i].value;
    }
    if (hv_streq_ci(resp->headers[i].name, "Content-Type")) {
      ct = resp->headers[i].value;
    }
  }
  max_age = hv_parse_cache_control_max_age(cc);
  if (max_age == 0 && cc) {
    hv_http_cache_stats.rejected++;
    return;
  }
  for (i = 0; i < HV_HTTP_CACHE_MAX; i++) {
    if (hv_http_cache[i].body && kstreq(hv_http_cache[i].url, url)) {
      hv_http_cache_release_entry(i, 0, 0);
      break;
    }
  }
  slot = hv_http_cache_slot(resp->body_len);
  if (!slot) return;
  body_copy = (uint8_t *)kalloc(resp->body_len);
  if (!body_copy) {
    hv_http_cache_stats.rejected++;
    return;
  }
  kmemcpy(body_copy, resp->body, resp->body_len);
  kstrcpy(slot->url, sizeof(slot->url), url);
  slot->body = body_copy;
  slot->body_len = resp->body_len;
  slot->max_age = max_age;
  slot->age = 0;
  if (ct) {
    kstrcpy(slot->content_type, sizeof(slot->content_type), ct);
  } else {
    slot->content_type[0] = '\0';
  }
  hv_http_cache_stats.entries++;
  hv_http_cache_stats.total_bytes += resp->body_len;
  hv_http_cache_stats.stores++;
}

void hv_bookmark_add(const char *url, const char *title) {
  if (!url || !url[0]) return;
  for (int i = 0; i < hv_bookmark_count; i++) {
    if (kstreq(hv_bookmark_url[i], url)) return;
  }
  if (hv_bookmark_count >= HV_BOOKMARK_MAX) return;
  kstrcpy(hv_bookmark_url[hv_bookmark_count], HTML_URL_MAX, url);
  kstrcpy(hv_bookmark_title[hv_bookmark_count], HTML_TITLE_MAX,
          title && title[0] ? title : url);
  hv_bookmark_count++;
}

int hv_is_bookmarked(const char *url) {
  if (!url) return 0;
  for (int i = 0; i < hv_bookmark_count; i++) {
    if (kstreq(hv_bookmark_url[i], url)) return 1;
  }
  return 0;
}

void hv_history_push(const char *url) {
  if (hv_navigating_history || !url || !url[0]) return;
  if (hv_history_cur >= 0 && kstreq(hv_history[hv_history_cur], url)) return;
  hv_history_count = hv_history_cur + 1;
  if (hv_history_count >= HV_HISTORY_MAX) {
    for (int i = 0; i < HV_HISTORY_MAX - 1; i++) {
      kmemcpy(hv_history[i], hv_history[i + 1], HTML_URL_MAX);
    }
    hv_history_cur = HV_HISTORY_MAX - 2;
    hv_history_count = HV_HISTORY_MAX - 1;
  }
  kstrcpy(hv_history[hv_history_count], sizeof(hv_history[0]), url);
  hv_history_cur = hv_history_count;
  hv_history_count++;
}
