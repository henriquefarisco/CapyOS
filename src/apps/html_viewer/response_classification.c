#include "internal/html_viewer_internal.h"

struct html_node *html_push_node(struct html_document *doc) {
  struct html_node *node = NULL;
  if (!doc || doc->node_count >= HTML_MAX_NODES) return NULL;
  /* Parse-phase budget. The current parsing app (set via hv_parse_app_set)
   * is consulted; if no parse is in progress, the budget is skipped (this
   * keeps internal scaffolding pages like quick_start unrestricted). */
  {
    struct html_viewer_app *parse_app = hv_parse_app_get();
    if (parse_app && !hv_parse_budget_take(parse_app, "parse")) {
      return NULL;
    }
  }
  node = &doc->nodes[doc->node_count++];
  kmemzero(node, sizeof(*node));
  node->type = HTML_NODE_TEXT;
  node->color = 0xCDD6F4;
  node->font_size = 16;
  return node;
}

uint8_t hv_form_input_type(const char *type) {
  if (hv_streq_ci(type, "search")) return HTML_INPUT_TYPE_SEARCH;
  if (hv_streq_ci(type, "textarea")) return HTML_INPUT_TYPE_TEXTAREA;
  if (hv_streq_ci(type, "hidden")) return HTML_INPUT_TYPE_HIDDEN;
  if (hv_streq_ci(type, "submit")) return HTML_INPUT_TYPE_SUBMIT;
  if (hv_streq_ci(type, "button")) return HTML_INPUT_TYPE_BUTTON;
  if (hv_streq_ci(type, "checkbox")) return HTML_INPUT_TYPE_CHECKBOX;
  if (hv_streq_ci(type, "radio")) return HTML_INPUT_TYPE_RADIO;
  return HTML_INPUT_TYPE_TEXT;
}

int hv_body_match_ci_at(const uint8_t *body, size_t len, size_t pos,
                        const char *needle) {
  size_t i = 0;
  if (!body || !needle || pos >= len) return 0;
  while (needle[i]) {
    char a;
    char b;
    if (pos + i >= len) return 0;
    a = (char)body[pos + i];
    b = needle[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
    i++;
  }
  return 1;
}

size_t hv_body_find_ci(const uint8_t *body, size_t len,
                       const char *needle, size_t max_scan) {
  size_t limit = len;
  if (!body || !needle || !needle[0]) return len;
  if (limit > max_scan) limit = max_scan;
  for (size_t i = 0; i < limit; i++) {
    if (hv_body_match_ci_at(body, len, i, needle)) return i;
  }
  return len;
}

size_t hv_find_html_start(const uint8_t *body, size_t len) {
  size_t best = len;
  static const char *needles[] = {"<!doctype html", "<html", "<head", "<body",
                                  "<main", "<article", "<section", "<div",
                                  "<p", "<h1", "<noscript", "<title", NULL};
  if (!body || len == 0) return len;
  for (int i = 0; needles[i]; i++) {
    size_t pos = hv_body_find_ci(body, len, needles[i], HV_HTML_SNIFF_LIMIT);
    if (pos < best) best = pos;
  }
  return best;
}

int hv_body_looks_html(const uint8_t *body, size_t len) {
  return hv_find_html_start(body, len) < len;
}

int hv_body_looks_textual(const uint8_t *body, size_t len) {
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

int hv_content_type_is_html(const char *content_type) {
  return content_type && content_type[0] &&
         (hv_contains_ci(content_type, "text/html") ||
          hv_contains_ci(content_type, "application/xhtml+xml"));
}

int hv_content_type_is_textual(const char *content_type) {
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

int hv_content_type_is_script_or_data(const char *content_type) {
  if (!content_type || !content_type[0]) return 0;
  return hv_contains_ci(content_type, "application/json") ||
         hv_contains_ci(content_type, "+json") ||
         hv_contains_ci(content_type, "application/javascript") ||
         hv_contains_ci(content_type, "text/javascript") ||
         hv_contains_ci(content_type, "application/x-javascript") ||
         hv_contains_ci(content_type, "application/ecmascript") ||
         hv_contains_ci(content_type, "text/ecmascript");
}

int hv_body_looks_script_or_data(const uint8_t *body, size_t len) {
  size_t pos = 0;
  if (!body || len == 0) return 0;
  while (pos < len &&
         (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' ||
          body[pos] == '\n')) {
    pos++;
  }
  if (pos >= len) return 0;
  if (body[pos] == '{' || body[pos] == '[') return 1;
  return hv_body_match_ci_at(body, len, pos, "var ") ||
         hv_body_match_ci_at(body, len, pos, "let ") ||
         hv_body_match_ci_at(body, len, pos, "const ") ||
         hv_body_match_ci_at(body, len, pos, "function") ||
         hv_body_match_ci_at(body, len, pos, "!function") ||
         hv_body_match_ci_at(body, len, pos, "(()=>") ||
         hv_body_match_ci_at(body, len, pos, "(function") ||
         hv_body_match_ci_at(body, len, pos, "window.") ||
         hv_body_match_ci_at(body, len, pos, "self.") ||
         hv_body_match_ci_at(body, len, pos, "import ");
}

int hv_parse_meta_refresh_content(const char *content, uint32_t *out_delay,
                                  char *out_url, size_t out_url_len) {
  const char *p = content;
  const char *url = NULL;
  uint32_t delay = 0;
  if (out_delay) *out_delay = 0;
  if (out_url && out_url_len > 0) out_url[0] = '\0';
  if (!content || !out_url || out_url_len == 0) return 0;
  while (*p && hv_is_space(*p)) p++;
  while (*p >= '0' && *p <= '9') {
    delay = delay * 10u + (uint32_t)(*p - '0');
    p++;
  }
  while (*p) {
    if ((p[0] == 'u' || p[0] == 'U') &&
        (p[1] == 'r' || p[1] == 'R') &&
        (p[2] == 'l' || p[2] == 'L')) {
      const char *q = p + 3;
      while (*q && hv_is_space(*q)) q++;
      if (*q == '=') {
        url = q + 1;
        break;
      }
    }
    p++;
  }
  if (!url) return 0;
  while (*url && hv_is_space(*url)) url++;
  if (*url == '"' || *url == '\'') {
    char quote = *url++;
    size_t ui = 0;
    while (*url && *url != quote && ui + 1 < out_url_len) {
      out_url[ui++] = *url++;
    }
    out_url[ui] = '\0';
  } else {
    size_t ui = 0;
    while (*url && *url != ';' && *url != '\r' && *url != '\n' &&
           ui + 1 < out_url_len) {
      out_url[ui++] = *url++;
    }
    out_url[ui] = '\0';
  }
  hv_trim_text(out_url);
  if (out_delay) *out_delay = delay;
  return out_url[0] != '\0';
}

int hv_base64_value(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

uint8_t *hv_base64_decode_alloc(const char *text, size_t len, size_t *out_len) {
  size_t alloc_len = 0;
  size_t wi = 0;
  uint8_t *out = NULL;
  uint32_t chunk = 0;
  int chunk_chars = 0;
  if (out_len) *out_len = 0;
  if (!text || len == 0) return NULL;
  alloc_len = ((len + 3u) / 4u) * 3u;
  if (alloc_len == 0 || alloc_len > HTTP_MAX_RESPONSE_SIZE) return NULL;
  out = (uint8_t *)kmalloc(alloc_len);
  if (!out) return NULL;
  for (size_t i = 0; i < len; i++) {
    int val;
    char ch = text[i];
    if (ch == '=') break;
    if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
    val = hv_base64_value(ch);
    if (val < 0) continue;
    chunk = (chunk << 6) | (uint32_t)val;
    chunk_chars++;
    if (chunk_chars == 4) {
      if (wi + 3 > alloc_len) break;
      out[wi++] = (uint8_t)((chunk >> 16) & 0xFFu);
      out[wi++] = (uint8_t)((chunk >> 8) & 0xFFu);
      out[wi++] = (uint8_t)(chunk & 0xFFu);
      chunk = 0;
      chunk_chars = 0;
    }
  }
  if (chunk_chars == 3) {
    chunk <<= 6;
    if (wi + 2 <= alloc_len) {
      out[wi++] = (uint8_t)((chunk >> 16) & 0xFFu);
      out[wi++] = (uint8_t)((chunk >> 8) & 0xFFu);
    }
  } else if (chunk_chars == 2) {
    chunk <<= 12;
    if (wi + 1 <= alloc_len) {
      out[wi++] = (uint8_t)((chunk >> 16) & 0xFFu);
    }
  }
  if (out_len) *out_len = wi;
  return out;
}

int hv_decode_data_image(const char *url, struct png_image *img,
                         struct jpeg_image *jimg) {
  const char *meta_end = NULL;
  const char *payload = NULL;
  size_t payload_len = 0;
  size_t blob_len = 0;
  uint8_t *blob = NULL;
  int is_png = 0;
  int is_jpeg = 0;
  int rc = -1;
  if (!url || !img || !jimg) return -1;
  if (hv_strncmp(url, "data:image/", 11) != 0) return -1;
  meta_end = url;
  while (*meta_end && *meta_end != ',') meta_end++;
  if (*meta_end != ',') return -1;
  if (!hv_contains_ci(url, ";base64")) return -1;
  is_png = hv_contains_ci(url, "data:image/png");
  is_jpeg = hv_contains_ci(url, "data:image/jpeg") ||
            hv_contains_ci(url, "data:image/jpg");
  if (!is_png && !is_jpeg) return -1;
  payload = meta_end + 1;
  payload_len = kstrlen(payload);
  blob = hv_base64_decode_alloc(payload, payload_len, &blob_len);
  if (!blob || blob_len == 0) {
    if (blob) kfree(blob);
    return -1;
  }
  kmemzero(img, sizeof(*img));
  kmemzero(jimg, sizeof(*jimg));
  if (is_png) {
    rc = png_decode(blob, blob_len, img);
  } else if (jpeg_decode(blob, blob_len, jimg) == 0 &&
             jimg->pixels && jimg->width > 0 && jimg->height > 0) {
    img->pixels = jimg->pixels;
    img->width = jimg->width;
    img->height = jimg->height;
    rc = 0;
  }
  kfree(blob);
  return rc;
}
