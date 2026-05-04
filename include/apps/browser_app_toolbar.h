/* include/apps/browser_app_toolbar.h
 *
 * Etapa F4 toolbar (2026-05-03): toolbar de navegacao (Back,
 * Forward, Reload, Home, Go) embutido na URL bar do browser_app.
 * Modulo dedicado para manter browser_app.c sob 900 linhas.
 *
 * Layout dentro da URL bar (32 px de altura):
 *   x:    0 .. 28  -> Back ("<")
 *   x:   28 .. 56  -> Forward (">")
 *   x:   56 .. 84  -> Reload ("R")
 *   x:   84 .. 112 -> Home ("H")
 *   x:  116 .. 128 -> Status indicator (8x8 glyph)
 *   x:  132 .. w-44 -> URL text + cursor
 *   x:  w-40 .. w-4 -> Go ("Go")
 *
 * Tudo desenhado com 1 px border + label centralizado. As cores
 * vem do tema (accent / accent_alt / window_border / text). Sem
 * icones (font default e 8x8 ASCII; usar letra na hora.)
 */
#ifndef APPS_BROWSER_APP_TOOLBAR_H
#define APPS_BROWSER_APP_TOOLBAR_H

#include <stdint.h>

struct gui_surface;
struct gui_theme_palette;
struct font;

enum browser_app_toolbar_action {
    BROWSER_APP_TOOLBAR_NONE     = 0,
    BROWSER_APP_TOOLBAR_BACK     = 1,
    BROWSER_APP_TOOLBAR_FORWARD  = 2,
    BROWSER_APP_TOOLBAR_RELOAD   = 3,
    BROWSER_APP_TOOLBAR_HOME     = 4,
    BROWSER_APP_TOOLBAR_GO       = 5
};

/* Pinta os 5 botoes (Back/Forward/Reload/Home/Go) na regiao da
 * URL bar localizada em `bar_top..bar_top+BROWSER_APP_URLBAR_H`. */
void browser_app_toolbar_paint(struct gui_surface *s,
                                const struct gui_theme_palette *theme,
                                const struct font *font,
                                uint32_t bar_top);

/* Calcula a regiao [out_x, out_x + out_w) onde o texto da URL deve
 * ser desenhado, dada a largura total da janela. Garante que o
 * texto nao colide nem com os botoes da esquerda nem com o Go da
 * direita. Se o surface for muito estreito (< 200 px) reduz a
 * regiao mas nunca devolve out_w <= 0. */
void browser_app_toolbar_url_region(uint32_t surface_width,
                                     int32_t *out_x, int32_t *out_w);

/* Hit-test: dado um click em (x, y) relativo ao topo-esquerdo da
 * janela, retorna a acao correspondente (ou NONE se o click foi
 * em area neutra do toolbar -- e.g. dentro do retangulo do URL
 * text, fora dos botoes).
 * Recebe surface_width para saber onde fica o Go (na direita).
 * Recebe surface_height para deduzir bar_top = h - URLBAR_H. */
enum browser_app_toolbar_action
browser_app_toolbar_hit_test(int32_t x, int32_t y,
                              uint32_t surface_width,
                              uint32_t surface_height);

/* Etapa F4 cursors (2026-05-03): retorna 1 se a posicao (lx, ly)
 * relativa ao surface esta sobre a regiao do texto da URL bar
 * (entre os botoes esquerdos + status indicator e o botao Go). O
 * caller usa para devolver COMP_CURSOR_TEXT no on_cursor_hint. */
int browser_app_toolbar_is_url_text_region(int32_t lx, int32_t ly,
                                            uint32_t surface_width,
                                            uint32_t surface_height);

#endif /* APPS_BROWSER_APP_TOOLBAR_H */
