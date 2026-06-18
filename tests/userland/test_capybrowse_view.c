/*
 * tests/userland/test_capybrowse_view.c — host tests for the CapyBrowse Text
 * view formatter (Etapa 6 / Slice 6.4).
 *
 * Gated by CAPYOS_HAVE_CAPYBROWSER_TEXT (set by the Makefile when the
 * CapyBrowser sibling is present): the formatter and the capy_text_doc ABI it
 * consumes live in the sibling, so when it is absent this run function is a
 * no-op and the suite still links. The run function is always defined so
 * tests/test_runner.c can call it unconditionally (mirrors the CapyUI-gated
 * tests). Under -DUNIT_TEST the freestanding userland <string.h> defers to the
 * host C library via #include_next, so strstr/strcmp/strlen are the real ones.
 */

#include <stdio.h>

#ifdef CAPYOS_HAVE_CAPYBROWSER_TEXT

#include <string.h>

#include "capybrowse_view.h"

static int g_runs;
static int g_passes;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    g_runs++;                                                                  \
    if (cond) {                                                                \
      g_passes++;                                                              \
    } else {                                                                   \
      printf("  FAIL: %s\n", (msg));                                           \
    }                                                                          \
  } while (0)

#endif /* CAPYOS_HAVE_CAPYBROWSER_TEXT */

int test_capybrowse_view_run(void) {
#ifdef CAPYOS_HAVE_CAPYBROWSER_TEXT
  printf("[test_capybrowse_view]\n");
  g_runs = 0;
  g_passes = 0;

  /* Full page: title + body (with inline [n] markers) + 2 resolved links +
   * a parse warning + truncation. */
  {
    struct capy_text_doc doc;
    char out[512];
    size_t n;
    memset(&doc, 0, sizeof(doc));
    strcpy(doc.title, "Example Page");
    doc.has_title = 1;
    doc.link_count = 2u;
    strcpy(doc.links[0].url, "https://a.example/");
    strcpy(doc.links[1].url, "https://b.example/");
    doc.warnings.count = 1u;
    doc.truncated = 1;
    n = capybrowse_format_page(&doc, "Hello [1] world [2].", out, sizeof(out));
    CHECK(n > 0u && n < sizeof(out), "returns bounded length");
    CHECK(strlen(out) == n, "NUL-terminated at returned length");
    CHECK(strstr(out, "Example Page") != NULL, "title present");
    CHECK(strstr(out, "Hello [1] world [2].") != NULL, "body present");
    CHECK(strstr(out, "Links:") != NULL, "links header present");
    CHECK(strstr(out, "[1] https://a.example/") != NULL, "link 1 resolved");
    CHECK(strstr(out, "[2] https://b.example/") != NULL, "link 2 resolved");
    CHECK(strstr(out, "1 parse warning") != NULL, "warning count present");
    CHECK(strstr(out, "truncated") != NULL, "truncation note present");
  }

  /* Body-only doc: no title/links/warnings/truncation -> no decoration. */
  {
    struct capy_text_doc d2;
    char o2[64];
    memset(&d2, 0, sizeof(d2));
    capybrowse_format_page(&d2, "plain text", o2, sizeof(o2));
    CHECK(strcmp(o2, "plain text") == 0, "body-only has no decoration");
  }

  /* Bounded: a tiny buffer never overflows and stays NUL-terminated. */
  {
    struct capy_text_doc d3;
    char o3[8];
    size_t n3;
    memset(&d3, 0, sizeof(d3));
    d3.has_title = 1;
    strcpy(d3.title, "A Long Title That Exceeds The Buffer");
    n3 = capybrowse_format_page(&d3, "body", o3, sizeof(o3));
    CHECK(n3 < sizeof(o3), "tiny buffer bounded");
    CHECK(strlen(o3) == n3, "tiny buffer NUL-terminated");
  }

  /* Safety: NULL out / zero cap return 0 without writing. */
  CHECK(capybrowse_format_page(NULL, "x", NULL, 0u) == 0u, "NULL out safe");
  {
    char o4[4];
    CHECK(capybrowse_format_page(NULL, "x", o4, 0u) == 0u, "zero cap safe");
  }

  /* NULL doc: the body is still rendered (fetch-only fallback). */
  {
    char o5[32];
    size_t n5 = capybrowse_format_page(NULL, "just body", o5, sizeof(o5));
    CHECK(n5 == 9u && strcmp(o5, "just body") == 0, "NULL doc renders body");
  }

  printf("  -> %d/%d passed\n", g_passes, g_runs);
  return g_runs - g_passes;
#else
  return 0; /* CapyBrowser text core not present in this build */
#endif
}
