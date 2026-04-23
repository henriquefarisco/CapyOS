#include "internal/css_parser_internal.h"

void css_apply_prop(const char *name, const char *value,
                    struct html_node *node) {
    if (!name || !value || !node) return;
    if (css_streq_ci(name, "color")) {
        uint32_t c = css_parse_color(value);
        if (c) node->css_color = c;
    } else if (css_streq_ci(name, "background-color") ||
               css_streq_ci(name, "background")) {
        uint32_t c = css_parse_color(value);
        if (c) node->css_bg_color = c;
    } else if (css_streq_ci(name, "display")) {
        if (css_streq_ci(value, "none")) node->hidden = 1;
        else if (css_streq_ci(value, "inline")) node->css_display = 1;
        else if (css_streq_ci(value, "inline-block")) node->css_display = 2;
        else if (css_streq_ci(value, "flex") ||
                 css_streq_ci(value, "inline-flex")) node->css_display = 3;
        else if (css_streq_ci(value, "block") ||
                 css_streq_ci(value, "list-item")) node->css_display = 0;
    } else if (css_streq_ci(name, "visibility")) {
        if (css_streq_ci(value, "hidden")) node->hidden = 1;
    } else if (css_streq_ci(name, "font-weight")) {
        if (css_streq_ci(value, "bold") ||
            css_streq_ci(value, "bolder") ||
            (value[0] >= '5' && value[0] <= '9' && value[1] == '0' &&
             value[2] == '0')) {
            node->bold = 1;
        }
    } else if (css_streq_ci(name, "text-decoration") ||
               css_streq_ci(name, "text-decoration-line")) {
        if (css_streq_ci(value, "none")) node->no_underline = 1;
        else if (css_streq_ci(value, "underline")) node->no_underline = 0;
    } else if (css_streq_ci(name, "list-style-type") ||
               css_streq_ci(name, "list-style")) {
        if (css_streq_ci(value, "none")) node->list_style_none = 1;
    } else if (css_streq_ci(name, "opacity")) {
        if (value[0] == '0' && (value[1] == '\0' || value[1] == '.'))
            node->hidden = 1;
    } else if (css_streq_ci(name, "font-size")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
        if (px > 0) {
            if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem"))
                px = px * 16;
            node->font_size = px;
        }
    } else if (css_streq_ci(name, "text-align")) {
        if (css_streq_ci(value, "center")) node->text_align = 1;
        else if (css_streq_ci(value, "right")) node->text_align = 2;
        else node->text_align = 0;
    } else if (css_streq_ci(name, "margin") || css_streq_ci(name, "padding")) {
        uint32_t vals[4] = {0, 0, 0, 0};
        int nvals = 0;
        const char *p = value;
        while (*p && nvals < 4) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            if (css_streq_ci(p, "auto") || *p < '0' || *p > '9') {
                nvals++;
                while (*p && *p != ' ' && *p != '\t') p++;
                continue;
            }
            uint32_t v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; }
            if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
            if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) v = v * 16;
            while (*p && *p != ' ' && *p != '\t') p++;
            vals[nvals++] = v;
        }
        {
            uint32_t left_val = (nvals == 1) ? vals[0] :
                                (nvals == 2) ? vals[1] :
                                (nvals == 3) ? vals[1] : vals[3];
            uint32_t top_val = (nvals >= 1) ? vals[0] : 0;
            uint32_t bot_val = (nvals == 1) ? vals[0] :
                               (nvals == 2) ? vals[0] :
                               (nvals == 3) ? vals[2] :
                               (nvals == 4) ? vals[2] : 0;
            if (left_val > 0 && (int)left_val > node->indent)
                node->indent = (int)left_val;
            if (top_val > 0 && top_val < 128) node->css_margin_top = (uint8_t)top_val;
            if (bot_val > 0 && bot_val < 128) node->css_margin_bottom = (uint8_t)bot_val;
        }
    } else if (css_streq_ci(name, "margin-top") ||
               css_streq_ci(name, "padding-top")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && px < 128) node->css_margin_top = (uint8_t)px;
    } else if (css_streq_ci(name, "margin-bottom") ||
               css_streq_ci(name, "padding-bottom")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && px < 128) node->css_margin_bottom = (uint8_t)px;
    } else if (css_streq_ci(name, "margin-left") ||
               css_streq_ci(name, "padding-left")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && (int)px > node->indent) node->indent = (int)px;
    } else if (css_streq_ci(name, "border") ||
               css_streq_ci(name, "border-top") || css_streq_ci(name, "border-bottom") ||
               css_streq_ci(name, "border-left") || css_streq_ci(name, "border-right") ||
               css_streq_ci(name, "outline")) {
        if (css_streq_ci(value, "none") || css_streq_ci(value, "0")) {
            node->css_border_width = 0;
        } else {
            const char *p = value;
            uint32_t w = 0;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (uint32_t)(*p - '0'); p++; }
            if (w == 0) w = 1;
            if (w > 4) w = 4;
            node->css_border_width = (uint8_t)w;
            while (*p && *p != '#' && !(*p >= 'a' && *p <= 'z') &&
                   !(*p >= 'A' && *p <= 'Z')) p++;
            if (*p) {
                uint32_t bc = css_parse_color(p);
                if (bc) node->css_border_color = bc;
            }
            if (!node->css_border_color) node->css_border_color = 0x888888;
        }
    } else if (css_streq_ci(name, "border-width")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (px > 4) px = 4;
        if (node->css_border_width == 0 && px > 0) node->css_border_width = (uint8_t)px;
    } else if (css_streq_ci(name, "border-color")) {
        uint32_t bc = css_parse_color(value);
        if (bc) node->css_border_color = bc;
    } else if (css_streq_ci(name, "max-width")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && px < 65535) node->css_max_width = (uint16_t)px;
    } else if (css_streq_ci(name, "text-transform")) {
        if (css_streq_ci(value, "uppercase")) node->css_text_transform = 1;
        else if (css_streq_ci(value, "lowercase")) node->css_text_transform = 2;
        else if (css_streq_ci(value, "capitalize")) node->css_text_transform = 3;
        else node->css_text_transform = 0;
    } else if (css_streq_ci(name, "width") && !css_streq_ci(value, "100%") &&
               !css_streq_ci(value, "auto")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && px < 65535) node->css_width = (uint16_t)px;
    } else if (css_streq_ci(name, "line-height")) {
        if (!css_streq_ci(value, "normal")) {
            uint32_t whole = 0, frac = 0, frac_div = 1;
            const char *p = value;
            while (*p >= '0' && *p <= '9') { whole = whole * 10 + (uint32_t)(*p - '0'); p++; }
            if (*p == '.') {
                p++;
                while (*p >= '0' && *p <= '9') {
                    frac = frac * 10 + (uint32_t)(*p - '0');
                    frac_div *= 10;
                    p++;
                }
            }
            {
                uint32_t px;
                if (css_startswith_ci(p, "px")) {
                    px = whole;
                } else if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) {
                    px = whole * 16 + frac * 16 / frac_div;
                } else {
                    px = whole * 16 + frac * 16 / frac_div;
                }
                if (px > 8 && px < 255) node->css_line_height = (uint8_t)px;
            }
        }
    }
}

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
        while (pos < len && style_attr[pos] != ':' && style_attr[pos] != ';') pos++;
        n_end = pos;
        if (pos >= len || style_attr[pos] != ':') {
            while (pos < len && style_attr[pos] != ';') pos++;
            if (pos < len) pos++;
            continue;
        }
        pos++;
        while (pos < len && css_is_space(style_attr[pos])) pos++;
        v_start = pos;
        while (pos < len && style_attr[pos] != ';') pos++;
        v_end = pos;
        if (pos < len) pos++;
        {
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
            css_strip_important(value);
            css_apply_prop(name, value, node);
        }
    }
}

static const char *css_node_tag(const struct html_node *node) {
    if (!node) return "";
    switch (node->type) {
        case HTML_NODE_TAG_P: return "p";
        case HTML_NODE_TAG_H1: return "h1";
        case HTML_NODE_TAG_H2: return "h2";
        case HTML_NODE_TAG_H3: return "h3";
        case HTML_NODE_TAG_H4: return "h4";
        case HTML_NODE_TAG_H5: return "h5";
        case HTML_NODE_TAG_H6: return "h6";
        case HTML_NODE_TAG_A: return "a";
        case HTML_NODE_TAG_DIV: return "div";
        case HTML_NODE_TAG_SPAN: return "span";
        case HTML_NODE_TAG_BR: return "br";
        case HTML_NODE_TAG_IMG: return "img";
        case HTML_NODE_TAG_UL: return "ul";
        case HTML_NODE_TAG_LI: return "li";
        case HTML_NODE_TAG_INPUT: return "input";
        case HTML_NODE_TAG_BUTTON: return "button";
        case HTML_NODE_TAG_PRE: return "pre";
        case HTML_NODE_TAG_CODE: return "code";
        case HTML_NODE_TAG_BLOCKQUOTE: return "blockquote";
        case HTML_NODE_TAG_HR: return "hr";
        case HTML_NODE_TAG_MARK: return "mark";
        case HTML_NODE_TAG_TD: return "td";
        case HTML_NODE_TAG_TR: return "tr";
        case HTML_NODE_TAG_MEDIA: return "video";
        case HTML_NODE_TAG_FIGCAPTION: return "figcaption";
        case HTML_NODE_TAG_DETAILS: return "details";
        case HTML_NODE_TAG_BODY: return "body";
        case HTML_NODE_TAG_HTML: return "html";
        case HTML_NODE_TAG_HEAD: return "head";
        default: return "";
    }
}

static int css_has_class(const char *class_list, const char *cls) {
    size_t clen = kstrlen(cls);
    size_t i = 0;
    size_t llen = kstrlen(class_list);
    if (!class_list || !cls || clen == 0) return 0;
    while (i < llen) {
        while (i < llen && css_is_space(class_list[i])) i++;
        {
            size_t start = i;
            while (i < llen && !css_is_space(class_list[i])) i++;
            if (i - start == clen && kmemcmp(class_list + start, cls, clen) == 0)
                return 1;
        }
    }
    return 0;
}

static int css_simple_selector_matches(const char *sel,
                                       const struct html_node *node) {
    char tag_part[64];
    char cls_part[64];
    char id_part[64];
    char stripped[CSS_SELECTOR_MAX];
    size_t pos = 0;
    size_t len = 0;
    if (!sel || !node) return 0;
    if (css_streq_ci(sel, ":root")) return node->type == HTML_NODE_TAG_HTML;
    {
        size_t slen = kstrlen(sel);
        size_t si = 0;
        size_t di = 0;
        while (si < slen && di < CSS_SELECTOR_MAX - 1) {
            if (sel[si] == ':') {
                while (si < slen && sel[si] != '.' && sel[si] != '#' &&
                       sel[si] != '[') si++;
            } else if (sel[si] == '[') {
                while (si < slen && sel[si] != ']') si++;
                if (si < slen) si++;
            } else {
                stripped[di++] = sel[si++];
            }
        }
        stripped[di] = '\0';
        sel = stripped;
    }
    if (sel[0] == '*' && sel[1] == '\0') return 1;
    len = kstrlen(sel);
    tag_part[0] = cls_part[0] = id_part[0] = '\0';
    while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
           pos < sizeof(tag_part) - 1) {
        tag_part[pos] = css_tolower(sel[pos]);
        pos++;
    }
    tag_part[pos] = '\0';
    if (pos < len && sel[pos] == '.') {
        size_t cs = 0;
        pos++;
        while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
               cs < sizeof(cls_part) - 1) {
            cls_part[cs++] = css_tolower(sel[pos++]);
        }
        cls_part[cs] = '\0';
    }
    if (pos < len && sel[pos] == '#') {
        size_t is = 0;
        pos++;
        while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
               is < sizeof(id_part) - 1) {
            id_part[is++] = sel[pos++];
        }
        id_part[is] = '\0';
    }
    if (tag_part[0] && !css_streq_ci(tag_part, css_node_tag(node))) return 0;
    if (cls_part[0] && !css_has_class(node->class_list, cls_part)) return 0;
    while (pos < len && sel[pos] == '.') {
        char extra_cls[64];
        size_t es = 0;
        pos++;
        while (pos < len && sel[pos] != '.' && sel[pos] != '#' &&
               es < sizeof(extra_cls) - 1)
            extra_cls[es++] = css_tolower(sel[pos++]);
        extra_cls[es] = '\0';
        if (extra_cls[0] && !css_has_class(node->class_list, extra_cls)) return 0;
    }
    if (id_part[0] && !css_streq_ci(id_part, node->id)) return 0;
    return 1;
}

int css_selector_matches(const char *selector, const struct html_node *node) {
    char part[CSS_SELECTOR_MAX];
    size_t pos = 0;
    size_t len = kstrlen(selector);
    if (!selector || !node) return 0;
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
        pos++;
    }
    return 0;
}

static int css_expand_var(const struct css_stylesheet *css,
                          const char *value, char *out, size_t out_len) {
    const char *p;
    size_t ol = 0;
    if (!css || !value || !out || out_len == 0) return 0;
    p = value;
    while (*p && ol + 1 < out_len) {
        if (p[0] == 'v' && p[1] == 'a' && p[2] == 'r' && p[3] == '(') {
            const char *inner = p + 4;
            while (*inner == ' ') inner++;
            if (inner[0] == '-' && inner[1] == '-') {
                const char *nstart = inner;
                size_t ni2 = 0;
                char varname[CSS_VAR_NAME_MAX];
                while (*inner && *inner != ',' && *inner != ')') inner++;
                ni2 = (size_t)(inner - nstart);
                if (ni2 >= CSS_VAR_NAME_MAX) ni2 = CSS_VAR_NAME_MAX - 1;
                kmemcpy(varname, nstart, ni2);
                varname[ni2] = '\0';
                css_trim(varname);
                {
                    const char *resolved = NULL;
                    const char *fallback = NULL;
                    char fb_buf[CSS_VAR_VALUE_MAX];
                    for (int vi = 0; vi < css->var_count; vi++) {
                        if (css_streq_ci(css->vars[vi].name, varname)) {
                            resolved = css->vars[vi].value;
                            break;
                        }
                    }
                    if (!resolved && *inner == ',') {
                        size_t fb_len;
                        inner++;
                        while (*inner == ' ') inner++;
                        {
                            const char *fb_start = inner;
                            while (*inner && *inner != ')') inner++;
                            fb_len = (size_t)(inner - fb_start);
                            if (fb_len > 0 && fb_len < CSS_VAR_VALUE_MAX) {
                                kmemcpy(fb_buf, fb_start, fb_len);
                                fb_buf[fb_len] = '\0';
                                css_trim(fb_buf);
                                fallback = fb_buf;
                            }
                        }
                    }
                    while (*inner && *inner != ')') inner++;
                    if (*inner == ')') inner++;
                    {
                        const char *use = resolved ? resolved : fallback;
                        if (use) {
                            size_t rlen = kstrlen(use);
                            if (ol + rlen >= out_len) rlen = out_len - ol - 1;
                            kmemcpy(out + ol, use, rlen);
                            ol += rlen;
                        }
                    }
                    p = inner;
                    continue;
                }
            }
        }
        out[ol++] = *p++;
    }
    out[ol] = '\0';
    return ol > 0;
}

void css_apply_to_doc(const struct css_stylesheet *css,
                      struct html_document *doc) {
    if (!css || !doc) return;
    for (int ni = 0; ni < doc->node_count; ni++) {
        struct html_node *node = &doc->nodes[ni];
        for (int ri = 0; ri < css->rule_count; ri++) {
            const struct css_rule *rule = &css->rules[ri];
            if (!css_selector_matches(rule->selector, node)) continue;
            for (int pi = 0; pi < rule->prop_count; pi++) {
                const char *val = rule->props[pi].value;
                char expanded[CSS_PROP_VALUE_MAX];
                if (kstrlen(val) >= 4 &&
                    val[0] == 'v' && val[1] == 'a' && val[2] == 'r') {
                    css_expand_var(css, val, expanded, sizeof(expanded));
                    val = expanded;
                }
                css_apply_prop(rule->props[pi].name, val, node);
            }
        }
    }
}
