/* src/apps/browser_app/homepage.c
 *
 * Etapa F4 homepage (2026-05-03): implementacao do contrato em
 * include/apps/browser_app_homepage.h.
 *
 * Estado: singleton estatico com 4 campos:
 *   - g_url[256]: URL inicial resolvida (do config.ini ou default)
 *   - g_url_len: comprimento de g_url (cache para evitar strlen)
 *   - g_sent: 0/1, se ja despachamos o NAVIGATE inicial
 *   - g_fallback_used: 0/1, se ja caimos pro offline
 *
 * Sem libc, sem alocacao dinamica.
 */
#include "apps/browser_app_homepage.h"
#include "apps/browser_chrome_runtime.h"
#include "apps/browser_chrome.h"
#include "core/system_init.h"

#include <stddef.h>

/* Default code-side caso /system/config.ini nao tenha
 * `browser_homepage=` ou o load falhe completamente. */
static const char k_url_default[]  = "https://wikipedia.org";
static const char k_url_fallback[] = "file://capyos/wikipedia";

static char     g_url[SYSTEM_BROWSER_HOMEPAGE_MAX];
static uint16_t g_url_len;
static uint8_t  g_sent;
static uint8_t  g_fallback_used;

/* Copia src em dst preservando NUL e devolve len copiado.
 * Sem libc para manter o modulo `freestanding-clean`. */
static uint16_t copy_url(char *dst, uint32_t max, const char *src) {
    uint16_t i = 0;
    if (!dst || max == 0u) return 0u;
    if (src) {
        while (src[i] != '\0' && (uint32_t)(i + 1u) < max) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
    return i;
}

/* Le o config.ini e populariza g_url. system_load_settings ja
 * preenche o default (`https://wikipedia.org`) mesmo em caso de
 * falha de load, entao a chamada nunca deixa o campo vazio. */
static void resolve_homepage(void) {
    struct system_settings live;
    (void)system_load_settings(&live);
    const char *src = live.browser_homepage[0] ? live.browser_homepage
                                                 : k_url_default;
    g_url_len = copy_url(g_url, sizeof(g_url), src);
    if (g_url_len == 0u) {
        /* Defensivo: se config tiver string vazia (cenario teoricamente
         * impossivel apos set_defaults), forca o code-default. */
        g_url_len = copy_url(g_url, sizeof(g_url), k_url_default);
    }
}

/* === API publica ====================================================== */

void browser_app_homepage_open(void) {
    resolve_homepage();
    g_sent = 0u;
    g_fallback_used = 0u;
}

void browser_app_homepage_close(void) {
    g_sent = 0u;
    g_fallback_used = 0u;
}

const char *browser_app_homepage_initial_url(void) {
    return g_url;
}

uint16_t browser_app_homepage_initial_url_len(void) {
    return g_url_len;
}

const char *browser_app_homepage_fallback_url(void) {
    return k_url_fallback;
}

int browser_app_homepage_tick(struct chrome_runtime *rt) {
    if (!rt) return BROWSER_APP_HOMEPAGE_TICK_NOOP;

    /* Primeiro tick: dispara nav inicial. */
    if (!g_sent) {
        if (chrome_runtime_send_navigate(rt, g_url, (size_t)g_url_len) == 0) {
            g_sent = 1u;
            return BROWSER_APP_HOMEPAGE_TICK_SENT_INITIAL;
        }
        return BROWSER_APP_HOMEPAGE_TICK_NOOP;
    }

    /* Pos-primeira-nav: se a homepage configurada falhou, cai uma
     * vez para o offline embedded. So opera enquanto !fallback_used
     * para nao mascarar erros de URLs digitadas pelo usuario. */
    if (!g_fallback_used &&
        rt->chrome.status == BROWSER_CHROME_STATUS_FAILED) {
        size_t flen = sizeof(k_url_fallback) - 1u;
        if (chrome_runtime_send_navigate(rt, k_url_fallback, flen) == 0) {
            g_fallback_used = 1u;
            return BROWSER_APP_HOMEPAGE_TICK_SENT_FALLBACK;
        }
    }
    return BROWSER_APP_HOMEPAGE_TICK_NOOP;
}
