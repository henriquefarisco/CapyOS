#include "toolbar.h"

#include "drivers/input/keyboard_layout.h"

#include <stdio.h>
#include <string.h>

static int g_runs;
static int g_failures;
static struct capy_dl g_dl;

#define CHECK(condition, message)                                          \
  do {                                                                     \
    g_runs++;                                                              \
    if (!(condition)) {                                                    \
      g_failures++;                                                        \
      printf("[FAIL] capygfx-toolbar: %s (line %d)\n", message, __LINE__); \
    }                                                                      \
  } while (0)

static int rect_inside(const struct capygfx_toolbar_rect *r, uint32_t w,
                       uint32_t h) {
  if (r->width == 0u || r->height == 0u) return 1;
  return r->x < w && r->y < h && r->width <= w - r->x && r->height <= h - r->y;
}

static void test_layout_and_hitboxes(void) {
  struct capygfx_toolbar toolbar;
  struct capygfx_toolbar_layout layout;
  capygfx_toolbar_init(&toolbar);
  capygfx_toolbar_compute_layout(320u, 240u, &layout);
  CHECK(layout.toolbar_height == 40u && layout.page_y == 40u &&
            layout.page_height == 200u,
        "320x240 layout reserves toolbar and page viewport");
  CHECK(rect_inside(&layout.back, 320u, 240u) &&
            rect_inside(&layout.forward, 320u, 240u) &&
            rect_inside(&layout.reload, 320u, 240u) &&
            rect_inside(&layout.address, 320u, 240u) &&
            rect_inside(&layout.go, 320u, 240u) &&
            rect_inside(&layout.status, 320u, 240u),
        "every hitbox is inside the surface");
  CHECK(layout.back.x + layout.back.width <= layout.forward.x &&
            layout.forward.x + layout.forward.width <= layout.reload.x &&
            layout.reload.x + layout.reload.width <= layout.address.x &&
            layout.address.x + layout.address.width <= layout.go.x,
        "controls do not overlap");
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout, 4, 4) ==
            CAPYGFX_TOOLBAR_ACTION_NONE,
        "disabled back hit is inert");
  toolbar.can_back = toolbar.can_forward = 1;
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout, 4, 4) ==
            CAPYGFX_TOOLBAR_ACTION_BACK,
        "enabled back hit");
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout,
                                 (int32_t)layout.forward.x + 1,
                                 (int32_t)layout.forward.y + 1) ==
            CAPYGFX_TOOLBAR_ACTION_FORWARD,
        "forward hit");
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout,
                                 (int32_t)layout.reload.x + 1, 4) ==
            CAPYGFX_TOOLBAR_ACTION_RELOAD,
        "reload hit");
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout,
                                 (int32_t)layout.address.x + 1, 4) ==
            CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS,
        "address hit");
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout, (int32_t)layout.go.x + 1,
                                 4) == CAPYGFX_TOOLBAR_ACTION_GO,
        "go hit");
  CHECK(capygfx_toolbar_hit_test(&toolbar, &layout, 319, 39) ==
            CAPYGFX_TOOLBAR_ACTION_NONE,
        "outside control rectangles is inert");

  capygfx_toolbar_compute_layout(64u, 24u, &layout);
  CHECK(layout.toolbar_height == 24u && layout.page_height == 0u &&
            rect_inside(&layout.back, 64u, 24u) &&
            rect_inside(&layout.forward, 64u, 24u) &&
            rect_inside(&layout.reload, 64u, 24u) &&
            layout.address.width == 0u && layout.go.width == 0u,
        "narrow/short layout collapses safely without overflow");
}

static void test_editing(void) {
  struct capygfx_toolbar toolbar;
  struct capygfx_toolbar_layout layout;
  char huge[BROWSER_NAVIGATION_URL_MAX + 1u];
  size_t i;
  capygfx_toolbar_init(&toolbar);
  capygfx_toolbar_compute_layout(320u, 240u, &layout);
  CHECK(capygfx_toolbar_set_address(&toolbar, "https://old.example/") == 1 &&
            toolbar.address_len == strlen("https://old.example/") &&
            toolbar.cursor == toolbar.address_len,
        "bounded setter updates length and caret");
  CHECK(capygfx_toolbar_key(&toolbar, KEY_F6) ==
            CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS &&
            toolbar.address_focused && toolbar.select_all,
        "F6 focuses and selects address");
  CHECK(capygfx_toolbar_key(&toolbar, 'a') == CAPYGFX_TOOLBAR_ACTION_NONE &&
            strcmp(toolbar.address, "a") == 0 && toolbar.cursor == 1u,
        "typing replaces F6 selection");
  (void)capygfx_toolbar_key(&toolbar, 'c');
  (void)capygfx_toolbar_key(&toolbar, 'd');
  (void)capygfx_toolbar_key(&toolbar, KEY_LEFT);
  (void)capygfx_toolbar_key(&toolbar, KEY_LEFT);
  (void)capygfx_toolbar_key(&toolbar, 'b');
  CHECK(strcmp(toolbar.address, "abcd") == 0 && toolbar.cursor == 2u,
        "printable insertion occurs at caret");
  (void)capygfx_toolbar_key(&toolbar, '\b');
  CHECK(strcmp(toolbar.address, "acd") == 0 && toolbar.cursor == 1u,
        "backspace removes previous byte");
  (void)capygfx_toolbar_key(&toolbar, KEY_DELETE);
  CHECK(strcmp(toolbar.address, "ad") == 0 && toolbar.cursor == 1u,
        "delete removes byte at caret");
  (void)capygfx_toolbar_key(&toolbar, KEY_HOME);
  (void)capygfx_toolbar_key(&toolbar, 'z');
  (void)capygfx_toolbar_key(&toolbar, KEY_END);
  CHECK(strcmp(toolbar.address, "zad") == 0 && toolbar.cursor == 3u,
        "home/end editing");
  CHECK(capygfx_toolbar_key(&toolbar, '\n') == CAPYGFX_TOOLBAR_ACTION_GO,
        "Enter submits nonempty address");
  CHECK(capygfx_toolbar_key(&toolbar, KEY_F5) ==
            CAPYGFX_TOOLBAR_ACTION_RELOAD,
        "F5 requests reload");

  (void)capygfx_toolbar_set_address(&toolbar, "https://long.example/path");
  toolbar.address_focused = 0;
  CHECK(capygfx_toolbar_mouse_down(&toolbar, &layout,
                                   (int32_t)layout.address.x + 4, 4) ==
            CAPYGFX_TOOLBAR_ACTION_FOCUS_ADDRESS &&
            toolbar.cursor == 0u,
        "address click positions caret from visible column");

  for (i = 0u; i < sizeof(huge) - 1u; ++i) huge[i] = 'x';
  huge[sizeof(huge) - 1u] = '\0';
  CHECK(capygfx_toolbar_set_address(&toolbar, huge) == 0 &&
            strcmp(toolbar.address, "https://long.example/path") == 0,
        "overflowing setter is atomic");
  huge[BROWSER_NAVIGATION_URL_MAX - 1u] = '\0';
  CHECK(capygfx_toolbar_set_address(&toolbar, huge) == 1 &&
            toolbar.address_len == BROWSER_NAVIGATION_URL_MAX - 1u,
        "maximum fitting address is accepted");
  toolbar.address_focused = 1;
  (void)capygfx_toolbar_key(&toolbar, 'q');
  CHECK(toolbar.address_len == BROWSER_NAVIGATION_URL_MAX - 1u &&
            toolbar.address[BROWSER_NAVIGATION_URL_MAX - 1u] == '\0',
        "typing at capacity is ignored without losing terminator");
}

static void test_navigation_sync(void) {
  struct capygfx_toolbar toolbar;
  static struct browser_navigation nav;
  capygfx_toolbar_init(&toolbar);
  browser_navigation_init(&nav);
  strcpy(nav.current_url, "https://ready.example/");
  strcpy(nav.history[0], "https://before.example/");
  strcpy(nav.history[1], nav.current_url);
  nav.history_count = 2u;
  nav.history_index = 1u;
  nav.state = BROWSER_NAVIGATION_READY;
  nav.last_http_status = 200;
  capygfx_toolbar_sync_navigation(&toolbar, &nav);
  CHECK(strcmp(toolbar.address, nav.current_url) == 0 && toolbar.can_back &&
            !toolbar.can_forward && toolbar.navigation_state ==
                                         BROWSER_NAVIGATION_READY &&
            toolbar.http_status == 200,
        "READY sync updates URL, buttons and status");
  toolbar.address_focused = 1;
  toolbar.select_all = 1;
  CHECK(capygfx_toolbar_key(&toolbar, 'x') == CAPYGFX_TOOLBAR_ACTION_NONE &&
            strcmp(toolbar.address, "x") == 0,
        "focused address accepts replacement typing");
  capygfx_toolbar_sync_navigation(&toolbar, &nav);
  CHECK(strcmp(toolbar.address, "x") == 0 && toolbar.address_focused,
        "READY repaint preserves focused address edits");
  CHECK(capygfx_toolbar_key(&toolbar, '\b') == CAPYGFX_TOOLBAR_ACTION_NONE &&
            strcmp(toolbar.address, "") == 0,
        "focused address accepts backspace deletion");
  capygfx_toolbar_sync_navigation(&toolbar, &nav);
  CHECK(strcmp(toolbar.address, "") == 0,
        "READY repaint preserves focused deletion");
  toolbar.address_focused = 0;
  capygfx_toolbar_sync_navigation(&toolbar, &nav);
  CHECK(strcmp(toolbar.address, nav.current_url) == 0,
        "unfocused READY sync restores canonical URL");
  nav.state = BROWSER_NAVIGATION_FETCH_ERROR;
  strcpy(nav.attempted_url, "https://failed.example/");
  capygfx_toolbar_sync_navigation(&toolbar, &nav);
  CHECK(strcmp(toolbar.address, "https://ready.example/") == 0 &&
            toolbar.navigation_state == BROWSER_NAVIGATION_FETCH_ERROR,
        "error sync preserves editable/current address");
}

static void test_status_detail_is_bounded(void) {
  struct capygfx_toolbar toolbar;
  char long_detail[CAPYGFX_TOOLBAR_DETAIL_MAX + 32u];
  size_t i;
  capygfx_toolbar_init(&toolbar);
  for (i = 0u; i + 1u < sizeof(long_detail); ++i) long_detail[i] = 'x';
  long_detail[sizeof(long_detail) - 1u] = '\0';
  capygfx_toolbar_set_detail(&toolbar, long_detail);
  CHECK(toolbar.detail[CAPYGFX_TOOLBAR_DETAIL_MAX - 1u] == '\0' &&
            strlen(toolbar.detail) == CAPYGFX_TOOLBAR_DETAIL_MAX - 1u,
        "status detail is bounded and terminated");
  capygfx_toolbar_set_detail(&toolbar, NULL);
  CHECK(toolbar.detail[0] == '\0', "NULL status detail clears message");
}

static void test_draw_is_bounded(void) {
  struct capygfx_toolbar toolbar;
  static uint32_t guarded[320u * 40u + 2u];
  uint32_t first_hash = 2166136261u;
  uint32_t second_hash = 2166136261u;
  size_t i;
  capygfx_toolbar_init(&toolbar);
  (void)capygfx_toolbar_set_address(&toolbar, "https://example.com/");
  toolbar.navigation_state = BROWSER_NAVIGATION_READY;
  toolbar.http_status = 200;
  toolbar.can_back = 1;
  guarded[0] = 0x11223344u;
  guarded[320u * 40u + 1u] = 0x55667788u;
  for (i = 1u; i <= 320u * 40u; ++i) guarded[i] = 0u;
  CHECK(capygfx_toolbar_draw(&toolbar, guarded + 1u, 320u, 40u) == 0,
        "toolbar draws into caller surface");
  CHECK(guarded[0] == 0x11223344u &&
            guarded[320u * 40u + 1u] == 0x55667788u,
        "draw does not write before/after surface");
  for (i = 1u; i <= 320u * 40u; ++i)
    first_hash = (first_hash ^ guarded[i]) * 16777619u;
  CHECK(first_hash != 2166136261u, "draw produces visible pixels");
  CHECK(capygfx_toolbar_draw(&toolbar, guarded + 1u, 320u, 40u) == 0,
        "second deterministic draw succeeds");
  for (i = 1u; i <= 320u * 40u; ++i)
    second_hash = (second_hash ^ guarded[i]) * 16777619u;
  CHECK(first_hash == second_hash, "same model renders byte-deterministically");
  toolbar.http_status = 404;
  CHECK(capygfx_toolbar_draw(&toolbar, guarded + 1u, 320u, 40u) == 0,
        "changed HTTP status redraw succeeds");
  second_hash = 2166136261u;
  for (i = 1u; i <= 320u * 40u; ++i)
    second_hash = (second_hash ^ guarded[i]) * 16777619u;
  CHECK(first_hash != second_hash, "status row visibly includes HTTP code");
  CHECK(capygfx_toolbar_draw(NULL, guarded + 1u, 320u, 40u) < 0 &&
            capygfx_toolbar_draw(&toolbar, NULL, 320u, 40u) < 0,
        "draw rejects invalid arguments");
}

static void prepare_link(size_t index, long x, long y, long w, long h,
                         const char *url) {
  size_t len = strlen(url);
  struct capy_dl_node *node = &g_dl.nodes[index];
  node->kind = CAPY_DL_LINK;
  node->x = x;
  node->y = y;
  node->width = w;
  node->height = h;
  node->url_off = g_dl.string_len;
  node->url_len = len;
  memcpy(g_dl.strings + g_dl.string_len, url, len);
  g_dl.string_len += len;
  if (g_dl.node_count <= index) g_dl.node_count = index + 1u;
}

static void test_link_hit_test(void) {
  char url[128] = "stale";
  memset(&g_dl, 0, sizeof(g_dl));
  g_dl.version = CAPY_DL_VERSION;
  prepare_link(0u, 2, 3, 4, 2, "https://first.example/");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 16, 40 + 48, 40u, 0, 0, 8u,
                                      16u, url, sizeof(url)) == 1 &&
            strcmp(url, "https://first.example/") == 0,
        "page click maps toolbar offset into link cells");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 16, 39, 40u, 0, 0, 8u, 16u,
                                      url, sizeof(url)) == 0 && url[0] == '\0',
        "toolbar click never hits document link");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 0, 40, 40u, 16, 48, 8u, 16u,
                                      url, sizeof(url)) == 1,
        "scroll offsets participate in link hit-test");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 16 + 4 * 8, 40 + 48, 40u, 0, 0,
                                      8u, 16u, url, sizeof(url)) == 0,
        "right edge is exclusive");

  prepare_link(1u, 2, 3, 4, 2, "https://top.example/");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 16, 40 + 48, 40u, 0, 0, 8u,
                                      16u, url, sizeof(url)) == 1 &&
            strcmp(url, "https://top.example/") == 0,
        "reverse traversal selects topmost overlapping link");
  strcpy(url, "stale");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 16, 40 + 48, 40u, 0, 0, 8u,
                                      16u, url, 4u) == 0 && url[0] == '\0',
        "small output fails atomically");
  g_dl.nodes[1].url_off = g_dl.string_len + 1u;
  strcpy(url, "stale");
  CHECK(capygfx_toolbar_link_hit_test(&g_dl, 16, 40 + 48, 40u, 0, 0, 8u,
                                      16u, url, sizeof(url)) == 0 &&
            url[0] == '\0',
        "corrupt topmost URL slice fails closed");
  CHECK(capygfx_toolbar_link_hit_test(NULL, 0, 0, 0u, 0, 0, 8u, 16u, url,
                                      sizeof(url)) == 0 && url[0] == '\0',
        "NULL display list fails closed");
}

int main(void) {
  test_layout_and_hitboxes();
  test_editing();
  test_navigation_sync();
  test_status_detail_is_bounded();
  test_draw_is_bounded();
  test_link_hit_test();
  printf("[capygfx-toolbar] %d checks, %d failures\n", g_runs, g_failures);
  return g_failures ? 1 : 0;
}
