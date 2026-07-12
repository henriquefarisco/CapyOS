#include "browser_navigation.h"

#include <stdio.h>
#include <string.h>

static int g_runs;
static int g_failures;

#define CHECK(condition, message)                                             \
  do {                                                                        \
    g_runs++;                                                                 \
    if (!(condition)) {                                                       \
      g_failures++;                                                           \
      printf("[FAIL] browser-navigation: %s (line %d)\n", message, __LINE__); \
    }                                                                         \
  } while (0)

struct fetch_step {
  const char *url;
  int force_reload;
  int rc;
  int status;
  const char *location;
  const char *body;
  const char *content_type;
  int truncated;
};

struct scripted_fetch {
  const struct fetch_step *steps;
  size_t count;
  size_t call;
  int mismatch;
};

static int scripted_fetch(void *opaque, const char *url, int force_reload,
                          struct browser_navigation_response *out) {
  struct scripted_fetch *ctx = (struct scripted_fetch *)opaque;
  const struct fetch_step *step;
  if (!ctx || ctx->call >= ctx->count) return -999;
  step = &ctx->steps[ctx->call++];
  if (strcmp(url, step->url) != 0 || force_reload != step->force_reload)
    ctx->mismatch = 1;
  if (step->rc != 0) return step->rc;
  out->status_code = step->status;
  out->location = step->location;
  out->body = (const uint8_t *)step->body;
  out->body_len = step->body ? strlen(step->body) : 0u;
  out->content_type = step->content_type;
  out->truncated = step->truncated;
  return 0;
}

static void test_normalize_and_resolve(void) {
  char out[BROWSER_NAVIGATION_URL_MAX];
  char tiny[8] = "keep";

  CHECK(browser_navigation_normalize_input(" example.COM/a/../B#frag ", out,
                                           sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/B") == 0,
        "bare input promotes to HTTPS, trims, canonicalizes and strips fragment");
  CHECK(browser_navigation_normalize_input("HTTP://EXAMPLE.COM", out,
                                           sizeof(out)) > 0 &&
            strcmp(out, "http://example.com/") == 0,
        "explicit HTTP remains HTTP and gains root path");
  CHECK(browser_navigation_normalize_input("//EXAMPLE.com/p", out,
                                           sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/p") == 0,
        "scheme-relative user input promotes to HTTPS");
  CHECK(browser_navigation_normalize_input("localhost:8080/a", out,
                                           sizeof(out)) > 0 &&
            strcmp(out, "https://localhost:8080/a") == 0,
        "bare hostname with decimal port is accepted");
  CHECK(browser_navigation_normalize_input("ftp://example.com", out,
                                           sizeof(out)) < 0 && out[0] == '\0',
        "unsupported explicit scheme is rejected atomically");
  CHECK(browser_navigation_normalize_input("https://user@example.com/", out,
                                           sizeof(out)) < 0 && out[0] == '\0',
        "userinfo is rejected");
  CHECK(browser_navigation_normalize_input("example.com", tiny,
                                           sizeof(tiny)) < 0 && tiny[0] == '\0',
        "small destination fails without a truncated URL");

  CHECK(browser_navigation_resolve_redirect(
            "https://example.com/a/b?old=1", "../c", out, sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/c") == 0,
        "path-relative redirect resolves dot segments");
  CHECK(browser_navigation_resolve_redirect(
            "https://example.com/a/b/page", "../c", out, sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/a/c") == 0,
        "dot segment pop preserves exactly one path separator");
  CHECK(browser_navigation_resolve_redirect(
            "https://example.com/a/b?old=1", "/root", out, sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/root") == 0,
        "root-relative redirect");
  CHECK(browser_navigation_resolve_redirect(
            "https://example.com/a/b?old=1", "?new=2", out, sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/a/b?new=2") == 0,
        "query-only redirect replaces query");
  CHECK(browser_navigation_resolve_redirect(
            "https://example.com/a/b?old=1", "#anchor", out, sizeof(out)) > 0 &&
            strcmp(out, "https://example.com/a/b?old=1") == 0,
        "fragment-only redirect preserves query and strips fragment");
  CHECK(browser_navigation_resolve_redirect(
            "http://example.com/a", "//CDN.example/x", out, sizeof(out)) > 0 &&
            strcmp(out, "http://cdn.example/x") == 0,
        "scheme-relative redirect inherits base scheme");
  CHECK(browser_navigation_resolve_redirect(
            "https://example.com/a", "javascript:alert(1)", out,
            sizeof(out)) < 0 && out[0] == '\0',
        "unsafe redirect scheme is rejected");
}

static void test_redirect_success(void) {
  static const struct fetch_step steps[] = {
      {"https://example.com/", 0, 0, 302, "/final", "", "text/html", 0},
      {"https://example.com/final", 0, 0, 200, NULL, "FINAL", "text/html", 0},
  };
  static struct browser_navigation nav;
  struct scripted_fetch ctx = {steps, 2u, 0u, 0};
  browser_navigation_init(&nav);
  CHECK(browser_navigation_navigate(&nav, "example.com", scripted_fetch, &ctx) ==
            0,
        "navigation follows a relative redirect");
  CHECK(ctx.call == 2u && !ctx.mismatch, "redirect fetch sequence is exact");
  CHECK(nav.state == BROWSER_NAVIGATION_READY &&
            strcmp(nav.current_url, "https://example.com/final") == 0 &&
            nav.last_http_status == 200 && nav.last_redirect_count == 1u,
        "final effective URL and typed ready state committed");
  CHECK(nav.history_count == 1u && nav.history_index == 0u &&
            strcmp(nav.history[0], nav.current_url) == 0,
        "only final effective URL enters history");
  CHECK(nav.body_len == 5u && memcmp(nav.body, "FINAL", 5u) == 0 &&
            strcmp(nav.content_type, "text/html") == 0,
        "final response view committed");
}

static void test_redirect_failures_are_transactional(void) {
  static const struct fetch_step seed[] = {
      {"https://safe.example/", 0, 0, 200, NULL, "SAFE", "text/html", 0},
  };
  static const struct fetch_step loop[] = {
      {"https://loop.example/a", 0, 0, 302, "/b", "", NULL, 0},
      {"https://loop.example/b", 0, 0, 302, "/a", "", NULL, 0},
  };
  static const struct fetch_step downgrade[] = {
      {"https://secure.example/", 0, 0, 302, "http://secure.example/plain", "",
       NULL, 0},
  };
  static const struct fetch_step invalid[] = {
      {"https://bad.example/", 0, 0, 302, "javascript:bad()", "", NULL, 0},
  };
  static struct browser_navigation nav;
  struct scripted_fetch seed_ctx = {seed, 1u, 0u, 0};
  struct scripted_fetch loop_ctx = {loop, 2u, 0u, 0};
  struct scripted_fetch down_ctx = {downgrade, 1u, 0u, 0};
  struct scripted_fetch bad_ctx = {invalid, 1u, 0u, 0};
  const uint8_t *old_body;
  size_t old_index;

  browser_navigation_init(&nav);
  CHECK(browser_navigation_navigate(&nav, "safe.example", scripted_fetch,
                                    &seed_ctx) == 0,
        "transaction fixture loads");
  old_body = nav.body;
  old_index = nav.history_index;

  CHECK(browser_navigation_navigate(&nav, "https://loop.example/a",
                                    scripted_fetch, &loop_ctx) < 0 &&
            nav.state == BROWSER_NAVIGATION_REDIRECT_LOOP,
        "redirect loop is detected before a third fetch");
  CHECK(loop_ctx.call == 2u && !loop_ctx.mismatch &&
            strcmp(nav.current_url, "https://safe.example/") == 0 &&
            nav.body == old_body && nav.history_index == old_index &&
            nav.history_count == 1u,
        "loop failure preserves current document and history");

  CHECK(browser_navigation_navigate(&nav, "secure.example", scripted_fetch,
                                    &down_ctx) < 0 &&
            nav.state == BROWSER_NAVIGATION_DOWNGRADE_BLOCKED &&
            down_ctx.call == 1u,
        "HTTPS to HTTP redirect is blocked before downgrade fetch");
  CHECK(strcmp(nav.current_url, "https://safe.example/") == 0 &&
            nav.history_count == 1u,
        "downgrade failure is transactional");

  CHECK(browser_navigation_navigate(&nav, "bad.example", scripted_fetch,
                                    &bad_ctx) < 0 &&
            nav.state == BROWSER_NAVIGATION_REDIRECT_INVALID,
        "invalid redirect target has a dedicated state");
}

struct redirect_limit_ctx {
  unsigned int calls;
  char location[32];
};

static int redirect_limit_fetch(void *opaque, const char *url, int force_reload,
                                struct browser_navigation_response *out) {
  struct redirect_limit_ctx *ctx = (struct redirect_limit_ctx *)opaque;
  unsigned int n = ctx->calls++;
  (void)url;
  (void)force_reload;
  snprintf(ctx->location, sizeof(ctx->location), "/%u", n + 1u);
  out->status_code = 302;
  out->location = ctx->location;
  out->body = NULL;
  out->body_len = 0u;
  out->content_type = NULL;
  out->truncated = 0;
  return 0;
}

static void test_redirect_limit(void) {
  static struct browser_navigation nav;
  struct redirect_limit_ctx ctx = {0u, {0}};
  browser_navigation_init(&nav);
  CHECK(browser_navigation_navigate(&nav, "https://limit.example/0",
                                    redirect_limit_fetch, &ctx) < 0 &&
            nav.state == BROWSER_NAVIGATION_REDIRECT_LIMIT,
        "ninth redirect is rejected by the eight-hop bound");
  CHECK(ctx.calls == BROWSER_NAVIGATION_REDIRECT_MAX + 1u &&
            nav.last_redirect_count == BROWSER_NAVIGATION_REDIRECT_MAX,
        "redirect bound has deterministic fetch count");
}

struct simple_fetch_ctx {
  int calls;
  int force_calls;
  const char *fail_url;
  int fail_rc;
  const char *truncate_url;
  const char *http_error_url;
  char body[64];
};

static int simple_fetch(void *opaque, const char *url, int force_reload,
                        struct browser_navigation_response *out) {
  struct simple_fetch_ctx *ctx = (struct simple_fetch_ctx *)opaque;
  ctx->calls++;
  if (force_reload) ctx->force_calls++;
  if (ctx->fail_url && strcmp(url, ctx->fail_url) == 0) return ctx->fail_rc;
  snprintf(ctx->body, sizeof(ctx->body), "body:%d", ctx->calls);
  out->status_code = ctx->http_error_url && strcmp(url, ctx->http_error_url) == 0
                         ? 404
                         : 200;
  out->location = NULL;
  out->body = (const uint8_t *)ctx->body;
  out->body_len = strlen(ctx->body);
  out->content_type = "text/html; charset=utf-8";
  out->truncated = ctx->truncate_url && strcmp(url, ctx->truncate_url) == 0;
  return 0;
}

static void test_history_reload_and_failure(void) {
  static struct browser_navigation nav;
  struct simple_fetch_ctx ctx = {0, 0, NULL, -77, NULL, NULL, {0}};
  size_t generation;
  browser_navigation_init(&nav);
  CHECK(browser_navigation_navigate(&nav, "a.example", simple_fetch, &ctx) == 0 &&
            browser_navigation_navigate(&nav, "b.example", simple_fetch, &ctx) ==
                0 &&
            browser_navigation_navigate(&nav, "c.example", simple_fetch, &ctx) ==
                0,
        "three direct navigations commit");
  CHECK(nav.history_count == 3u && nav.history_index == 2u &&
            browser_navigation_can_back(&nav) &&
            !browser_navigation_can_forward(&nav),
        "history cursor/buttons after direct navigation");
  CHECK(browser_navigation_back(&nav, simple_fetch, &ctx) == 0 &&
            strcmp(nav.current_url, "https://b.example/") == 0 &&
            nav.history_index == 1u && browser_navigation_can_forward(&nav),
        "back loads then moves cursor");
  CHECK(browser_navigation_forward(&nav, simple_fetch, &ctx) == 0 &&
            strcmp(nav.current_url, "https://c.example/") == 0 &&
            nav.history_index == 2u,
        "forward loads then moves cursor");
  CHECK(browser_navigation_back(&nav, simple_fetch, &ctx) == 0 &&
            browser_navigation_navigate(&nav, "d.example", simple_fetch, &ctx) ==
                0 &&
            nav.history_count == 3u && nav.history_index == 2u &&
            strcmp(nav.history[2], "https://d.example/") == 0 &&
            !browser_navigation_can_forward(&nav),
        "new navigation drops only the forward branch");

  generation = (size_t)nav.document_generation;
  CHECK(browser_navigation_reload(&nav, simple_fetch, &ctx) == 0 &&
            ctx.force_calls == 1 && nav.history_count == 3u &&
            nav.history_index == 2u && nav.document_generation == generation + 1u,
        "reload forces fetch without appending history");

  ctx.fail_url = "https://b.example/";
  CHECK(browser_navigation_back(&nav, simple_fetch, &ctx) < 0 &&
            nav.state == BROWSER_NAVIGATION_FETCH_ERROR &&
            nav.last_fetch_error == -77,
        "failed back has typed transport state");
  CHECK(strcmp(nav.current_url, "https://d.example/") == 0 &&
            nav.history_index == 2u && nav.history_count == 3u,
        "failed back preserves document and history cursor");
}

static void test_http_and_size_errors(void) {
  static struct browser_navigation nav;
  struct simple_fetch_ctx ctx = {0, 0, NULL, -1, NULL, NULL, {0}};
  browser_navigation_init(&nav);
  CHECK(browser_navigation_navigate(&nav, "ok.example", simple_fetch, &ctx) == 0,
        "error fixture loads current page");
  ctx.http_error_url = "https://missing.example/";
  CHECK(browser_navigation_navigate(&nav, "missing.example", simple_fetch,
                                    &ctx) < 0 &&
            nav.state == BROWSER_NAVIGATION_HTTP_ERROR &&
            nav.last_http_status == 404 && nav.history_count == 1u,
        "non-2xx has typed HTTP error and is not committed");
  ctx.http_error_url = NULL;
  ctx.truncate_url = "https://large.example/";
  CHECK(browser_navigation_navigate(&nav, "large.example", simple_fetch, &ctx) <
            0 &&
            nav.state == BROWSER_NAVIGATION_TOO_LARGE && nav.history_count == 1u,
        "truncated response fails closed as TOO_LARGE");
  CHECK(strcmp(nav.current_url, "https://ok.example/") == 0,
        "HTTP/size errors preserve previous URL");
}

static void test_history_bound_and_names(void) {
  static struct browser_navigation nav;
  struct simple_fetch_ctx ctx = {0, 0, NULL, -1, NULL, NULL, {0}};
  char url[64];
  unsigned int i;
  browser_navigation_init(&nav);
  for (i = 0u; i < BROWSER_NAVIGATION_HISTORY_MAX + 2u; ++i) {
    snprintf(url, sizeof(url), "site%u.example", i);
    CHECK(browser_navigation_navigate(&nav, url, simple_fetch, &ctx) == 0,
          "bounded history fixture navigation");
  }
  CHECK(nav.history_count == BROWSER_NAVIGATION_HISTORY_MAX &&
            nav.history_index == BROWSER_NAVIGATION_HISTORY_MAX - 1u,
        "history remains fixed-capacity");
  CHECK(strcmp(nav.history[0], "https://site2.example/") == 0 &&
            strcmp(nav.current_url, "https://site17.example/") == 0,
        "history evicts oldest entries deterministically");
  CHECK(strcmp(browser_navigation_state_name(BROWSER_NAVIGATION_READY), "READY") ==
            0 &&
            strcmp(browser_navigation_state_name(
                       BROWSER_NAVIGATION_UNSUPPORTED_CONTENT),
                   "UNSUPPORTED_CONTENT") == 0 &&
            strcmp(browser_navigation_state_name(BROWSER_NAVIGATION_RENDER_ERROR),
                   "RENDER_ERROR") == 0 &&
            strcmp(browser_navigation_state_name((enum browser_navigation_state)99),
                   "UNKNOWN") == 0,
        "state names are stable and total");
}

int main(void) {
  test_normalize_and_resolve();
  test_redirect_success();
  test_redirect_failures_are_transactional();
  test_redirect_limit();
  test_history_reload_and_failure();
  test_http_and_size_errors();
  test_history_bound_and_names();
  printf("[browser-navigation] %d checks, %d failures\n", g_runs, g_failures);
  return g_failures ? 1 : 0;
}
