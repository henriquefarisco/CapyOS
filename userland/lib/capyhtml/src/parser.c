/* libcapyhtml - HTML parser implementation (Slice 2 MVP).
 *
 * Pure ring-3 / host-portable implementation. No kernel headers.
 * Modeled after src/apps/html_viewer/html_parser.c but stripped to
 * the representative tag set documented in capyhtml/parser.h.
 *
 * Coverage Slice 2:
 *   - Block tags: <h1> <h2> <h3> <p> <div> <ul> <li>
 *   - Inline tags: <a href="..."> <span> <strong> <b> <em> <i>
 *   - Void tags: <br> <hr>
 *   - Skipped-as-block: <script> <style> <head> contents
 *   - Title extraction from <title> or first <h1>
 *   - Entity decoding: &amp; &lt; &gt; &quot; &apos; &nbsp;
 *
 * Out of scope (Slice 2b):
 *   - CSS (<style> / <link rel=stylesheet>)
 *   - Forms (input/select/textarea/button)
 *   - Tables (table/tr/td)
 *   - Image alt + srcset/picture
 *   - Meta refresh, base href, cookie storage (those live in
 *     navigation_state, not the parser)
 */
#include "capyhtml/parser.h"

#include <stddef.h>
#include <stdint.h>

#define CAPYHTML_PARSE_YIELD_EVERY 1024u

/* Per-parse yield context. Plumbed through file-static state so the
 * inner byte-loops (`collect_text`, `collect_text_until_tag`) can
 * cooperate without changing every helper signature. The parser is
 * not re-entrant, so this is safe. */
static capyhtml_yield_fn g_capyhtml_yield_cb;
static void *g_capyhtml_yield_user;
static unsigned int g_capyhtml_yield_counter;

static inline void capyhtml_yield_tick(void) {
    if (!g_capyhtml_yield_cb) return;
    if ((++g_capyhtml_yield_counter % CAPYHTML_PARSE_YIELD_EVERY) == 0u) {
        g_capyhtml_yield_cb(g_capyhtml_yield_user);
    }
}

/* ---- portable freestanding helpers (no libc) ----------------------- */
/* We deliberately do NOT include <string.h> so this TU compiles under
 * the same -ffreestanding -nostdinc invocation that the future ring-3
 * link will use. capylibc's own string.h does not yet exist; once it
 * does, these can be replaced with the canonical names. */

static void *cap_memset(void *dst, int v, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (unsigned char)v;
    return dst;
}

static void *cap_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        size_t i;
        for (i = 0; i < n; i++) d[i] = s[i];
    } else {
        size_t i;
        for (i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

static int cap_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static size_t cap_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ---- portable string helpers (kernel-equivalent inlines) ----------- */

static int hv_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static char hv_to_lower(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
}

static int hv_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (hv_to_lower(*a) != hv_to_lower(*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void hv_strcpy(char *dst, size_t dst_len, const char *src) {
    size_t i = 0;
    if (!dst || dst_len == 0) return;
    if (src) {
        for (; i + 1 < dst_len && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void hv_trim_text(char *text) {
    size_t len;
    size_t start = 0;
    if (!text) return;
    len = cap_strlen(text);
    while (start < len && hv_is_space(text[start])) start++;
    while (len > start && hv_is_space(text[len - 1])) len--;
    if (start > 0 && len > start) {
        cap_memmove(text, text + start, len - start);
    } else if (start >= len) {
        text[0] = '\0';
        return;
    }
    text[len - start] = '\0';
}

/* ---- entity decoding ---------------------------------------------- */

/* Returns 1 on success and writes a single-byte replacement; *consumed
 * counts the bytes (including the leading '&'). Multi-byte / numeric
 * entities are out of scope for Slice 2 — they fall through and the
 * '&' is emitted verbatim. */
static int decode_entity(const char *html, size_t len, size_t *consumed,
                         char *out_ch) {
    static const struct {
        const char *name;
        char value;
    } table[] = {
        {"amp",   '&'},
        {"lt",    '<'},
        {"gt",    '>'},
        {"quot",  '"'},
        {"apos",  '\''},
        {"nbsp",  ' '},
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        size_t name_len = cap_strlen(table[i].name);
        if (len < name_len + 2) continue;
        if (html[0] != '&') continue;
        if (cap_memcmp(html + 1, table[i].name, name_len) != 0) continue;
        if (html[1 + name_len] != ';') continue;
        *consumed = name_len + 2;
        *out_ch = table[i].value;
        return 1;
    }
    return 0;
}

/* ---- tag scanning -------------------------------------------------- */

static size_t skip_until(const char *html, size_t len, size_t pos, char ch) {
    while (pos < len && html[pos] != ch) pos++;
    return pos < len ? pos + 1 : len;
}

/* Skip <!-- comment -->, <!doctype>, <?xml ?> blocks. Caller passed
 * `pos` pointing at the byte AFTER the leading '<'. */
static size_t skip_special_tag(const char *html, size_t len, size_t pos) {
    if (pos < len && html[pos] == '!' && pos + 2 < len &&
        html[pos + 1] == '-' && html[pos + 2] == '-') {
        pos += 3;
        while (pos + 2 < len) {
            if (html[pos] == '-' && html[pos + 1] == '-' &&
                html[pos + 2] == '>') {
                return pos + 3;
            }
            pos++;
        }
        return len;
    }
    return skip_until(html, len, pos, '>');
}

/* Read tag name into `out` (lowercased). Caller passes `*ppos` pointing
 * at the first byte of the name; on return `*ppos` points one past the
 * last name byte. */
static void read_tag_name(const char *html, size_t len, size_t *ppos,
                          char *out, size_t out_len) {
    size_t pos = *ppos;
    size_t i = 0;
    while (pos < len && i + 1 < out_len) {
        char ch = html[pos];
        if (hv_is_space(ch) || ch == '>' || ch == '/') break;
        out[i++] = hv_to_lower(ch);
        pos++;
    }
    out[i] = '\0';
    *ppos = pos;
}

/* Advance to the byte AFTER '>' that closes the current tag. Returns
 * the new pos and writes the position of '>' into `*tag_end_out`.
 * Sets `*self_closing_out` if a '/' precedes the '>'. */
static size_t scan_tag_end(const char *html, size_t len, size_t pos,
                           size_t *tag_end_out, int *self_closing_out) {
    int self_closing = 0;
    while (pos < len && html[pos] != '>') {
        if (html[pos] == '/' && pos + 1 < len &&
            (html[pos + 1] == '>' || hv_is_space(html[pos + 1]))) {
            self_closing = 1;
        }
        pos++;
    }
    if (tag_end_out) *tag_end_out = pos;
    if (self_closing_out) *self_closing_out = self_closing;
    return pos < len ? pos + 1 : len;
}

/* Skip everything up to the matching </tag>. Used for <script>, <style>
 * and <head> in the Slice 2 MVP. */
static size_t skip_block(const char *html, size_t len, size_t pos,
                         const char *tag) {
    size_t tag_len = cap_strlen(tag);
    while (pos + tag_len + 2 < len) {
        if (html[pos] == '<' && html[pos + 1] == '/') {
            size_t i;
            int match = 1;
            for (i = 0; i < tag_len; i++) {
                if (hv_to_lower(html[pos + 2 + i]) != tag[i]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                size_t end = pos + 2 + tag_len;
                while (end < len && html[end] != '>') end++;
                return end < len ? end + 1 : len;
            }
        }
        pos++;
    }
    return len;
}

/* Extract the value of `attr_name` from the attribute span [start, end).
 * Returns 1 on found and copies into `out`. */
static int extract_attr(const char *attrs, size_t attrs_len,
                        const char *attr_name, char *out, size_t out_len) {
    size_t name_len = cap_strlen(attr_name);
    size_t pos = 0;
    if (out_len > 0) out[0] = '\0';
    while (pos < attrs_len) {
        while (pos < attrs_len && hv_is_space(attrs[pos])) pos++;
        if (pos + name_len > attrs_len) break;
        /* Match name (case-insensitive) followed by '=', space or end. */
        int matches = 1;
        size_t i;
        for (i = 0; i < name_len; i++) {
            if (hv_to_lower(attrs[pos + i]) != hv_to_lower(attr_name[i])) {
                matches = 0;
                break;
            }
        }
        size_t after = pos + name_len;
        if (matches && (after == attrs_len || attrs[after] == '=' ||
                        hv_is_space(attrs[after]))) {
            pos = after;
            while (pos < attrs_len && hv_is_space(attrs[pos])) pos++;
            if (pos < attrs_len && attrs[pos] == '=') {
                pos++;
                while (pos < attrs_len && hv_is_space(attrs[pos])) pos++;
                char quote = 0;
                if (pos < attrs_len && (attrs[pos] == '"' || attrs[pos] == '\'')) {
                    quote = attrs[pos++];
                }
                size_t v = 0;
                while (pos < attrs_len && v + 1 < out_len) {
                    char ch = attrs[pos];
                    if (quote && ch == quote) { pos++; break; }
                    if (!quote && (hv_is_space(ch) || ch == '>')) break;
                    out[v++] = ch;
                    pos++;
                }
                out[v] = '\0';
                return 1;
            }
            return 1;  /* boolean attribute, no value. */
        }
        /* Skip current attribute (including any quoted value). */
        while (pos < attrs_len && !hv_is_space(attrs[pos])) {
            if (attrs[pos] == '=') {
                pos++;
                while (pos < attrs_len && hv_is_space(attrs[pos])) pos++;
                if (pos < attrs_len && (attrs[pos] == '"' || attrs[pos] == '\'')) {
                    char q = attrs[pos++];
                    while (pos < attrs_len && attrs[pos] != q) pos++;
                    if (pos < attrs_len) pos++;
                } else {
                    while (pos < attrs_len && !hv_is_space(attrs[pos])) pos++;
                }
                break;
            }
            pos++;
        }
    }
    return 0;
}

/* ---- node management ---------------------------------------------- */

static struct capyhtml_node *push_node(struct capyhtml_document *doc) {
    if (!doc || doc->node_count >= CAPYHTML_MAX_NODES) return NULL;
    struct capyhtml_node *n = &doc->nodes[doc->node_count++];
    cap_memset(n, 0, sizeof(*n));
    return n;
}

/* Collect text up to the next '<', writing into `out` with whitespace
 * collapsed and entities decoded. Returns the new pos. */
static size_t collect_text(const char *html, size_t len, size_t pos,
                           char *out, size_t out_len) {
    size_t op = 0;
    int last_was_space = 1;  /* leading-space suppression. */
    if (out_len == 0) return pos;
    while (pos < len && html[pos] != '<') {
        capyhtml_yield_tick();
        char ch = html[pos];
        size_t consumed = 0;
        if (ch == '&') {
            char decoded;
            if (decode_entity(html + pos, len - pos, &consumed, &decoded)) {
                if (op + 1 < out_len) {
                    out[op++] = decoded;
                    last_was_space = (decoded == ' ');
                }
                pos += consumed;
                continue;
            }
        }
        if (hv_is_space(ch)) {
            if (!last_was_space && op + 1 < out_len) {
                out[op++] = ' ';
                last_was_space = 1;
            }
            pos++;
            continue;
        }
        if (op + 1 < out_len) {
            out[op++] = ch;
            last_was_space = 0;
        }
        pos++;
    }
    out[op] = '\0';
    return pos;
}

/* Collect text content until a closing </tag>. Inline tags encountered
 * along the way (<a>, <br>, <hr>, <strong>, <b>, <em>, <i>) are pushed
 * as separate child nodes into `doc` so callers can keep semantic
 * structure (anchors with href, line breaks) instead of flattening
 * everything into raw text. The non-inline-tag text accumulates into
 * `out` (whitespace-collapsed, entities decoded).
 *
 * Returns the pos AFTER the matching '>' of the closing tag.
 *
 * `doc` may be NULL, in which case inline tags are silently stripped
 * (used by <title> harvesting). */
static size_t collect_text_until_tag(const char *html, size_t len, size_t pos,
                                     const char *close_tag,
                                     struct capyhtml_document *doc,
                                     char *out, size_t out_len) {
    size_t op = 0;
    int last_was_space = 1;
    size_t tag_len = cap_strlen(close_tag);
    if (out_len > 0) out[0] = '\0';
    while (pos < len) {
        capyhtml_yield_tick();
        if (html[pos] == '<') {
            /* Closing tag matches? */
            if (pos + 1 < len && html[pos + 1] == '/') {
                size_t i;
                int match = 1;
                for (i = 0; i < tag_len; i++) {
                    if (pos + 2 + i >= len ||
                        hv_to_lower(html[pos + 2 + i]) != close_tag[i]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    size_t end = pos + 2 + tag_len;
                    while (end < len && html[end] != '>') end++;
                    out[op] = '\0';
                    hv_trim_text(out);
                    return end < len ? end + 1 : len;
                }
            }

            /* Inline tag detection. Read tag name then decide. */
            size_t after_lt = pos + 1;
            int closing_inner = 0;
            if (after_lt < len && html[after_lt] == '/') {
                closing_inner = 1;
                after_lt++;
            }
            char inner_tag[16];
            size_t name_pos = after_lt;
            read_tag_name(html, len, &name_pos, inner_tag, sizeof(inner_tag));
            size_t inner_attr_start = name_pos;
            size_t inner_tag_end;
            int inner_self_closing;
            size_t after_tag = scan_tag_end(html, len, name_pos,
                                            &inner_tag_end, &inner_self_closing);

            /* Whitelist of inline tags we promote to nodes. */
            if (!closing_inner && doc) {
                if (hv_streq_ci(inner_tag, "br")) {
                    struct capyhtml_node *n = push_node(doc);
                    if (n) n->type = CAPYHTML_NODE_TAG_BR;
                    pos = after_tag;
                    continue;
                }
                if (hv_streq_ci(inner_tag, "hr")) {
                    struct capyhtml_node *n = push_node(doc);
                    if (n) n->type = CAPYHTML_NODE_TAG_HR;
                    pos = after_tag;
                    continue;
                }
                if (hv_streq_ci(inner_tag, "a") && !inner_self_closing) {
                    char href[CAPYHTML_URL_MAX];
                    char inner_text[CAPYHTML_TEXT_MAX];
                    href[0] = '\0';
                    extract_attr(html + inner_attr_start,
                                 inner_tag_end - inner_attr_start,
                                 "href", href, sizeof(href));
                    pos = collect_text_until_tag(html, len, after_tag, "a",
                                                 NULL, inner_text,
                                                 sizeof(inner_text));
                    struct capyhtml_node *n = push_node(doc);
                    if (n) {
                        n->type = CAPYHTML_NODE_TAG_A;
                        hv_strcpy(n->text, sizeof(n->text), inner_text);
                        hv_strcpy(n->href, sizeof(n->href), href);
                    }
                    continue;
                }
                /* Etapa 3 seção a (2026-05-03): <img> inline. Void
                 * tag; hoist como node IMG proprio (mesmo shape do
                 * <a>), sem recursao porque nao ha filhos. O parent
                 * block absorve o restante do texto normal. Sem
                 * esta branch, IMGs dentro de <p>/<li>/<div> eram
                 * silenciosamente descartados. */
                if (hv_streq_ci(inner_tag, "img")) {
                    char src_buf[CAPYHTML_URL_MAX];
                    char alt_buf[CAPYHTML_TEXT_MAX];
                    src_buf[0] = '\0';
                    alt_buf[0] = '\0';
                    extract_attr(html + inner_attr_start,
                                 inner_tag_end - inner_attr_start,
                                 "src", src_buf, sizeof(src_buf));
                    extract_attr(html + inner_attr_start,
                                 inner_tag_end - inner_attr_start,
                                 "alt", alt_buf, sizeof(alt_buf));
                    struct capyhtml_node *n = push_node(doc);
                    if (n) {
                        n->type = CAPYHTML_NODE_TAG_IMG;
                        hv_strcpy(n->href, sizeof(n->href), src_buf);
                        hv_strcpy(n->text, sizeof(n->text), alt_buf);
                    }
                    pos = after_tag;
                    continue;
                }
            }
            /* Other inline tags (<strong>, <em>, ...): drop the tag
             * itself but keep their text content in the running buffer. */
            pos = after_tag;
            continue;
        }
        char ch = html[pos];
        size_t consumed = 0;
        if (ch == '&') {
            char decoded;
            if (decode_entity(html + pos, len - pos, &consumed, &decoded)) {
                if (op + 1 < out_len) {
                    out[op++] = decoded;
                    last_was_space = (decoded == ' ');
                }
                pos += consumed;
                continue;
            }
        }
        if (hv_is_space(ch)) {
            if (!last_was_space && op + 1 < out_len) {
                out[op++] = ' ';
                last_was_space = 1;
            }
            pos++;
            continue;
        }
        if (op + 1 < out_len) {
            out[op++] = ch;
            last_was_space = 0;
        }
        pos++;
    }
    out[op] = '\0';
    hv_trim_text(out);
    return pos;
}

/* ---- main parser --------------------------------------------------- */

int capyhtml_parse(const char *html, size_t len,
                   struct capyhtml_document *out_doc,
                   capyhtml_yield_fn yield_cb,
                   void *yield_user_data) {
    if (!html || !out_doc) return -1;
    cap_memset(out_doc, 0, sizeof(*out_doc));

    g_capyhtml_yield_cb = yield_cb;
    g_capyhtml_yield_user = yield_user_data;
    g_capyhtml_yield_counter = 0;

    size_t pos = 0;

    while (pos < len && out_doc->node_count < CAPYHTML_MAX_NODES) {
        capyhtml_yield_tick();
        if (hv_is_space(html[pos])) {
            pos++;
            continue;
        }
        if (html[pos] == '<') {
            char tag[32];
            size_t attr_start;
            size_t tag_end;
            int closing = 0;
            int self_closing = 0;
            pos++;
            if (pos < len && (html[pos] == '!' || html[pos] == '?')) {
                pos = skip_special_tag(html, len, pos);
                continue;
            }
            if (pos < len && html[pos] == '/') {
                closing = 1;
                pos++;
            }
            read_tag_name(html, len, &pos, tag, sizeof(tag));
            attr_start = pos;
            pos = scan_tag_end(html, len, pos, &tag_end, &self_closing);
            if (!tag[0]) continue;
            if (closing) continue;

            /* <head>: walk through it normally; <title> handler will
             * harvest the title and <script>/<style> handlers strip
             * scripts/css. */
            if (hv_streq_ci(tag, "head") || hv_streq_ci(tag, "html") ||
                hv_streq_ci(tag, "body")) {
                continue;
            }
            if (hv_streq_ci(tag, "script") || hv_streq_ci(tag, "style") ||
                hv_streq_ci(tag, "svg") || hv_streq_ci(tag, "iframe") ||
                hv_streq_ci(tag, "object") || hv_streq_ci(tag, "embed") ||
                hv_streq_ci(tag, "template")) {
                if (!self_closing) pos = skip_block(html, len, pos, tag);
                continue;
            }
            if (hv_streq_ci(tag, "title")) {
                pos = collect_text_until_tag(html, len, pos, "title",
                                             NULL,
                                             out_doc->title,
                                             sizeof(out_doc->title));
                continue;
            }

            /* Void tags. */
            if (hv_streq_ci(tag, "br")) {
                struct capyhtml_node *n = push_node(out_doc);
                if (n) n->type = CAPYHTML_NODE_TAG_BR;
                continue;
            }
            if (hv_streq_ci(tag, "hr")) {
                struct capyhtml_node *n = push_node(out_doc);
                if (n) n->type = CAPYHTML_NODE_TAG_HR;
                continue;
            }
            /* Etapa 3 seção a (2026-05-03): <img>. Void tag; extrai
             * `src` para o campo `href` do node (reusa o slot porque
             * IMG nao tem href separado) e opcionalmente
             * `width`/`height` para alt-text nos primeiros 32 chars
             * de `text`. Sem src, ignora silenciosamente. */
            if (hv_streq_ci(tag, "img")) {
                char src_buf[CAPYHTML_URL_MAX];
                char alt_buf[CAPYHTML_TEXT_MAX];
                src_buf[0] = '\0';
                alt_buf[0] = '\0';
                extract_attr(html + attr_start, tag_end - attr_start,
                             "src", src_buf, sizeof(src_buf));
                extract_attr(html + attr_start, tag_end - attr_start,
                             "alt", alt_buf, sizeof(alt_buf));
                struct capyhtml_node *n = push_node(out_doc);
                if (n) {
                    n->type = CAPYHTML_NODE_TAG_IMG;
                    hv_strcpy(n->href, sizeof(n->href), src_buf);
                    /* Alt vira texto do node; o renderer pode decidir
                     * escrever o alt dentro do placeholder ou nao. */
                    hv_strcpy(n->text, sizeof(n->text), alt_buf);
                }
                continue;
            }
            /* Etapa 3 seção c (2026-05-03): <form action="..." method="...">.
             * Block tag (tem </form>) mas tratado como marcador inline:
             * empurra um node FORM com `href` = action e segue
             * processando o conteudo normalmente. INPUT children que
             * vierem entre o FORM open e o proximo FORM (ou fim do doc)
             * pertencem a este formulario. O </form> e processado pelo
             * branch de closing tags (que apenas avanca pos). */
            if (hv_streq_ci(tag, "form")) {
                char action_buf[CAPYHTML_URL_MAX];
                action_buf[0] = '\0';
                extract_attr(html + attr_start, tag_end - attr_start,
                             "action", action_buf, sizeof(action_buf));
                struct capyhtml_node *n = push_node(out_doc);
                if (n) {
                    n->type = CAPYHTML_NODE_TAG_FORM;
                    hv_strcpy(n->href, sizeof(n->href), action_buf);
                }
                continue;
            }
            /* Etapa 3 seção c (2026-05-03): <input>. Void tag; extrai
             * type, name, value, placeholder. Subtipo codificado no
             * byte `bold`. Default type = "text". Inputs sem name
             * sao aceitos mas nao contribuem para query string em
             * submit. */
            if (hv_streq_ci(tag, "input")) {
                char type_buf[16];
                char name_buf[CAPYHTML_NAME_MAX];
                char value_buf[CAPYHTML_TEXT_MAX];
                char placeholder_buf[CAPYHTML_TEXT_MAX];
                type_buf[0] = '\0';
                name_buf[0] = '\0';
                value_buf[0] = '\0';
                placeholder_buf[0] = '\0';
                extract_attr(html + attr_start, tag_end - attr_start,
                             "type", type_buf, sizeof(type_buf));
                extract_attr(html + attr_start, tag_end - attr_start,
                             "name", name_buf, sizeof(name_buf));
                extract_attr(html + attr_start, tag_end - attr_start,
                             "value", value_buf, sizeof(value_buf));
                extract_attr(html + attr_start, tag_end - attr_start,
                             "placeholder", placeholder_buf,
                             sizeof(placeholder_buf));
                /* Hidden inputs nao sao renderizaveis; ignora. */
                if (hv_streq_ci(type_buf, "hidden")) continue;

                struct capyhtml_node *n = push_node(out_doc);
                if (n) {
                    n->type = CAPYHTML_NODE_TAG_INPUT;
                    hv_strcpy(n->name, sizeof(n->name), name_buf);
                    /* Value tem prioridade sobre placeholder em "text".
                     * Placeholder so e usado pelo raster quando text
                     * estiver vazio (futuro: campo separado). */
                    if (value_buf[0]) {
                        hv_strcpy(n->text, sizeof(n->text), value_buf);
                    } else {
                        /* Sem value, deixa text vazio. Placeholder
                         * fica no mesmo buffer prefixado com '\1'
                         * sentinela para o raster distinguir? Nao;
                         * por simplicidade, value vazio = string
                         * vazia, e raster sabe que CMD_INPUT vazio
                         * deve mostrar dica visual. */
                        n->text[0] = '\0';
                    }
                    /* Subtipo. */
                    if (hv_streq_ci(type_buf, "submit")) {
                        n->bold = (uint8_t)CAPYHTML_INPUT_TYPE_SUBMIT;
                        /* Submit sem value mostra "Submit" como label;
                         * usamos o text do node como label. */
                        if (!value_buf[0]) {
                            hv_strcpy(n->text, sizeof(n->text), "Submit");
                        }
                    } else if (hv_streq_ci(type_buf, "password")) {
                        n->bold = (uint8_t)CAPYHTML_INPUT_TYPE_PASSWORD;
                    } else {
                        /* "text", "search", "" -> text. */
                        n->bold = (uint8_t)CAPYHTML_INPUT_TYPE_TEXT;
                    }
                }
                continue;
            }

            /* Etapa 3 seção c refinement (2026-05-03): <textarea>.
             * Diferente de <input>, o conteudo nao vem em "value="
             * mas entre as tags <textarea>...</textarea>. Empurramos
             * como TAG_INPUT com subtype TEXTAREA; o text guarda o
             * conteudo capturado. */
            if (hv_streq_ci(tag, "textarea") && !self_closing) {
                char name_buf[CAPYHTML_NAME_MAX];
                char body_buf[CAPYHTML_TEXT_MAX];
                name_buf[0] = '\0';
                body_buf[0] = '\0';
                extract_attr(html + attr_start, tag_end - attr_start,
                             "name", name_buf, sizeof(name_buf));
                pos = collect_text_until_tag(html, len, pos, "textarea",
                                             NULL, body_buf,
                                             sizeof(body_buf));
                struct capyhtml_node *n = push_node(out_doc);
                if (n) {
                    n->type = CAPYHTML_NODE_TAG_INPUT;
                    n->bold = (uint8_t)CAPYHTML_INPUT_TYPE_TEXTAREA;
                    hv_strcpy(n->name, sizeof(n->name), name_buf);
                    hv_strcpy(n->text, sizeof(n->text), body_buf);
                }
                continue;
            }

            /* Etapa 3 seção c refinement (2026-05-03): <select>.
             * Empurra TAG_INPUT (subtype SELECT) e captura name. O
             * text do select recebe o label do PRIMEIRO option (default
             * selecionado) durante o processamento dos option-children
             * abaixo. Os <option> aparecem como TAG_OPTION sibling
             * nodes -- cada um com `name=value` + `text=label`. */
            if (hv_streq_ci(tag, "select") && !self_closing) {
                char name_buf[CAPYHTML_NAME_MAX];
                name_buf[0] = '\0';
                extract_attr(html + attr_start, tag_end - attr_start,
                             "name", name_buf, sizeof(name_buf));
                struct capyhtml_node *n = push_node(out_doc);
                if (n) {
                    n->type = CAPYHTML_NODE_TAG_INPUT;
                    n->bold = (uint8_t)CAPYHTML_INPUT_TYPE_SELECT;
                    hv_strcpy(n->name, sizeof(n->name), name_buf);
                    /* text comeca vazio; primeiro option vira default. */
                }
                continue;
            }
            /* <option value="V">Label</option>: armazena value em name,
             * label em text. Tambem propaga label para o text do select
             * pai (assumido como o ultimo TAG_INPUT/SELECT empurrado
             * sem text definido). */
            if (hv_streq_ci(tag, "option") && !self_closing) {
                char value_buf[CAPYHTML_NAME_MAX];
                char label_buf[CAPYHTML_TEXT_MAX];
                value_buf[0] = '\0';
                label_buf[0] = '\0';
                extract_attr(html + attr_start, tag_end - attr_start,
                             "value", value_buf, sizeof(value_buf));
                pos = collect_text_until_tag(html, len, pos, "option",
                                             NULL, label_buf,
                                             sizeof(label_buf));
                /* Sem value? usa label como value (compat HTML). */
                if (!value_buf[0]) {
                    hv_strcpy(value_buf, sizeof(value_buf), label_buf);
                }
                struct capyhtml_node *n = push_node(out_doc);
                if (n) {
                    n->type = CAPYHTML_NODE_TAG_OPTION;
                    hv_strcpy(n->name, sizeof(n->name), value_buf);
                    hv_strcpy(n->text, sizeof(n->text), label_buf);
                }
                /* Propaga primeiro option label para o select pai. */
                for (int j = out_doc->node_count - 2; j >= 0; --j) {
                    struct capyhtml_node *p = &out_doc->nodes[j];
                    if (p->type == CAPYHTML_NODE_TAG_INPUT &&
                        p->bold == (uint8_t)CAPYHTML_INPUT_TYPE_SELECT) {
                        if (p->text[0] == '\0') {
                            hv_strcpy(p->text, sizeof(p->text), label_buf);
                        }
                        break;
                    }
                    /* Outro INPUT no meio quebra a busca. */
                    if (p->type == CAPYHTML_NODE_TAG_INPUT) break;
                }
                continue;
            }

            /* Etapa 3 seção d (2026-05-03): <table>, <tr>: marcadores
             * sem texto direto. Layout/render usam o sequenciamento
             * linear (TABLE -> TR -> TD/TH ... -> next non-cell) para
             * formar a grade. Igual ao tratamento de <ul>/<div>: empurra
             * o nodo e avanca; o conteudo das celulas e processado no
             * branch de TD/TH abaixo. */
            if (hv_streq_ci(tag, "table")) {
                struct capyhtml_node *n = push_node(out_doc);
                if (n) n->type = CAPYHTML_NODE_TAG_TABLE;
                continue;
            }
            if (hv_streq_ci(tag, "tr")) {
                struct capyhtml_node *n = push_node(out_doc);
                if (n) n->type = CAPYHTML_NODE_TAG_TR;
                continue;
            }

            /* Block tags whose direct text content is captured. */
            enum capyhtml_node_type nt = CAPYHTML_NODE_NONE;
            int bold = 0;
            /* Etapa 3 seção d refinement (2026-05-03): colspan attr
             * para TD/TH. Default 0 (render trata como 1). Clamp em
             * 1..255 (uint8_t). */
            int colspan_attr = 0;
            if (hv_streq_ci(tag, "h1")) { nt = CAPYHTML_NODE_TAG_H1; bold = 1; }
            else if (hv_streq_ci(tag, "h2")) { nt = CAPYHTML_NODE_TAG_H2; bold = 1; }
            else if (hv_streq_ci(tag, "h3")) { nt = CAPYHTML_NODE_TAG_H3; bold = 1; }
            else if (hv_streq_ci(tag, "p"))  { nt = CAPYHTML_NODE_TAG_P; }
            else if (hv_streq_ci(tag, "li")) { nt = CAPYHTML_NODE_TAG_LI; }
            else if (hv_streq_ci(tag, "div")) { nt = CAPYHTML_NODE_TAG_DIV; }
            else if (hv_streq_ci(tag, "span")) { nt = CAPYHTML_NODE_TAG_SPAN; }
            else if (hv_streq_ci(tag, "a"))   { nt = CAPYHTML_NODE_TAG_A; }
            else if (hv_streq_ci(tag, "ul"))  { nt = CAPYHTML_NODE_TAG_UL; }
            /* Etapa 3 seção d (2026-05-03): <td>/<th> capturam texto
             * inline igual a <p>. <th> seta bold=1 para o raster
             * desenhar em HEADING color. colspan extraido como int
             * e armazenado em reserved[0] do node. */
            else if (hv_streq_ci(tag, "td") || hv_streq_ci(tag, "th")) {
                nt = hv_streq_ci(tag, "th") ? CAPYHTML_NODE_TAG_TH
                                            : CAPYHTML_NODE_TAG_TD;
                if (nt == CAPYHTML_NODE_TAG_TH) bold = 1;
                char span_buf[8];
                span_buf[0] = '\0';
                if (extract_attr(html + attr_start, tag_end - attr_start,
                                 "colspan", span_buf, sizeof(span_buf))) {
                    /* Parse decimal sem libc; clamp 1..255. */
                    int v = 0;
                    for (size_t k = 0; span_buf[k] >= '0' &&
                                       span_buf[k] <= '9'; ++k) {
                        v = v * 10 + (span_buf[k] - '0');
                        if (v > 255) { v = 255; break; }
                    }
                    if (v < 1) v = 1;
                    colspan_attr = v;
                }
            }
            else if (hv_streq_ci(tag, "strong") || hv_streq_ci(tag, "b")) {
                nt = CAPYHTML_NODE_TEXT;
                bold = 1;
            }
            else if (hv_streq_ci(tag, "em") || hv_streq_ci(tag, "i") ||
                     hv_streq_ci(tag, "small") || hv_streq_ci(tag, "u")) {
                nt = CAPYHTML_NODE_TEXT;
            }

            if (nt == CAPYHTML_NODE_TAG_UL) {
                /* Emit a UL marker; <li> children are pushed separately. */
                struct capyhtml_node *n = push_node(out_doc);
                if (n) n->type = CAPYHTML_NODE_TAG_UL;
                continue;
            }
            if (nt == CAPYHTML_NODE_TAG_DIV) {
                /* Container with no direct text capture — ignore the
                 * open tag, emit children naturally. */
                struct capyhtml_node *n = push_node(out_doc);
                if (n) n->type = CAPYHTML_NODE_TAG_DIV;
                continue;
            }

            if (nt != CAPYHTML_NODE_NONE && !self_closing) {
                char text_buf[CAPYHTML_TEXT_MAX];
                char href_buf[CAPYHTML_URL_MAX];
                href_buf[0] = '\0';
                if (nt == CAPYHTML_NODE_TAG_A) {
                    extract_attr(html + attr_start, tag_end - attr_start,
                                 "href", href_buf, sizeof(href_buf));
                }
                /* Capture the leading text segment AND let the helper
                 * push any nested inline tags (<a>, <br>, <hr>) into
                 * the document. The parent node's `.text` holds the
                 * leading run only; subsequent inline tags appear as
                 * sibling nodes. */
                int parent_idx = out_doc->node_count;
                struct capyhtml_node *n = push_node(out_doc);
                if (!n) break;
                n->type = nt;
                n->bold = (uint8_t)bold;
                hv_strcpy(n->href, sizeof(n->href), href_buf);
                /* Etapa 3 seção d refinement (2026-05-03): TD/TH
                 * armazena colspan em reserved[0]. 0 = sem colspan
                 * (render trata como 1). */
                if (colspan_attr > 0) {
                    n->reserved[0] = (uint8_t)colspan_attr;
                }
                pos = collect_text_until_tag(html, len, pos, tag, out_doc,
                                             text_buf, sizeof(text_buf));
                /* Re-fetch pointer: collect_text_until_tag may have
                 * pushed new nodes, invalidating the prior address. */
                hv_strcpy(out_doc->nodes[parent_idx].text,
                          sizeof(out_doc->nodes[parent_idx].text), text_buf);
                continue;
            }

            /* Unknown / unsupported tag: ignore opening, content will
             * be processed in the next iteration. */
            continue;
        }

        /* Top-level text node (outside any block). */
        {
            char buf[CAPYHTML_TEXT_MAX];
            pos = collect_text(html, len, pos, buf, sizeof(buf));
            hv_trim_text(buf);
            if (buf[0]) {
                struct capyhtml_node *n = push_node(out_doc);
                if (!n) break;
                n->type = CAPYHTML_NODE_TEXT;
                hv_strcpy(n->text, sizeof(n->text), buf);
            }
        }
    }

    /* Title fallback: first H1's text. */
    if (!out_doc->title[0]) {
        int i;
        for (i = 0; i < out_doc->node_count; i++) {
            if (out_doc->nodes[i].type == CAPYHTML_NODE_TAG_H1 &&
                out_doc->nodes[i].text[0]) {
                hv_strcpy(out_doc->title, sizeof(out_doc->title),
                          out_doc->nodes[i].text);
                break;
            }
        }
    }
    return 0;
}
