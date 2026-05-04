/*
 * src/apps/browser_app/url_edit.c
 *
 * Editor puro da URL bar. Sem dependencia de kernel/compositor;
 * cobertura via tests/test_browser_app_url_edit.c.
 */

#include "apps/browser_app_url_edit.h"

#include <stddef.h>

static int is_printable_ascii(char ch) {
    unsigned char u = (unsigned char)ch;
    return (u >= 32u && u < 127u) ? 1 : 0;
}

void url_edit_clear(struct url_edit *e) {
    if (!e) return;
    e->buf[0] = '\0';
    e->len = 0u;
    e->cursor = 0u;
}

void url_edit_set(struct url_edit *e, const char *src) {
    if (!e) return;
    url_edit_clear(e);
    if (!src) return;
    uint16_t n = 0u;
    while (n + 1u < BROWSER_APP_URL_EDIT_CAP && src[n] != '\0') {
        e->buf[n] = src[n];
        n++;
    }
    e->buf[n] = '\0';
    e->len = n;
    e->cursor = n;
}

int url_edit_insert_char(struct url_edit *e, char ch) {
    if (!e) return 0;
    if (!is_printable_ascii(ch)) return 0;
    if ((uint32_t)e->len + 1u >= BROWSER_APP_URL_EDIT_CAP) return 0;
    /* Desloca [cursor..len-1] uma posicao para a direita. */
    for (uint16_t i = e->len; i > e->cursor; --i) {
        e->buf[i] = e->buf[i - 1u];
    }
    e->buf[e->cursor] = ch;
    e->len++;
    e->cursor++;
    e->buf[e->len] = '\0';
    return 1;
}

int url_edit_backspace(struct url_edit *e) {
    if (!e) return 0;
    if (e->cursor == 0u || e->len == 0u) return 0;
    for (uint16_t i = e->cursor - 1u; i + 1u < e->len; ++i) {
        e->buf[i] = e->buf[i + 1u];
    }
    e->len--;
    e->cursor--;
    e->buf[e->len] = '\0';
    return 1;
}

int url_edit_delete(struct url_edit *e) {
    if (!e) return 0;
    if (e->cursor >= e->len) return 0;
    for (uint16_t i = e->cursor; i + 1u < e->len; ++i) {
        e->buf[i] = e->buf[i + 1u];
    }
    e->len--;
    e->buf[e->len] = '\0';
    return 1;
}

int url_edit_move_left(struct url_edit *e) {
    if (!e || e->cursor == 0u) return 0;
    e->cursor--;
    return 1;
}

int url_edit_move_right(struct url_edit *e) {
    if (!e || e->cursor >= e->len) return 0;
    e->cursor++;
    return 1;
}

int url_edit_move_home(struct url_edit *e) {
    if (!e || e->cursor == 0u) return 0;
    e->cursor = 0u;
    return 1;
}

int url_edit_move_end(struct url_edit *e) {
    if (!e || e->cursor >= e->len) return 0;
    e->cursor = e->len;
    return 1;
}
