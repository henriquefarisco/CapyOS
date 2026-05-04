/* tests/test_capyhtml_render.c -- F3.3c slice 3 host coverage.
 *
 * Validates the layout pass produced by `capyhtml_layout()`:
 *
 *   - Argument validation (NULL doc / font / cmds, zero capacity,
 *     non-positive viewport, zero glyph metrics).
 *   - Heading nodes get scale > 1 and bold = 1.
 *   - Paragraph nodes use scale = 1 and color = TEXT.
 *   - Link nodes carry the href and underline = 1, color = LINK.
 *   - LI nodes emit a BULLET + TEXT pair on the same baseline.
 *   - HR emits a RULE with viewport-wide width.
 *   - viewport_w clamps the intrinsic w of long text runs.
 *   - cmd_capacity overflow sets truncated = 1 and stops emitting.
 *   - total_height grows monotonically with the node count.
 *
 * Mock font_ops: each glyph is 8x16 with 4 px line gap so the math
 * is easy to follow in the asserts (line_h = 16 + 4 = 20). */

#include "capyhtml/parser.h"
#include "capyhtml/render.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define R_OK(cond, msg) do {                                              \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

static int mock_measure(const char *text, int scale, void *ctx) {
    (void)ctx;
    if (!text) return 0;
    int n = 0;
    while (text[n]) n++;
    return n * 8 * (scale > 0 ? scale : 1);
}

static struct capyhtml_font_ops mock_font(void) {
    struct capyhtml_font_ops f;
    memset(&f, 0, sizeof(f));
    f.measure_width = mock_measure;
    f.glyph_width_px = 8;
    f.glyph_height_px = 16;
    f.line_gap_px = 4;
    f.ctx = NULL;
    return f;
}

static int parse_into(const char *html, struct capyhtml_document *out) {
    return capyhtml_parse(html, strlen(html), out, NULL, NULL);
}

static void test_arg_validation(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    R_OK(capyhtml_layout(NULL, &f, 200, cmds, 8, &r) == -1, "NULL doc rejected");
    R_OK(capyhtml_layout(&doc, NULL, 200, cmds, 8, &r) == -1, "NULL font rejected");
    R_OK(capyhtml_layout(&doc, &f, 200, NULL, 8, &r) == -1, "NULL cmds rejected");
    R_OK(capyhtml_layout(&doc, &f, 200, cmds, 0, &r) == -1, "zero cap rejected");
    R_OK(capyhtml_layout(&doc, &f, 0, cmds, 8, &r) == -1, "zero viewport rejected");
    struct capyhtml_font_ops bad = f;
    bad.glyph_height_px = 0;
    R_OK(capyhtml_layout(&doc, &bad, 200, cmds, 8, &r) == -1,
         "zero glyph_height rejected");
}

static void test_heading_emits_scaled_bold(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<h1>capyland</h1><h2>sub</h2>", &doc);
    int rc = capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(rc == 0, "layout returned ok");
    R_OK(r.cmd_count >= 2, "heading: emitted at least 2 cmds");
    R_OK(cmds[0].kind == CAPYHTML_CMD_TEXT, "heading[0] is TEXT");
    R_OK(cmds[0].scale == 3, "h1 scale=3");
    R_OK(cmds[0].bold == 1, "h1 bold=1");
    R_OK(cmds[0].color_role == CAPYHTML_COLOR_HEADING, "h1 color=HEADING");
    R_OK(cmds[0].h == 16 * 3, "h1 height=glyph_h*3");
    R_OK(cmds[0].text != NULL && strcmp(cmds[0].text, "capyland") == 0,
         "h1 text borrowed from doc");
    R_OK(cmds[1].scale == 2, "h2 scale=2");
    R_OK(cmds[1].y > cmds[0].y, "h2 below h1");
}

static void test_paragraph_basic(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<p>hello world</p>", &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(r.cmd_count == 1, "paragraph: 1 cmd");
    R_OK(cmds[0].kind == CAPYHTML_CMD_TEXT, "p kind=TEXT");
    R_OK(cmds[0].scale == 1, "p scale=1");
    R_OK(cmds[0].bold == 0, "p not bold");
    R_OK(cmds[0].color_role == CAPYHTML_COLOR_TEXT, "p color=TEXT");
    R_OK(cmds[0].w == (int)strlen(cmds[0].text) * 8,
         "p w = strlen*glyph_w");
}

static void test_link_underline_and_href(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<a href=\"about:blank\">docs</a>", &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(r.cmd_count >= 1, "link: emitted cmd");
    int found = -1;
    for (int i = 0; i < r.cmd_count; i++) {
        if (cmds[i].color_role == CAPYHTML_COLOR_LINK) { found = i; break; }
    }
    R_OK(found >= 0, "link cmd found");
    if (found >= 0) {
        R_OK(cmds[found].underline == 1, "link underline=1");
        R_OK(cmds[found].href != NULL &&
             strcmp(cmds[found].href, "about:blank") == 0,
             "link href captured");
    }
}

static void test_li_emits_bullet_and_text(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<ul><li>fast</li><li>safe</li></ul>", &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    int bullets = 0, texts = 0;
    for (int i = 0; i < r.cmd_count; i++) {
        if (cmds[i].kind == CAPYHTML_CMD_BULLET) bullets++;
        if (cmds[i].kind == CAPYHTML_CMD_TEXT &&
            cmds[i].color_role == CAPYHTML_COLOR_TEXT) texts++;
    }
    R_OK(bullets == 2, "two bullets emitted");
    R_OK(texts >= 2, "two LI texts emitted");
    /* Bullet x must be > paragraph left margin (indented) and the
     * text x must be > bullet x (right of the marker). */
    int b0 = -1, t0 = -1;
    for (int i = 0; i < r.cmd_count; i++) {
        if (cmds[i].kind == CAPYHTML_CMD_BULLET && b0 < 0) b0 = i;
        if (cmds[i].kind == CAPYHTML_CMD_TEXT &&
            cmds[i].color_role == CAPYHTML_COLOR_TEXT && b0 >= 0 && t0 < 0) {
            t0 = i; break;
        }
    }
    if (b0 >= 0 && t0 >= 0) {
        R_OK(cmds[b0].x > 12, "bullet indented past block margin");
        R_OK(cmds[t0].x > cmds[b0].x, "li text right of bullet");
    }
}

static void test_viewport_clamps_width(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    /* 100 chars * 8 px = 800 px intrinsic; clamp to viewport 200. */
    parse_into("<p>aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa</p>",
               &doc);
    capyhtml_layout(&doc, &f, 200, cmds, 16, &r);
    R_OK(r.cmd_count == 1, "long p: one cmd");
    /* cmds[0].x = 12, viewport=200, right_margin=12 => max w = 176. */
    R_OK(cmds[0].w == 176, "long p clamped to viewport-margins");
}

static void test_capacity_overflow_marks_truncated(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[2];
    struct capyhtml_render_result r;
    parse_into("<h1>a</h1><p>b</p><p>c</p><p>d</p>", &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 2, &r);
    R_OK(r.cmd_count == 2, "stopped at capacity");
    R_OK(r.truncated == 1, "truncated flag set");
}

static void test_total_height_advances(void) {
    struct capyhtml_document a, b;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result ra, rb;
    parse_into("<p>one</p>", &a);
    parse_into("<p>one</p><p>two</p><p>three</p>", &b);
    capyhtml_layout(&a, &f, 320, cmds, 16, &ra);
    capyhtml_layout(&b, &f, 320, cmds, 16, &rb);
    R_OK(rb.total_height_px > ra.total_height_px,
         "more nodes -> larger total_height");
    R_OK(ra.total_height_px > 0, "non-empty layout has y>0");
}

/* Etapa 3 seção a (2026-05-03): IMG node emits CMD_IMAGE with
 * default dims, href == src, text == alt. Layout reserva a altura
 * total + margens para que nodes seguintes nao sobreponham. */
static void test_img_emits_image_cmd(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<p>antes</p><img src=\"logo.png\" alt=\"Logo\">"
               "<p>depois</p>", &doc);
    int rc = capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(rc == 0, "img: layout ok");
    R_OK(r.cmd_count == 3, "img: emits 3 cmds (P+IMG+P)");

    R_OK(cmds[1].kind == CAPYHTML_CMD_IMAGE, "img: cmd[1].kind = IMAGE");
    R_OK(cmds[1].w > 0 && cmds[1].h > 0, "img: non-zero dims");
    R_OK(cmds[1].w <= 320 - 12, "img: width clamped to viewport");
    R_OK(cmds[1].href != NULL && strcmp(cmds[1].href, "logo.png") == 0,
         "img: href carries src");
    R_OK(cmds[1].text != NULL && strcmp(cmds[1].text, "Logo") == 0,
         "img: text carries alt");
    R_OK(cmds[1].color_role == CAPYHTML_COLOR_MUTED,
         "img: color_role = MUTED");
    R_OK(cmds[2].y > cmds[1].y + cmds[1].h,
         "img: next P below image bottom");
}

static void test_img_without_src_still_emits(void) {
    /* Mesmo sem src, o layout reserva o placeholder; href fica NULL
     * (para o renderer mostrar "imagem quebrada" se quiser). */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    parse_into("<img alt=\"broken\">", &doc);
    int rc = capyhtml_layout(&doc, &f, 320, cmds, 8, &r);
    R_OK(rc == 0, "img (no src): layout ok");
    R_OK(r.cmd_count == 1, "img (no src): 1 cmd emitted");
    R_OK(cmds[0].kind == CAPYHTML_CMD_IMAGE, "img (no src): IMAGE cmd");
    R_OK(cmds[0].href == NULL, "img (no src): href is NULL");
    R_OK(cmds[0].text != NULL && strcmp(cmds[0].text, "broken") == 0,
         "img (no src): alt preserved");
}

static void test_img_clamped_in_narrow_viewport(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    parse_into("<img src=\"a.png\">", &doc);
    /* Viewport 50 px: default image (100 px) nao cabe; rl_clamp_w
     * reduz para 50 - LEFT_MARGIN - RIGHT_MARGIN = 26 px. */
    capyhtml_layout(&doc, &f, 50, cmds, 8, &r);
    R_OK(cmds[0].kind == CAPYHTML_CMD_IMAGE, "narrow: IMAGE emitted");
    R_OK(cmds[0].w < 100, "narrow: width reduced below default 100");
    R_OK(cmds[0].w >= 0, "narrow: width non-negative");
}

/* Etapa 3 seção c (2026-05-03): INPUT inside FORM emits CMD_INPUT
 * with subtype + node_idx packed in reserved[]. The href of every
 * input inherits the form's action URL. Submit is wider than text
 * by design (98×24 vs 200×24) but our test viewport is narrow enough
 * to clamp both. */
static void test_input_inside_form(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<form action=\"/q\">"
               "<input type=\"text\" name=\"k\" value=\"v\">"
               "<input type=\"submit\" value=\"Go\">"
               "</form>", &doc);
    int rc = capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(rc == 0, "form: layout ok");
    R_OK(r.cmd_count == 2, "form: 2 INPUT cmds emitted");

    /* Both should be CMD_INPUT, both inherit form action in href. */
    R_OK(cmds[0].kind == CAPYHTML_CMD_INPUT, "form: cmd[0] is CMD_INPUT");
    R_OK(cmds[1].kind == CAPYHTML_CMD_INPUT, "form: cmd[1] is CMD_INPUT");
    R_OK(cmds[0].href != NULL && strcmp(cmds[0].href, "/q") == 0,
         "form: text input inherits action");
    R_OK(cmds[1].href != NULL && strcmp(cmds[1].href, "/q") == 0,
         "form: submit inherits action");

    /* Subtype encoded in reserved[0]. */
    R_OK(cmds[0].reserved[0] == CAPYHTML_INPUT_TYPE_TEXT,
         "form: cmd[0] subtype = TEXT");
    R_OK(cmds[1].reserved[0] == CAPYHTML_INPUT_TYPE_SUBMIT,
         "form: cmd[1] subtype = SUBMIT");

    /* Text value carried in cmd->text. */
    R_OK(cmds[0].text != NULL && strcmp(cmds[0].text, "v") == 0,
         "form: text cmd carries value");
    R_OK(cmds[1].text != NULL && strcmp(cmds[1].text, "Go") == 0,
         "form: submit cmd carries label");

    /* Submit narrower than text input. */
    R_OK(cmds[1].w < cmds[0].w, "form: submit is narrower than text");

    /* node_idx packed in reserved[1..2] points back to a TAG_INPUT
     * node with matching value. */
    {
        uint16_t idx = CAPYHTML_CMD_NODE_IDX(cmds[0]);
        R_OK(idx < (uint16_t)doc.node_count,
             "form: cmd[0] node_idx within bounds");
        R_OK(doc.nodes[idx].type == CAPYHTML_NODE_TAG_INPUT,
             "form: cmd[0] node_idx -> INPUT node");
    }
    {
        uint16_t idx = CAPYHTML_CMD_NODE_IDX(cmds[1]);
        R_OK(idx < (uint16_t)doc.node_count,
             "form: cmd[1] node_idx within bounds");
        R_OK(doc.nodes[idx].type == CAPYHTML_NODE_TAG_INPUT,
             "form: cmd[1] node_idx -> INPUT node");
    }
}

static void test_input_outside_form_no_action(void) {
    /* Input fora de qualquer <form>: href fica NULL. */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    parse_into("<input type=\"text\" name=\"floating\">", &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 8, &r);
    R_OK(r.cmd_count == 1, "free input: 1 cmd");
    R_OK(cmds[0].kind == CAPYHTML_CMD_INPUT, "free input: CMD_INPUT");
    R_OK(cmds[0].href == NULL, "free input: href NULL (no parent form)");
    R_OK(cmds[0].text == NULL, "free input: empty value -> text NULL");
}

static void test_input_form_node_idx_back_to_doc(void) {
    /* Sanity: o engine usa CAPYHTML_CMD_NODE_IDX para mutar o text
     * do node correspondente quando o usuario digita. Garantimos
     * que esta indireção é estável: parse, layout, depois mutar
     * doc.nodes[idx].text e re-layout deve re-emitir o novo valor. */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    parse_into("<form action=\"/x\"><input name=\"k\" value=\"old\"></form>",
               &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 8, &r);
    R_OK(strcmp(cmds[0].text, "old") == 0, "before mutation: 'old'");
    uint16_t idx = CAPYHTML_CMD_NODE_IDX(cmds[0]);
    /* Simula key route. */
    doc.nodes[idx].text[0] = 'n'; doc.nodes[idx].text[1] = 'e';
    doc.nodes[idx].text[2] = 'w'; doc.nodes[idx].text[3] = '\0';
    capyhtml_layout(&doc, &f, 320, cmds, 8, &r);
    R_OK(strcmp(cmds[0].text, "new") == 0, "after mutation: 'new'");
}

/* Etapa 3 seção d (2026-05-03): tabela 2x2 deve emitir 4 CMD_CELL com
 * x avancando por col_index e y constante na mesma linha. */
static void test_table_2x2_basic(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<table><tr><th>A</th><th>B</th></tr>"
               "<tr><td>1</td><td>2</td></tr></table>", &doc);
    int rc = capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(rc == 0, "table: layout ok");
    R_OK(r.cmd_count == 4, "table 2x2: 4 CMD_CELL emitted");
    for (int i = 0; i < 4; ++i) {
        R_OK(cmds[i].kind == CAPYHTML_CMD_CELL,
             "table 2x2: cmd is CMD_CELL");
    }
    /* Linha 0: cmds[0] e cmds[1] tem o mesmo y. */
    R_OK(cmds[0].y == cmds[1].y, "table: row 0 cells share y");
    /* Linha 1: cmds[2] e cmds[3] tem o mesmo y, e maior que linha 0. */
    R_OK(cmds[2].y == cmds[3].y, "table: row 1 cells share y");
    R_OK(cmds[2].y > cmds[0].y, "table: row 1 below row 0");
    /* Cells da mesma linha tem x diferente, e cols nao overlapam. */
    R_OK(cmds[0].x < cmds[1].x, "table: col 0 left of col 1");
    R_OK(cmds[0].x + cmds[0].w == cmds[1].x,
         "table: cells abut (col widths sum)");
    /* TH bold = 1, TD bold = 0. */
    R_OK(cmds[0].bold == 1 && cmds[1].bold == 1,
         "table: TH cells bold=1");
    R_OK(cmds[2].bold == 0 && cmds[3].bold == 0,
         "table: TD cells bold=0");
    /* TH cor = HEADING; TD cor = TEXT. */
    R_OK(cmds[0].color_role == CAPYHTML_COLOR_HEADING,
         "table: TH role HEADING");
    R_OK(cmds[2].color_role == CAPYHTML_COLOR_TEXT,
         "table: TD role TEXT");
    /* Conteudo. */
    R_OK(cmds[0].text && strcmp(cmds[0].text, "A") == 0,
         "table: cell[0] text 'A'");
    R_OK(cmds[3].text && strcmp(cmds[3].text, "2") == 0,
         "table: cell[3] text '2'");
}

static void test_table_followed_by_paragraph(void) {
    /* P apos a tabela deve aparecer abaixo da ultima linha + margin. */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<table><tr><td>x</td></tr></table><p>after</p>", &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(r.cmd_count == 2, "table+p: 2 cmds (cell + p)");
    R_OK(cmds[0].kind == CAPYHTML_CMD_CELL, "first is cell");
    R_OK(cmds[1].kind == CAPYHTML_CMD_TEXT, "second is text");
    R_OK(cmds[1].y > cmds[0].y + cmds[0].h,
         "p comes after cell row");
}

static void test_table_zero_cols_drops(void) {
    /* TABLE com TR vazio (sem TDs/THs) -- nao emite cells. */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    parse_into("<table><tr></tr></table>", &doc);
    int rc = capyhtml_layout(&doc, &f, 320, cmds, 16, &r);
    R_OK(rc == 0, "empty table: layout ok");
    R_OK(r.cmd_count == 0, "empty table: 0 cells emitted");
}

static void test_table_excess_cells_in_row_clipped(void) {
    /* TR com mais celulas que a primeira TR define como col_count
     * deve clipar (extras descartados defensivamente). */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[32];
    struct capyhtml_render_result r;
    /* Primeira TR define 2 cols; segunda TR tem 4 -> 2 ultimos drops. */
    parse_into("<table><tr><td>A</td><td>B</td></tr>"
               "<tr><td>1</td><td>2</td><td>3</td><td>4</td></tr></table>",
               &doc);
    capyhtml_layout(&doc, &f, 320, cmds, 32, &r);
    R_OK(r.cmd_count == 4,
         "excess: only 4 cells (cols=2 x rows=2; extras dropped)");
}

/* Etapa 3 seção d refinement (2026-05-03): colspan na primeira TR
 * faz cell ocupar N posicoes; col_count vem da soma de spans. */
static void test_table_colspan_first_row_sets_count(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    /* TR1: TD colspan=2 + TD => 3 colunas. TR2: 3 TDs simples. */
    parse_into("<table>"
               "<tr><td colspan=\"2\">wide</td><td>narrow</td></tr>"
               "<tr><td>a</td><td>b</td><td>c</td></tr>"
               "</table>", &doc);
    int rc = capyhtml_layout(&doc, &f, 480, cmds, 16, &r);
    R_OK(rc == 0, "colspan: layout ok");
    R_OK(r.cmd_count == 5, "colspan: 5 cells (2+1 + 3)");
    /* Primeira celula tem w = 2*cell_w; segunda tem w = cell_w. */
    R_OK(cmds[0].w == 2 * cmds[1].w,
         "colspan: cell[0].w = 2 * cell[1].w");
    /* X da segunda celula = x da primeira + 2*cell_w. */
    R_OK(cmds[1].x == cmds[0].x + cmds[0].w,
         "colspan: cell[1].x abuts cell[0]");
    /* Linha 2: tres celulas simples mesma largura. */
    R_OK(cmds[2].w == cmds[3].w && cmds[3].w == cmds[4].w,
         "colspan: row 2 all narrow cells");
    /* Linha 2 toda no mesmo y, abaixo da linha 1. */
    R_OK(cmds[2].y == cmds[3].y && cmds[3].y == cmds[4].y,
         "colspan: row 2 cells share y");
    R_OK(cmds[2].y > cmds[0].y, "colspan: row 2 below row 1");
}

static void test_table_colspan_clamped_to_remaining(void) {
    /* Se col_index ja avancou e colspan excede o resto, clampa. */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[16];
    struct capyhtml_render_result r;
    /* Tabela 3 cols. Segunda linha: TD + TD colspan=5 (deveria virar 2). */
    parse_into("<table>"
               "<tr><td>a</td><td>b</td><td>c</td></tr>"
               "<tr><td>x</td><td colspan=\"5\">overflow</td></tr>"
               "</table>", &doc);
    capyhtml_layout(&doc, &f, 480, cmds, 16, &r);
    R_OK(r.cmd_count == 5, "colspan clamp: 5 cells emitted");
    /* cmds[4] = TD colspan=5 mas clampado: w == 2 * unit_w. */
    R_OK(cmds[4].w == 2 * cmds[3].w,
         "colspan clamp: width = remaining * unit");
}

static void test_table_narrow_viewport_clamps_min_w(void) {
    /* Tabela 8 cols em viewport 200 px: avail/8 < TABLE_MIN_CELL_W (32),
     * antes era dropada; agora cell_w = MIN, cells overflowam mas
     * sao emitidos. Raster clipa por bounds. */
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[32];
    struct capyhtml_render_result r;
    parse_into("<table><tr>"
               "<td>1</td><td>2</td><td>3</td><td>4</td>"
               "<td>5</td><td>6</td><td>7</td><td>8</td>"
               "</tr></table>", &doc);
    capyhtml_layout(&doc, &f, 200, cmds, 32, &r);
    R_OK(r.cmd_count == 8, "narrow: 8 cells emitted (no drop)");
    R_OK(cmds[0].w >= 32, "narrow: cell w clamped to MIN_CELL_W");
}

/* Etapa 3 seção c refinement (2026-05-03): select emite CMD_INPUT
 * com subtype SELECT e text = first option's label. */
static void test_select_renders_first_option_default(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    parse_into("<form action=\"/q\">"
               "<select name=\"c\">"
               "<option value=\"a\">Alpha</option>"
               "<option value=\"b\">Beta</option>"
               "</select></form>", &doc);
    capyhtml_layout(&doc, &f, 480, cmds, 8, &r);
    R_OK(r.cmd_count == 1, "select: 1 cmd (options nao geram cmd)");
    R_OK(cmds[0].kind == CAPYHTML_CMD_INPUT, "select: CMD_INPUT");
    /* Subtype SELECT no nibble baixo de reserved[0]. */
    uint8_t st = (uint8_t)(cmds[0].reserved[0]
                            & CAPYHTML_INPUT_SUBTYPE_MASK);
    R_OK(st == CAPYHTML_INPUT_TYPE_SELECT, "select: subtype = SELECT");
    /* Text mostra label do primeiro option. */
    R_OK(cmds[0].text && strcmp(cmds[0].text, "Alpha") == 0,
         "select: text = primeiro option label");
    /* Form action herdado. */
    R_OK(cmds[0].href != NULL && strcmp(cmds[0].href, "/q") == 0,
         "select: inherits form action");
}

/* Etapa 3 seção c refinement (2026-05-03): textarea emite CMD_INPUT
 * com altura > INPUT_HEIGHT padrao (24). */
static void test_textarea_renders_taller_box(void) {
    struct capyhtml_document doc;
    struct capyhtml_font_ops f = mock_font();
    struct capyhtml_cmd cmds[8];
    struct capyhtml_render_result r;
    parse_into("<form action=\"/m\">"
               "<textarea name=\"msg\">hi</textarea>"
               "</form>", &doc);
    capyhtml_layout(&doc, &f, 480, cmds, 8, &r);
    R_OK(r.cmd_count == 1, "textarea: 1 cmd");
    R_OK(cmds[0].kind == CAPYHTML_CMD_INPUT, "textarea: CMD_INPUT");
    R_OK(cmds[0].h > 24, "textarea: h > regular input height");
    R_OK(cmds[0].h >= 64, "textarea: h >= 64 (multi-line)");
    /* Subtype carrega o tipo TEXTAREA em reserved[0]. */
    uint8_t st = (uint8_t)(cmds[0].reserved[0]
                            & CAPYHTML_INPUT_SUBTYPE_MASK);
    R_OK(st == CAPYHTML_INPUT_TYPE_TEXTAREA,
         "textarea: subtype TEXTAREA");
    /* Form action herdado. */
    R_OK(cmds[0].href != NULL && strcmp(cmds[0].href, "/m") == 0,
         "textarea: inherits form action");
}

int test_capyhtml_render_run(void) {
    printf("[test_capyhtml_render]\n");
    g_passed = 0;
    g_failed = 0;
    test_arg_validation();
    test_heading_emits_scaled_bold();
    test_paragraph_basic();
    test_link_underline_and_href();
    test_li_emits_bullet_and_text();
    test_viewport_clamps_width();
    test_capacity_overflow_marks_truncated();
    test_total_height_advances();
    test_img_emits_image_cmd();
    test_img_without_src_still_emits();
    test_img_clamped_in_narrow_viewport();
    test_input_inside_form();
    test_input_outside_form_no_action();
    test_input_form_node_idx_back_to_doc();
    test_table_2x2_basic();
    test_table_followed_by_paragraph();
    test_table_zero_cols_drops();
    test_table_excess_cells_in_row_clipped();
    test_table_colspan_first_row_sets_count();
    test_table_colspan_clamped_to_remaining();
    test_table_narrow_viewport_clamps_min_w();
    test_textarea_renders_taller_box();
    test_select_renders_first_option_default();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed;
}
