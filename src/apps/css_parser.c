#include "apps/css_parser.h"
#include "apps/html_viewer.h"
#include "util/kstring.h"
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static int css_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static char css_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int css_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a || *b) {
        if (css_tolower(*a) != css_tolower(*b)) return 0;
        a++; b++;
    }
    return 1;
}

static int css_startswith_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (css_tolower(*s) != css_tolower(*prefix)) return 0;
        s++; prefix++;
    }
    return 1;
}

static size_t css_skip_space(const char *s, size_t pos, size_t len) {
    while (pos < len && css_is_space(s[pos])) pos++;
    return pos;
}

static size_t css_skip_comment(const char *s, size_t pos, size_t len) {
    if (pos + 1 < len && s[pos] == '/' && s[pos + 1] == '*') {
        pos += 2;
        while (pos + 1 < len) {
            if (s[pos] == '*' && s[pos + 1] == '/') return pos + 2;
            pos++;
        }
        return len;
    }
    return pos;
}

static void css_trim(char *s) {
    size_t len = kstrlen(s);
    size_t start = 0;
    if (!s || !s[0]) return;
    while (start < len && css_is_space(s[start])) start++;
    while (len > start && css_is_space(s[len - 1])) len--;
    if (start > 0 && len > start) kmemmove(s, s + start, len - start);
    s[len > start ? len - start : 0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Color parsing                                                        */
/* ------------------------------------------------------------------ */

struct css_named_color { const char *name; uint32_t rgb; };

static const struct css_named_color g_named_colors[] = {
    {"black",       0x000000}, {"white",       0xFFFFFF},
    {"red",         0xFF0000}, {"green",       0x008000},
    {"blue",        0x0000FF}, {"yellow",      0xFFFF00},
    {"orange",      0xFFA500}, {"purple",      0x800080},
    {"pink",        0xFFC0CB}, {"brown",       0xA52A2A},
    {"gray",        0x808080}, {"grey",        0x808080},
    {"silver",      0xC0C0C0}, {"gold",        0xFFD700},
    {"navy",        0x000080}, {"teal",        0x008080},
    {"cyan",        0x00FFFF}, {"magenta",     0xFF00FF},
    {"maroon",      0x800000}, {"olive",       0x808000},
    {"lime",        0x00FF00}, {"aqua",        0x00FFFF},
    {"fuchsia",     0xFF00FF}, {"coral",       0xFF7F50},
    {"salmon",      0xFA8072}, {"khaki",       0xF0E68C},
    {"indigo",      0x4B0082}, {"violet",      0xEE82EE},
    {"transparent", 0x000000}, /* treat transparent as black for now */
    {NULL, 0}
};

static uint32_t css_parse_hex_digit(char c) {
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return 10u + (uint32_t)(c - 'a');
    if (c >= 'A' && c <= 'F') return 10u + (uint32_t)(c - 'A');
    return 0;
}

uint32_t css_parse_color(const char *value) {
    size_t i = 0;
    if (!value) return 0;
    /* Skip leading spaces */
    while (css_is_space(value[i])) i++;
    /* Named color */
    if (value[i] != '#' && !css_startswith_ci(value + i, "rgb")) {
        for (size_t n = 0; g_named_colors[n].name; n++) {
            if (css_streq_ci(value + i, g_named_colors[n].name))
                return g_named_colors[n].rgb;
        }
        return 0;
    }
    /* Hex color: #RGB or #RRGGBB */
    if (value[i] == '#') {
        i++;
        size_t hex_len = 0;
        while (value[i + hex_len] &&
               ((value[i + hex_len] >= '0' && value[i + hex_len] <= '9') ||
                (value[i + hex_len] >= 'a' && value[i + hex_len] <= 'f') ||
                (value[i + hex_len] >= 'A' && value[i + hex_len] <= 'F')))
            hex_len++;
        if (hex_len == 3) {
            uint32_t r = css_parse_hex_digit(value[i]);
            uint32_t g = css_parse_hex_digit(value[i + 1]);
            uint32_t b = css_parse_hex_digit(value[i + 2]);
            return ((r * 17u) << 16) | ((g * 17u) << 8) | (b * 17u);
        }
        if (hex_len >= 6) {
            uint32_t r = (css_parse_hex_digit(value[i]) << 4) |
                          css_parse_hex_digit(value[i + 1]);
            uint32_t g = (css_parse_hex_digit(value[i + 2]) << 4) |
                          css_parse_hex_digit(value[i + 3]);
            uint32_t b = (css_parse_hex_digit(value[i + 4]) << 4) |
                          css_parse_hex_digit(value[i + 5]);
            return (r << 16) | (g << 8) | b;
        }
        return 0;
    }
    /* rgb(r, g, b) or rgba(r, g, b, a) */
    if (css_startswith_ci(value + i, "rgb")) {
        while (value[i] && value[i] != '(') i++;
        if (!value[i]) return 0;
        i++;
        uint32_t channels[3] = {0, 0, 0};
        for (int ch = 0; ch < 3; ch++) {
            while (css_is_space(value[i]) || value[i] == ',') i++;
            uint32_t v = 0;
            int is_pct = 0;
            while (value[i] >= '0' && value[i] <= '9') {
                v = v * 10u + (uint32_t)(value[i++] - '0');
            }
            if (value[i] == '%') { is_pct = 1; i++; }
            if (is_pct) v = (v * 255u) / 100u;
            if (v > 255) v = 255;
            channels[ch] = v;
        }
        return (channels[0] << 16) | (channels[1] << 8) | channels[2];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Property application                                                 */
/* ------------------------------------------------------------------ */

static void css_apply_prop(const char *name, const char *value,
                           struct html_node *node) {
    if (!name || !value || !node) return;
    if (css_streq_ci(name, "color")) {
        uint32_t c = css_parse_color(value);
        if (c) node->css_color = c;
    } else if (css_streq_ci(name, "background-color")) {
        uint32_t c = css_parse_color(value);
        if (c) node->css_bg_color = c;
    } else if (css_streq_ci(name, "display")) {
        if (css_streq_ci(value, "none")) node->hidden = 1;
    } else if (css_streq_ci(name, "visibility")) {
        if (css_streq_ci(value, "hidden")) node->hidden = 1;
    } else if (css_streq_ci(name, "font-weight")) {
        if (css_streq_ci(value, "bold") ||
            css_streq_ci(value, "bolder") ||
            (value[0] >= '5' && value[0] <= '9' && value[1] == '0' &&
             value[2] == '0')) {
            node->bold = 1;
        }
    } else if (css_streq_ci(name, "text-decoration")) {
        /* underline is already implicit for links; none removes it */
        (void)value; /* reserved for future rendering use */
    } else if (css_streq_ci(name, "opacity")) {
        /* treat opacity:0 as hidden */
        if (value[0] == '0' && (value[1] == '\0' || value[1] == '.'))
            node->hidden = 1;
    } else if (css_streq_ci(name, "font-size")) {
        /* Parse px / em values → store in node->font_size */
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; } /* skip fraction */
        if (px > 0) {
            if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem"))
                px = px * 16; /* base 16px */
            node->font_size = px;
        }
    } else if (css_streq_ci(name, "text-align")) {
        if (css_streq_ci(value, "center")) node->text_align = 1;
        else if (css_streq_ci(value, "right")) node->text_align = 2;
        else node->text_align = 0;
    } else if (css_streq_ci(name, "margin-left") ||
               css_streq_ci(name, "padding-left")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem"))
            px = px * 16;
        if (px > 0 && (int)px > node->indent)
            node->indent = (int)px;
    }
}

/* ------------------------------------------------------------------ */
/* Inline style parsing                                                 */
/* ------------------------------------------------------------------ */

void css_apply_inline(const char *style_attr, struct html_node *node) {
    char name[CSS_PROP_NAME_MAX];
    char value[CSS_PROP_VALUE_MAX];
    size_t pos = 0;
    size_t len = 0;
    if (!style_attr || !node) return;
    len = kstrlen(style_attr);
    while (pos < len) {
        size_t n_start, n_end, v_start, v_end;
        while (pos < len && css_is_space(style_attr[pos])) pos++;
        if (pos >= len) break;
        n_start = pos;
        while (pos < len && style_attr[pos] != ':' &&
               style_attr[pos] != ';') pos++;
        n_end = pos;
        if (pos >= len || style_attr[pos] != ':') {
            while (pos < len && style_attr[pos] != ';') pos++;
            if (pos < len) pos++;
            continue;
        }
        pos++; /* skip ':' */
        while (pos < len && css_is_space(style_attr[pos])) pos++;
        v_start = pos;
        while (pos < len && style_attr[pos] != ';') pos++;
        v_end = pos;
        if (pos < len) pos++; /* skip ';' */
        /* copy name and value */
        size_t nlen = n_end > n_start ? n_end - n_start : 0;
        size_t vlen = v_end > v_start ? v_end - v_start : 0;
        if (nlen == 0 || nlen >= CSS_PROP_NAME_MAX) continue;
        if (vlen >= CSS_PROP_VALUE_MAX) vlen = CSS_PROP_VALUE_MAX - 1;
        kmemcpy(name, style_attr + n_start, nlen);
        name[nlen] = '\0';
        kmemcpy(value, style_attr + v_start, vlen);
        value[vlen] = '\0';
        css_trim(name);
        css_trim(value);
        css_apply_prop(name, value, node);
    }
}

/* ------------------------------------------------------------------ */
/* Stylesheet parser                                                    */
/* ------------------------------------------------------------------ */

int css_parse(const char *css, size_t len, struct css_stylesheet *out) {
    size_t pos = 0;
    if (!css || !out) return -1;
    kmemzero(out, sizeof(*out));
    while (pos < len) {
        struct css_rule *rule = NULL;
        size_t sel_start, sel_end;
        /* Skip whitespace and comments */
        size_t prev = pos;
        pos = css_skip_space(css, pos, len);
        pos = css_skip_comment(css, pos, len);
        if (pos == prev && pos < len) {
            /* Not whitespace or comment but also not moving — guard */
            if (!css_is_space(css[pos]) &&
                !(css[pos] == '/' && pos + 1 < len && css[pos + 1] == '*'))
                pos++;
            continue;
        }
        if (pos >= len) break;
        /* Skip @-rules (media, keyframes, import, etc.) */
        if (css[pos] == '@') {
            /* find next '{' or ';' */
            while (pos < len && css[pos] != '{' && css[pos] != ';') pos++;
            if (pos < len && css[pos] == '{') {
                /* skip block */
                int depth = 1;
                pos++;
                while (pos < len && depth > 0) {
                    if (css[pos] == '{') depth++;
                    else if (css[pos] == '}') depth--;
                    pos++;
                }
            } else if (pos < len) {
                pos++; /* skip ';' */
            }
            continue;
        }
        /* Read selector string until '{' */
        sel_start = pos;
        while (pos < len && css[pos] != '{') pos++;
        if (pos >= len) break;
        sel_end = pos;
        pos++; /* skip '{' */
        if (out->rule_count >= CSS_MAX_RULES) {
            /* Skip the block even if we can't store more rules */
            while (pos < len && css[pos] != '}') pos++;
            if (pos < len) pos++;
            continue;
        }
        rule = &out->rules[out->rule_count++];
        kmemzero(rule, sizeof(*rule));
        /* Copy and trim selector */
        {
            size_t slen = sel_end > sel_start ? sel_end - sel_start : 0;
            if (slen >= CSS_SELECTOR_MAX) slen = CSS_SELECTOR_MAX - 1;
            kmemcpy(rule->selector, css + sel_start, slen);
            rule->selector[slen] = '\0';
            css_trim(rule->selector);
        }
        /* Parse property declarations inside { ... } */
        while (pos < len && css[pos] != '}') {
            size_t n_start, n_end, v_start, v_end;
            pos = css_skip_space(css, pos, len);
            pos = css_skip_comment(css, pos, len);
            if (pos >= len || css[pos] == '}') break;
            n_start = pos;
            while (pos < len && css[pos] != ':' && css[pos] != '}') pos++;
            if (pos >= len || css[pos] == '}') break;
            n_end = pos++;  /* skip ':' */
            pos = css_skip_space(css, pos, len);
            v_start = pos;
            while (pos < len && css[pos] != ';' && css[pos] != '}') pos++;
            v_end = pos;
            if (pos < len && css[pos] == ';') pos++;
            if (rule->prop_count >= CSS_MAX_PROPS) continue;
            {
                struct css_property *p = &rule->props[rule->prop_count];
                size_t nlen = n_end > n_start ? n_end - n_start : 0;
                size_t vlen = v_end > v_start ? v_end - v_start : 0;
                if (nlen == 0 || nlen >= CSS_PROP_NAME_MAX) continue;
                if (vlen >= CSS_PROP_VALUE_MAX) vlen = CSS_PROP_VALUE_MAX - 1;
                kmemcpy(p->name, css + n_start, nlen);
                p->name[nlen] = '\0';
                kmemcpy(p->value, css + v_start, vlen);
                p->value[vlen] = '\0';
                css_trim(p->name);
                css_trim(p->value);
                if (p->name[0]) rule->prop_count++;
            }
        }
        if (pos < len && css[pos] == '}') pos++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Selector matching                                                    */
/* ------------------------------------------------------------------ */

/* Map node type to its tag name string */
static const char *css_node_tag(const struct html_node *node) {
    if (!node) return "";
    switch (node->type) {
        case HTML_NODE_TAG_P:          return "p";
        case HTML_NODE_TAG_H1:         return "h1";
        case HTML_NODE_TAG_H2:         return "h2";
        case HTML_NODE_TAG_H3:         return "h3";
        case HTML_NODE_TAG_H4:         return "h4";
        case HTML_NODE_TAG_H5:         return "h5";
        case HTML_NODE_TAG_H6:         return "h6";
        case HTML_NODE_TAG_A:          return "a";
        case HTML_NODE_TAG_DIV:        return "div";
        case HTML_NODE_TAG_SPAN:       return "span";
        case HTML_NODE_TAG_BR:         return "br";
        case HTML_NODE_TAG_IMG:        return "img";
        case HTML_NODE_TAG_UL:         return "ul";
        case HTML_NODE_TAG_LI:         return "li";
        case HTML_NODE_TAG_INPUT:      return "input";
        case HTML_NODE_TAG_BUTTON:     return "button";
        case HTML_NODE_TAG_PRE:        return "pre";
        case HTML_NODE_TAG_CODE:       return "code";
        case HTML_NODE_TAG_BLOCKQUOTE: return "blockquote";
        case HTML_NODE_TAG_HR:         return "hr";
        case HTML_NODE_TAG_MARK:       return "mark";
        case HTML_NODE_TAG_TD:         return "td";
        case HTML_NODE_TAG_TR:         return "tr";
        case HTML_NODE_TAG_MEDIA:      return "video";
        case HTML_NODE_TAG_FIGCAPTION: return "figcaption";
        case HTML_NODE_TAG_DETAILS:    return "details";
        default:                       return "";
    }
}

/* Check if a class_list string contains a given class token */
static int css_has_class(const char *class_list, const char *cls) {
    size_t clen = kstrlen(cls);
    size_t i = 0;
    size_t llen = kstrlen(class_list);
    if (!class_list || !cls || clen == 0) return 0;
    while (i < llen) {
        while (i < llen && css_is_space(class_list[i])) i++;
        size_t start = i;
        while (i < llen && !css_is_space(class_list[i])) i++;
        size_t tlen = i - start;
        if (tlen == clen && kmemcmp(class_list + start, cls, clen) == 0)
            return 1;
    }
    return 0;
}

/* Match one simple selector (no comma, no descendant) against a node */
static int css_simple_selector_matches(const char *sel,
                                       const struct html_node *node) {
    char tag_part[64];
    char cls_part[64];
    char id_part[64];
    size_t pos = 0;
    size_t len = 0;
    if (!sel || !node) return 0;
    /* Universal */
    if (sel[0] == '*' && sel[1] == '\0') return 1;
    len = kstrlen(sel);
    tag_part[0] = cls_part[0] = id_part[0] = '\0';
    /* Extract optional tag name (before '.' or '#') */
    while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
           pos < sizeof(tag_part) - 1) {
        tag_part[pos] = css_tolower(sel[pos]);
        pos++;
    }
    tag_part[pos] = '\0';
    /* Extract optional class (.classname) */
    if (pos < len && sel[pos] == '.') {
        pos++;
        size_t cs = 0;
        while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
               cs < sizeof(cls_part) - 1) {
            cls_part[cs++] = css_tolower(sel[pos++]);
        }
        cls_part[cs] = '\0';
    }
    /* Extract optional id (#id) */
    if (pos < len && sel[pos] == '#') {
        pos++;
        size_t is = 0;
        while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
               is < sizeof(id_part) - 1) {
            id_part[is++] = sel[pos++];
        }
        id_part[is] = '\0';
    }
    /* Match tag */
    if (tag_part[0] && !css_streq_ci(tag_part, css_node_tag(node))) return 0;
    /* Match class */
    if (cls_part[0] && !css_has_class(node->class_list, cls_part)) return 0;
    /* Match id */
    if (id_part[0] && !css_streq_ci(id_part, node->id)) return 0;
    return 1;
}

/* Match a selector (may contain commas) against a node.
 * For descendant selectors ("div p"), we match only the last token. */
static int css_selector_matches(const char *selector,
                                const struct html_node *node) {
    char part[CSS_SELECTOR_MAX];
    size_t pos = 0;
    size_t len = kstrlen(selector);
    if (!selector || !node) return 0;
    /* Split on ',' and check each alternative */
    while (pos <= len) {
        size_t start = pos;
        while (pos <= len && selector[pos] != ',' && pos <= len) pos++;
        {
            size_t plen = pos - start;
            const char *last_token_start = NULL;
            size_t last_token_len = 0;
            size_t pi = 0;
            if (plen >= CSS_SELECTOR_MAX) plen = CSS_SELECTOR_MAX - 1;
            kmemcpy(part, selector + start, plen);
            part[plen] = '\0';
            css_trim(part);
            /* For descendant selectors, use only the last whitespace-delimited
             * token (simplified: we don't walk ancestor chain). */
            pi = 0;
            while (part[pi]) {
                while (part[pi] && css_is_space(part[pi])) pi++;
                if (!part[pi]) break;
                last_token_start = part + pi;
                while (part[pi] && !css_is_space(part[pi])) pi++;
                last_token_len = (size_t)(part + pi - last_token_start);
            }
            if (last_token_start && last_token_len > 0) {
                char tok[CSS_SELECTOR_MAX];
                if (last_token_len >= CSS_SELECTOR_MAX)
                    last_token_len = CSS_SELECTOR_MAX - 1;
                kmemcpy(tok, last_token_start, last_token_len);
                tok[last_token_len] = '\0';
                if (css_simple_selector_matches(tok, node)) return 1;
            }
        }
        pos++; /* skip ',' */
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Apply stylesheet to document                                         */
/* ------------------------------------------------------------------ */

void css_apply_to_doc(const struct css_stylesheet *css,
                      struct html_document *doc) {
    if (!css || !doc) return;
    for (int ni = 0; ni < doc->node_count; ni++) {
        struct html_node *node = &doc->nodes[ni];
        for (int ri = 0; ri < css->rule_count; ri++) {
            const struct css_rule *rule = &css->rules[ri];
            if (!css_selector_matches(rule->selector, node)) continue;
            for (int pi = 0; pi < rule->prop_count; pi++) {
                css_apply_prop(rule->props[pi].name, rule->props[pi].value,
                               node);
            }
        }
    }
}
