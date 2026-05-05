/* libcapyhtml/render.h -- F3.3c slice 3 (branch feature/m5-w4).
 *
 * Layout pass: walks a `struct capyhtml_document` produced by
 * capyhtml_parse() and emits a flat list of draw commands for the
 * caller's renderer. The library is intentionally pixel/font/color
 * agnostic:
 *
 *   - Caller injects a `capyhtml_font_ops` describing how wide
 *     characters are at each scale (1x, 2x, 3x for h3/h2/h1) and the
 *     line-height in pixels.
 *   - Caller picks the actual ARGB color from a `color_role` hint
 *     (text, accent, muted, link). Library never names colors.
 *   - Output is an array of `capyhtml_cmd` structs that hold (x, y,
 *     w, h, kind, scale, bold, underline, color_role, text*, href*).
 *     `text` and `href` are borrowed pointers into the document; the
 *     render list's lifetime must be <= document's lifetime.
 *
 * This is the seam that lets:
 *
 *   - The kernel compositor (Slice 6) call into libcapyhtml the
 *     same way as the userland capybrowser (Slice 4-final).
 *   - Host tests (this slice) validate layout decisions with a
 *     fixed-metric mock font without any pixel rendering.
 *
 * Intentional non-goals for slice 3:
 *
 *   - Word-wrap on long text runs. Each block node renders as a
 *     single line whose width is min(intrinsic, viewport_w). Wrap
 *     comes in slice 3b once we have CSS text-wrap rules to lean on.
 *   - Inline mixing of styles within a block. The MVP parser already
 *     hoists <a>/<strong> children into separate nodes (or into
 *     bold flags on the parent), so each `capyhtml_cmd` has a single
 *     style.
 *   - Tables / forms. Out of scope until slice 5/6.
 */
#ifndef CAPYHTML_RENDER_H
#define CAPYHTML_RENDER_H

#include "capyhtml/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logical color roles. The caller maps these to ARGB picking from
 * its own palette (compositor theme, browser default, etc). */
enum capyhtml_color_role {
    CAPYHTML_COLOR_TEXT       = 0, /* default body text */
    CAPYHTML_COLOR_HEADING    = 1, /* h1/h2/h3 -- caller may emphasize */
    CAPYHTML_COLOR_LINK       = 2, /* <a> */
    CAPYHTML_COLOR_MUTED      = 3, /* secondary / placeholder text */
    CAPYHTML_COLOR_BULLET     = 4  /* <li> marker */
};

/* Kinds of draw commands. The renderer is expected to dispatch on
 * `kind` and consume the relevant fields. Unknown kinds must be
 * ignored (forward-compat for slice 3b additions). */
enum capyhtml_cmd_kind {
    CAPYHTML_CMD_NONE   = 0,
    CAPYHTML_CMD_TEXT   = 1, /* (x,y) + scale + text*; w/h are intrinsic */
    CAPYHTML_CMD_BULLET = 2, /* (x,y) -- circle/square; w==h */
    CAPYHTML_CMD_RULE   = 3, /* horizontal rule rect of (x..x+w, y..y+h) */
    /* Etapa 3 seção a (2026-05-03): placeholder de imagem. O renderer
     * desenha um retangulo cinza com borda; `href` carrega a URL do
     * src (para hit-test e futuro fetch+decode), `text` e NULL. w/h
     * vem do layout (width/height atributos do <img> ou 100x80
     * default). Consumers que ainda nao decodificam imagens podem
     * tratar como "muted rect"; o placeholder ja serve de ancora
     * visual para o usuario saber que ha um `<img>` ali. */
    CAPYHTML_CMD_IMAGE  = 4,
    /* Etapa 3 seção c (2026-05-03): caixa de input de formulario.
     *   - `text` = current value (NULL = vazio; raster mostra placeholder)
     *   - `href` = form action URL (para que o engine saiba onde
     *              submeter sem re-walk do doc); NULL para inputs
     *              fora de um <form>.
     *   - `reserved[0]` = input_subtype | focused_flag bit (ver
     *                      CAPYHTML_INPUT_SUBTYPE_MASK / FLAG_FOCUSED
     *                      abaixo). Layout escreve apenas o subtype;
     *                      o engine seta FLAG_FOCUSED antes de chamar
     *                      o raster para o input com foco atual.
     *   - `reserved[1]` = node_idx_lo (low byte do indice no doc).
     *   - `reserved[2]` = node_idx_hi (high byte; geralmente 0 com
     *                     CAPYHTML_MAX_NODES = 64).
     * Consumers que querem hit-test de focus iteram cmds, casam por
     * (x,y,w,h), e usam `node_idx` para mutar `doc->nodes[idx].text`. */
    CAPYHTML_CMD_INPUT  = 5,
    /* Etapa 3 seção d (2026-05-03): celula de tabela. Render emite
     * uma celula por <td>/<th> com (x, y, w, h) ocupando uma posicao
     * da grade, mais a borda 1 px LINK no perimetro. `text` carrega
     * o conteudo; `bold = 1` em <th> faz o raster pintar em
     * HEADING color (e usar fundo MUTED claro). Single command em
     * vez de 5 (4 rules + 1 text) para nao estourar o cmd cap em
     * tabelas razoaveis (10 cmds ~ tabela 2x4). */
    CAPYHTML_CMD_CELL   = 6
};

/* Etapa 3 seção c polish (2026-05-03): mascaras para reserved[0] em
 * CMD_INPUT. Subtype ocupa o nibble baixo (valores 0..3 hoje); a
 * flag de foco vive no bit alto. Mantem ABI estavel: layout sempre
 * escreve subtype puro, e engines que nao sabem da flag continuam
 * lendo o subtype correto. */
#define CAPYHTML_INPUT_SUBTYPE_MASK 0x0Fu
#define CAPYHTML_INPUT_FLAG_FOCUSED 0x80u

/* Etapa 3 seção c (2026-05-03): macros para empacotar/desempacotar
 * o `node_idx` em reserved[1..2] sem mudar a ABI. Mantem a serializacao
 * pequena (16 bits = ate 65535 nodes; CAPYHTML_MAX_NODES = 64 hoje). */
#define CAPYHTML_CMD_PACK_NODE_IDX(cmd_ptr, idx)                              \
    do {                                                                       \
        (cmd_ptr)->reserved[1] = (uint8_t)((idx) & 0xFFu);                     \
        (cmd_ptr)->reserved[2] = (uint8_t)(((idx) >> 8) & 0xFFu);              \
    } while (0)
#define CAPYHTML_CMD_NODE_IDX(cmd) \
    ((uint16_t)((cmd).reserved[1] | ((uint16_t)(cmd).reserved[2] << 8)))

struct capyhtml_cmd {
    uint8_t  kind;        /* enum capyhtml_cmd_kind */
    uint8_t  scale;       /* 1 = body, 2 = h2/h3, 3 = h1 */
    uint8_t  bold;        /* 0/1 -- caller may double-blit shifted 1px */
    uint8_t  underline;   /* 0/1 -- caller draws line under text */
    uint8_t  color_role;  /* enum capyhtml_color_role */
    /* reserved[0]: input_subtype para CMD_INPUT (text/submit/password).
     * reserved[1..2]: node_idx u16 LE (ver macros acima). Para outros
     * kinds, todos os 3 bytes sao 0. */
    uint8_t  reserved[3];
    int32_t  x;           /* px from window content origin */
    int32_t  y;           /* px from content origin (top of doc = 0) */
    int32_t  w;           /* intrinsic width in px */
    int32_t  h;           /* intrinsic height in px */
    const char *text;     /* borrowed pointer into doc->nodes[i].text */
    const char *href;     /* borrowed; non-NULL only for LINK runs */
    /* Etapa 3 secao a fetch+decode (2026-05-05): para CMD_IMAGE, o
     * caller (engine ring 3) pode populá-los apos `capyhtml_layout`
     * com pixels BGRA32 decodificados de um cache; o raster
     * `capyhtml_raster_draw` blita esses pixels no lugar do
     * placeholder quando todos os tres campos sao validos
     * (image_pixels != NULL && image_w > 0 && image_h > 0).
     *
     * Capyhtml NUNCA escreve nesses campos -- sao preenchidos pelo
     * engine apos layout (ou ficam zero, e raster volta ao placeholder).
     * Para outros kinds (TEXT, BULLET, RULE, INPUT, CELL) os campos
     * sao ignorados pelo raster. */
    const uint8_t *image_pixels; /* BGRA32 row-major, image_w cols */
    uint16_t image_w;
    uint16_t image_h;
};

/* Font metrics + width measurement injected by the caller. The
 * library does NO drawing -- it only consumes these to advance x/y.
 *
 * `measure_width` returns the intrinsic pixel width of `text` at the
 * given scale. For a fixed-pitch font this is just
 * `strlen(text) * glyph_w * scale`; the kernel font and capybrowser's
 * embedded font both fit that contract. Variable-width fonts (slice
 * 3b) plug in here without changing the layout algorithm. */
struct capyhtml_font_ops {
    int (*measure_width)(const char *text, int scale, void *ctx);
    int      glyph_width_px;   /* width of one base glyph (scale=1) */
    int      glyph_height_px;  /* height of one base glyph */
    int      line_gap_px;      /* extra px between successive lines */
    void    *ctx;              /* opaque, passed to measure_width */
};

/* Capacity of the layout output buffer. 64 commands cover the
 * canonical capybrowser demo page (1 h1 + 1 p + 4 li + 1 p with
 * inline link = 7 cmds) with 9x headroom for slice 5 real pages.
 * The output buffer is caller-allocated -- this define is just the
 * recommended cap for sizing on the stack. */
#define CAPYHTML_RENDER_RECOMMENDED_CAPACITY 64

/* Result metadata returned alongside the command buffer. */
struct capyhtml_render_result {
    uint16_t cmd_count;       /* number of valid entries in cmds[] */
    uint8_t  truncated;       /* 1 if cmd_capacity was exceeded */
    uint8_t  reserved;
    int32_t  total_height_px; /* y of the last command + its height */
};

/* Lay out a parsed document into draw commands.
 *
 * Returns 0 on success, -1 on argument error (NULL doc/font_ops/cmds
 * or non-positive cmd_capacity / viewport_w). On success `result`
 * is populated. If the document produces more commands than fit in
 * cmd_capacity, the extras are dropped, `truncated` is set, and
 * cmd_count == cmd_capacity. The function never writes outside the
 * caller's buffer. */
int capyhtml_layout(const struct capyhtml_document   *doc,
                    const struct capyhtml_font_ops   *font_ops,
                    int32_t                            viewport_w,
                    struct capyhtml_cmd              *cmds,
                    uint16_t                           cmd_capacity,
                    struct capyhtml_render_result    *result);

#ifdef __cplusplus
}
#endif

#endif /* CAPYHTML_RENDER_H */
