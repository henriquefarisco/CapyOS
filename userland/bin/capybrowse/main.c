/*
 * userland/bin/capybrowse/main.c — CapyBrowse Text ring-3 app (Etapa 6 /
 * Slice 6.4).
 *
 * The first real CapyOS-side consumer of the published capy-browser-core text
 * subset (CapyBrowser v0.6.0, package org.capyos.browser.text). It is the thin
 * adapter the integration contract assigns to CapyOS:
 *
 *   1. fetch a URL over the Etapa 5 userland HTTPS/TLS path (capy_http_get);
 *   2. hand the response bytes to the decoupled core (capy_html_to_text) to get
 *      a deterministic {title, body, numbered links, warnings, truncation};
 *   3. format that into the terminal text block (capybrowse_format_page) and
 *      print it on stdout — terminal.c owns word-wrap/scroll, so the app just
 *      writes text;
 *   4. on a transport failure, print the friendly, stage-aware, localized
 *      diagnostic (capy_net_diagnose_stage + capy_net_stage_message).
 *
 * It does NOT execute JavaScript, load sub-resources, or parse HTML itself —
 * the HTML-to-text core stays in CapyBrowser. Ring-3 conventions mirror the
 * other user binaries (hello/tls_smoke): no host libc beyond the freestanding
 * capylibc <string.h> the core links, mutable bulk state in zero-initialised
 * .bss (the ELF loader zeroes .bss). The endpoint is compile-time configurable
 * for the controlled smoke (make smoke-x64-vmware-capybrowse-text), like
 * tls_smoke; the language defaults to the mandatory EN base until a ring-3
 * "current session language" syscall exists.
 */

#include <capylibc/capylibc.h>
#include <capylibc-net/capy_net.h>

/* capy-browser-core also exports `capy_url_parse` (a DIFFERENT function: it
 * resolves+normalizes a URL reference against a base). capylibc-net above
 * exports its OWN `capy_url_parse` (used by capy_http_get). Rename the core's
 * symbol so the two coexist in this TU and at link — the core objects are
 * compiled with the matching -Dcapy_url_parse=capybrowse_core_url_parse
 * (Makefile CAPYBROWSER_TEXT_RENAME). Defined AFTER capy_net.h so the net
 * declaration keeps its real name; this app calls neither parser directly. */
#define capy_url_parse capybrowse_core_url_parse

#include "html_text.h"       /* capy-browser-core: capy_html_to_text, capy_text_doc */
#include "capybrowse_view.h" /* CapyOS-side view formatter */

#ifndef CAPYOS_CAPYBROWSE_URL
#define CAPYOS_CAPYBROWSE_URL "https://example.com/"
#endif
#ifndef CAPYOS_CAPYBROWSE_LANG
#define CAPYOS_CAPYBROWSE_LANG "en"
#endif

/* Bounded retry to absorb the async DHCP lease window (mirrors tls_smoke). */
#ifndef CAPYBROWSE_MAX_ATTEMPTS
#define CAPYBROWSE_MAX_ATTEMPTS 600u
#endif
#define CAPYBROWSE_SLEEP_TICKS 10u

/* Bulk buffers in .bss (zeroed by the ELF loader). Modest by design for the
 * text app; oversize pages truncate fail-safe (resp.truncated / doc.truncated)
 * rather than overflow. */
#ifndef CAPYBROWSE_FETCH_MAX
#define CAPYBROWSE_FETCH_MAX 32768u
#endif
#ifndef CAPYBROWSE_TEXT_MAX
#define CAPYBROWSE_TEXT_MAX 32768u
#endif
#ifndef CAPYBROWSE_VIEW_MAX
#define CAPYBROWSE_VIEW_MAX 49152u
#endif

static uint8_t g_fetch[CAPYBROWSE_FETCH_MAX];
static char g_text[CAPYBROWSE_TEXT_MAX];
static char g_view[CAPYBROWSE_VIEW_MAX];

/* The HTTP response and parsed-document structs also live in .bss, not on
 * main()'s stack. As stack locals they push main()'s frame to ~140 KiB, which
 * exceeds the kernel's eagerly-mapped top-of-stack window and would force a
 * demand-paged stack-clash probe on entry. capybrowse is a single-shot,
 * single-threaded program, so file scope is safe and keeps the frame small. */
static struct capy_http_response resp;
static struct capy_text_doc doc;

static size_t cb_cstr_len(const char *s) {
  size_t n = 0u;
  while (s[n]) {
    n++;
  }
  return n;
}

static void cb_print(const char *s) { capy_write(1, s, cb_cstr_len(s)); }

/* Print the friendly stage-aware localized diagnostic for the current error
 * and exit fail-closed. tls_state distinguishes a real TLS handshake/cert
 * failure from a generic transport error even though the net layer collapses
 * them into one code. */
static void cb_fail(capy_net_err_t net_err, capy_tls_state_t tls_state) {
  capy_net_stage_t stage = capy_net_diagnose_stage(net_err, tls_state);
  const char *hint;
  cb_print(capy_net_stage_message(stage, CAPYOS_CAPYBROWSE_LANG));
  cb_print("\n");
  hint = capy_net_stage_hint(stage, CAPYOS_CAPYBROWSE_LANG);
  if (hint[0]) {
    cb_print(hint);
    cb_print("\n");
  }
  capy_exit(1);
}

int main(int rank) {
  unsigned attempt;
  int got_response = 0;

  (void)rank;

  /* 1. Fetch over Etapa 5 HTTPS/TLS. Retry only the transport-level failures
   *    (capy_http_get < 0) across the DHCP window; a delivered response (>= 0,
   *    any HTTP status) is rendered as-is. */
  for (attempt = 0u; attempt < CAPYBROWSE_MAX_ATTEMPTS; ++attempt) {
    if (capy_http_get(CAPYOS_CAPYBROWSE_URL, g_fetch, sizeof(g_fetch), &resp) ==
        0) {
      got_response = 1;
      break;
    }
    capy_yield();
    capy_sleep(CAPYBROWSE_SLEEP_TICKS);
  }
  if (!got_response) {
    cb_fail(capy_net_last_error(), capy_tls_last_state());
  }

  /* 2. HTML -> deterministic text view via the decoupled core. */
  if (capy_html_to_text(g_fetch, resp.body_len, CAPYOS_CAPYBROWSE_URL, g_text,
                        sizeof(g_text), &doc) != CAPY_TEXT_OK) {
    /* Only NULL inputs make the tolerant core fail; treat as an input error. */
    cb_print(capy_net_stage_message(CAPY_NET_STAGE_INPUT, CAPYOS_CAPYBROWSE_LANG));
    cb_print("\n");
    cb_print(capy_net_stage_hint(CAPY_NET_STAGE_INPUT, CAPYOS_CAPYBROWSE_LANG));
    cb_print("\n");
    capy_exit(1);
  }

  /* 3. Format (title + body + numbered links) and print; terminal.c wraps. */
  capybrowse_format_page(&doc, g_text, g_view, sizeof(g_view));
  cb_print(g_view);

  capy_exit(0);
}
