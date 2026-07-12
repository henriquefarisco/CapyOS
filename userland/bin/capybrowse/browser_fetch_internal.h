#ifndef CAPYOS_BROWSER_FETCH_INTERNAL_H
#define CAPYOS_BROWSER_FETCH_INTERNAL_H

/* Internal seam between the capylibc-net wire response and the kernel-model
 * response used by http_session. It is exposed only to focused host tests; the
 * public browser_fetch.h API remains unchanged. */

#include <stdint.h>

#include "capylibc-net/capy_net.h"
#include "net/http.h"

int browser_fetch_map_wire_response(const struct capy_http_response *wire,
                                    uint8_t *body,
                                    struct http_response *out);

#endif /* CAPYOS_BROWSER_FETCH_INTERNAL_H */
