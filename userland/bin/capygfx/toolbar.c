#include "toolbar.h"

#include "drivers/input/keyboard_layout.h"

extern const uint8_t font8x8_basic[128][8];

#define TB_BG 0xffe9edf3u
#define TB_BORDER 0xff6b7280u
#define TB_BUTTON 0xfff8fafcu
#define TB_BUTTON_DISABLED 0xffd1d5dbu
#define TB_ADDRESS 0xffffffffu
#define TB_ADDRESS_FOCUS 0xffdbeafeu
#define TB_TEXT 0xff111827u
#define TB_MUTED 0xff4b5563u
#define TB_SECURE 0xff087f5bu
#define TB_ERROR 0xffb42318u

static size_t tb_len(const char *s) {
  size_t n = 0u;
  if (!s) return 0u;
  while (s[n]) n++;
  return n;
}

static int tb_copy(char *dst, size_t cap, const char *src, size_t *out_len) {
  size_t i = 0u;
  if (!dst || cap == 0u || !src) return 0;
  while (src[i]) {
    if (i + 1u >= cap) return 0;
    i++;
  }
  while (i > 0u) {
    dst[i - 1u] = src[i - 1u];
    i--;
  }
  if (out_len) *out_len = tb_len(src);
  dst[tb_len(src)] = '\0';
  return 1;
}

static void tb_zero_rect(struct capygfx_toolbar_rect *r) {
  r->x = r->y = r->width = r->height = 0u;
}

static struct capygfx_toolbar_rect tb_rect(uint32_t x, uint32_t y, uint32_t w,
                                           uint32_t h, uint32_t max_w,
                                           uint32_t max_h) {
  struct capygfx_toolbar_rect r;
  if (x >= max_w || y >= max_h) {
    tb_zero_rect(&r);
    return r;
  }
  if (w > max_w - x) w = max_w - x;
  if (h > max_h - y) h = max_h - y;
  r.x = x;
  r.y = y;
  r.width = w;
  r.height = h;
  return r;
}

void capygfx_toolbar_compute_layout(uint32_t width, uint32_t height,
                                    struct capygfx_toolbar_layout *out) {
  const uint32_t margin = 2u;
  const uint32_t gap = 2u;
  const uint32_t button_w = 24u;
  const uint32_t controls_h = 20u;
  const uint32_t go_w = 30u;
  uint32_t toolbar_h;
  uint32_t address_x;
  uint32_t go_x;

  if (!out) return;
  tb_zero_rect(&out->back);
  tb_zero_rect(&out->forward);
  tb_zero_rect(&out->reload);
  tb_zero_rect(&out->address);
  tb_zero_rect(&out->go);
  tb_zero_rect(&out->status);
  toolbar_h = height < CAPYGFX_TOOLBAR_HEIGHT ? height : CAPYGFX_TOOLBAR_HEIGHT;
  out->toolbar_height = toolbar_h;
  out->page_y = toolbar_h;
  out->page_height = height - toolbar_h;
  if (width == 0u || toolbar_h == 0u) return;

  out->back = tb_rect(margin, margin, button_w, controls_h, width, toolbar_h);
  out->forward = tb_rect(margin + button_w + gap, margin, button_w, controls_h,
                         width, toolbar_h);
  out->reload = tb_rect(margin + (button_w + gap) * 2u, margin, button_w,
                        controls_h, width, toolbar_h);
  address_x = margin + (button_w + gap) * 3u;
  go_x = width > margin + go_w ? width - margin - go_w : width;
  if (go_x > address_x + gap) {
    out->address = tb_rect(address_x, margin, go_x - address_x - gap,
                           controls_h, width, toolbar_h);
    out->go = tb_rect(go_x, margin, go_w, controls_h, width, toolbar_h);
  }
  if (toolbar_h > 24u)
    out->status = tb_rect(margin, 24u, width > 2u * margin ? width - 2u * margin : 0u,
                          toolbar_h - 24u, width, toolbar_h);
}

void capygfx_toolbar_init(struct capygfx_toolbar *toolbar) {
  if (!toolbar) return;
  toolbar->address[0] = '\0';
  toolbar->address_len = 0u;
  toolbar->cursor = 0u;
  toolbar->address_focused = 0;
  toolbar->select_all = 0;
  toolbar->can_back = 0;
  toolbar->can_forward = 0;
  toolbar->navigation_state = BROWSER_NAVIGATION_IDLE;
  toolbar->http_status = 0;
  toolbar->detail[0] = '\0';
}

void capygfx_toolbar_set_detail(struct capygfx_toolbar *toolbar,
                               const char *detail) {
  size_t i = 0u;
  if (!toolbar) return;
  if (!detail) {
    toolbar->detail[0] = '\0';
    return;
  }
  while (detail[i] && i + 1u < sizeof(toolbar->detail)) {
    toolbar->detail[i] = detail[i];
    i++;
  }
  toolbar->detail[i] = '\0';
}

int capygfx_toolbar_set_address(struct capygfx_toolbar *toolbar,
                               const char *address) {
  char copy[BROWSER_NAVIGATION_URL_MAX];
  size_t len = 0u;
  if (!toolbar || !address || !tb_copy(copy, sizeof(copy), address, &len))
    return 0;
  (void)tb_copy(toolbar->address, sizeof(toolbar->address), copy, NULL);
  toolbar->address_len = len;
  toolbar->cursor = len;
  toolbar->select_all = 0;
  return 1;
}

void capygfx_toolbar_sync_navigation(
    struct capygfx_toolbar *toolbar,
    const struct browser_navigation *navigation) {
  if (!toolbar || !navigation) return;
  toolbar->can_back = browser_navigation_can_back(navigation);
  toolbar->can_forward = browser_navigation_can_forward(navigation);
  toolbar->navigation_state = navigation->state;
  toolbar->http_status = navigation->last_http_status;
  if (navigation->state == BROWSER_NAVIGATION_READY &&
      !toolbar->address_focused &&
      navigation->current_url[0] != '\0')
    (void)capygfx_toolbar_set_address(toolbar, navigation->current_url);
}

static int tb_contains(const struct capygfx_toolbar_rect *r, int32_t x,
                       int32_t y) {
  uint32_t ux, uy;
  if (!r || r->width == 0u || r->height == 0u || x < 0 || y < 0) return 0;
  ux = (uint32_t)x;
  uy = (uint32_t)y;
  return ux >= r->x && uy >= r->y && ux - r->x < r->width &&
         uy - r->y < r->height;
}

enum capygfx_toolbar_action capygfx_toolbar_hit_test(
    const struct capygfx_toolbar *toolbar,
    const struct capygfx_toolbar_layout *layout, int32_t x, int32_t y) {
  if (!toolbar || !layout) return CAPYGFX_TOOLBAR_ACTION_NONE;
  if (tb_contains(&layout->back, x, y))
    return toolbar->can_back ? CAPYGFX_TOOLBAR_ACTION_BACK
                             : CAPYGFX_TOOLBAR_ACTION_NONE;
  if (tb_contains(&layout->forward, x, y))
    return toolbar->can_forward ? CAPYGFX_TOOLBAR_ACTION_FORWARD
                                : CAPYGFX_TOOLBAR_ACTION_NONE;
  if (tb_contains(&layout->reload, x, y))
    return CAPYGFX_TOOLBAR_ACTION_RELOAD;
  if (tb_contains(&layout->address, x, y))
    return CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS;
  if (tb_contains(&layout->go, x, y)) return CAPYGFX_TOOLBAR_ACTION_GO;
  return CAPYGFX_TOOLBAR_ACTION_NONE;
}

static size_t tb_visible_start(const struct capygfx_toolbar *toolbar,
                               const struct capygfx_toolbar_rect *address) {
  size_t columns;
  if (!toolbar || !address || address->width <= 8u) return 0u;
  columns = (address->width - 8u) / CAPYGFX_TOOLBAR_GLYPH_W;
  if (columns == 0u || !toolbar->address_focused || toolbar->cursor < columns)
    return 0u;
  return toolbar->cursor - columns + 1u;
}

enum capygfx_toolbar_action capygfx_toolbar_mouse_down(
    struct capygfx_toolbar *toolbar,
    const struct capygfx_toolbar_layout *layout, int32_t x, int32_t y) {
  enum capygfx_toolbar_action action;
  if (!toolbar || !layout) return CAPYGFX_TOOLBAR_ACTION_NONE;
  action = capygfx_toolbar_hit_test(toolbar, layout, x, y);
  if (action == CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS) {
    size_t start = tb_visible_start(toolbar, &layout->address);
    size_t column = 0u;
    toolbar->address_focused = 1;
    toolbar->select_all = 0;
    if (x > (int32_t)layout->address.x + 4)
      column = (size_t)(x - (int32_t)layout->address.x - 4) /
               CAPYGFX_TOOLBAR_GLYPH_W;
    toolbar->cursor = start + column;
    if (toolbar->cursor > toolbar->address_len)
      toolbar->cursor = toolbar->address_len;
  } else if (action != CAPYGFX_TOOLBAR_ACTION_NONE) {
    toolbar->address_focused = 0;
    toolbar->select_all = 0;
  }
  return action;
}

static void tb_delete_selection(struct capygfx_toolbar *toolbar) {
  if (!toolbar->select_all) return;
  toolbar->address[0] = '\0';
  toolbar->address_len = 0u;
  toolbar->cursor = 0u;
  toolbar->select_all = 0;
}

enum capygfx_toolbar_action capygfx_toolbar_key(
    struct capygfx_toolbar *toolbar, uint32_t keycode) {
  size_t i;
  if (!toolbar) return CAPYGFX_TOOLBAR_ACTION_NONE;
  if (keycode == KEY_F5)
    return CAPYGFX_TOOLBAR_ACTION_RELOAD;
  if (keycode == KEY_F6) {
    toolbar->address_focused = 1;
    toolbar->select_all = 1;
    toolbar->cursor = toolbar->address_len;
    return CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS;
  }
  if (!toolbar->address_focused) return CAPYGFX_TOOLBAR_ACTION_NONE;
  if (keycode == '\r' || keycode == '\n')
    return toolbar->address_len ? CAPYGFX_TOOLBAR_ACTION_GO
                                : CAPYGFX_TOOLBAR_ACTION_NONE;
  if (keycode == 0x1bu) {
    toolbar->address_focused = 0;
    toolbar->select_all = 0;
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode == KEY_LEFT) {
    toolbar->select_all = 0;
    if (toolbar->cursor > 0u) toolbar->cursor--;
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode == KEY_RIGHT) {
    toolbar->select_all = 0;
    if (toolbar->cursor < toolbar->address_len) toolbar->cursor++;
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode == KEY_HOME) {
    toolbar->select_all = 0;
    toolbar->cursor = 0u;
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode == KEY_END) {
    toolbar->select_all = 0;
    toolbar->cursor = toolbar->address_len;
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode == '\b' || keycode == 0x7fu) {
    if (toolbar->select_all) {
      tb_delete_selection(toolbar);
    } else if (toolbar->cursor > 0u) {
      for (i = toolbar->cursor; i <= toolbar->address_len; ++i)
        toolbar->address[i - 1u] = toolbar->address[i];
      toolbar->cursor--;
      toolbar->address_len--;
    }
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode == KEY_DELETE) {
    if (toolbar->select_all) {
      tb_delete_selection(toolbar);
    } else if (toolbar->cursor < toolbar->address_len) {
      for (i = toolbar->cursor + 1u; i <= toolbar->address_len; ++i)
        toolbar->address[i - 1u] = toolbar->address[i];
      toolbar->address_len--;
    }
    return CAPYGFX_TOOLBAR_ACTION_NONE;
  }
  if (keycode >= 0x20u && keycode <= 0x7eu) {
    tb_delete_selection(toolbar);
    if (toolbar->address_len + 1u >= sizeof(toolbar->address))
      return CAPYGFX_TOOLBAR_ACTION_NONE;
    for (i = toolbar->address_len + 1u; i > toolbar->cursor; --i)
      toolbar->address[i] = toolbar->address[i - 1u];
    toolbar->address[toolbar->cursor++] = (char)keycode;
    toolbar->address_len++;
  }
  return CAPYGFX_TOOLBAR_ACTION_NONE;
}

static void tb_pixel(uint32_t *pixels, uint32_t width, uint32_t height,
                     int32_t x, int32_t y, uint32_t color) {
  if (!pixels || x < 0 || y < 0 || (uint32_t)x >= width ||
      (uint32_t)y >= height)
    return;
  pixels[(size_t)(uint32_t)y * width + (uint32_t)x] = color;
}

static void tb_fill(uint32_t *pixels, uint32_t width, uint32_t height,
                    const struct capygfx_toolbar_rect *r, uint32_t color) {
  uint32_t y, x;
  if (!r) return;
  for (y = 0u; y < r->height && r->y + y < height; ++y)
    for (x = 0u; x < r->width && r->x + x < width; ++x)
      pixels[(size_t)(r->y + y) * width + r->x + x] = color;
}

static void tb_border(uint32_t *pixels, uint32_t width, uint32_t height,
                      const struct capygfx_toolbar_rect *r, uint32_t color) {
  uint32_t x, y;
  if (!r || r->width == 0u || r->height == 0u) return;
  for (x = 0u; x < r->width; ++x) {
    tb_pixel(pixels, width, height, (int32_t)(r->x + x), (int32_t)r->y, color);
    tb_pixel(pixels, width, height, (int32_t)(r->x + x),
             (int32_t)(r->y + r->height - 1u), color);
  }
  for (y = 0u; y < r->height; ++y) {
    tb_pixel(pixels, width, height, (int32_t)r->x, (int32_t)(r->y + y), color);
    tb_pixel(pixels, width, height, (int32_t)(r->x + r->width - 1u),
             (int32_t)(r->y + y), color);
  }
}

static void tb_glyph(uint32_t *pixels, uint32_t width, uint32_t height,
                     int32_t x, int32_t y, unsigned char ch, uint32_t color) {
  uint32_t gy, gx;
  const uint8_t *glyph = font8x8_basic[ch & 0x7fu];
  for (gy = 0u; gy < 8u; ++gy)
    for (gx = 0u; gx < 8u; ++gx)
      if (glyph[gy] & (uint8_t)(0x80u >> gx))
        tb_pixel(pixels, width, height, x + (int32_t)gx, y + (int32_t)gy,
                 color);
}

static void tb_text(uint32_t *pixels, uint32_t width, uint32_t height, int32_t x,
                    int32_t y, const char *text, size_t max_chars,
                    uint32_t color) {
  size_t i = 0u;
  if (!text) return;
  while (text[i] && i < max_chars) {
    tb_glyph(pixels, width, height, x + (int32_t)(i * 8u), y,
             (unsigned char)text[i], color);
    i++;
  }
}

static int tb_state_is_error(enum browser_navigation_state state) {
  return state >= BROWSER_NAVIGATION_INVALID_URL &&
         state <= BROWSER_NAVIGATION_NO_HISTORY;
}

static void tb_status_text(const struct capygfx_toolbar *toolbar, char *out,
                           size_t cap) {
  const char *name = toolbar->detail[0]
                         ? toolbar->detail
                         : browser_navigation_state_name(toolbar->navigation_state);
  size_t pos = 0u;
  char digits[12];
  size_t count = 0u;
  unsigned int status;
  while (name[pos] && pos + 1u < cap) {
    out[pos] = name[pos];
    pos++;
  }
  if (toolbar->http_status > 0 && pos + 6u < cap) {
    static const char suffix[] = " HTTP ";
    size_t i;
    for (i = 0u; suffix[i] && pos + 1u < cap; ++i) out[pos++] = suffix[i];
    status = (unsigned int)toolbar->http_status;
    do {
      digits[count++] = (char)('0' + status % 10u);
      status /= 10u;
    } while (status && count < sizeof(digits));
    while (count > 0u && pos + 1u < cap) out[pos++] = digits[--count];
  }
  if (cap > 0u) out[pos < cap ? pos : cap - 1u] = '\0';
}

int capygfx_toolbar_draw(const struct capygfx_toolbar *toolbar, uint32_t *pixels,
                         uint32_t width, uint32_t height) {
  struct capygfx_toolbar_layout layout;
  struct capygfx_toolbar_rect whole;
  size_t start, columns, shown;
  char status_text[CAPYGFX_TOOLBAR_DETAIL_MAX];
  uint32_t status_color;
  if (!toolbar || !pixels || width == 0u || height == 0u) return -1;
  capygfx_toolbar_compute_layout(width, height, &layout);
  whole = tb_rect(0u, 0u, width, layout.toolbar_height, width, height);
  tb_fill(pixels, width, height, &whole, TB_BG);

  tb_fill(pixels, width, height, &layout.back,
          toolbar->can_back ? TB_BUTTON : TB_BUTTON_DISABLED);
  tb_fill(pixels, width, height, &layout.forward,
          toolbar->can_forward ? TB_BUTTON : TB_BUTTON_DISABLED);
  tb_fill(pixels, width, height, &layout.reload, TB_BUTTON);
  tb_fill(pixels, width, height, &layout.go, TB_BUTTON);
  tb_fill(pixels, width, height, &layout.address,
          toolbar->address_focused ? TB_ADDRESS_FOCUS : TB_ADDRESS);
  tb_border(pixels, width, height, &layout.back, TB_BORDER);
  tb_border(pixels, width, height, &layout.forward, TB_BORDER);
  tb_border(pixels, width, height, &layout.reload, TB_BORDER);
  tb_border(pixels, width, height, &layout.address, TB_BORDER);
  tb_border(pixels, width, height, &layout.go, TB_BORDER);
  if (layout.back.width >= 12u)
    tb_text(pixels, width, height, (int32_t)layout.back.x + 8,
            (int32_t)layout.back.y + 6, "<", 1u, TB_TEXT);
  if (layout.forward.width >= 12u)
    tb_text(pixels, width, height, (int32_t)layout.forward.x + 8,
            (int32_t)layout.forward.y + 6, ">", 1u, TB_TEXT);
  if (layout.reload.width >= 12u)
    tb_text(pixels, width, height, (int32_t)layout.reload.x + 8,
            (int32_t)layout.reload.y + 6, "R", 1u, TB_TEXT);
  if (layout.go.width >= 20u)
    tb_text(pixels, width, height, (int32_t)layout.go.x + 7,
            (int32_t)layout.go.y + 6, "Go", 2u, TB_TEXT);

  start = tb_visible_start(toolbar, &layout.address);
  columns = layout.address.width > 8u
                ? (layout.address.width - 8u) / CAPYGFX_TOOLBAR_GLYPH_W
                : 0u;
  shown = toolbar->address_len > start ? toolbar->address_len - start : 0u;
  if (shown > columns) shown = columns;
  if (shown > 0u)
    tb_text(pixels, width, height, (int32_t)layout.address.x + 4,
            (int32_t)layout.address.y + 6, toolbar->address + start, shown,
            TB_TEXT);
  if (toolbar->address_focused && !toolbar->select_all && columns > 0u &&
      layout.address.height >= 8u &&
      toolbar->cursor >= start && toolbar->cursor - start <= columns) {
    int32_t caret_x = (int32_t)layout.address.x + 4 +
                      (int32_t)((toolbar->cursor - start) * 8u);
    int32_t caret_y;
    for (caret_y = (int32_t)layout.address.y + 4;
         caret_y < (int32_t)(layout.address.y + layout.address.height - 3u);
         ++caret_y)
      tb_pixel(pixels, width, height, caret_x, caret_y, TB_TEXT);
  }

  tb_status_text(toolbar, status_text, sizeof(status_text));
  status_color = tb_state_is_error(toolbar->navigation_state)
                     ? TB_ERROR
                     : (toolbar->navigation_state == BROWSER_NAVIGATION_READY &&
                                toolbar->address_len >= 8u &&
                                toolbar->address[4] == 's'
                            ? TB_SECURE
                            : TB_MUTED);
  if (layout.status.width > 8u)
    tb_text(pixels, width, height, (int32_t)layout.status.x + 2,
            (int32_t)layout.status.y + 3, status_text,
            layout.status.width / 8u, status_color);
  return 0;
}

int capygfx_toolbar_link_hit_test(const struct capy_dl *display_list,
                                  int32_t window_x, int32_t window_y,
                                  uint32_t page_y, int32_t scroll_x,
                                  int32_t scroll_y, uint32_t cell_width,
                                  uint32_t cell_height, char *out_url,
                                  size_t out_url_cap) {
  int64_t page_x;
  int64_t page_pixel_y;
  int64_t cell_x;
  int64_t cell_y;
  size_t i;
  if (!out_url || out_url_cap == 0u) return 0;
  out_url[0] = '\0';
  if (!display_list || display_list->version != CAPY_DL_VERSION ||
      window_x < 0 || window_y < 0 || (uint32_t)window_y < page_y ||
      cell_width == 0u || cell_height == 0u)
    return 0;
  page_x = (int64_t)window_x + (int64_t)scroll_x;
  page_pixel_y = (int64_t)window_y - (int64_t)page_y + (int64_t)scroll_y;
  if (page_x < 0 || page_pixel_y < 0) return 0;
  cell_x = page_x / (int64_t)cell_width;
  cell_y = page_pixel_y / (int64_t)cell_height;
  for (i = display_list->node_count; i > 0u; --i) {
    const struct capy_dl_node *node = &display_list->nodes[i - 1u];
    if (node->kind != CAPY_DL_LINK || node->x < 0 || node->y < 0 ||
        node->width <= 0 || node->height <= 0)
      continue;
    if (cell_x < node->x || cell_y < node->y ||
        cell_x - node->x >= node->width || cell_y - node->y >= node->height)
      continue;
    if (node->url_len == 0u || node->url_off > display_list->string_len ||
        node->url_len > display_list->string_len - node->url_off ||
        node->url_len + 1u > out_url_cap)
      return 0;
    for (size_t j = 0u; j < node->url_len; ++j)
      out_url[j] = display_list->strings[node->url_off + j];
    out_url[node->url_len] = '\0';
    return 1;
  }
  return 0;
}
