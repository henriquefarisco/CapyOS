/* src/apps/browser_app/toolbar.c
 *
 * Etapa F4 toolbar (2026-05-03): implementacao do contrato em
 * include/apps/browser_app_toolbar.h.
 *
 * Sem libc, sem alloc. Todos os retangulos sao constantes
 * dependentes apenas de surface_width/height + URL bar height.
 */
#include "apps/browser_app_toolbar.h"
#include "apps/browser_dimensions.h"
#include "gui/compositor.h"
#include "gui/font.h"

#include <stddef.h>

/* Geometria estatica dos botoes esquerdos. Cada celula de 28 px
 * de largura, comecando em x=0. O botao real ocupa a celula com
 * 1 px de gap entre celulas. */
#define TB_LEFT_CELL_W   28u
#define TB_RIGHT_CELL_W  40u    /* "Go" no canto direito */
#define TB_BTN_TOP_PAD   5u
#define TB_BTN_HEIGHT   22u
#define TB_LEFT_COUNT    4u
#define TB_STATUS_W     12u
#define TB_STATUS_GAP    4u

/* === Helpers ============================================================ */

static void tb_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                          uint32_t w, uint32_t h, uint32_t color) {
    if (!s || !s->pixels) return;
    for (uint32_t r = 0; r < h; ++r) {
        int32_t py = y + (int32_t)r;
        if (py < 0 || (uint32_t)py >= s->height) continue;
        uint32_t *line = (uint32_t *)((uint8_t *)s->pixels +
                                       (uint32_t)py * s->pitch);
        for (uint32_t c = 0; c < w; ++c) {
            int32_t px = x + (int32_t)c;
            if (px < 0 || (uint32_t)px >= s->width) continue;
            line[px] = color;
        }
    }
}

/* Pinta um botao com fundo `bg`, borda `border` (1 px) e label
 * centralizado horizontalmente. label_x_offset = espaco do texto
 * em relacao ao retangulo, para acomodar labels de tamanhos
 * diferentes ("<" 1 char vs "Go" 2 chars). */
static void tb_paint_button(struct gui_surface *s, const struct font *f,
                             int32_t x, int32_t y, uint32_t w, uint32_t h,
                             const char *label, uint32_t bg,
                             uint32_t fg, uint32_t border) {
    if (!s || !f) return;
    /* Fundo. */
    tb_fill_rect(s, x, y, w, h, bg);
    /* Borda 1 px (top, bottom, left, right). */
    tb_fill_rect(s, x, y, w, 1, border);
    tb_fill_rect(s, x, y + (int32_t)h - 1, w, 1, border);
    tb_fill_rect(s, x, y, 1, h, border);
    tb_fill_rect(s, x + (int32_t)w - 1, y, 1, h, border);
    /* Label centralizado. Glyph width = 8 px. */
    if (label) {
        uint32_t llen = 0;
        while (label[llen] != '\0' && llen < 8u) llen++;
        uint32_t label_w_px = llen * f->glyph_width;
        int32_t lx = x + (int32_t)((w > label_w_px)
                                    ? (w - label_w_px) / 2u : 0u);
        int32_t ly = y + (int32_t)((h > f->glyph_height)
                                    ? (h - f->glyph_height) / 2u : 0u);
        font_draw_string(s, f, lx, ly, label, fg);
    }
}

/* X coord do botao i-esimo da esquerda (0..3). */
static int32_t tb_left_x(uint32_t i) {
    return (int32_t)(i * TB_LEFT_CELL_W);
}

/* X coord do botao Go (direita). */
static int32_t tb_go_x(uint32_t surface_width) {
    if (surface_width < (TB_RIGHT_CELL_W + 4u)) return (int32_t)surface_width;
    return (int32_t)(surface_width - TB_RIGHT_CELL_W - 4u);
}

/* === Public API ========================================================= */

void browser_app_toolbar_paint(struct gui_surface *s,
                                const struct gui_theme_palette *theme,
                                const struct font *font,
                                uint32_t bar_top) {
    if (!s || !theme || !font) return;
    int32_t y = (int32_t)(bar_top + TB_BTN_TOP_PAD);
    uint32_t bg     = theme->accent_alt;
    uint32_t fg     = theme->accent_text;
    uint32_t border = theme->window_border;
    uint32_t go_bg  = theme->accent;
    /* 4 botoes da esquerda. Labels curtos ASCII para nao depender
     * de fontes Unicode. */
    static const char *const k_labels[TB_LEFT_COUNT] = {
        "<", ">", "R", "H"
    };
    for (uint32_t i = 0; i < TB_LEFT_COUNT; ++i) {
        int32_t bx = tb_left_x(i) + 2;
        tb_paint_button(s, font, bx, y, TB_LEFT_CELL_W - 4u, TB_BTN_HEIGHT,
                         k_labels[i], bg, fg, border);
    }
    /* Botao Go na direita, em accent (cor mais forte). */
    int32_t gx = tb_go_x(s->width);
    tb_paint_button(s, font, gx, y, TB_RIGHT_CELL_W, TB_BTN_HEIGHT,
                     "Go", go_bg, fg, border);
}

void browser_app_toolbar_url_region(uint32_t surface_width,
                                     int32_t *out_x, int32_t *out_w) {
    /* x_left = depois dos 4 botoes esquerdos + status indicator. */
    int32_t x_left = (int32_t)(TB_LEFT_COUNT * TB_LEFT_CELL_W +
                                TB_STATUS_W + TB_STATUS_GAP);
    int32_t x_right = tb_go_x(surface_width) - 4;
    if (x_right <= x_left) {
        if (out_x) *out_x = x_left;
        if (out_w) *out_w = 4;
        return;
    }
    if (out_x) *out_x = x_left;
    if (out_w) *out_w = x_right - x_left;
}

int browser_app_toolbar_is_url_text_region(int32_t lx, int32_t ly,
                                            uint32_t surface_width,
                                            uint32_t surface_height) {
    if (surface_height <= BROWSER_APP_URLBAR_H) return 0;
    int32_t bar_top = (int32_t)(surface_height - BROWSER_APP_URLBAR_H);
    if (ly < bar_top) return 0;
    int32_t tx = 0, tw = 0;
    browser_app_toolbar_url_region(surface_width, &tx, &tw);
    return (lx >= tx && lx < tx + tw);
}

enum browser_app_toolbar_action
browser_app_toolbar_hit_test(int32_t x, int32_t y,
                              uint32_t surface_width,
                              uint32_t surface_height) {
    if (surface_height <= BROWSER_APP_URLBAR_H) {
        return BROWSER_APP_TOOLBAR_NONE;
    }
    int32_t bar_top = (int32_t)(surface_height - BROWSER_APP_URLBAR_H);
    int32_t btn_y0 = bar_top + (int32_t)TB_BTN_TOP_PAD;
    int32_t btn_y1 = btn_y0 + (int32_t)TB_BTN_HEIGHT;
    if (y < btn_y0 || y >= btn_y1) return BROWSER_APP_TOOLBAR_NONE;

    /* 4 botoes esquerdos: cell de 28 px, conteudo no offset 2..26. */
    for (uint32_t i = 0; i < TB_LEFT_COUNT; ++i) {
        int32_t bx0 = tb_left_x(i) + 2;
        int32_t bx1 = bx0 + (int32_t)(TB_LEFT_CELL_W - 4u);
        if (x >= bx0 && x < bx1) {
            switch (i) {
                case 0u: return BROWSER_APP_TOOLBAR_BACK;
                case 1u: return BROWSER_APP_TOOLBAR_FORWARD;
                case 2u: return BROWSER_APP_TOOLBAR_RELOAD;
                case 3u: return BROWSER_APP_TOOLBAR_HOME;
                default: break;
            }
        }
    }
    /* Botao Go na direita. */
    int32_t gx0 = tb_go_x(surface_width);
    int32_t gx1 = gx0 + (int32_t)TB_RIGHT_CELL_W;
    if (x >= gx0 && x < gx1) {
        return BROWSER_APP_TOOLBAR_GO;
    }
    return BROWSER_APP_TOOLBAR_NONE;
}
