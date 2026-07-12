#ifndef CAPYOS_BROWSER_NAVIGATION_H
#define CAPYOS_BROWSER_NAVIGATION_H

/*
 * Pure, caller-owned navigation controller for the graphical static browser.
 *
 * Network I/O is injected through browser_navigation_fetch_fn, which keeps the
 * URL/redirect/history state machine deterministic in host tests and lets the
 * ring-3 app bind it to browser_fetch without making this module depend on a
 * socket, clock, compositor or allocator.
 */

#include <stddef.h>
#include <stdint.h>

#define BROWSER_NAVIGATION_URL_MAX 2048u
#define BROWSER_NAVIGATION_CONTENT_TYPE_MAX 128u
#define BROWSER_NAVIGATION_HISTORY_MAX 16u
#define BROWSER_NAVIGATION_REDIRECT_MAX 8u

enum browser_navigation_state {
  BROWSER_NAVIGATION_IDLE = 0,
  BROWSER_NAVIGATION_LOADING,
  BROWSER_NAVIGATION_READY,
  BROWSER_NAVIGATION_INVALID_URL,
  BROWSER_NAVIGATION_FETCH_ERROR,
  BROWSER_NAVIGATION_HTTP_ERROR,
  BROWSER_NAVIGATION_TOO_LARGE,
  BROWSER_NAVIGATION_UNSUPPORTED_CONTENT,
  BROWSER_NAVIGATION_RENDER_ERROR,
  BROWSER_NAVIGATION_REDIRECT_INVALID,
  BROWSER_NAVIGATION_REDIRECT_LOOP,
  BROWSER_NAVIGATION_REDIRECT_LIMIT,
  BROWSER_NAVIGATION_DOWNGRADE_BLOCKED,
  BROWSER_NAVIGATION_NO_HISTORY
};

/* Response view returned by the injected fetcher. All pointers remain owned by
 * the fetcher. On a successful navigation `body` must stay valid until the
 * embedding app has built its retained page/display list or starts another
 * fetch. `location` is consulted only for redirect responses. */
struct browser_navigation_response {
  int status_code;
  const uint8_t *body;
  size_t body_len;
  const char *content_type;
  const char *location;
  int truncated;
};

/* `force_reload` is 1 for every request in a reload redirect chain and 0 for a
 * normal/back/forward navigation. Return 0 when `out` is valid, nonzero for a
 * transport failure; that exact value is retained as last_fetch_error. */
typedef int (*browser_navigation_fetch_fn)(
    void *ctx, const char *url, int force_reload,
    struct browser_navigation_response *out);

struct browser_navigation {
  enum browser_navigation_state state;
  char current_url[BROWSER_NAVIGATION_URL_MAX];
  char attempted_url[BROWSER_NAVIGATION_URL_MAX];
  char content_type[BROWSER_NAVIGATION_CONTENT_TYPE_MAX];

  char history[BROWSER_NAVIGATION_HISTORY_MAX][BROWSER_NAVIGATION_URL_MAX];
  /* Redirect loop scratch is controller-owned so a ring-3 call never places
   * ~18 KiB on its deliberately small process stack. Embedders should keep the
   * complete controller in .bss/static storage. */
  char redirect_visited[BROWSER_NAVIGATION_REDIRECT_MAX + 1u]
                       [BROWSER_NAVIGATION_URL_MAX];
  size_t history_count;
  size_t history_index;

  const uint8_t *body;
  size_t body_len;
  int last_http_status;
  int last_fetch_error;
  unsigned int last_redirect_count;
  uint64_t document_generation;
};

void browser_navigation_init(struct browser_navigation *nav);

/* Canonicalize user input. Leading/trailing ASCII whitespace is trimmed; a
 * bare host/path and a scheme-relative URL are promoted to HTTPS. Only HTTP(S)
 * absolute URLs are accepted. Scheme and authority are lower-cased, fragments
 * are removed and dot path segments are collapsed. Atomic: `out` is empty on
 * failure. Returns the byte length (>0), or -1. */
int browser_navigation_normalize_input(const char *input, char *out,
                                       size_t out_cap);

/* Resolve one redirect Location against an already absolute base URL using the
 * same canonical representation as normalize_input. Supports absolute,
 * scheme-relative, root-relative, query-only and path-relative targets.
 * Returns the byte length (>0), or -1 with empty output. */
int browser_navigation_resolve_redirect(const char *base_url,
                                        const char *location, char *out,
                                        size_t out_cap);

/* A direct navigation appends/replaces the forward branch only after a final
 * 2xx response. Any fetch/redirect/HTTP failure preserves the current document
 * and history cursor. */
int browser_navigation_navigate(struct browser_navigation *nav,
                                const char *input,
                                browser_navigation_fetch_fn fetch,
                                void *fetch_ctx);
int browser_navigation_back(struct browser_navigation *nav,
                            browser_navigation_fetch_fn fetch,
                            void *fetch_ctx);
int browser_navigation_forward(struct browser_navigation *nav,
                               browser_navigation_fetch_fn fetch,
                               void *fetch_ctx);
int browser_navigation_reload(struct browser_navigation *nav,
                              browser_navigation_fetch_fn fetch,
                              void *fetch_ctx);

int browser_navigation_can_back(const struct browser_navigation *nav);
int browser_navigation_can_forward(const struct browser_navigation *nav);
const char *browser_navigation_state_name(enum browser_navigation_state state);

#endif /* CAPYOS_BROWSER_NAVIGATION_H */
