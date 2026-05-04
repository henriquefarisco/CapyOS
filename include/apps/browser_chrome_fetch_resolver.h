#ifndef APPS_BROWSER_CHROME_FETCH_RESOLVER_H
#define APPS_BROWSER_CHROME_FETCH_RESOLVER_H

/*
 * Browser chrome fetch resolver (F3.3c slice 5c).
 *
 * In-memory page table mapping `file://capyos/<page>` URLs to
 * canned (content_type, body, status) tuples. Used by the chrome
 * runtime to answer EVENT_FETCH_REQUEST without going through
 * the network stack -- which is the right design until F4 ships
 * `libcapy-net` and we can route real HTTP through the engine.
 *
 * Pure host-portable C: no kernel deps, no allocations. The
 * returned pointers reference static const storage and live for
 * the duration of the program.
 */

#include <stddef.h>
#include <stdint.h>

struct browser_chrome_fetch_result {
    uint16_t       status;            /* browser_ipc_fetch_status */
    const uint8_t *content_type;      /* borrowed; static storage */
    uint16_t       content_type_len;
    const uint8_t *body;              /* borrowed; static storage */
    uint32_t       body_len;
};

/* Resolve `url` (length-delimited, NOT NUL-terminated) into a
 * `browser_chrome_fetch_result`. Always succeeds: unknown URLs
 * resolve to a 404 result with a small HTML body so the engine
 * has something to parse and surface to the user.
 *
 * `out` must not be NULL. `url` may be NULL only if `url_len`
 * is 0, in which case the result is the 404 fallback. */
void browser_chrome_resolve_local(const char *url, uint16_t url_len,
                                  struct browser_chrome_fetch_result *out);

/* Number of entries in the built-in page table. Stable across
 * minor releases; exposed for tests. */
uint32_t browser_chrome_resolver_page_count(void);

/* Returns the URL of the i-th built-in page (i in
 * [0, page_count)). NUL-terminated, borrowed from static
 * storage. NULL on out-of-range. */
const char *browser_chrome_resolver_page_url(uint32_t i);

#endif /* APPS_BROWSER_CHROME_FETCH_RESOLVER_H */
