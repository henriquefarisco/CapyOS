/* libcapyhtml - public parser surface.
 *
 * Slice 2 (branch feature/m5-w4) ships an MVP parser that handles the
 * representative tag set needed to render the F3.3c Slice 4 demo
 * (`<h1>capyland</h1><p>...</p>`). The parser runs in pure ring-3 with
 * no kernel dependency. Slice 2b will add CSS, forms, and the
 * picture/srcset edge cases by porting the remaining helpers
 * inventoried in `../README.md`.
 *
 * The kernel-side `html_parse` in `src/apps/html_viewer/html_parser.c`
 * remains untouched until Slice 6 (delete-and-replace).
 */
#ifndef CAPYHTML_PARSER_H
#define CAPYHTML_PARSER_H

#include "capyhtml/types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional cooperative-yield callback. Slice 2 wires the parser's
 * inner loop to invoke this every N iterations. The kernel-side
 * adapter passes `task_yield`; the userland engine passes NULL (the
 * preemptive scheduler will service it). */
typedef void (*capyhtml_yield_fn)(void *user_data);

/* Returns 0 on success, -1 on NULL inputs. The parser is best-effort:
 * malformed HTML produces a partial document rather than a hard error.
 *
 * `html` MAY be a non-NUL-terminated buffer of `len` bytes.
 * `out_doc` is fully reset before parsing; caller does not need to
 * pre-zero it.
 * `yield_cb` is invoked roughly every CAPYHTML_PARSE_YIELD_EVERY
 * iterations to give the userland scheduler a chance to run; pass
 * NULL when running under a preemptive scheduler. */
int capyhtml_parse(const char *html, size_t len,
                   struct capyhtml_document *out_doc,
                   capyhtml_yield_fn yield_cb,
                   void *yield_user_data);

#ifdef __cplusplus
}
#endif

#endif /* CAPYHTML_PARSER_H */
