#include "apps/html_viewer.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "net/http.h"
#include "memory/kmem.h"
#include <stddef.h>

static struct html_viewer_app g_viewer;

static void hv_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void hv_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}
static int hv_strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] - b[i];
    if (a[i] == 0) return 0;
  }
  return 0;
}

static void html_viewer_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  html_viewer_paint((struct html_viewer_app *)win->user_data);
}

static void html_viewer_window_scroll(struct gui_window *win, int32_t delta) {
  if (!win || !win->user_data) return;
  /* delta > 0 means scroll up, delta < 0 means scroll down */
  html_viewer_scroll((struct html_viewer_app *)win->user_data, -delta);
}

int html_parse(const char *html, size_t len, struct html_document *doc) {
  if (!html || !doc) return -1;
  hv_memset(doc, 0, sizeof(*doc));

  size_t pos = 0;
  while (pos < len && doc->node_count < HTML_MAX_NODES) {
    /* Skip whitespace */
    while (pos < len && (html[pos] == ' ' || html[pos] == '\n' ||
           html[pos] == '\r' || html[pos] == '\t')) pos++;
    if (pos >= len) break;

    if (html[pos] == '<') {
      pos++;
      /* Check for closing tag */
      if (pos < len && html[pos] == '/') {
        while (pos < len && html[pos] != '>') pos++;
        if (pos < len) pos++;
        continue;
      }

      /* Parse tag name */
      char tag[32];
      int ti = 0;
      while (pos < len && html[pos] != '>' && html[pos] != ' ' && ti < 31) {
        tag[ti++] = html[pos++];
      }
      tag[ti] = '\0';

      /* Skip attributes */
      char href[HTML_URL_MAX] = {0};
      while (pos < len && html[pos] != '>') {
        if (hv_strncmp(html + pos, "href=\"", 6) == 0) {
          pos += 6;
          int hi = 0;
          while (pos < len && html[pos] != '"' && hi < HTML_URL_MAX - 1)
            href[hi++] = html[pos++];
          href[hi] = '\0';
          if (pos < len && html[pos] == '"') pos++;
        } else {
          pos++;
        }
      }
      if (pos < len) pos++; /* skip > */

      struct html_node *node = &doc->nodes[doc->node_count];
      hv_memset(node, 0, sizeof(*node));
      node->color = 0xCDD6F4;
      node->font_size = 16;

      if (hv_strncmp(tag, "h1", 2) == 0) {
        node->type = HTML_NODE_TAG_H1; node->font_size = 32; node->bold = 1;
      } else if (hv_strncmp(tag, "h2", 2) == 0) {
        node->type = HTML_NODE_TAG_H2; node->font_size = 24; node->bold = 1;
      } else if (hv_strncmp(tag, "h3", 2) == 0) {
        node->type = HTML_NODE_TAG_H3; node->font_size = 20; node->bold = 1;
      } else if (hv_strncmp(tag, "p", 1) == 0 && tag[1] == '\0') {
        node->type = HTML_NODE_TAG_P;
      } else if (hv_strncmp(tag, "a", 1) == 0 && tag[1] == '\0') {
        node->type = HTML_NODE_TAG_A; node->color = 0x89B4FA;
        hv_strcpy(node->href, href, HTML_URL_MAX);
      } else if (hv_strncmp(tag, "br", 2) == 0) {
        node->type = HTML_NODE_TAG_BR;
        doc->node_count++;
        continue;
      } else if (hv_strncmp(tag, "title", 5) == 0) {
        node->type = HTML_NODE_TAG_TITLE;
        int txi = 0;
        while (pos < len && html[pos] != '<' && txi < 127)
          doc->title[txi++] = html[pos++];
        doc->title[txi] = '\0';
        while (pos < len && html[pos] != '>') pos++;
        if (pos < len) pos++;
        continue;
      } else {
        /* Unknown tag, skip */
        continue;
      }

      /* Read text content until closing tag */
      int txi = 0;
      while (pos < len && html[pos] != '<' && txi < 127)
        node->text[txi++] = html[pos++];
      node->text[txi] = '\0';
      doc->node_count++;

    } else {
      /* Plain text */
      struct html_node *node = &doc->nodes[doc->node_count];
      hv_memset(node, 0, sizeof(*node));
      node->type = HTML_NODE_TEXT;
      node->color = 0xCDD6F4;
      node->font_size = 16;
      int txi = 0;
      while (pos < len && html[pos] != '<' && txi < 127)
        node->text[txi++] = html[pos++];
      node->text[txi] = '\0';
      if (txi > 0) doc->node_count++;
    }
  }
  return 0;
}

void html_viewer_open(void) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  hv_memset(&g_viewer, 0, sizeof(g_viewer));
  g_viewer.window = compositor_create_window("CapyBrowser", 60, 40,
                                             640 + 160 * (scale - 1),
                                             480 + 140 * (scale - 1));
  if (!g_viewer.window) return;
  g_viewer.window->bg_color = theme->window_bg;
  g_viewer.window->border_color = theme->window_border;
  g_viewer.window->user_data = &g_viewer;
  g_viewer.window->on_paint = html_viewer_window_paint;
  g_viewer.window->on_scroll = html_viewer_window_scroll;
  compositor_show_window(g_viewer.window->id);
  compositor_focus_window(g_viewer.window->id);

  /* Load a default page */
  const char *default_html =
    "<html><head><title>CapyBrowser</title></head>"
    "<body>"
    "<h1>Welcome to CapyBrowser</h1>"
    "<p>This is the built-in web viewer for CapyOS.</p>"
    "<h2>Features</h2>"
    "<p>Basic HTML rendering with headings, paragraphs, and links.</p>"
    "<p>Enter a URL in the address bar to navigate.</p>"
    "<a href=\"about:version\">About CapyOS</a>"
    "<br>"
    "<p>Powered by CapyOS networking stack.</p>"
    "</body></html>";

  size_t len = 0;
  while (default_html[len]) len++;
  html_parse(default_html, len, &g_viewer.doc);
  hv_strcpy(g_viewer.url, "about:home", HTML_URL_MAX);
}

void html_viewer_navigate(struct html_viewer_app *app, const char *url) {
  if (!app || !url) return;
  hv_strcpy(app->url, url, HTML_URL_MAX);
  app->scroll_offset = 0;
  app->loading = 1;

  /* Try HTTP fetch */
  struct http_response resp;
  hv_memset(&resp, 0, sizeof(resp));
  if (http_get(url, &resp) == 0 && resp.status_code == 200 && resp.body) {
    html_parse((const char *)resp.body, resp.body_len, &app->doc);
    if (app->doc.title[0]) {
      compositor_set_title(app->window->id, app->doc.title);
    }
  } else {
    hv_memset(&app->doc, 0, sizeof(app->doc));
    app->doc.nodes[0].type = HTML_NODE_TAG_H1;
    hv_strcpy(app->doc.nodes[0].text, "Navigation Error", 128);
    app->doc.nodes[0].color = 0xF38BA8;
    app->doc.nodes[1].type = HTML_NODE_TAG_P;
    hv_strcpy(app->doc.nodes[1].text, "Could not load the requested page.", 128);
    app->doc.nodes[1].color = 0xCDD6F4;
    app->doc.node_count = 2;
  }
  app->loading = 0;
}

void html_viewer_paint(struct html_viewer_app *app) {
  if (!app || !app->window) return;
  struct gui_surface *s = &app->window->surface;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  if (!f) return;

  /* Clear */
  for (uint32_t y = 0; y < s->height; y++) {
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
    for (uint32_t x = 0; x < s->width; x++) line[x] = theme->window_bg;
  }

  /* URL bar */
  for (uint32_t y = 0; y < 24; y++) {
      uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
      for (uint32_t x = 0; x < s->width; x++) {
      row[x] = theme->accent_alt;
      }
  }
  font_draw_string(s, f, 4, 4, app->url, theme->text_muted);

  /* Render nodes */
  int32_t y = 28 - app->scroll_offset;
  int32_t margin = 12;

  for (int i = 0; i < app->doc.node_count; i++) {
    struct html_node *node = &app->doc.nodes[i];
    if (y > (int32_t)s->height) break;

    switch (node->type) {
    case HTML_NODE_TAG_H1:
      y += 8;
      if (y >= 0) font_draw_string(s, f, margin, y, node->text, theme->accent);
      y += 20;
      break;
    case HTML_NODE_TAG_H2:
      y += 6;
      if (y >= 0) font_draw_string(s, f, margin, y, node->text, theme->accent_alt);
      y += 18;
      break;
    case HTML_NODE_TAG_H3:
      y += 4;
      if (y >= 0) font_draw_string(s, f, margin, y, node->text, theme->text);
      y += 18;
      break;
    case HTML_NODE_TAG_P:
    case HTML_NODE_TEXT:
      if (y >= 0) font_draw_string(s, f, margin, y, node->text, node->color);
      y += 18;
      break;
    case HTML_NODE_TAG_A:
      if (y >= 0) {
        font_draw_string(s, f, margin, y, node->text, theme->accent);
        /* Underline */
        uint32_t tw = font_string_width(f, node->text);
        int32_t uy = y + (int32_t)f->glyph_height;
        if (uy >= 0 && (uint32_t)uy < s->height) {
          uint32_t *uline = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)uy * s->pitch);
          for (uint32_t ux = (uint32_t)margin; ux < (uint32_t)margin + tw && ux < s->width; ux++)
            uline[ux] = theme->accent;
        }
      }
      y += 18;
      break;
    case HTML_NODE_TAG_BR:
      y += 16;
      break;
    default:
      break;
    }
  }

  if (app->loading) {
    font_draw_string(s, f, (int32_t)(s->width - 80), 4, "Loading...", theme->text);
  }
}

void html_viewer_scroll(struct html_viewer_app *app, int delta) {
  if (!app) return;
  app->scroll_offset += delta * 20;
  if (app->scroll_offset < 0) app->scroll_offset = 0;
}
