/* src/apps/browser_app/nav.c
 *
 * Etapa F4 nav (2026-05-03): implementacao do contrato em
 * include/apps/browser_app_nav.h. Movido de browser_app.c para
 * manter o monolito sob 900 linhas.
 */
#include "apps/browser_app_nav.h"
#include "kernel/process.h"
#include "kernel/process_iter.h"

#include <stddef.h>

/* Buffer .bss para a URL normalizada. Compartilhado entre chamadas
 * porque o browser_app eh single-thread e o resultado eh consumido
 * imediatamente em chrome_runtime_send_navigate. Tamanho 1024
 * cobre URL_MAX (1024) + esquema "http://" prefix. */
#define BROWSER_APP_NAV_URL_MAX 1024u
static char g_nav_scratch[BROWSER_APP_NAV_URL_MAX];

/* Procura "://" nos primeiros `n` bytes de `s`. Retorna 1 se achar,
 * 0 caso contrario. Usado para detectar se a URL ja tem esquema. */
static int url_has_scheme(const char *s, size_t n) {
    if (!s || n < 3u) return 0;
    for (size_t i = 0; i + 2u < n; ++i) {
        if (s[i] == ':' && s[i + 1] == '/' && s[i + 2] == '/') return 1;
    }
    return 0;
}

const char *browser_app_nav_normalize(const char *src, uint16_t src_len,
                                       uint16_t *out_len) {
    if (!src || src_len == 0u) return (const char *)0;
    /* Strip leading/trailing whitespace (space/tab). */
    uint16_t lo = 0u;
    while (lo < src_len && (src[lo] == ' ' || src[lo] == '\t')) ++lo;
    uint16_t hi = src_len;
    while (hi > lo && (src[hi - 1u] == ' ' || src[hi - 1u] == '\t')) --hi;
    if (hi <= lo) return (const char *)0;
    uint16_t eff_len = (uint16_t)(hi - lo);
    size_t cap = sizeof(g_nav_scratch);

    if (url_has_scheme(src + lo, eff_len)) {
        if ((size_t)eff_len + 1u > cap) return (const char *)0;
        for (uint16_t i = 0; i < eff_len; ++i) g_nav_scratch[i] = src[lo + i];
        g_nav_scratch[eff_len] = '\0';
        if (out_len) *out_len = eff_len;
        return g_nav_scratch;
    }

    /* No scheme: prefix "http://". */
    static const char k_prefix[] = "http://";
    const uint16_t plen = (uint16_t)(sizeof(k_prefix) - 1u);
    if ((size_t)plen + (size_t)eff_len + 1u > cap) return (const char *)0;
    for (uint16_t i = 0; i < plen; ++i) g_nav_scratch[i] = k_prefix[i];
    for (uint16_t i = 0; i < eff_len; ++i) {
        g_nav_scratch[plen + i] = src[lo + i];
    }
    g_nav_scratch[plen + eff_len] = '\0';
    if (out_len) *out_len = (uint16_t)(plen + eff_len);
    return g_nav_scratch;
}

int browser_app_nav_engine_is_dead(uint32_t engine_pid) {
    if (engine_pid == 0u) return 1;
    struct process_stats st;
    if (process_stats_get(engine_pid, &st) != 0) {
        /* Slot UNUSED -> ja foi reapado. */
        return 1;
    }
    if (st.state == PROC_STATE_ZOMBIE ||
        st.state == PROC_STATE_UNUSED) {
        return 1;
    }
    return 0;
}
