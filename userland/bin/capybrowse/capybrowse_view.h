#ifndef CAPYBROWSE_VIEW_H
#define CAPYBROWSE_VIEW_H

#include <stddef.h>

#include "html_text.h" /* capy-browser-core published ABI: struct capy_text_doc */

/*
 * CapyBrowse Text view formatter (Etapa 6 / Slice 6.4, CapyOS-side adapter).
 *
 * Pure presentation: turns the capy-browser-core text output (a capy_text_doc
 * plus the normalized body buffer capy_html_to_text wrote) into the single text
 * block the app prints to the terminal — title, body (with the inline "[n]"
 * link markers the core already emitted), a numbered "Links:" list resolving
 * each marker to its absolute URL, a parse-warning count, and a truncation note
 * when a budget was hit.
 *
 * CapyOS owns this presentation per browser-core-integration-contract.md
 * ("janela, scroll, input e render backend"); the HTML-to-text core stays in
 * CapyBrowser. Self-contained: no libc, no syscalls — bounded writes into the
 * caller buffer, always NUL-terminated when out_cap > 0. Returns the byte
 * length written (excluding the NUL); the result never reaches out_cap.
 */
size_t capybrowse_format_page(const struct capy_text_doc *doc, const char *body,
                              char *out, size_t out_cap);

#endif /* CAPYBROWSE_VIEW_H */
