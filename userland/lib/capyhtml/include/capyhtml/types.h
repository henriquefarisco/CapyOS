/* libcapyhtml - portable type definitions.
 *
 * F3.3c slice 2 (branch feature/m5-w4) introduces a userland-portable
 * mirror of the kernel-side `struct html_document` / `struct html_node`
 * defined in `include/apps/html_viewer.h`. The mirror is intentionally
 * independent: the userland engine (capybrowser ring 3) never includes
 * any kernel header.
 *
 * The numeric values of the enum constants below DO NOT need to match
 * the kernel side because the two parsers serve different consumers:
 * the kernel-side parser feeds the in-kernel compositor, the userland
 * parser feeds the engine's IPC EVENT_FRAME builder. Slice 6 deletes
 * the kernel-side parser; until then both coexist.
 *
 * Buffer sizes are chosen to be small enough to keep the doc on a
 * ring-3 stack (no kernel kmalloc) while big enough for typical web
 * pages: 256 nodes × ~3 KB each ≈ 768 KB. The capybrowser stack is
 * generously sized in `_start`; a future Slice 3 can move the doc to
 * a heap allocation via capylibc malloc when that lands.
 */
#ifndef CAPYHTML_TYPES_H
#define CAPYHTML_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer caps. Sized so a full document fits comfortably on a ring-3
 * stack:
 *
 *   sizeof(capyhtml_document) =
 *       node_count(4) + title[128] +
 *       MAX_NODES * (enum(4) + text[256] + href[256] + bold+reserved(4))
 *     = 4 + 128 + 64 * 520 = ~33 KB
 *
 * The capybrowser ring-3 default stack accommodates this with
 * headroom. Slice 5 (real fetch on real pages) may revisit if
 * truncation becomes a usability issue. The kernel-side parser uses
 * larger caps but lives in kmalloc territory; userland keeps them
 * tight to avoid a heap allocation in Slice 4. */
#define CAPYHTML_MAX_NODES   64
#define CAPYHTML_TEXT_MAX    256
#define CAPYHTML_URL_MAX     256
#define CAPYHTML_TITLE_MAX   128
/* Etapa 3 seção c (2026-05-03): comprimento maximo do `name` de form
 * fields. 32 bytes cobre a quase totalidade dos sites reais ("q",
 * "username", "email", "search_term") sem inflar muito o struct
 * (32B × 64 nodes = +2 KiB no doc). */
#define CAPYHTML_NAME_MAX     32

enum capyhtml_node_type {
    CAPYHTML_NODE_NONE = 0,
    CAPYHTML_NODE_TEXT,
    CAPYHTML_NODE_TAG_H1,
    CAPYHTML_NODE_TAG_H2,
    CAPYHTML_NODE_TAG_H3,
    CAPYHTML_NODE_TAG_P,
    CAPYHTML_NODE_TAG_A,
    CAPYHTML_NODE_TAG_DIV,
    CAPYHTML_NODE_TAG_SPAN,
    CAPYHTML_NODE_TAG_UL,
    CAPYHTML_NODE_TAG_LI,
    CAPYHTML_NODE_TAG_BR,
    CAPYHTML_NODE_TAG_HR,
    /* Etapa 3 seção a (2026-05-03): <img> com src extraido em `href`.
     * Layout reserva uma bounding box (default 100x80 ou width/height
     * atributos quando presentes). Raster desenha um placeholder cinza
     * com borda. Fetch + decode real fica para uma seção futura
     * quando o chrome expor IPC IMAGE_REQUEST/RESPONSE. */
    CAPYHTML_NODE_TAG_IMG,
    /* Etapa 3 seção c (2026-05-03): <form action="..."> ... </form>.
     * `href` carrega action URL. Filhos INPUT entre o FORM open e o
     * proximo FORM (ou fim do doc) sao considerados parte deste form. */
    CAPYHTML_NODE_TAG_FORM,
    /* Etapa 3 seção c (2026-05-03): <input type="..." name="..." value="..." />.
     * Void tag.
     *   - `text` = current value (mutavel; KEY routing escreve aqui)
     *   - `name` = form field name (usado para construir query string)
     *   - `bold` byte repurposed as `input_subtype` (0=text, 1=submit,
     *     2=password, 3=hidden) -- ver CAPYHTML_INPUT_TYPE_* abaixo.
     * Layout emite CMD_INPUT; raster desenha caixa de texto editavel
     * (text/password) ou um botao (submit). */
    CAPYHTML_NODE_TAG_INPUT,
    /* Etapa 3 seção d (2026-05-03): tabelas. Modelo de dados linear
     * (sem arvore) para encaixar no `capyhtml_document` flat:
     *   - TABLE: marca o inicio da tabela. Layout subsequente conta
     *            os <td> da primeira <tr> para definir col_count e
     *            posiciona celulas em grade.
     *   - TR:    nova linha. O y avanca por uma altura de celula.
     *   - TD:    celula com texto inline em `text[]`. col_index
     *            implicito = numero de TDs ja vistos nesta TR.
     *   - TH:    igual ao TD mas com `bold = 1` (raster em
     *            HEADING color).
     * O fim da tabela e detectado pelo proximo nao-TR/TD/TH (ou EOF). */
    CAPYHTML_NODE_TAG_TABLE,
    CAPYHTML_NODE_TAG_TR,
    CAPYHTML_NODE_TAG_TD,
    CAPYHTML_NODE_TAG_TH,
    /* Etapa 3 seção c refinement (2026-05-03): <option> dentro de
     * <select>. Cada option vira um node sibling do TAG_INPUT
     * (subtype SELECT) que o precede:
     *   - `name` = atributo `value` do option (vai pra query string)
     *   - `text` = texto visivel (label)
     * Layout/render ignoram OPTIONs (eles nao tem cmd proprio); engine
     * pode iterar sobre eles para abrir um dropdown ou ciclar valores
     * via Enter/Space. O parser copia o `text` do PRIMEIRO option para
     * o `text` do TAG_INPUT/SELECT pai como valor default exibido. */
    CAPYHTML_NODE_TAG_OPTION
};

/* Etapa 3 seção c (2026-05-03): subtipos de <input>. Codificados no
 * byte `bold` do node (que nao tem outro uso para INPUT) e
 * propagados para o cmd como `input_subtype` em `reserved[0]`. */
#define CAPYHTML_INPUT_TYPE_TEXT     0u
#define CAPYHTML_INPUT_TYPE_SUBMIT   1u
#define CAPYHTML_INPUT_TYPE_PASSWORD 2u
#define CAPYHTML_INPUT_TYPE_HIDDEN   3u
/* Etapa 3 seção c refinement (2026-05-03): textarea como subtype
 * dedicado. Conteudo do node e capturado entre <textarea>...</textarea>
 * pelo parser; render emite CMD_INPUT com altura maior (3 linhas). */
#define CAPYHTML_INPUT_TYPE_TEXTAREA 4u
/* Etapa 3 seção c refinement (2026-05-03): select como subtype.
 * Render desenha como caixa de texto + indicador "▼" a direita; engine
 * cicla value via Enter/Space lendo TAG_OPTION nodes seguintes. */
#define CAPYHTML_INPUT_TYPE_SELECT   5u

struct capyhtml_node {
    enum capyhtml_node_type type;
    char                    text[CAPYHTML_TEXT_MAX];
    char                    href[CAPYHTML_URL_MAX];
    /* Etapa 3 seção c (2026-05-03): nome do campo para INPUT/FORM
     * (form action -> href; INPUT name -> aqui). Vazio para outros
     * node types. */
    char                    name[CAPYHTML_NAME_MAX];
    uint8_t                 bold;        /* h1/h2/h3/strong; INPUT subtype */
    uint8_t                 reserved[3];
};

struct capyhtml_document {
    struct capyhtml_node nodes[CAPYHTML_MAX_NODES];
    int                  node_count;
    char                 title[CAPYHTML_TITLE_MAX];
};

#ifdef __cplusplus
}
#endif

#endif /* CAPYHTML_TYPES_H */
