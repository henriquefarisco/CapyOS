/* Retained ring-3 graphical browser: navigation + resources + toolbar. */
#include "browser_app.h"

#include <capylibc/capylibc.h>
#include <capylibc/capy_gfx.h>

#include "browser_fetch.h"
#include "browser_navigation.h"
#include "browser_render_pixel.h"
#include "browser_resources.h"
#include "page_budget.h"
#include "page_render.h"
#include "toolbar.h"

#include "drivers/input/keyboard_layout.h"

#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
#include "browser_image.h"
#endif

#ifdef CAPYGFX_SITE_SMOKE
#undef CAPYGFX_INITIAL_URL
#define CAPYGFX_INITIAL_URL "http://10.0.2.2:18082/start"
#elif !defined(CAPYGFX_INITIAL_URL)
#define CAPYGFX_INITIAL_URL "https://example.com/"
#endif

#define APP_CELL_W 8u
#define APP_CELL_H 16u
#define APP_DOCUMENT_MAX (256u * 1024u)
#define APP_PAGE_BYTES_MAX (2u * 1024u * 1024u)
#define APP_PAGE_TICKS_MAX 6000L
#define APP_IMAGE_MAX 4u
#define APP_IMAGE_BYTES_MAX (64u * 1024u)

#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
struct app_image_entry {
  char url[BROWSER_NAVIGATION_URL_MAX];
  uint8_t bytes[APP_IMAGE_BYTES_MAX];
  size_t len;
  int state; /* 0 empty, 1 encoded bytes cached, -1 permanent page-local miss */
};
#endif

/* All large state lives in .bss, never on the 64 KiB ring-3 task stack. */
static struct browser_fetch_ctx g_fetch;
static struct browser_navigation g_navigation;
static struct capygfx_toolbar g_toolbar;
static struct capy_page g_page;
static struct browser_resources g_resources;
static struct page_budget g_budget;
static struct http_response g_http_response;
static uint8_t g_document[APP_DOCUMENT_MAX];
static char g_css[BROWSER_RESOURCES_CSS_MAX];
static size_t g_css_len;
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
static struct app_image_entry g_images[APP_IMAGE_MAX];
#endif
static const struct capy_dl *g_display_list;
static struct capyos_browser_pixel_opts g_pixel_opts;
static int32_t g_scroll_y;
static int32_t g_max_scroll_y;
static int g_images_enabled;
static int g_images_pending;

#ifdef CAPYGFX_SITE_SMOKE
static size_t app_len(const char *s) {
  size_t n = 0u;
  if (!s) return 0u;
  while (s[n]) n++;
  return n;
}
#endif

static int app_copy(char *dst, size_t cap, const char *src) {
  size_t i = 0u;
  if (!dst || cap == 0u || !src) return 0;
  while (src[i]) {
    if (i + 1u >= cap) return 0;
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return 1;
}

static int app_equal(const char *a, const char *b) {
  size_t i = 0u;
  if (!a || !b) return 0;
  while (a[i] && b[i] && a[i] == b[i]) i++;
  return a[i] == '\0' && b[i] == '\0';
}

static char app_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int app_prefix_ci(const char *s, const char *prefix) {
  size_t i = 0u;
  if (!s || !prefix) return 0;
  while (prefix[i]) {
    if (!s[i] || app_lower(s[i]) != app_lower(prefix[i])) return 0;
    i++;
  }
  return 1;
}

static const char *app_header(const struct http_response *response,
                              const char *name) {
  uint32_t i;
  if (!response || !name) return NULL;
  for (i = 0u; i < response->header_count && i < HTTP_MAX_HEADERS; ++i) {
    if (app_equal(response->headers[i].name, name))
      return response->headers[i].value;
    /* Header names are case-insensitive. */
    {
      size_t j = 0u;
      while (response->headers[i].name[j] && name[j] &&
             app_lower(response->headers[i].name[j]) == app_lower(name[j]))
        j++;
      if (response->headers[i].name[j] == '\0' && name[j] == '\0')
        return response->headers[i].value;
    }
  }
  return NULL;
}

static long app_now(void) {
  long now = capy_clock_realtime();
  return now >= 0 ? now : capy_time();
}

/* Bind the pure navigation controller to the persistent browser session. */
static int app_fetch_navigation(void *unused, const char *url, int force_reload,
                                struct browser_navigation_response *out) {
  char effective[BROWSER_FETCH_URL_MAX];
  int hsts_required = 0;
  enum http_fetch_decision decision;
  int result;
  const char *wire_url = url;
  (void)unused;
  if (!url || !out) return -1;

  decision = browser_fetch_plan(&g_fetch, url, app_now(), effective,
                                sizeof(effective), &hsts_required);
  if (decision == HTTP_FETCH_BLOCK) return -1;
  /* Bare inputs are already promoted to HTTPS by browser_navigation. An
   * explicitly typed http:// URL is respected unless a learned HSTS rule makes
   * the upgrade mandatory. */
  if (!app_prefix_ci(url, "http://") || hsts_required) wire_url = effective;

  /* Reload bypasses only this URL's cache entry. Cookies, HSTS state and other
   * cached resources survive, so repeated browsing does not regress into a
   * full-session reset. */
  result = force_reload
               ? browser_fetch_get_reload(&g_fetch, wire_url, &g_http_response,
                                          app_now())
               : browser_fetch_get(&g_fetch, wire_url, &g_http_response,
                                   app_now());
  if (result < 0) return -1;

  out->status_code = g_http_response.status_code;
  out->body = g_http_response.body;
  out->body_len = g_http_response.body_len;
  out->content_type = app_header(&g_http_response, "Content-Type");
  out->location = g_http_response.location[0] ? g_http_response.location : NULL;
  out->truncated = 0; /* browser_fetch rejects a truncated wire response. */

  /* Isolate the retained document from the fetch scratch/cache: CSS and image
   * requests may mutate those stores before the page is rasterized again. */
  if (out->status_code >= 200 && out->status_code < 300) {
    size_t i;
    if (out->body_len > sizeof(g_document)) {
      out->truncated = 1;
      return 0;
    }
    for (i = 0u; i < out->body_len; ++i) g_document[i] = out->body[i];
    out->body = g_document;
  }
  return 0;
}

static int app_media_type_is(const char *content_type, const char *wanted) {
  size_t i = 0u;
  if (!content_type || content_type[0] == '\0') return 1;
  while (wanted[i]) {
    if (!content_type[i] || app_lower(content_type[i]) != app_lower(wanted[i]))
      return 0;
    i++;
  }
  return content_type[i] == '\0' || content_type[i] == ';' ||
         content_type[i] == ' ' || content_type[i] == '\t';
}

static int app_document_type_allowed(const char *content_type) {
  return app_media_type_is(content_type, "text/html") ||
         app_media_type_is(content_type, "application/xhtml+xml") ||
         app_media_type_is(content_type, "text/plain");
}

static int app_css_append(const uint8_t *bytes, size_t len) {
  size_t i;
  if ((!bytes && len != 0u) || len > sizeof(g_css) - g_css_len) return 0;
  for (i = 0u; i < len; ++i) g_css[g_css_len++] = (char)bytes[i];
  if (g_css_len < sizeof(g_css)) g_css[g_css_len++] = '\n';
  return 1;
}

static void app_reset_images(void) {
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
  size_t i;
  for (i = 0u; i < APP_IMAGE_MAX; ++i) {
    g_images[i].url[0] = '\0';
    g_images[i].len = 0u;
    g_images[i].state = 0;
  }
  g_images_enabled = 0;
  g_images_pending = 1;
#else
  g_images_enabled = 0;
  g_images_pending = 0;
#endif
}

#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
static struct app_image_entry *app_image_find(const char *url) {
  size_t i;
  struct app_image_entry *empty = NULL;
  for (i = 0u; i < APP_IMAGE_MAX; ++i) {
    if (g_images[i].state != 0 && app_equal(g_images[i].url, url))
      return &g_images[i];
    if (!empty && g_images[i].state == 0) empty = &g_images[i];
  }
  return empty;
}
#endif

static int app_page_secure(void) {
  return app_prefix_ci(g_navigation.current_url, "https://");
}

static int app_resolve_image(void *unused, const char *src, size_t src_len,
                             const uint32_t **pixels, uint32_t *width,
                             uint32_t *height) {
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
  char url[BROWSER_NAVIGATION_URL_MAX];
  struct app_image_entry *entry;
  struct capyos_image image;
  size_t i;
  int fetched;
  (void)unused;
  if (!g_images_enabled || !src || src_len == 0u || src_len >= sizeof(url) ||
      !pixels || !width || !height)
    return 0;
  for (i = 0u; i < src_len; ++i) url[i] = src[i];
  url[src_len] = '\0';
  if (!browser_fetch_subresource_allowed(app_page_secure(), url)) return 0;
  entry = app_image_find(url);
  if (!entry) return 0; /* page-local cache full: never refetch on every frame */
  if (entry->state == 0) {
    (void)app_copy(entry->url, sizeof(entry->url), url);
    entry->state = -1; /* negative-cache every failure path */
    fetched = browser_fetch_get(&g_fetch, url, &g_http_response, app_now());
    if (fetched < 0 || g_http_response.status_code < 200 ||
        g_http_response.status_code >= 300 ||
        g_http_response.body_len > sizeof(entry->bytes) ||
        !page_budget_add_bytes(&g_budget, g_http_response.body_len))
      return 0;
    for (i = 0u; i < g_http_response.body_len; ++i)
      entry->bytes[i] = g_http_response.body[i];
    entry->len = g_http_response.body_len;
    entry->state = 1;
  }
  if (entry->state != 1 ||
      capyos_image_decode(entry->bytes, entry->len, &image) != 0)
    return 0;
  *pixels = image.pixels;
  *width = image.width;
  *height = image.height;
  return 1;
#else
  (void)unused;
  (void)src;
  (void)src_len;
  (void)pixels;
  (void)width;
  (void)height;
  return 0;
#endif
}

/* Build a retained page. Pass 1 discovers inline/linked CSS; pass 2 applies
 * the bounded aggregate. Missing stylesheets degrade to unstyled content. */
static int app_build_page(uint32_t page_width, uint32_t page_height) {
  size_t i;
  long viewport = (long)(page_width / APP_CELL_W);
  if (!g_navigation.body || viewport < 1)
    return -1;
  page_budget_init(&g_budget, APP_PAGE_BYTES_MAX, APP_PAGE_TICKS_MAX, app_now());
  if (!page_budget_add_bytes(&g_budget, g_navigation.body_len)) return -1;

  if (capy_page_render((const char *)g_navigation.body, g_navigation.body_len,
                       NULL, 0u, g_navigation.current_url, viewport,
                       &g_page) != CAPY_PAGE_OK)
    return -1;
  if (browser_resources_discover(&g_page.dom, g_navigation.current_url,
                                 &g_resources) != 0)
    return -1;

  g_css_len = 0u;
  if (!app_css_append((const uint8_t *)g_resources.inline_css,
                      g_resources.inline_css_len))
    g_resources.truncated = 1;
  for (i = 0u; i < g_resources.stylesheet_count; ++i) {
    const char *url = g_resources.stylesheet_urls[i];
    const char *content_type;
    int fetched;
    if (!browser_fetch_subresource_allowed(app_page_secure(), url)) continue;
    fetched = browser_fetch_get(&g_fetch, url, &g_http_response, app_now());
    content_type = app_header(&g_http_response, "Content-Type");
    if (fetched < 0 || g_http_response.status_code < 200 ||
        g_http_response.status_code >= 300 ||
        !app_media_type_is(content_type, "text/css") ||
        !page_budget_add_bytes(&g_budget, g_http_response.body_len) ||
        !app_css_append(g_http_response.body, g_http_response.body_len))
      continue;
  }

  if (capy_page_render((const char *)g_navigation.body, g_navigation.body_len,
                       g_css, g_css_len, g_navigation.current_url, viewport,
                       &g_page) != CAPY_PAGE_OK)
    return -1;
  g_display_list = &g_page.display_list;
  g_scroll_y = 0;
  g_max_scroll_y =
      (int32_t)((int64_t)g_display_list->content_height * APP_CELL_H) -
      (int32_t)page_height;
  if (g_max_scroll_y < 0) g_max_scroll_y = 0;
  app_reset_images();
  return 0;
}

static void app_fill(uint32_t *pixels, uint32_t width, uint32_t height,
                     uint32_t color) {
  size_t i;
  for (i = 0u; i < (size_t)width * height; ++i) pixels[i] = color;
}

static int app_present(int window, uint32_t *pixels, uint32_t width,
                       uint32_t height) {
  struct capygfx_toolbar_layout layout;
  struct capyos_browser_pixel_stats stats;
  capygfx_toolbar_compute_layout(width, height, &layout);
  app_fill(pixels, width, height, 0xffffffffu);
  if (g_display_list && layout.page_height > 0u) {
    if (capyos_browser_render_pixels_scrolled(
            g_display_list, pixels + (size_t)layout.page_y * width, width,
            layout.page_height, &g_pixel_opts, 0, g_scroll_y, &stats) != 0)
      return -1;
  }
  capygfx_toolbar_sync_navigation(&g_toolbar, &g_navigation);
  if (capygfx_toolbar_draw(&g_toolbar, pixels, width, height) != 0) return -1;
  if (capy_surface_blit(window, pixels, width, height, 0u, 0u) != 0) return -1;
  return capy_window_present(window);
}

static int app_do_navigation(enum capygfx_toolbar_action action,
                             const char *target, int window, uint32_t *pixels,
                             uint32_t width, uint32_t height) {
  int rc = -1;
  struct capygfx_toolbar_layout layout;
  g_navigation.state = BROWSER_NAVIGATION_LOADING;
  if (app_present(window, pixels, width, height) != 0) return -1;
  if (action == CAPYGFX_TOOLBAR_ACTION_BACK)
    rc = browser_navigation_back(&g_navigation, app_fetch_navigation, NULL);
  else if (action == CAPYGFX_TOOLBAR_ACTION_FORWARD)
    rc = browser_navigation_forward(&g_navigation, app_fetch_navigation, NULL);
  else if (action == CAPYGFX_TOOLBAR_ACTION_RELOAD)
    rc = browser_navigation_reload(&g_navigation, app_fetch_navigation, NULL);
  else if (action == CAPYGFX_TOOLBAR_ACTION_GO)
    rc = browser_navigation_navigate(&g_navigation, target,
                                     app_fetch_navigation, NULL);
  capygfx_toolbar_compute_layout(width, height, &layout);
  if (rc == 0 && !app_document_type_allowed(g_navigation.content_type)) {
    g_navigation.state = BROWSER_NAVIGATION_UNSUPPORTED_CONTENT;
    g_display_list = NULL;
  } else if (rc == 0 && app_build_page(width, layout.page_height) != 0) {
    /* Navigation fetched successfully but the static renderer refused it.
     * Keep the window alive and expose a typed visible error in the toolbar. */
    g_navigation.state = BROWSER_NAVIGATION_RENDER_ERROR;
    g_display_list = NULL;
  }
  return app_present(window, pixels, width, height);
}

static int app_scroll_by(int32_t delta) {
  int32_t before = g_scroll_y;
  g_scroll_y += delta;
  if (g_scroll_y < 0) g_scroll_y = 0;
  if (g_scroll_y > g_max_scroll_y) g_scroll_y = g_max_scroll_y;
  return before != g_scroll_y;
}

#ifdef CAPYGFX_SITE_SMOKE
static void app_smoke_write(const char *text) {
  capy_write(1, text, app_len(text));
}

static int app_smoke_first_link(char *url, size_t cap) {
  size_t i, j;
  if (!url || cap == 0u || !g_display_list) return 0;
  url[0] = '\0';
  for (i = 0u; i < g_display_list->node_count; ++i) {
    const struct capy_dl_node *node = &g_display_list->nodes[i];
    if (node->kind != CAPY_DL_LINK || node->url_len == 0u ||
        node->url_off > g_display_list->string_len ||
        node->url_len > g_display_list->string_len - node->url_off ||
        node->url_len + 1u > cap)
      continue;
    for (j = 0u; j < node->url_len; ++j)
      url[j] = g_display_list->strings[node->url_off + j];
    url[node->url_len] = '\0';
    return 1;
  }
  return 0;
}

static int app_site_smoke(int window, uint32_t *pixels, uint32_t width,
                          uint32_t height) {
  char link[BROWSER_NAVIGATION_URL_MAX];
  size_t i;
  int image_ok = 0;
  if (g_navigation.state != BROWSER_NAVIGATION_READY || !g_display_list ||
      g_navigation.last_redirect_count != 1u ||
      g_page.stylesheet.rule_count == 0u) {
    app_smoke_write("capygfx-site: initial/redirect/css failed\n");
    return -1;
  }
  g_images_pending = 0;
  g_images_enabled = 1;
  if (app_present(window, pixels, width, height) != 0) return -1;
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
  for (i = 0u; i < APP_IMAGE_MAX; ++i)
    if (g_images[i].state == 1) image_ok = 1;
#else
  (void)i;
#endif
  if (!image_ok || !app_smoke_first_link(link, sizeof(link))) {
    app_smoke_write("capygfx-site: image/link failed\n");
    return -1;
  }
  (void)capygfx_toolbar_set_address(&g_toolbar, link);
  if (app_do_navigation(CAPYGFX_TOOLBAR_ACTION_GO, g_toolbar.address, window,
                        pixels, width, height) != 0 ||
      g_navigation.state != BROWSER_NAVIGATION_READY ||
      !browser_navigation_can_back(&g_navigation)) {
    app_smoke_write("capygfx-site: link navigation failed\n");
    return -1;
  }
  if (app_do_navigation(CAPYGFX_TOOLBAR_ACTION_BACK, NULL, window, pixels,
                        width, height) != 0 ||
      g_navigation.state != BROWSER_NAVIGATION_READY ||
      !browser_navigation_can_forward(&g_navigation)) {
    app_smoke_write("capygfx-site: back failed\n");
    return -1;
  }
  if (app_do_navigation(CAPYGFX_TOOLBAR_ACTION_FORWARD, NULL, window, pixels,
                        width, height) != 0 ||
      g_navigation.state != BROWSER_NAVIGATION_READY) {
    app_smoke_write("capygfx-site: forward failed\n");
    return -1;
  }
  if (app_do_navigation(CAPYGFX_TOOLBAR_ACTION_RELOAD, NULL, window, pixels,
                        width, height) != 0 ||
      g_navigation.state != BROWSER_NAVIGATION_READY) {
    app_smoke_write("capygfx-site: reload failed\n");
    return -1;
  }
  app_smoke_write("[smoke] capygfx site redirect+css+image+link+history ready\n");
  return 0;
}
#endif /* CAPYGFX_SITE_SMOKE */

int capygfx_browser_run(int window, uint32_t *pixels, uint32_t width,
                        uint32_t height) {
  struct capygfx_toolbar_layout layout;
  int running = 1;
  if (window <= 0 || !pixels || width == 0u || height == 0u) return -1;
  browser_fetch_init(&g_fetch);
  browser_navigation_init(&g_navigation);
  capygfx_toolbar_init(&g_toolbar);
  g_display_list = NULL;
  g_pixel_opts.cell_w = APP_CELL_W;
  g_pixel_opts.cell_h = APP_CELL_H;
  g_pixel_opts.bg = 0xffffffffu;
  g_pixel_opts.fg = 0xff111111u;
  g_pixel_opts.link = 0xff1a4fd0u;
  g_pixel_opts.resolve_image = app_resolve_image;
  g_pixel_opts.image_ctx = NULL;
  (void)capygfx_toolbar_set_address(&g_toolbar, CAPYGFX_INITIAL_URL);
  if (app_do_navigation(CAPYGFX_TOOLBAR_ACTION_GO, g_toolbar.address, window,
                        pixels, width, height) != 0)
    return -1;

#ifdef CAPYGFX_SITE_SMOKE
  return app_site_smoke(window, pixels, width, height);
#endif

  while (running) {
    struct capy_gfx_event event;
    int polled;
    int redraw = 0;
    while ((polled = capy_window_poll_event(window, &event)) > 0) {
      enum capygfx_toolbar_action action = CAPYGFX_TOOLBAR_ACTION_NONE;
      if (event.kind == CAPY_GFX_EV_CLOSE) {
        running = 0;
        break;
      }
      if (event.kind == CAPY_GFX_EV_SCROLL) {
        redraw |= app_scroll_by(-event.dy * (int32_t)(APP_CELL_H * 3u));
        continue;
      }
      if (event.kind == CAPY_GFX_EV_KEY_DOWN) {
        action = capygfx_toolbar_key(&g_toolbar, event.code);
        if (action == CAPYGFX_TOOLBAR_ACTION_NONE &&
            !g_toolbar.address_focused) {
          if (event.code == KEY_UP)
            redraw |= app_scroll_by(-(int32_t)APP_CELL_H);
          else if (event.code == KEY_DOWN)
            redraw |= app_scroll_by((int32_t)APP_CELL_H);
          else if (event.code == KEY_PGUP)
            redraw |= app_scroll_by(-(int32_t)(height / 2u));
          else if (event.code == KEY_PGDN)
            redraw |= app_scroll_by((int32_t)(height / 2u));
        } else {
          redraw = 1;
        }
      } else if (event.kind == CAPY_GFX_EV_MOUSE_DOWN) {
        char link[BROWSER_NAVIGATION_URL_MAX];
        capygfx_toolbar_compute_layout(width, height, &layout);
        action = capygfx_toolbar_mouse_down(&g_toolbar, &layout, event.x,
                                            event.y);
        if (action == CAPYGFX_TOOLBAR_ACTION_NONE && g_display_list &&
            capygfx_toolbar_link_hit_test(
                g_display_list, event.x, event.y, layout.page_y, 0, g_scroll_y,
                APP_CELL_W, APP_CELL_H, link, sizeof(link))) {
          (void)capygfx_toolbar_set_address(&g_toolbar, link);
          action = CAPYGFX_TOOLBAR_ACTION_GO;
        }
        redraw = 1;
      }

      if (action == CAPYGFX_TOOLBAR_ACTION_GO ||
          action == CAPYGFX_TOOLBAR_ACTION_BACK ||
          action == CAPYGFX_TOOLBAR_ACTION_FORWARD ||
          action == CAPYGFX_TOOLBAR_ACTION_RELOAD) {
        const char *target = g_toolbar.address;
        if (app_do_navigation(action, target, window, pixels, width, height) !=
            0)
          return -1;
        redraw = 0; /* app_do_navigation already presented */
      }
    }
    if (polled < 0) return -1;
    if (!running) break;
    /* Paint decoded network images only after the document + toolbar have been
     * presented once, so a slow optional resource never hides page progress. */
    if (g_images_pending) {
      g_images_pending = 0;
      g_images_enabled = 1;
      redraw = 1;
    }
    if (redraw && app_present(window, pixels, width, height) != 0) return -1;
    capy_sleep(5u);
  }
  return 0;
}
