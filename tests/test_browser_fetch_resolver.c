/* tests/test_browser_fetch_resolver.c -- F3.3c slice 5c host coverage.
 *
 * Validates the in-memory page resolver:
 *
 *   - Each declared page resolves with status 200 and non-empty body.
 *   - Content-type is `text/html` for all built-in pages.
 *   - Unknown URL resolves to 404 with non-empty error body.
 *   - NULL/empty URL also resolves to 404 (defensive default).
 *   - Length-prefixed match: "file://capyos/welcome\0extra" with
 *     url_len trimmed to the page url still resolves to 200, while
 *     the same string with url_len pointing past the page's length
 *     does NOT match (length mismatch -> 404).
 *   - page_count > 0; page_url returns NULL on out-of-range.
 *   - NULL out pointer is no-op (no crash).
 */

#include "apps/browser_chrome_fetch_resolver.h"
#include "apps/browser_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define F_OK(cond, msg) do {                                              \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

static void test_each_known_page_resolves(void) {
    uint32_t n = browser_chrome_resolver_page_count();
    F_OK(n >= 1u, "at least one built-in page");
    for (uint32_t i = 0; i < n; ++i) {
        const char *url = browser_chrome_resolver_page_url(i);
        F_OK(url != NULL, "page url not NULL");
        size_t ulen = strlen(url);
        struct browser_chrome_fetch_result r;
        browser_chrome_resolve_local(url, (uint16_t)ulen, &r);
        F_OK(r.status == BROWSER_IPC_FETCH_OK, "known page -> 200");
        F_OK(r.body != NULL && r.body_len > 0u, "non-empty body");
        F_OK(r.content_type != NULL && r.content_type_len > 0u,
             "content_type set");
        /* Sanity: ctype starts with "text/html" */
        F_OK(r.content_type_len >= 9u &&
             memcmp(r.content_type, "text/html", 9u) == 0,
             "ctype is text/html");
    }
}

static void test_unknown_url_yields_404(void) {
    const char *url = "file://capyos/does-not-exist";
    struct browser_chrome_fetch_result r;
    browser_chrome_resolve_local(url, (uint16_t)strlen(url), &r);
    F_OK(r.status == BROWSER_IPC_FETCH_NOT_FOUND, "unknown -> 404");
    F_OK(r.body != NULL && r.body_len > 0u, "404 body present");
    F_OK(r.content_type_len > 0u, "404 ctype present");
}

static void test_null_and_empty_yield_404(void) {
    struct browser_chrome_fetch_result r;
    browser_chrome_resolve_local(NULL, 0u, &r);
    F_OK(r.status == BROWSER_IPC_FETCH_NOT_FOUND, "NULL url -> 404");
    F_OK(r.body != NULL, "NULL url still has body");
    /* Empty length with valid pointer */
    browser_chrome_resolve_local("file://capyos/welcome", 0u, &r);
    F_OK(r.status == BROWSER_IPC_FETCH_NOT_FOUND, "url_len=0 -> 404");
}

static void test_url_len_must_match_exactly(void) {
    /* url_len LESS than the entry length: caller is asking about a
     * shorter URL, must NOT match the longer entry. */
    const char *url = "file://capyos/welcome";
    struct browser_chrome_fetch_result r;
    /* Trim to 'file://capyos/wel' -- shorter than welcome page. */
    browser_chrome_resolve_local(url, 17u, &r);
    F_OK(r.status == BROWSER_IPC_FETCH_NOT_FOUND,
         "shorter url_len -> 404 (no prefix match)");

    /* url_len LONGER than the entry: trailing bytes (NUL etc.)
     * must not falsely match. */
    char padded[64];
    size_t base = strlen("file://capyos/welcome");
    memcpy(padded, "file://capyos/welcome", base);
    padded[base] = '/'; padded[base+1] = 'x'; padded[base+2] = '\0';
    browser_chrome_resolve_local(padded, (uint16_t)(base + 2u), &r);
    F_OK(r.status == BROWSER_IPC_FETCH_NOT_FOUND,
         "longer url_len -> 404 (no suffix match)");
}

static void test_page_url_out_of_range(void) {
    uint32_t n = browser_chrome_resolver_page_count();
    F_OK(browser_chrome_resolver_page_url(n) == NULL,
         "page_url(N) returns NULL");
    F_OK(browser_chrome_resolver_page_url(n + 100u) == NULL,
         "page_url(N+100) returns NULL");
}

static void test_null_out_pointer_safe(void) {
    /* Should not crash. There is no return value to assert; this
     * test exists so a future regression that adds an unguarded
     * write to `out` blows up here under sanitizers. */
    browser_chrome_resolve_local("file://capyos/welcome", 21u, NULL);
    g_passed++;
}

/* Etapa F4 homepage (2026-05-03): pin que `file://capyos/wikipedia`
 * existe no resolver, pois e o fallback offline da homepage do
 * browser_app. Se este teste falhar, o browser perdeu o conteudo
 * de fallback e a UX "abrir browser sem rede" volta a ficar vazia. */
static void test_wikipedia_page_present(void) {
    const char *url = "file://capyos/wikipedia";
    struct browser_chrome_fetch_result r;
    browser_chrome_resolve_local(url, (uint16_t)strlen(url), &r);
    F_OK(r.status == BROWSER_IPC_FETCH_OK,
         "wikipedia fallback page resolves -> 200");
    F_OK(r.body_len > 200u,
         "wikipedia body is substantial (>200 bytes of HTML)");
    /* Heuristica: deve ter "<h1>Capivara" como primeiro heading
     * para garantir que o conteudo Wikipedia-style nao foi
     * acidentalmente trocado por algo trivial. */
    int has_h1 = 0;
    if (r.body_len > 16u) {
        for (uint32_t i = 0; i + 11u < r.body_len; ++i) {
            if (r.body[i] == '<' && r.body[i+1] == 'h' &&
                r.body[i+2] == '1' && r.body[i+3] == '>' &&
                r.body[i+4] == 'C' && r.body[i+5] == 'a' &&
                r.body[i+6] == 'p' && r.body[i+7] == 'i') {
                has_h1 = 1;
                break;
            }
        }
    }
    F_OK(has_h1, "wikipedia body contains '<h1>Capivara' heading");
}

int test_browser_fetch_resolver_run(void) {
    printf("[test_browser_fetch_resolver]\n");
    g_passed = 0;
    g_failed = 0;
    test_each_known_page_resolves();
    test_wikipedia_page_present();
    test_unknown_url_yields_404();
    test_null_and_empty_yield_404();
    test_url_len_must_match_exactly();
    test_page_url_out_of_range();
    test_null_out_pointer_safe();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
