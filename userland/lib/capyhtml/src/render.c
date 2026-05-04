/* libcapyhtml/src/render.c -- F3.3c slice 3 layout pass.
 *
 * Walks a `struct capyhtml_document` (produced by capyhtml_parse)
 * and emits a flat list of `struct capyhtml_cmd` describing how the
 * caller should draw the page. Pure C, freestanding (no libc), no
 * allocations -- the caller owns the cmd buffer.
 *
 * Block flow model (intentionally simple):
 *
 *   - Each block-level node (H1/H2/H3, P, LI, DIV) consumes one
 *     line and advances y by its scaled height plus a margin.
 *   - Inline runs (TEXT, A, SPAN) emit on the SAME line as the
 *     previous block when they appear consecutively after that
 *     block's TEXT child has not been emitted -- in practice the
 *     parser already inlines TEXT into the parent's `text` field
 *     for headings/p, so inline runs after a heading actually mean
 *     "inline link inside the heading". The MVP keeps this minimal:
 *     a TEXT/A node that follows a block is rendered AS its own
 *     line, indented like the block. Slice 3b will add proper
 *     in-line composition once the parser supports child lists.
 *   - UL emits its own line "" (empty placeholder ignored by the
 *     renderer); LI emits a bullet command followed by a TEXT
 *     command on the same baseline.
 *   - HR / BR emit a RULE command and a blank line, respectively.
 *
 * The viewport_w argument clamps the intrinsic width of each text
 * run; word-wrap is intentionally not implemented (see render.h).
 */

#include "capyhtml/render.h"

/* Block layout constants. Not exposed because the API is "tell me
 * the metrics of your font and I'll lay things out"; these tunables
 * live entirely inside libcapyhtml so that both the kernel
 * compositor and the userland engine pick the same values. */
#define BLOCK_LEFT_MARGIN    12
#define BLOCK_RIGHT_MARGIN   12
#define HEADING_MARGIN_TOP    8
#define HEADING_MARGIN_BOT    6
#define PARAGRAPH_MARGIN_TOP  4
#define PARAGRAPH_MARGIN_BOT  4
#define LIST_INDENT          18
#define BULLET_DIAMETER       4
#define BULLET_GAP            6
#define RULE_HEIGHT           2
/* Etapa 3 seção a (2026-05-03): dimensoes default do placeholder de
 * <img>. 100x80 e proposital: altura menor que largura emula fotos
 * paisagem tipicas, mantendo o layout legivel em viewports de 480 px. */
#define IMAGE_DEFAULT_W     100
#define IMAGE_DEFAULT_H      80
#define IMAGE_MARGIN_TOP      4
#define IMAGE_MARGIN_BOT      4

/* Etapa 3 seção c (2026-05-03): dimensoes default de <input>. Texto
 * 200×24 e suficiente para campos de busca/login tipicos sem dominar
 * o viewport. Submit e mais estreito (96×24) para parecer botao.
 * Refinement (2026-05-03): textarea ~3 linhas de altura (72 px),
 * largura igual ao text. */
#define INPUT_TEXT_W        200
#define INPUT_SUBMIT_W       96
#define INPUT_HEIGHT         24
#define INPUT_TEXTAREA_W    280
#define INPUT_TEXTAREA_H     72
#define INPUT_MARGIN_TOP      4
#define INPUT_MARGIN_BOT      4

/* Etapa 3 seção d (2026-05-03): geometria de tabela.
 *   - TABLE_MARGIN_*: espaco vertical antes/depois da tabela inteira.
 *   - CELL_HEIGHT: altura fixa por linha (line_h tem variacao com a
 *     fonte; aqui usamos um valor independente para garantir que o
 *     teste sem mexer com glyphs ainda valha. raster ajusta texto
 *     vertical com (cell_h - glyph_h) / 2).
 *   - CELL_PADDING_X/Y: padding interno para o texto.
 *   - TABLE_MAX_COLS: limite defensivo para evitar divisao por zero
 *     ou layout absurdo se o doc tiver TR sem TDs.
 *   - TABLE_MIN_CELL_W: largura minima para um cell (8 chars * 8 px).
 *     Quando viewport_w / col_count fica abaixo, layout para de
 *     emitir cells e segue.
 */
#define TABLE_MARGIN_TOP      6
#define TABLE_MARGIN_BOT      6
#define CELL_HEIGHT           20
#define CELL_PADDING_X         6
#define CELL_PADDING_Y         3
#define TABLE_MAX_COLS         16
#define TABLE_MIN_CELL_W      32

/* --- portable helpers (no libc) ------------------------------------ */

static int rl_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int rl_default_measure(const char *text, int scale, void *ctx) {
    /* Fallback used only when the caller provides no measure_width.
     * Treats glyph_width_px as the per-character advance. */
    const struct capyhtml_font_ops *ops =
        (const struct capyhtml_font_ops *)ctx;
    if (!ops) return 0;
    if (scale < 1) scale = 1;
    return rl_strlen(text) * ops->glyph_width_px * scale;
}

static int rl_measure(const struct capyhtml_font_ops *ops,
                      const char *text, int scale) {
    if (!ops) return 0;
    if (scale < 1) scale = 1;
    if (ops->measure_width) {
        return ops->measure_width(text, scale, ops->ctx);
    }
    return rl_default_measure(text, scale, (void *)ops);
}

/* --- emit helpers -------------------------------------------------- */

struct rl_state {
    struct capyhtml_cmd *cmds;
    uint16_t             cap;
    uint16_t             count;
    uint8_t              truncated;
    int32_t              y;
    int32_t              viewport_w;
    const struct capyhtml_font_ops *font;
    /* Etapa 3 seção c (2026-05-03): contexto de form ativo. Aponta
     * para o action URL do FORM mais recente (NULL = "fora de form").
     * Resetado quando outro FORM aparece ou implicitamente em </form>
     * (que o parser nao distingue, mas e raro ter mais de 1 form). */
    const char          *current_form_action;
    /* Etapa 3 seção d (2026-05-03): contexto de tabela ativa.
     *   - col_count: 0 = nao em tabela; > 0 = colunas determinadas.
     *   - col_index: posicao do proximo TD/TH dentro da TR atual.
     *   - cell_w:    largura calculada (viewport_w / col_count) em px.
     *   - row_y:     y do topo da linha atual.
     *   - row_open:  1 se ja vimos TR; 0 espera primeira TR. */
    int                  table_col_count;
    int                  table_col_index;
    int                  table_cell_w;
    int32_t              table_row_y;
    uint8_t              table_row_open;
};

static struct capyhtml_cmd *rl_alloc(struct rl_state *st) {
    if (st->count >= st->cap) {
        st->truncated = 1u;
        return (struct capyhtml_cmd *)0;
    }
    struct capyhtml_cmd *c = &st->cmds[st->count++];
    /* Zero out the slot so the caller sees a deterministic, fully
     * initialized command even if a future field is added that this
     * slice doesn't populate. Manual loop because we are
     * freestanding. */
    uint8_t *p = (uint8_t *)c;
    for (unsigned i = 0; i < sizeof(*c); i++) p[i] = 0u;
    return c;
}

static int rl_clamp_w(struct rl_state *st, int x, int w) {
    int max_x = st->viewport_w - BLOCK_RIGHT_MARGIN;
    if (x + w > max_x) {
        if (max_x > x) return max_x - x;
        return 0;
    }
    return w;
}

static int rl_heading_scale(enum capyhtml_node_type t) {
    if (t == CAPYHTML_NODE_TAG_H1) return 3;
    if (t == CAPYHTML_NODE_TAG_H2) return 2;
    if (t == CAPYHTML_NODE_TAG_H3) return 2;
    return 1;
}

/* --- per-node emitters -------------------------------------------- */

static void rl_emit_heading(struct rl_state *st,
                            const struct capyhtml_node *n) {
    int scale = rl_heading_scale(n->type);
    int line_h = st->font->glyph_height_px * scale + st->font->line_gap_px;
    st->y += HEADING_MARGIN_TOP;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_TEXT;
    c->scale = (uint8_t)scale;
    c->bold = 1u;
    c->underline = 0u;
    c->color_role = CAPYHTML_COLOR_HEADING;
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    c->w = rl_clamp_w(st, c->x, rl_measure(st->font, n->text, scale));
    c->h = st->font->glyph_height_px * scale;
    c->text = n->text;
    c->href = (const char *)0;
    st->y += line_h + HEADING_MARGIN_BOT;
}

static void rl_emit_paragraph(struct rl_state *st,
                              const struct capyhtml_node *n) {
    int line_h = st->font->glyph_height_px + st->font->line_gap_px;
    st->y += PARAGRAPH_MARGIN_TOP;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_TEXT;
    c->scale = 1u;
    c->bold = (uint8_t)(n->bold ? 1u : 0u);
    c->underline = 0u;
    c->color_role = CAPYHTML_COLOR_TEXT;
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    c->w = rl_clamp_w(st, c->x, rl_measure(st->font, n->text, 1));
    c->h = st->font->glyph_height_px;
    c->text = n->text;
    c->href = (const char *)0;
    st->y += line_h + PARAGRAPH_MARGIN_BOT;
}

static void rl_emit_link(struct rl_state *st,
                         const struct capyhtml_node *n) {
    int line_h = st->font->glyph_height_px + st->font->line_gap_px;
    /* Links inherit the previous line's vertical rhythm: they are
     * inlined visually but, in this MVP, occupy their own line so
     * the renderer can hit-test by (x,y,w,h). */
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_TEXT;
    c->scale = 1u;
    c->bold = 0u;
    c->underline = 1u;
    c->color_role = CAPYHTML_COLOR_LINK;
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    c->w = rl_clamp_w(st, c->x, rl_measure(st->font, n->text, 1));
    c->h = st->font->glyph_height_px;
    c->text = n->text;
    c->href = n->href[0] ? n->href : (const char *)0;
    st->y += line_h;
}

static void rl_emit_li(struct rl_state *st,
                       const struct capyhtml_node *n) {
    int line_h = st->font->glyph_height_px + st->font->line_gap_px;
    int bullet_x = BLOCK_LEFT_MARGIN + LIST_INDENT;
    int bullet_y = st->y + (st->font->glyph_height_px - BULLET_DIAMETER) / 2;
    struct capyhtml_cmd *bullet = rl_alloc(st);
    if (!bullet) return;
    bullet->kind = CAPYHTML_CMD_BULLET;
    bullet->scale = 1u;
    bullet->color_role = CAPYHTML_COLOR_BULLET;
    bullet->x = bullet_x;
    bullet->y = bullet_y;
    bullet->w = BULLET_DIAMETER;
    bullet->h = BULLET_DIAMETER;
    bullet->text = (const char *)0;
    bullet->href = (const char *)0;

    int text_x = bullet_x + BULLET_DIAMETER + BULLET_GAP;
    struct capyhtml_cmd *txt = rl_alloc(st);
    if (txt) {
        txt->kind = CAPYHTML_CMD_TEXT;
        txt->scale = 1u;
        txt->color_role = CAPYHTML_COLOR_TEXT;
        txt->x = text_x;
        txt->y = st->y;
        txt->w = rl_clamp_w(st, txt->x, rl_measure(st->font, n->text, 1));
        txt->h = st->font->glyph_height_px;
        txt->text = n->text;
        txt->href = (const char *)0;
    }
    st->y += line_h;
}

static void rl_emit_text(struct rl_state *st,
                         const struct capyhtml_node *n) {
    /* Free-floating TEXT outside of a P/H*: render at body scale.
     * Used by the parser when text appears directly inside <body>. */
    int line_h = st->font->glyph_height_px + st->font->line_gap_px;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_TEXT;
    c->scale = 1u;
    c->color_role = CAPYHTML_COLOR_TEXT;
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    c->w = rl_clamp_w(st, c->x, rl_measure(st->font, n->text, 1));
    c->h = st->font->glyph_height_px;
    c->text = n->text;
    c->href = (const char *)0;
    st->y += line_h;
}

static void rl_emit_hr(struct rl_state *st) {
    st->y += PARAGRAPH_MARGIN_TOP;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_RULE;
    c->color_role = CAPYHTML_COLOR_MUTED;
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    c->w = st->viewport_w - BLOCK_LEFT_MARGIN - BLOCK_RIGHT_MARGIN;
    if (c->w < 0) c->w = 0;
    c->h = RULE_HEIGHT;
    st->y += RULE_HEIGHT + PARAGRAPH_MARGIN_BOT;
}

/* Etapa 3 seção c (2026-05-03): emite CMD_INPUT para um node INPUT.
 * Dimensoes:
 *   - text/password: 200×24
 *   - submit:         96×24
 * `href` carrega action URL do form ativo (NULL fora de form);
 * `text` carrega o valor atual (para text/password) ou label (para
 * submit); reserved[0] carrega o subtipo; reserved[1..2] carrega o
 * node_idx no doc para o engine encontrar a posicao do INPUT
 * sem re-walk completo. */
static void rl_emit_input(struct rl_state *st,
                          const struct capyhtml_node *n,
                          uint16_t node_idx) {
    st->y += INPUT_MARGIN_TOP;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_INPUT;
    /* Submit usa color LINK (visualmente distintivo, parece botao);
     * text/password usa MUTED (parece campo neutro). O raster
     * interpreta estes roles segundo o subtipo, nao apenas color_role. */
    if (n->bold == CAPYHTML_INPUT_TYPE_SUBMIT) {
        c->color_role = CAPYHTML_COLOR_LINK;
    } else {
        c->color_role = CAPYHTML_COLOR_MUTED;
    }
    c->reserved[0] = n->bold;  /* input_subtype */
    CAPYHTML_CMD_PACK_NODE_IDX(c, node_idx);
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    /* Etapa 3 seção c refinement (2026-05-03): textarea tem largura
     * e altura proprias; select usa dimensoes do text input mas o
     * raster desenha um marcador "▼" a direita; submit estreito;
     * text/password padrao. */
    int target_w;
    int target_h;
    if (n->bold == CAPYHTML_INPUT_TYPE_SUBMIT) {
        target_w = INPUT_SUBMIT_W;
        target_h = INPUT_HEIGHT;
    } else if (n->bold == CAPYHTML_INPUT_TYPE_TEXTAREA) {
        target_w = INPUT_TEXTAREA_W;
        target_h = INPUT_TEXTAREA_H;
    } else {
        /* TEXT, PASSWORD, SELECT, default */
        target_w = INPUT_TEXT_W;
        target_h = INPUT_HEIGHT;
    }
    c->w = rl_clamp_w(st, c->x, target_w);
    c->h = target_h;
    c->text = n->text[0] ? n->text : (const char *)0;
    c->href = (st->current_form_action && st->current_form_action[0])
               ? st->current_form_action : (const char *)0;
    st->y += target_h + INPUT_MARGIN_BOT;
}

/* Etapa 3 seção a (2026-05-03): emite CMD_IMAGE para um node IMG.
 * Dimensoes vem de IMAGE_DEFAULT_W/H (futuro: parsear atributos
 * width/height). `href` carrega o src URL (para futuro fetch +
 * decode); `text` carrega o alt text (para screen-readers ou para
 * renderizacao dentro do placeholder quando a imagem nao carregar).
 * Layout reserva a altura completa + margens para que nodes
 * subsequentes nao sobreponham. */
static void rl_emit_image(struct rl_state *st,
                          const struct capyhtml_node *n) {
    st->y += IMAGE_MARGIN_TOP;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_IMAGE;
    c->color_role = CAPYHTML_COLOR_MUTED;
    c->x = BLOCK_LEFT_MARGIN;
    c->y = st->y;
    c->w = rl_clamp_w(st, c->x, IMAGE_DEFAULT_W);
    c->h = IMAGE_DEFAULT_H;
    /* Mesmo "text" do node vira alt-text; fica acessivel ao raster
     * que pode optar por escrever dentro do placeholder. */
    c->text = n->text[0] ? n->text : (const char *)0;
    /* src carregado no href para hit-test + future IMAGE_REQUEST. */
    c->href = n->href[0] ? n->href : (const char *)0;
    st->y += IMAGE_DEFAULT_H + IMAGE_MARGIN_BOT;
}

/* Etapa 3 seção d (2026-05-03; refinement 2026-05-03): conta TDs/THs
 * da primeira TR somando colspans. node->reserved[0] guarda colspan
 * (0 = sem atributo = trata como 1). Retorna 0..TABLE_MAX_COLS. */
static int rl_count_table_first_row_cols(const struct capyhtml_document *doc,
                                          int table_idx) {
    int n = doc->node_count;
    int found_tr = 0;
    int cols = 0;
    for (int j = table_idx + 1; j < n; ++j) {
        enum capyhtml_node_type t = doc->nodes[j].type;
        if (t == CAPYHTML_NODE_TAG_TR) {
            if (found_tr) break; /* segunda TR fecha a contagem. */
            found_tr = 1;
            continue;
        }
        if (!found_tr) {
            /* TDs/THs antes de qualquer TR -- aceita como linha
             * implicita (HTML real permite via tbody implicito). */
            if (t == CAPYHTML_NODE_TAG_TD || t == CAPYHTML_NODE_TAG_TH) {
                int span = doc->nodes[j].reserved[0];
                if (span < 1) span = 1;
                if (cols + span > TABLE_MAX_COLS) {
                    cols = TABLE_MAX_COLS;
                } else {
                    cols += span;
                }
                found_tr = 1; /* "linha implicita" passa a contar. */
            } else if (t == CAPYHTML_NODE_TAG_TABLE) {
                break;
            }
            continue;
        }
        if (t == CAPYHTML_NODE_TAG_TD || t == CAPYHTML_NODE_TAG_TH) {
            int span = doc->nodes[j].reserved[0];
            if (span < 1) span = 1;
            if (cols + span > TABLE_MAX_COLS) {
                cols = TABLE_MAX_COLS;
            } else {
                cols += span;
            }
        } else if (t == CAPYHTML_NODE_TAG_TABLE) {
            break;
        } else {
            /* Outro tipo no meio da TR? Ignora; HTML real nao deve
             * ter mas o parser e best-effort. */
        }
    }
    return cols;
}

/* Etapa 3 seção d (2026-05-03; refinement 2026-05-03): inicia tabela.
 * Conta colunas (somando colspans) da primeira linha, calcula
 * cell_w, e prepara o estado. Refinement: cell_w abaixo de
 * TABLE_MIN_CELL_W e clampado para MIN ao inves de descartar a
 * tabela inteira -- cells overflowam a viewport e o raster clipa
 * via put_pixel; isso permite tabelas grandes em viewport estreito
 * (anteriormente desapareciam). Avanca y pelo TABLE_MARGIN_TOP. */
static void rl_emit_table_open(struct rl_state *st,
                                const struct capyhtml_document *doc,
                                int table_idx) {
    int cols = rl_count_table_first_row_cols(doc, table_idx);
    if (cols <= 0) {
        st->table_col_count = 0;
        return;
    }
    int avail_w = st->viewport_w - BLOCK_LEFT_MARGIN - BLOCK_RIGHT_MARGIN;
    if (avail_w <= 0) {
        st->table_col_count = 0;
        return;
    }
    int cell_w = avail_w / cols;
    if (cell_w < TABLE_MIN_CELL_W) cell_w = TABLE_MIN_CELL_W;
    st->table_col_count = cols;
    st->table_col_index = 0;
    st->table_cell_w = cell_w;
    st->table_row_open = 0u;
    st->y += TABLE_MARGIN_TOP;
    st->table_row_y = st->y;
}

/* Etapa 3 seção d (2026-05-03; refinement 2026-05-03): emite uma
 * celula. Le colspan de n->reserved[0] (0 = trata como 1) e
 * - emite UM CMD_CELL com w = colspan * cell_w;
 * - avanca col_index por colspan;
 * - clampa colspan ao remaining para nao overflow.
 * Mais celulas que cols na mesma linha: ignora (defesa). */
static void rl_emit_cell(struct rl_state *st,
                          const struct capyhtml_node *n) {
    if (st->table_col_count <= 0) return; /* fora de tabela */
    if (!st->table_row_open) {
        /* Linha implicita: parser pulou TR mas tem TDs. Abre. */
        st->table_row_open = 1u;
        st->table_col_index = 0;
        st->table_row_y = st->y;
    }
    if (st->table_col_index >= st->table_col_count) {
        /* Mais celulas que colunas: ignora extras (defesa contra
         * tabelas mal-formadas). */
        return;
    }
    int colspan = n->reserved[0];
    if (colspan < 1) colspan = 1;
    int remaining = st->table_col_count - st->table_col_index;
    if (colspan > remaining) colspan = remaining;
    struct capyhtml_cmd *c = rl_alloc(st);
    if (!c) return;
    c->kind = CAPYHTML_CMD_CELL;
    c->scale = 1u;
    c->bold = (uint8_t)(n->bold ? 1u : 0u);
    c->color_role = n->bold
                     ? CAPYHTML_COLOR_HEADING
                     : CAPYHTML_COLOR_TEXT;
    c->x = BLOCK_LEFT_MARGIN
           + st->table_col_index * st->table_cell_w;
    c->y = st->table_row_y;
    c->w = colspan * st->table_cell_w;
    c->h = CELL_HEIGHT;
    c->text = n->text[0] ? n->text : (const char *)0;
    c->href = (const char *)0;
    st->table_col_index += colspan;
}

/* Etapa 3 seção d (2026-05-03): nova TR. Se ja havia uma linha
 * aberta, fecha avancando y. Sempre reseta col_index = 0. */
static void rl_emit_tr_open(struct rl_state *st) {
    if (st->table_col_count <= 0) return;
    if (st->table_row_open) {
        /* Fecha a linha anterior. */
        st->y = st->table_row_y + CELL_HEIGHT;
    }
    st->table_col_index = 0;
    st->table_row_y = st->y;
    st->table_row_open = 1u;
}

/* Etapa 3 seção d (2026-05-03): fecha a tabela (chamado quando
 * encontramos um node que nao pertence a uma tabela). Avanca y
 * e zera o estado de tabela. */
static void rl_emit_table_close(struct rl_state *st) {
    if (st->table_col_count <= 0) return;
    if (st->table_row_open) {
        /* Fecha a ultima linha aberta. */
        st->y = st->table_row_y + CELL_HEIGHT;
    }
    st->y += TABLE_MARGIN_BOT;
    st->table_col_count = 0;
    st->table_col_index = 0;
    st->table_cell_w = 0;
    st->table_row_open = 0u;
}

/* --- public entrypoint -------------------------------------------- */

int capyhtml_layout(const struct capyhtml_document *doc,
                    const struct capyhtml_font_ops *font_ops,
                    int32_t viewport_w,
                    struct capyhtml_cmd *cmds,
                    uint16_t cmd_capacity,
                    struct capyhtml_render_result *result) {
    if (!doc || !font_ops || !cmds || cmd_capacity == 0u || viewport_w <= 0) {
        return -1;
    }
    if (font_ops->glyph_height_px <= 0 || font_ops->glyph_width_px <= 0) {
        return -1;
    }

    struct rl_state st;
    st.cmds = cmds;
    st.cap = cmd_capacity;
    st.count = 0;
    st.truncated = 0u;
    st.y = 0;
    st.viewport_w = viewport_w;
    st.font = font_ops;
    st.current_form_action = (const char *)0;
    st.table_col_count = 0;
    st.table_col_index = 0;
    st.table_cell_w = 0;
    st.table_row_y = 0;
    st.table_row_open = 0u;

    int n = doc->node_count;
    if (n < 0) n = 0;
    if (n > CAPYHTML_MAX_NODES) n = CAPYHTML_MAX_NODES;

    for (int i = 0; i < n; i++) {
        const struct capyhtml_node *node = &doc->nodes[i];
        /* Etapa 3 seção d (2026-05-03): qualquer node que NAO seja
         * TABLE/TR/TD/TH fecha a tabela ativa antes de processar.
         * Mantem y consistente para o conteudo apos a tabela. */
        if (st.table_col_count > 0 &&
            node->type != CAPYHTML_NODE_TAG_TABLE &&
            node->type != CAPYHTML_NODE_TAG_TR &&
            node->type != CAPYHTML_NODE_TAG_TD &&
            node->type != CAPYHTML_NODE_TAG_TH) {
            rl_emit_table_close(&st);
        }
        switch (node->type) {
            case CAPYHTML_NODE_TAG_H1:
            case CAPYHTML_NODE_TAG_H2:
            case CAPYHTML_NODE_TAG_H3:
                rl_emit_heading(&st, node);
                break;
            case CAPYHTML_NODE_TAG_P:
            case CAPYHTML_NODE_TAG_DIV:
                rl_emit_paragraph(&st, node);
                break;
            case CAPYHTML_NODE_TAG_A:
                rl_emit_link(&st, node);
                break;
            case CAPYHTML_NODE_TAG_LI:
                rl_emit_li(&st, node);
                break;
            case CAPYHTML_NODE_TAG_HR:
                rl_emit_hr(&st);
                break;
            case CAPYHTML_NODE_TAG_BR:
                /* blank line, no command */
                st.y += st.font->glyph_height_px + st.font->line_gap_px;
                break;
            case CAPYHTML_NODE_TAG_IMG:
                rl_emit_image(&st, node);
                break;
            case CAPYHTML_NODE_TAG_FORM:
                /* Etapa 3 seção c (2026-05-03): atualiza contexto de
                 * form ativo. Inputs subsequentes herdam este action.
                 * Nao emite cmd proprio (form e organizacional). */
                st.current_form_action =
                    node->href[0] ? node->href : (const char *)0;
                break;
            case CAPYHTML_NODE_TAG_INPUT:
                rl_emit_input(&st, node, (uint16_t)i);
                break;
            case CAPYHTML_NODE_TAG_TABLE:
                /* Etapa 3 seção d (2026-05-03): table aninhada nao
                 * suportada -- fecha a anterior antes. (O switch ja
                 * fecha a tabela ativa no topo do loop quando o tipo
                 * mudou; mas TABLE e o mesmo tipo do anterior, entao
                 * forcamos aqui). */
                if (st.table_col_count > 0) rl_emit_table_close(&st);
                rl_emit_table_open(&st, doc, i);
                break;
            case CAPYHTML_NODE_TAG_TR:
                rl_emit_tr_open(&st);
                break;
            case CAPYHTML_NODE_TAG_TD:
            case CAPYHTML_NODE_TAG_TH:
                rl_emit_cell(&st, node);
                break;
            case CAPYHTML_NODE_TEXT:
                rl_emit_text(&st, node);
                break;
            case CAPYHTML_NODE_TAG_UL:
            case CAPYHTML_NODE_TAG_SPAN:
            case CAPYHTML_NODE_TAG_OPTION:
            case CAPYHTML_NODE_NONE:
            default:
                /* container / no-op nodes: contribute no command but
                 * also no vertical advance. UL margins live on its
                 * LI children. OPTION nodes existem so para o engine
                 * iterar sobre eles; layout os ignora. */
                break;
        }
    }
    /* Etapa 3 seção d (2026-05-03): tabela ainda aberta no fim do
     * doc -- fecha para que total_height_px reflita corretamente. */
    if (st.table_col_count > 0) {
        rl_emit_table_close(&st);
    }

    if (result) {
        result->cmd_count = st.count;
        result->truncated = st.truncated;
        result->reserved = 0u;
        result->total_height_px = st.y;
    }
    return 0;
}
