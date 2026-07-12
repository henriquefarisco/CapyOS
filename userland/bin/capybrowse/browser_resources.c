/* Pure, allocation-free resource discovery for static HTML documents. */
#include "browser_resources.h"

#include <string.h>

static int slice_eq_ci(const char *s, size_t len, const char *lit) {
  size_t i;
  for (i = 0u; i < len && lit[i] != '\0'; ++i) {
    char a = s[i];
    char b = lit[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
  }
  return i == len && lit[i] == '\0';
}

static int rel_has_stylesheet(const char *s, size_t len) {
  size_t p = 0u;
  while (p < len) {
    size_t start;
    while (p < len && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' ||
                       s[p] == '\n' || s[p] == '\f'))
      ++p;
    start = p;
    while (p < len && s[p] != ' ' && s[p] != '\t' && s[p] != '\r' &&
           s[p] != '\n' && s[p] != '\f')
      ++p;
    if (p > start && slice_eq_ci(s + start, p - start, "stylesheet")) return 1;
  }
  return 0;
}

static void css_append(struct browser_resources *out, const char *s,
                       size_t len) {
  size_t room;
  if (len == 0u) return;
  room = BROWSER_RESOURCES_CSS_MAX - out->inline_css_len;
  if (len > room) {
    len = room;
    out->truncated = 1;
  }
  if (len > 0u) {
    memcpy(out->inline_css + out->inline_css_len, s, len);
    out->inline_css_len += len;
  }
  if (out->inline_css_len < BROWSER_RESOURCES_CSS_MAX) {
    out->inline_css[out->inline_css_len++] = '\n';
  } else {
    out->truncated = 1;
  }
}

static int url_duplicate(const struct browser_resources *out,
                         const char *url) {
  size_t i;
  for (i = 0u; i < out->stylesheet_count; ++i) {
    if (strcmp(out->stylesheet_urls[i], url) == 0) return 1;
  }
  return 0;
}

int browser_resources_discover(const struct capy_dom_doc *dom,
                               const char *base_url,
                               struct browser_resources *out) {
  size_t i;
  if (out == NULL) return -1;
  memset(out, 0, sizeof(*out));
  if (dom == NULL) return -1;

  for (i = 0u; i < dom->node_count; ++i) {
    const struct capy_dom_node *node = &dom->nodes[i];
    if (node->type == CAPY_DOM_TEXT && node->parent < dom->node_count &&
        strcmp(dom->nodes[node->parent].name, "style") == 0) {
      if (node->text_off <= dom->string_len &&
          node->text_len <= dom->string_len - node->text_off) {
        css_append(out, dom->strings + node->text_off, node->text_len);
      } else {
        out->truncated = 1;
      }
      continue;
    }

    if (node->type == CAPY_DOM_ELEMENT && strcmp(node->name, "link") == 0) {
      size_t rel_off, rel_len, href_off, href_len;
      char href[CAPY_URL_MAX_LEN + 1u];
      char resolved[BROWSER_RESOURCES_URL_MAX];
      struct capy_url parsed;
      if (!capy_dom_find_attr(dom, node, "rel", &rel_off, &rel_len) ||
          rel_off > dom->string_len || rel_len > dom->string_len - rel_off ||
          !rel_has_stylesheet(dom->strings + rel_off, rel_len))
        continue;
      if (!capy_dom_find_attr(dom, node, "href", &href_off, &href_len) ||
          href_len == 0u || href_len > CAPY_URL_MAX_LEN ||
          href_off > dom->string_len || href_len > dom->string_len - href_off)
        continue;
      memcpy(href, dom->strings + href_off, href_len);
      href[href_len] = '\0';
      if (capy_url_parse(href, base_url, &parsed, NULL) != CAPY_URL_OK ||
          (!slice_eq_ci(parsed.scheme, strlen(parsed.scheme), "http") &&
           !slice_eq_ci(parsed.scheme, strlen(parsed.scheme), "https")) ||
          capy_url_serialize(&parsed, resolved, sizeof(resolved)) < 0)
        continue;
      if (url_duplicate(out, resolved)) continue;
      if (out->stylesheet_count >= BROWSER_RESOURCES_MAX_STYLESHEETS) {
        out->truncated = 1;
        continue;
      }
      memcpy(out->stylesheet_urls[out->stylesheet_count], resolved,
             strlen(resolved) + 1u);
      out->stylesheet_count++;
    }
  }
  return 0;
}
