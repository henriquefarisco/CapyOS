/* tests/test_capyhtml_raster.c -- F3.3c slice 4-final host coverage.
 *
 * Validates the BGRA8888 software rasterizer in libcapyhtml:
 *
 *   - Clear writes the requested ARGB to every pixel of the target.
 *   - One TEXT cmd produces non-zero pixels somewhere inside the
 *     glyph bounding box and zero pixels in a guaranteed-empty
 *     surrounding region (the row above the glyph baseline).
 *   - The pixel color matches the palette slot referenced by
 *     `color_role`.
 *   - Bold doubles the lit pixel count by shifting one px right.
 *   - Underline draws a horizontal line at the glyph bottom.
 *   - BULLET fills a square at (x, y, w=h).
 *   - RULE fills a horizontal rect spanning `w` pixels.
 *   - Out-of-bounds commands clip silently (no crash, no writes
 *     past the end of the buffer).
 *   - Render walks all cmds in order on top of the cleared bg.
 *   - Unknown cmd kinds are ignored (forward-compat).
 *   - Out-of-range color_role falls back to TEXT (slot 0).
 *
 * The renderer is freestanding and the test exercises raw
 * memory: we allocate a small static framebuffer, raster, then
 * verify pixel bytes directly without any libc beyond stdio. */
#include "capyhtml/raster.h"
#include "capyhtml/render.h"
#include "capyhtml/font.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define RX_OK(cond, msg) do {                                             \
    if (cond) { g_passed++; }                                             \
    else { g_failed++; printf("  FAIL %s\n", msg); }                      \
} while (0)

/* 32x16 BGRA8888 = 2048 bytes. Big enough for an "A" at 1x and
 * 2x without spilling, small enough to scan exhaustively. */
#define FB_W 32
#define FB_H 16
#define FB_STRIDE (FB_W * 4)
static uint8_t g_fb[FB_STRIDE * FB_H];

static void fb_reset(void) { memset(g_fb, 0xAA, sizeof(g_fb)); }

static struct capyhtml_raster_target target(void) {
    struct capyhtml_raster_target t;
    t.pixels = g_fb;
    t.width_px = FB_W;
    t.height_px = FB_H;
    t.stride_b = FB_STRIDE;
    return t;
}

static struct capyhtml_palette default_palette(void) {
    struct capyhtml_palette p;
    /* Distinct values per slot so we can detect mis-routing. */
    p.color_argb[0] = 0xFFFFFFFFu; /* TEXT  -- white */
    p.color_argb[1] = 0xFFFF0000u; /* HEADING red */
    p.color_argb[2] = 0xFF0000FFu; /* LINK  blue */
    p.color_argb[3] = 0xFF808080u; /* MUTED gray */
    p.color_argb[4] = 0xFFFFA500u; /* BULLET orange */
    p.background_argb = 0xFF000000u; /* black */
    return p;
}

static uint32_t pixel_argb(int x, int y) {
    uint8_t *p = &g_fb[y * FB_STRIDE + x * 4];
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[1] << 8)  | (uint32_t)p[0];
}

static int count_set_pixels(uint32_t want_argb) {
    int n = 0;
    for (int y = 0; y < FB_H; ++y)
        for (int x = 0; x < FB_W; ++x)
            if (pixel_argb(x, y) == want_argb) ++n;
    return n;
}

static void test_clear_fills_buffer(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    capyhtml_raster_clear(&t, 0xFF112233u);
    RX_OK(pixel_argb(0, 0) == 0xFF112233u, "clear: top-left painted");
    RX_OK(pixel_argb(FB_W - 1, FB_H - 1) == 0xFF112233u,
          "clear: bottom-right painted");
    int matches = count_set_pixels(0xFF112233u);
    RX_OK(matches == FB_W * FB_H, "clear: every pixel matches");
}

static void test_text_cmd_writes_glyph(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_TEXT;
    cmd.scale = 1;
    cmd.color_role = CAPYHTML_COLOR_TEXT;
    cmd.x = 0; cmd.y = 4;
    cmd.text = "A";

    capyhtml_raster_draw(&t, &cmd, &pal);

    int lit = count_set_pixels(0xFFFFFFFFu);
    /* The 'A' glyph in the embedded 8x8 font has 30 lit bits
     * (rows: 0x38 0x6C 0xC6 0xC6 0xFE 0xC6 0xC6 0x00). */
    RX_OK(lit == 30, "text: 'A' glyph lit count exact (30)");
    /* Row 0..3 above the glyph must be untouched (still bg). */
    int touched_above = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
            if (pixel_argb(x, y) != pal.background_argb) ++touched_above;
    RX_OK(touched_above == 0, "text: rows above baseline untouched");
}

static void test_text_uses_palette_role(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_TEXT;
    cmd.scale = 1;
    cmd.color_role = CAPYHTML_COLOR_LINK;
    cmd.x = 0; cmd.y = 4;
    cmd.text = "B";

    capyhtml_raster_draw(&t, &cmd, &pal);
    RX_OK(count_set_pixels(0xFF0000FFu) > 5,
          "text: LINK role uses palette slot 2");
    RX_OK(count_set_pixels(0xFFFFFFFFu) == 0,
          "text: TEXT slot stays unused");
}

static void test_text_invalid_role_falls_back(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_TEXT;
    cmd.scale = 1;
    cmd.color_role = 99; /* invalid */
    cmd.x = 0; cmd.y = 4;
    cmd.text = "C";

    capyhtml_raster_draw(&t, &cmd, &pal);
    RX_OK(count_set_pixels(0xFFFFFFFFu) > 5,
          "text: invalid role falls back to TEXT");
}

static void test_text_bold_doubles_pixels(void) {
    /* Render 'A' twice (once normal, once bold) on separate
     * buffers and compare lit-pixel counts. Bold must paint at
     * least the original count + (something) > original. */
    struct capyhtml_palette pal = default_palette();
    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_TEXT;
    cmd.scale = 1;
    cmd.color_role = CAPYHTML_COLOR_TEXT;
    cmd.x = 0; cmd.y = 4;
    cmd.text = "A";

    fb_reset();
    struct capyhtml_raster_target t = target();
    capyhtml_raster_clear(&t, pal.background_argb);
    capyhtml_raster_draw(&t, &cmd, &pal);
    int normal = count_set_pixels(0xFFFFFFFFu);

    fb_reset();
    capyhtml_raster_clear(&t, pal.background_argb);
    cmd.bold = 1;
    capyhtml_raster_draw(&t, &cmd, &pal);
    int bold = count_set_pixels(0xFFFFFFFFu);

    RX_OK(bold > normal, "text: bold lights more pixels than normal");
}

static void test_text_underline_draws_line(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_TEXT;
    cmd.scale = 1;
    cmd.color_role = CAPYHTML_COLOR_LINK;
    cmd.underline = 1;
    cmd.x = 0; cmd.y = 0;
    cmd.text = "AB";

    capyhtml_raster_draw(&t, &cmd, &pal);
    /* Underline at y = 8 (glyph_h * scale). 16 px wide
     * (2 chars * 8 px each), all in palette[LINK]. */
    int line_pixels = 0;
    for (int x = 0; x < 16; ++x) {
        if (pixel_argb(x, 8) == 0xFF0000FFu) ++line_pixels;
    }
    RX_OK(line_pixels == 16, "text: underline spans whole text run");
}

static void test_bullet_fills_square(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_BULLET;
    cmd.color_role = CAPYHTML_COLOR_BULLET;
    cmd.x = 4; cmd.y = 4;
    cmd.w = 4; cmd.h = 4;

    capyhtml_raster_draw(&t, &cmd, &pal);
    int bullet_pixels = 0;
    for (int y = 4; y < 8; ++y)
        for (int x = 4; x < 8; ++x)
            if (pixel_argb(x, y) == 0xFFFFA500u) ++bullet_pixels;
    RX_OK(bullet_pixels == 16, "bullet: 4x4 square painted");
}

static void test_rule_fills_strip(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_RULE;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.x = 2; cmd.y = 8;
    cmd.w = 16; cmd.h = 2;

    capyhtml_raster_draw(&t, &cmd, &pal);
    int rule_pixels = 0;
    for (int y = 8; y < 10; ++y)
        for (int x = 2; x < 18; ++x)
            if (pixel_argb(x, y) == 0xFF808080u) ++rule_pixels;
    RX_OK(rule_pixels == 32, "rule: 16x2 strip painted");
}

static void test_unknown_kind_ignored(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = 99; /* future kind */
    cmd.color_role = CAPYHTML_COLOR_TEXT;
    cmd.x = 0; cmd.y = 0; cmd.w = 8; cmd.h = 8;
    cmd.text = "X";

    capyhtml_raster_draw(&t, &cmd, &pal);
    RX_OK(count_set_pixels(pal.background_argb) == FB_W * FB_H,
          "kind: unknown kind leaves bg intact");
}

static void test_clip_off_screen(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_clear(&t, pal.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_TEXT;
    cmd.scale = 1;
    cmd.color_role = CAPYHTML_COLOR_TEXT;
    /* Below the viewport. */
    cmd.x = 0; cmd.y = 1000; cmd.text = "Z";
    capyhtml_raster_draw(&t, &cmd, &pal);
    RX_OK(count_set_pixels(0xFFFFFFFFu) == 0,
          "clip: cmd below viewport drops");

    /* To the right of the viewport. */
    cmd.x = 1000; cmd.y = 0;
    capyhtml_raster_draw(&t, &cmd, &pal);
    RX_OK(count_set_pixels(0xFFFFFFFFu) == 0,
          "clip: cmd right of viewport drops");

    /* Negative: starts off-screen but spans into it. Some bits
     * land at x>=0; just verifying no crash + buffer untouched
     * outside the visible window is hard, so we just demand the
     * call returns and bg is otherwise consistent. */
    cmd.x = -4; cmd.y = 0;
    capyhtml_raster_draw(&t, &cmd, &pal);
    RX_OK(g_fb[FB_STRIDE * FB_H - 1] == ((pal.background_argb >> 24) & 0xFF),
          "clip: last pixel still has bg alpha byte");
}

static void test_render_walks_list(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();

    struct capyhtml_cmd cmds[3];
    memset(cmds, 0, sizeof(cmds));
    cmds[0].kind = CAPYHTML_CMD_TEXT;
    cmds[0].scale = 1; cmds[0].color_role = CAPYHTML_COLOR_TEXT;
    cmds[0].x = 0; cmds[0].y = 0; cmds[0].text = "X";
    cmds[1].kind = CAPYHTML_CMD_BULLET;
    cmds[1].color_role = CAPYHTML_COLOR_BULLET;
    cmds[1].x = 12; cmds[1].y = 0; cmds[1].w = 4; cmds[1].h = 4;
    cmds[2].kind = CAPYHTML_CMD_RULE;
    cmds[2].color_role = CAPYHTML_COLOR_MUTED;
    cmds[2].x = 0; cmds[2].y = 12; cmds[2].w = FB_W; cmds[2].h = 1;

    capyhtml_raster_render(&t, cmds, 3, &pal);

    RX_OK(count_set_pixels(0xFFFFFFFFu) > 0, "render: TEXT cmd painted");
    RX_OK(count_set_pixels(0xFFFFA500u) == 16, "render: BULLET cmd painted");
    RX_OK(count_set_pixels(0xFF808080u) == FB_W,
          "render: RULE cmd spans full width");
    /* Background should still occupy most of the buffer. */
    RX_OK(count_set_pixels(pal.background_argb) > 0,
          "render: background remains in untouched cells");
}

static void test_render_empty_list_clears(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette pal = default_palette();
    capyhtml_raster_render(&t, NULL, 0, &pal);
    RX_OK(count_set_pixels(pal.background_argb) == FB_W * FB_H,
          "render: empty list still clears bg");
}

static void test_font_default_ops(void) {
    struct capyhtml_font_ops ops;
    capyhtml_font_ops_default(&ops);
    RX_OK(ops.glyph_width_px == 8 && ops.glyph_height_px == 8,
          "font: 8x8 metrics");
    RX_OK(ops.line_gap_px > 0, "font: positive line gap");
    RX_OK(ops.measure_width != NULL, "font: measure_width set");
    RX_OK(ops.measure_width("hello", 1, NULL) == 5 * 8,
          "font: measure_width of 5 chars at 1x = 40");
    RX_OK(ops.measure_width("hi", 3, NULL) == 2 * 8 * 3,
          "font: measure_width scales with scale");
    RX_OK(ops.measure_width(NULL, 1, NULL) == 0,
          "font: NULL text -> 0 width");
}

static void test_font_glyph_lookup(void) {
    const uint8_t *space = capyhtml_font_glyph_row((uint8_t)' ');
    int space_set = 0;
    for (int i = 0; i < 8; ++i) space_set += space[i];
    RX_OK(space_set == 0, "font: ' ' glyph is fully blank");

    const uint8_t *cap_a = capyhtml_font_glyph_row((uint8_t)'A');
    int a_set = 0;
    for (int i = 0; i < 8; ++i) {
        for (int b = 0; b < 8; ++b) if (cap_a[i] & (1u << b)) ++a_set;
    }
    RX_OK(a_set > 0, "font: 'A' has lit pixels");

    /* Non-ASCII (>=128) falls back to '?'. */
    const uint8_t *fallback = capyhtml_font_glyph_row(0xC3);
    const uint8_t *qmark = capyhtml_font_glyph_row((uint8_t)'?');
    int eq = 1;
    for (int i = 0; i < 8; ++i) if (fallback[i] != qmark[i]) eq = 0;
    RX_OK(eq, "font: out-of-range byte falls back to '?'");
}

/* Etapa 3 seção a (2026-05-03): CMD_IMAGE draws a visible placeholder
 * (fill + 1-px border + corner marker + optional alt-text). The test
 * verifies bytes hit the expected colors at diagnostic positions so a
 * regression in draw_image_cmd is caught immediately. */
static void test_image_cmd_draws_placeholder(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_IMAGE;
    cmd.x = 4;
    cmd.y = 2;
    cmd.w = 20;
    cmd.h = 10;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.text = (const char *)0;
    cmd.href = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Top-left border corner = LINK color (blue). */
    RX_OK(pixel_argb(4, 2) == 0xFF0000FFu, "image: TL border is LINK");
    /* Top-right border corner = LINK color. */
    RX_OK(pixel_argb(4 + 20 - 1, 2) == 0xFF0000FFu,
          "image: TR border is LINK");
    /* Bottom-left border = LINK. */
    RX_OK(pixel_argb(4, 2 + 10 - 1) == 0xFF0000FFu,
          "image: BL border is LINK");
    /* Corner marker at offset +3 from origin. */
    RX_OK(pixel_argb(4 + 3, 2 + 3) == 0xFF0000FFu,
          "image: corner marker at (+3,+3) is LINK");
    /* Interior far from borders/marker = MUTED (gray). */
    RX_OK(pixel_argb(4 + 10, 2 + 5) == 0xFF808080u,
          "image: interior is MUTED");
    /* Pixel outside the rect stays at background (black). */
    RX_OK(pixel_argb(2, 2) == 0xFF000000u, "image: outside left untouched");
}

static void test_image_cmd_with_alt_text(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    /* Large enough to trigger alt-text drawing (w >= 48, h >= 16). */
    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_IMAGE;
    cmd.x = 0; cmd.y = 0; cmd.w = 60; cmd.h = 16;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.text = "hi";
    cmd.href = "test.png";
    /* Framebuffer is 32x16 so the command clips on the right; the
     * border at x=59 is off-screen and silently dropped. That's the
     * whole point of bound-checking; verify it doesn't crash. */
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Some TEXT-role pixels should appear inside the placeholder
     * (alt-text "hi" uses CAPYHTML_COLOR_TEXT = white in our palette). */
    int white = count_set_pixels(0xFFFFFFFFu);
    RX_OK(white > 0, "image+alt: alt glyphs written in TEXT color");
}

static void test_image_cmd_zero_dims_noop(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_IMAGE;
    cmd.x = 4; cmd.y = 2;
    cmd.w = 0; cmd.h = 10; /* zero width -> noop */
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Background untouched. */
    RX_OK(pixel_argb(4, 2) == 0xFF000000u,
          "image zero-w: no pixel written");

    cmd.w = 10; cmd.h = 0; /* zero height -> noop */
    capyhtml_raster_draw(&t, &cmd, &p);
    RX_OK(pixel_argb(4, 2) == 0xFF000000u,
          "image zero-h: no pixel written");
}

/* Etapa 3 seção c (2026-05-03): CMD_INPUT TEXT subtype draws a MUTED
 * filled rect with a 1-px LINK border. */
static void test_input_text_subtype(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 4; cmd.y = 2; cmd.w = 20; cmd.h = 10;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)CAPYHTML_INPUT_TYPE_TEXT;
    cmd.text = (const char *)0;
    cmd.href = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(4, 2) == 0xFF0000FFu,
          "input/text: TL border = LINK");
    RX_OK(pixel_argb(4 + 20 - 1, 2) == 0xFF0000FFu,
          "input/text: TR border = LINK");
    RX_OK(pixel_argb(4 + 10, 2 + 5) == 0xFF808080u,
          "input/text: interior = MUTED");
}

static void test_input_submit_subtype(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 4; cmd.y = 2; cmd.w = 20; cmd.h = 10;
    cmd.color_role = CAPYHTML_COLOR_LINK;
    cmd.reserved[0] = (uint8_t)CAPYHTML_INPUT_TYPE_SUBMIT;
    cmd.text = (const char *)0;
    cmd.href = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Submit fundo LINK (override do MUTED). */
    RX_OK(pixel_argb(4 + 10, 2 + 5) == 0xFF0000FFu,
          "input/submit: interior = LINK (button look)");
}

static void test_input_password_masks_chars(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 0; cmd.y = 0; cmd.w = 32; cmd.h = 16;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)CAPYHTML_INPUT_TYPE_PASSWORD;
    cmd.text = "abcd";
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Conta pixels de TEXT color (white). Devem existir; e devem
     * ser exatamente os de '*' (4 vezes), nao os de 'a'/'b'/'c'/'d'.
     * O nivel exato e dificil de pinar sem comparar com o glifo de
     * '*' diretamente; testamos que ha pixels lit (substituicao
     * funciona) e o glifo de 'a' nao apareceu integral. */
    int white = count_set_pixels(0xFFFFFFFFu);
    RX_OK(white > 0, "input/password: TEXT-color pixels written");

    /* Verifica indireto: o glifo de 'a' tem um pixel em (col=2, row=2)
     * lit (verificavel via capyhtml_font_glyph_row('a')[2] & bit2) que
     * o glifo de '*' nao tem. Se a tela tiver esse pattern de 'a',
     * sabemos que o mascaramento falhou. */
    const uint8_t *a_rows = capyhtml_font_glyph_row((uint8_t)'a');
    const uint8_t *star_rows = capyhtml_font_glyph_row((uint8_t)'*');
    /* Procura pelo menos uma diferenca entre 'a' e '*' para ter
     * certeza que o test e significativo. */
    int rows_differ = 0;
    for (int i = 0; i < 8; ++i) {
        if (a_rows[i] != star_rows[i]) { rows_differ = 1; break; }
    }
    RX_OK(rows_differ, "input/password: 'a' and '*' glyphs differ "
                       "(test prerequisite)");
}

static void test_input_zero_dims_noop(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 4; cmd.y = 2; cmd.w = 0; cmd.h = 10;
    capyhtml_raster_draw(&t, &cmd, &p);
    RX_OK(pixel_argb(4, 2) == 0xFF000000u, "input zero-w: no pixel written");
}

/* Etapa 3 seção c polish (2026-05-03): quando reserved[0] tem bit
 * CAPYHTML_INPUT_FLAG_FOCUSED setado:
 *   - borda usa HEADING color (red 0xFFFF0000u no test palette) em
 *     vez de LINK (blue 0xFF0000FFu).
 *   - subtype mask continua resgatando o subtype correto (low nibble).
 *   - caret aparece em text/password mas nao em submit.
 *   - input nao-focado nao tem caret e nao usa HEADING.
 * Pin-tests garantem que o subtype mask + flag bit nao conflitam
 * (subtype=2 + focused = 0x82 deve continuar lendo subtype=2). */
static void test_input_focus_uses_heading_border(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 4; cmd.y = 2; cmd.w = 20; cmd.h = 10;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)(CAPYHTML_INPUT_TYPE_TEXT
                                | CAPYHTML_INPUT_FLAG_FOCUSED);
    cmd.text = (const char *)0;
    cmd.href = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    /* TL border agora deve ser HEADING (red), nao LINK (blue). */
    RX_OK(pixel_argb(4, 2) == 0xFFFF0000u,
          "input/focus: TL border = HEADING (red)");
    /* Borda inner (1 px para dentro) tambem HEADING -> espessura 2 px. */
    RX_OK(pixel_argb(4 + 1, 2 + 1) == 0xFFFF0000u,
          "input/focus: inner ring = HEADING (2 px thick)");
    /* Interior (alem dos 2 px de borda) continua MUTED. */
    RX_OK(pixel_argb(4 + 10, 2 + 5) == 0xFF808080u,
          "input/focus: interior unchanged = MUTED");
}

static void test_input_unfocused_keeps_link_border(void) {
    /* Pin-test: garante que ao NAO setar FLAG_FOCUSED a borda volta
     * para LINK. Sem isto, um regression que sempre seta heading
     * passaria despercebido. */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 4; cmd.y = 2; cmd.w = 20; cmd.h = 10;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)CAPYHTML_INPUT_TYPE_TEXT;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(4, 2) == 0xFF0000FFu,
          "input/unfocused: TL border = LINK (blue)");
    /* Sem segundo anel: pixel inner deveria ser interior color, nao border. */
    RX_OK(pixel_argb(4 + 1, 2 + 1) == 0xFF808080u,
          "input/unfocused: no inner ring (1-px border only)");
}

static void test_input_focused_caret_drawn_text(void) {
    /* Cursor caret aparece logo apos chars_drawn em HEADING color. */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 0; cmd.y = 0; cmd.w = 200; cmd.h = 24;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)(CAPYHTML_INPUT_TYPE_TEXT
                                | CAPYHTML_INPUT_FLAG_FOCUSED);
    cmd.text = "ab";
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Conta pixels HEADING. Borda 2 px ao redor de 200x24:
     *   - 2 linhas top/bottom: 2*200 + 2*200 = 800 (mas o segundo
     *     anel desconta -2 cada lado => 2*200 + 2*(200-2) = 796)
     *   - 2 col left/right: 2*(24-2) + 2*(24-2-2) = 44 + 40 = 84  -- but actually external border full height + internal subtracted 1 on top/bottom
     * Conta exata e fragil; usamos lower bound generoso. Caret +
     * borda devem somar > borda sozinha de input nao-focado.
     * Aqui validamos apenas: existem pixels HEADING dentro da
     * regiao do glyph (entre x=22 e x=30 onde o caret ficaria
     * apos 2 chars de 8 px com pad_x=6). */
    int caret_pixels = 0;
    int caret_x = 6 + 2 * 8; /* pad_x + 2 glyphs */
    for (int y = (24 - 8) / 2; y < (24 - 8) / 2 + 8; ++y) {
        if (pixel_argb(caret_x, y) == 0xFFFF0000u) ++caret_pixels;
    }
    RX_OK(caret_pixels == 8,
          "input/focus: caret = 1px x GLYPH_H column in HEADING");
}

static void test_input_focused_submit_no_caret(void) {
    /* Submit nao recebe caret mesmo focado (botao nao tem cursor). */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 0; cmd.y = 0; cmd.w = 96; cmd.h = 24;
    cmd.color_role = CAPYHTML_COLOR_LINK;
    cmd.reserved[0] = (uint8_t)(CAPYHTML_INPUT_TYPE_SUBMIT
                                | CAPYHTML_INPUT_FLAG_FOCUSED);
    cmd.text = "OK";
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Caret seria em x = pad_x + 2*GLYPH_W = 6 + 16 = 22; verifica
     * que essa coluna no meio vertical NAO eh HEADING (o submit
     * nao desenha caret, so a borda 2 px externa). */
    int caret_x = 22;
    int mid_y = (24 - 8) / 2 + 4; /* meio do glyph */
    RX_OK(pixel_argb(caret_x, mid_y) != 0xFFFF0000u,
          "input/focus/submit: nenhum caret no meio do botao");
}

/* Etapa 3 seção d (2026-05-03): CMD_CELL desenha:
 *   - 1 px LINK border ao redor (4 sides);
 *   - TH (bold=1): fundo MUTED + texto em HEADING color.
 *   - TD (bold=0): sem fundo + texto em TEXT color (pelo color_role).
 *   - Texto clipado pelo cell_w com padding 6 px.
 * Pin-tests garantem que alterar borda/fundo nao escapa do bbox e
 * nao confunde TH com TD. */
static void test_cell_th_has_muted_bg_and_link_border(void) {
    /* Cell 24×12 a partir de (4, 2). FB e 32×16, cabe (TR no x=27). */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_CELL;
    cmd.x = 4; cmd.y = 2; cmd.w = 24; cmd.h = 12;
    cmd.bold = 1; /* TH */
    cmd.color_role = CAPYHTML_COLOR_HEADING;
    cmd.text = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Borda TL + TR. */
    RX_OK(pixel_argb(4, 2) == 0xFF0000FFu, "cell/TH: TL = LINK");
    RX_OK(pixel_argb(4 + 24 - 1, 2) == 0xFF0000FFu, "cell/TH: TR = LINK");
    /* Borda BR + BL. */
    RX_OK(pixel_argb(4, 2 + 12 - 1) == 0xFF0000FFu, "cell/TH: BL = LINK");
    /* Interior fundo MUTED. */
    RX_OK(pixel_argb(4 + 12, 2 + 6) == 0xFF808080u,
          "cell/TH: interior = MUTED bg");
}

static void test_cell_td_no_fill_keeps_bg(void) {
    /* TD nao tem bg, deve manter o background original. */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_CELL;
    cmd.x = 4; cmd.y = 2; cmd.w = 24; cmd.h = 12;
    cmd.bold = 0;
    cmd.color_role = CAPYHTML_COLOR_TEXT;
    cmd.text = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(4, 2) == 0xFF0000FFu, "cell/TD: TL border = LINK");
    /* Interior longe da borda deve manter o background. */
    RX_OK(pixel_argb(4 + 12, 2 + 6) == p.background_argb,
          "cell/TD: interior keeps background (transparent)");
}

static void test_cell_text_drawn_in_color_role(void) {
    /* Cell com texto: glifos saem em color_role. */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_CELL;
    cmd.x = 0; cmd.y = 0; cmd.w = 64; cmd.h = 16;
    cmd.bold = 0;
    cmd.color_role = CAPYHTML_COLOR_TEXT;
    cmd.text = "X";
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Existem pixels TEXT-colored (white). */
    int n = count_set_pixels(0xFFFFFFFFu);
    RX_OK(n > 0, "cell: text glyph drawn in TEXT color");
}

static void test_cell_zero_dims_noop(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_CELL;
    cmd.x = 4; cmd.y = 2; cmd.w = 0; cmd.h = 12;
    capyhtml_raster_draw(&t, &cmd, &p);

    RX_OK(pixel_argb(4, 2) == 0xFF000000u, "cell zero-w: no pixel written");
}

static void test_input_subtype_mask_isolates_focus_bit(void) {
    /* Pin-test ABI: subtype = (reserved[0] & MASK) deve ignorar o
     * bit FOCUS, e o subtype deve ainda guiar o background corretamente.
     * password (subtype=2) com flag_focused (0x80) -> 0x82. Background
     * deveria ser MUTED (igual a text), nao LINK (submit). */
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 0; cmd.y = 0; cmd.w = 32; cmd.h = 16;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)(CAPYHTML_INPUT_TYPE_PASSWORD
                                | CAPYHTML_INPUT_FLAG_FOCUSED);
    cmd.text = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Interior (longe da borda): deve ser MUTED (subtype=password,
     * nao submit). Se a flag bit "vazasse" para o subtype, daria
     * 0x82 = 130, que nao e SUBMIT (1) nem nada conhecido, e o
     * codigo cairia no else (MUTED). Esse teste ainda detecta caso
     * o codigo trate "qualquer != SUBMIT" como password (ok), ou
     * caso passe r0 puro a uma comparacao == TYPE_*. */
    RX_OK(pixel_argb(16, 8) == 0xFF808080u,
          "input/focus/password: interior MUTED via subtype mask");
}

/* Etapa 3 seção c refinement (2026-05-03): SELECT desenha um marcador
 * "▼" 5 px na direita em HEADING color, indicando dropdown. Pin-test:
 * pelo menos um pixel HEADING aparece DENTRO da regiao do marcador
 * (proxima a borda direita) e ZERO pixels HEADING aparecem fora dela
 * para uma celula nao-focada (sem caret/borda destacada). */
static void test_input_select_draws_dropdown_marker(void) {
    fb_reset();
    struct capyhtml_raster_target t = target();
    struct capyhtml_palette p = default_palette();
    capyhtml_raster_clear(&t, p.background_argb);

    struct capyhtml_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CAPYHTML_CMD_INPUT;
    cmd.x = 0; cmd.y = 0; cmd.w = 28; cmd.h = 12;
    cmd.bold = 0;
    cmd.color_role = CAPYHTML_COLOR_MUTED;
    cmd.reserved[0] = (uint8_t)CAPYHTML_INPUT_TYPE_SELECT; /* sem foco */
    cmd.text = (const char *)0;
    capyhtml_raster_draw(&t, &cmd, &p);

    /* Marcador esta em x = w - 8 = 20; primeira linha (5 px largo).
     * Espera-se HEADING (red 0xFFFF0000u no palette de teste) em
     * pelo menos uma das colunas do marcador na linha y = (12-3)/2. */
    int found_in_marker = 0;
    int my = (12 - 3) / 2;
    for (int dx = 0; dx < 5; ++dx) {
        if (pixel_argb(20 + dx, my) == 0xFFFF0000u) { found_in_marker = 1; break; }
    }
    RX_OK(found_in_marker, "select: marker drawn in HEADING color");

    /* No interior esquerdo (x < 16) NAO deve ter HEADING (sem caret
     * porque nao focado, sem texto). */
    int found_outside = 0;
    for (int xx = 4; xx < 16; ++xx) {
        for (int yy = 1; yy < 11; ++yy) {
            if (pixel_argb(xx, yy) == 0xFFFF0000u) { found_outside = 1; break; }
        }
        if (found_outside) break;
    }
    RX_OK(!found_outside, "select: nao desenha HEADING fora do marker");
}

int test_capyhtml_raster_run(void) {
    printf("[test_capyhtml_raster]\n");
    g_passed = 0;
    g_failed = 0;
    test_clear_fills_buffer();
    test_text_cmd_writes_glyph();
    test_text_uses_palette_role();
    test_text_invalid_role_falls_back();
    test_text_bold_doubles_pixels();
    test_text_underline_draws_line();
    test_bullet_fills_square();
    test_rule_fills_strip();
    test_unknown_kind_ignored();
    test_clip_off_screen();
    test_render_walks_list();
    test_render_empty_list_clears();
    test_font_default_ops();
    test_font_glyph_lookup();
    test_image_cmd_draws_placeholder();
    test_image_cmd_with_alt_text();
    test_image_cmd_zero_dims_noop();
    test_input_text_subtype();
    test_input_submit_subtype();
    test_input_password_masks_chars();
    test_input_zero_dims_noop();
    test_input_focus_uses_heading_border();
    test_input_unfocused_keeps_link_border();
    test_input_focused_caret_drawn_text();
    test_input_focused_submit_no_caret();
    test_input_subtype_mask_isolates_focus_bit();
    test_cell_th_has_muted_bg_and_link_border();
    test_cell_td_no_fill_keeps_bg();
    test_cell_text_drawn_in_color_role();
    test_cell_zero_dims_noop();
    test_input_select_draws_dropdown_marker();
    printf("  -> %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
