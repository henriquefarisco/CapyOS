/* Static document resource discovery for the graphical browser. */
#ifndef CAPYOS_BROWSER_RESOURCES_H
#define CAPYOS_BROWSER_RESOURCES_H

#include <stddef.h>

#include "dom.h"
#include "url_parse.h"

#define BROWSER_RESOURCES_MAX_STYLESHEETS 8u
#define BROWSER_RESOURCES_CSS_MAX 32768u
#define BROWSER_RESOURCES_URL_MAX (CAPY_URL_MAX_LEN + 1u)

struct browser_resources {
  char inline_css[BROWSER_RESOURCES_CSS_MAX];
  size_t inline_css_len;
  char stylesheet_urls[BROWSER_RESOURCES_MAX_STYLESHEETS]
                      [BROWSER_RESOURCES_URL_MAX];
  size_t stylesheet_count;
  int truncated;
};

/* Discover inline <style> bytes and absolute HTTP(S) stylesheet URLs from an
 * already parsed DOM. All output is caller-owned, bounded and reset first.
 * Relative hrefs are resolved against base_url by the released URL core.
 * Returns 0 on success (including a bounded/truncated result), -1 on bad args. */
int browser_resources_discover(const struct capy_dom_doc *dom,
                               const char *base_url,
                               struct browser_resources *out);

#endif /* CAPYOS_BROWSER_RESOURCES_H */
