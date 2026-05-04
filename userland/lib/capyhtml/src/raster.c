/* libcapyhtml/src/raster.c -- F3.3c slice 4-final.
 *
 * Software rasterizer: walks `capyhtml_cmd[]` and writes BGRA8888
 * pixels into a caller-provided buffer. Freestanding (no libc).
 */
#include "capyhtml/raster.h"
#include "capyhtml/font.h"

/* Write one pixel at (x, y) with `argb`. Bound-checks. Stores in
 * BGRA8888 order which is the layout the kernel framebuffer and
 * the compositor expect. The MSB of argb is alpha but the
 * compositor blits opaquely (no alpha blending in this slice), so
 * we store it as-is and let the receiver decide. */
static inline void put_pixel(const struct capyhtml_raster_target *t,
                             int32_t x, int32_t y, uint32_t argb) {
    if (x < 0 || y < 0 || x >= t->width_px || y >= t->height_px) return;
    uint8_t *p = t->pixels + (size_t)y * (size_t)t->stride_b
                          + (size_t)x * 4u;
    p[0] = (uint8_t)(argb >>  0); /* B */
    p[1] = (uint8_t)(argb >>  8); /* G */
    p[2] = (uint8_t)(argb >> 16); /* R */
    p[3] = (uint8_t)(argb >> 24); /* A */
}

/* Returns the ARGB color for `role`, defaulting to the TEXT slot
 * for any out-of-range value. */
static uint32_t pick_color(const struct capyhtml_palette *pal,
                           uint8_t role) {
    if (!pal) return 0xFFFFFFFFu;
    if (role > 4u) role = 0u;
    return pal->color_argb[role];
}

void capyhtml_raster_clear(const struct capyhtml_raster_target *t,
                           uint32_t argb) {
    if (!t || !t->pixels || t->width_px <= 0 || t->height_px <= 0) return;
    for (int32_t y = 0; y < t->height_px; ++y) {
        uint8_t *row = t->pixels + (size_t)y * (size_t)t->stride_b;
        for (int32_t x = 0; x < t->width_px; ++x) {
            row[x * 4 + 0] = (uint8_t)(argb >>  0);
            row[x * 4 + 1] = (uint8_t)(argb >>  8);
            row[x * 4 + 2] = (uint8_t)(argb >> 16);
            row[x * 4 + 3] = (uint8_t)(argb >> 24);
        }
    }
}

/* Blit a single ASCII glyph at `(x, y)` scaled by `scale`. Each
 * source pixel becomes a `scale x scale` block. Bits set in the
 * 8x8 row table draw with `argb`; clear bits are skipped (text
 * does NOT paint a background -- that has already been cleared
 * by capyhtml_raster_clear). */
static void blit_glyph(const struct capyhtml_raster_target *t,
                       int32_t px, int32_t py, int scale,
                       uint8_t ch, uint32_t argb) {
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    const uint8_t *rows = capyhtml_font_glyph_row(ch);
    for (int gy = 0; gy < CAPYHTML_FONT_GLYPH_H; ++gy) {
        uint8_t bits = rows[gy];
        for (int gx = 0; gx < CAPYHTML_FONT_GLYPH_W; ++gx) {
            /* MSB is leftmost pixel (matches kernel font usage). */
            if (bits & (uint8_t)(0x80u >> gx)) {
                int32_t bx = px + gx * scale;
                int32_t by = py + gy * scale;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        put_pixel(t, bx + dx, by + dy, argb);
                    }
                }
            }
        }
    }
}

/* Solid-color filled rectangle clipped to `t`. */
static void fill_rect(const struct capyhtml_raster_target *t,
                      int32_t x, int32_t y, int32_t w, int32_t h,
                      uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    int32_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > t->width_px) x1 = t->width_px;
    if (y1 > t->height_px) y1 = t->height_px;
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint8_t *row = t->pixels + (size_t)yy * (size_t)t->stride_b;
        for (int32_t xx = x0; xx < x1; ++xx) {
            row[xx * 4 + 0] = (uint8_t)(argb >>  0);
            row[xx * 4 + 1] = (uint8_t)(argb >>  8);
            row[xx * 4 + 2] = (uint8_t)(argb >> 16);
            row[xx * 4 + 3] = (uint8_t)(argb >> 24);
        }
    }
}

static void draw_text_cmd(const struct capyhtml_raster_target *t,
                          const struct capyhtml_cmd          *cmd,
                          const struct capyhtml_palette      *pal) {
    if (!cmd->text) return;
    int scale = cmd->scale;
    if (scale < 1) scale = 1;
    uint32_t argb = pick_color(pal, cmd->color_role);
    int32_t cursor_x = cmd->x;
    int32_t cursor_y = cmd->y;
    int advance = CAPYHTML_FONT_GLYPH_W * scale;
    /* Early abort if the entire run is below the viewport: nothing
     * to draw. Above, we still iterate so partial-bottom clips
     * work; put_pixel filters per-pixel. */
    if (cursor_y >= t->height_px) return;
    for (size_t i = 0; cmd->text[i] != '\0'; ++i) {
        if (cursor_x >= t->width_px) break;
        blit_glyph(t, cursor_x, cursor_y, scale,
                   (uint8_t)cmd->text[i], argb);
        if (cmd->bold) {
            /* +1px shift for an "embolden" effect; matches the
             * trick the compositor uses for window titles. */
            blit_glyph(t, cursor_x + 1, cursor_y, scale,
                       (uint8_t)cmd->text[i], argb);
        }
        cursor_x += advance;
    }
    if (cmd->underline) {
        int32_t under_y = cmd->y + CAPYHTML_FONT_GLYPH_H * scale;
        int32_t under_w = (cursor_x - cmd->x);
        fill_rect(t, cmd->x, under_y, under_w, 1, argb);
    }
}

/* Etapa 3 seção a (2026-05-03): desenha um placeholder visivel para
 * <img> quando o pipeline real de fetch+decode ainda nao existe.
 * Layout:
 *   - fill_rect com cor muted (background dim do palette)
 *   - borda de 1 px em MUTED role (mesma tonalidade, suficiente para
 *     demarcar sem poluir visualmente)
 *   - canto superior-esquerdo: barra de 3x3 px em color_role do
 *     link para sinalizar "isto e clicavel / carregavel"
 *   - se cmd->text (alt text) nao vazio, tenta escrever primeiros
 *     N caracteres centralizados com a fonte 8x8 embutida
 *
 * Clip: put_pixel / fill_rect ja bound-checam, entao partes fora da
 * viewport sao descartadas silenciosamente (IMG clipado quando o
 * usuario rola para fora). */
static void draw_image_cmd(const struct capyhtml_raster_target *t,
                           const struct capyhtml_cmd          *cmd,
                           const struct capyhtml_palette      *pal) {
    if (cmd->w <= 0 || cmd->h <= 0) return;

    uint32_t muted = pick_color(pal, CAPYHTML_COLOR_MUTED);
    uint32_t link  = pick_color(pal, CAPYHTML_COLOR_LINK);

    /* Background muted. Aceita ser "mais claro" quando o tema e
     * escuro; por ora usamos o muted role direto e confiamos na
     * palette injetada. */
    fill_rect(t, cmd->x, cmd->y, cmd->w, cmd->h, muted);

    /* Borda 1px por linha do perimetro. Evita `rect outline` custoso
     * usando 4 fill_rect 1D -- o clip interno do fill_rect cuida de
     * viewport. */
    fill_rect(t, cmd->x, cmd->y, cmd->w, 1, link);
    fill_rect(t, cmd->x, cmd->y + cmd->h - 1, cmd->w, 1, link);
    fill_rect(t, cmd->x, cmd->y, 1, cmd->h, link);
    fill_rect(t, cmd->x + cmd->w - 1, cmd->y, 1, cmd->h, link);

    /* Marcador de canto: 3x3 em LINK no topo-esquerdo. Ajuda o
     * usuario a reconhecer que o retangulo NAO e apenas um div
     * vazio mas uma imagem nao renderizada. */
    fill_rect(t, cmd->x + 3, cmd->y + 3, 3, 3, link);

    /* Alt text dentro do placeholder. So desenha se couber (w > 48
     * pixels permite pelo menos 5 glifos de 8px). O text e clipado
     * a um prefixo que cabe na largura. */
    if (cmd->text && cmd->w >= 48 && cmd->h >= 16) {
        int32_t text_x = cmd->x + 8;
        int32_t text_y = cmd->y + (cmd->h - CAPYHTML_FONT_GLYPH_H) / 2;
        int max_chars = (cmd->w - 16) / CAPYHTML_FONT_GLYPH_W;
        uint32_t text_col = pick_color(pal, CAPYHTML_COLOR_TEXT);
        for (int i = 0; i < max_chars && cmd->text[i] != '\0'; ++i) {
            blit_glyph(t, text_x + i * CAPYHTML_FONT_GLYPH_W,
                       text_y, 1, (uint8_t)cmd->text[i], text_col);
        }
    }
}

/* Etapa 3 seção c (2026-05-03): desenha um campo de input renderizado
 * com 3 estilos:
 *   - text: fundo branco-ish (BG color do palette se disponivel; caso
 *     contrario MUTED), borda 1px LINK, valor desenhado em TEXT color.
 *   - submit: fundo LINK preenchido + borda 1px LINK escurecida, label
 *     em TEXT color.
 *   - password: igual a text mas substitui glifos por '*'.
 *
 * Etapa 3 seção c polish (2026-05-03): se reserved[0] tiver bit
 * CAPYHTML_INPUT_FLAG_FOCUSED setado, redesenha a borda com 2 px
 * em HEADING color (laranja/destaque do tema) e adiciona um cursor
 * "|" no fim do texto (so para text/password; submit nao tem caret).
 * O engine seta esse bit no cmd antes de chamar o raster, o que
 * mantem este modulo livre de estado global. */
static void draw_input_cmd(const struct capyhtml_raster_target *t,
                           const struct capyhtml_cmd          *cmd,
                           const struct capyhtml_palette      *pal) {
    if (cmd->w <= 0 || cmd->h <= 0) return;

    uint8_t r0 = cmd->reserved[0];
    uint8_t subtype = (uint8_t)(r0 & CAPYHTML_INPUT_SUBTYPE_MASK);
    uint8_t focused = (uint8_t)((r0 & CAPYHTML_INPUT_FLAG_FOCUSED) ? 1u : 0u);
    uint32_t link    = pick_color(pal, CAPYHTML_COLOR_LINK);
    uint32_t muted   = pick_color(pal, CAPYHTML_COLOR_MUTED);
    uint32_t text_col = pick_color(pal, CAPYHTML_COLOR_TEXT);
    uint32_t heading = pick_color(pal, CAPYHTML_COLOR_HEADING);

    /* Background segundo subtype. */
    uint32_t bg;
    if (subtype == CAPYHTML_INPUT_TYPE_SUBMIT) {
        bg = link;
    } else {
        bg = muted;
    }
    fill_rect(t, cmd->x, cmd->y, cmd->w, cmd->h, bg);

    /* Borda: 1 px LINK normal; 2 px HEADING quando focado. A
     * espessura "2 px" e renderizada como 2 retangulos concentricos,
     * o externo na borda do bbox e o interno deslocado 1 px para
     * dentro. Mantem o conteudo do input nas mesmas coordenadas
     * (padding lateral inalterado), ele so aparece "destacado". */
    uint32_t border = focused ? heading : link;
    fill_rect(t, cmd->x, cmd->y, cmd->w, 1, border);
    fill_rect(t, cmd->x, cmd->y + cmd->h - 1, cmd->w, 1, border);
    fill_rect(t, cmd->x, cmd->y, 1, cmd->h, border);
    fill_rect(t, cmd->x + cmd->w - 1, cmd->y, 1, cmd->h, border);
    if (focused && cmd->w >= 4 && cmd->h >= 4) {
        /* segundo anel concentrico para virar 2 px de espessura */
        fill_rect(t, cmd->x + 1, cmd->y + 1, cmd->w - 2, 1, border);
        fill_rect(t, cmd->x + 1, cmd->y + cmd->h - 2, cmd->w - 2, 1, border);
        fill_rect(t, cmd->x + 1, cmd->y + 1, 1, cmd->h - 2, border);
        fill_rect(t, cmd->x + cmd->w - 2, cmd->y + 1, 1, cmd->h - 2, border);
    }

    /* Label / value text dentro do input. Centralizado verticalmente
     * para text/password/submit; alinhado ao topo (com padding) para
     * textarea (que tem h grande e parece ruim com 1 linha boiando
     * no meio). Padding lateral de 6 px sempre. */
    int32_t pad_x = 6;
    int32_t text_x = cmd->x + pad_x;
    int32_t text_y;
    if (subtype == CAPYHTML_INPUT_TYPE_TEXTAREA) {
        /* Etapa 3 seção c refinement (2026-05-03): textarea -> top. */
        text_y = cmd->y + 6;
    } else {
        text_y = cmd->y + (cmd->h - CAPYHTML_FONT_GLYPH_H) / 2;
    }
    int max_chars = (cmd->w - 2 * pad_x) / CAPYHTML_FONT_GLYPH_W;
    if (max_chars < 0) max_chars = 0;
    int chars_drawn = 0;
    if (cmd->text && cmd->w >= 16 && cmd->h >= CAPYHTML_FONT_GLYPH_H) {
        for (int i = 0; i < max_chars && cmd->text[i] != '\0'; ++i) {
            uint8_t ch = (uint8_t)cmd->text[i];
            /* Password: substitui cada caractere por '*'. */
            if (subtype == CAPYHTML_INPUT_TYPE_PASSWORD) ch = (uint8_t)'*';
            blit_glyph(t, text_x + i * CAPYHTML_FONT_GLYPH_W,
                       text_y, 1, ch, text_col);
            chars_drawn = i + 1;
        }
    }
    /* Cursor caret depois do ultimo caractere quando focado e ainda
     * couber. So em text/password/textarea (submit/select nao
     * recebem texto livre). Caret de 1 px largura por GLYPH_H altura,
     * em HEADING color para casar com a borda destacada. */
    if (focused && subtype != CAPYHTML_INPUT_TYPE_SUBMIT
        && subtype != CAPYHTML_INPUT_TYPE_SELECT
        && chars_drawn < max_chars && cmd->h >= CAPYHTML_FONT_GLYPH_H) {
        int32_t caret_x = text_x + chars_drawn * CAPYHTML_FONT_GLYPH_W;
        fill_rect(t, caret_x, text_y, 1, CAPYHTML_FONT_GLYPH_H, heading);
    }

    /* Etapa 3 seção c refinement (2026-05-03): SELECT desenha um
     * marcador "▼" 5 px largo na direita, em HEADING color, indicando
     * que o controle abre um dropdown. Triangulo:
     *      ##### (largura 5)
     *       ###
     *        #   */
    if (subtype == CAPYHTML_INPUT_TYPE_SELECT &&
        cmd->w >= 16 && cmd->h >= 8) {
        int32_t mx = cmd->x + cmd->w - 8; /* 8 px do lado direito */
        int32_t my = cmd->y + (cmd->h - 3) / 2;
        fill_rect(t, mx,     my,     5, 1, heading);
        fill_rect(t, mx + 1, my + 1, 3, 1, heading);
        fill_rect(t, mx + 2, my + 2, 1, 1, heading);
    }
}

/* Etapa 3 seção d (2026-05-03): desenha uma celula de tabela.
 * Estrutura visual:
 *   - Fundo: TH = MUTED (cinza claro, vira "header row"); TD =
 *     transparente (deixa o background passar).
 *   - Borda: 1 px LINK em todo perimetro. As bordas tipicas
 *     "compartilham" entre celulas adjacentes (mesma posicao),
 *     entao o usuario ve uma unica linha de borda separando.
 *   - Texto: cmd->text alinhado a esquerda com padding 6 px
 *     horizontal e centralizado verticalmente. Cor:
 *     bold=1 (TH) -> HEADING; bold=0 (TD) -> TEXT. */
static void draw_cell_cmd(const struct capyhtml_raster_target *t,
                           const struct capyhtml_cmd          *cmd,
                           const struct capyhtml_palette      *pal) {
    if (cmd->w <= 0 || cmd->h <= 0) return;
    uint32_t link    = pick_color(pal, CAPYHTML_COLOR_LINK);
    uint32_t muted   = pick_color(pal, CAPYHTML_COLOR_MUTED);

    /* Background (apenas para TH; TD fica transparente). */
    if (cmd->bold) {
        fill_rect(t, cmd->x, cmd->y, cmd->w, cmd->h, muted);
    }

    /* Borda 1 px LINK ao redor. */
    fill_rect(t, cmd->x, cmd->y, cmd->w, 1, link);
    fill_rect(t, cmd->x, cmd->y + cmd->h - 1, cmd->w, 1, link);
    fill_rect(t, cmd->x, cmd->y, 1, cmd->h, link);
    fill_rect(t, cmd->x + cmd->w - 1, cmd->y, 1, cmd->h, link);

    /* Texto. Cor segundo bold (TH=HEADING, TD=TEXT via color_role
     * que o render ja setou). */
    if (cmd->text && cmd->w >= 16 && cmd->h >= CAPYHTML_FONT_GLYPH_H) {
        uint32_t fg = pick_color(pal, cmd->color_role);
        int32_t pad_x = 6;
        int32_t text_x = cmd->x + pad_x;
        int32_t text_y = cmd->y + (cmd->h - CAPYHTML_FONT_GLYPH_H) / 2;
        int max_chars = (cmd->w - 2 * pad_x) / CAPYHTML_FONT_GLYPH_W;
        if (max_chars < 0) max_chars = 0;
        for (int i = 0; i < max_chars && cmd->text[i] != '\0'; ++i) {
            blit_glyph(t, text_x + i * CAPYHTML_FONT_GLYPH_W,
                       text_y, 1, (uint8_t)cmd->text[i], fg);
        }
    }
}

void capyhtml_raster_draw(const struct capyhtml_raster_target *t,
                          const struct capyhtml_cmd          *cmd,
                          const struct capyhtml_palette      *pal) {
    if (!t || !cmd || !t->pixels) return;
    switch (cmd->kind) {
        case CAPYHTML_CMD_TEXT:
            draw_text_cmd(t, cmd, pal);
            break;
        case CAPYHTML_CMD_BULLET: {
            /* Render as a small filled square. The layout pass
             * already sized w == h. */
            uint32_t argb = pick_color(pal, cmd->color_role);
            int32_t side = (cmd->w > 0) ? cmd->w : 4;
            fill_rect(t, cmd->x, cmd->y, side, side, argb);
            break;
        }
        case CAPYHTML_CMD_RULE: {
            uint32_t argb = pick_color(pal, cmd->color_role);
            int32_t w = (cmd->w > 0) ? cmd->w : t->width_px;
            int32_t h = (cmd->h > 0) ? cmd->h : 1;
            fill_rect(t, cmd->x, cmd->y, w, h, argb);
            break;
        }
        case CAPYHTML_CMD_IMAGE:
            draw_image_cmd(t, cmd, pal);
            break;
        case CAPYHTML_CMD_INPUT:
            draw_input_cmd(t, cmd, pal);
            break;
        case CAPYHTML_CMD_CELL:
            draw_cell_cmd(t, cmd, pal);
            break;
        case CAPYHTML_CMD_NONE:
        default:
            /* Forward-compat: silently drop unknown kinds so old
             * binaries keep working when slice 3b adds new ones. */
            break;
    }
}

void capyhtml_raster_render(const struct capyhtml_raster_target *t,
                            const struct capyhtml_cmd           *cmds,
                            uint16_t                              cmd_count,
                            const struct capyhtml_palette       *pal) {
    if (!t || !t->pixels) return;
    if (pal) capyhtml_raster_clear(t, pal->background_argb);
    if (!cmds || cmd_count == 0u) return;
    for (uint16_t i = 0; i < cmd_count; ++i) {
        capyhtml_raster_draw(t, &cmds[i], pal);
    }
}
