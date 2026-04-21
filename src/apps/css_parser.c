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

/* Strip trailing "!important" (and whitespace) from a CSS value in-place. */
static void css_strip_important(char *s) {
    size_t len;
    if (!s) return;
    len = kstrlen(s);
    /* Trim trailing whitespace first */
    while (len > 0 && css_is_space(s[len - 1])) len--;
    /* Check for '!important' suffix (case-insensitive) */
    if (len >= 10) {
        const char *tail = s + len - 10;
        if (css_tolower(tail[0]) == '!' &&
            css_tolower(tail[1]) == 'i' && css_tolower(tail[2]) == 'm' &&
            css_tolower(tail[3]) == 'p' && css_tolower(tail[4]) == 'o' &&
            css_tolower(tail[5]) == 'r' && css_tolower(tail[6]) == 't' &&
            css_tolower(tail[7]) == 'a' && css_tolower(tail[8]) == 'n' &&
            css_tolower(tail[9]) == 't') {
            len -= 10;
            /* Trim any whitespace before the '!' */
            while (len > 0 && css_is_space(s[len - 1])) len--;
        }
    }
    s[len] = '\0';
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
    {"crimson",     0xDC143C}, {"darkorange",  0xFF8C00},
    {"darkgreen",   0x006400}, {"darkblue",    0x00008B},
    {"darkred",     0x8B0000}, {"darkgray",    0xA9A9A9},
    {"darkgrey",    0xA9A9A9}, {"lightgray",   0xD3D3D3},
    {"lightgrey",   0xD3D3D3}, {"whitesmoke",  0xF5F5F5},
    {"ghostwhite",  0xF8F8FF}, {"beige",       0xF5F5DC},
    {"ivory",       0xFFFFF0}, {"lavender",    0xE6E6FA},
    {"mintcream",   0xF5FFFA}, {"honeydew",    0xF0FFF0},
    {"azure",       0xF0FFFF}, {"aliceblue",   0xF0F8FF},
    {"snow",        0xFFFAFA}, {"seashell",    0xFFF5EE},
    {"linen",       0xFAF0E6}, {"oldlace",     0xFDF5E6},
    {"floralwhite", 0xFFFAF0}, {"antiquewhite",0xFAEBD7},
    {"bisque",      0xFFE4C4}, {"wheat",       0xF5DEB3},
    {"tan",         0xD2B48C}, {"peru",        0xCD853F},
    {"sienna",      0xA0522D}, {"chocolate",   0xD2691E},
    {"saddlebrown", 0x8B4513}, {"firebrick",   0xB22222},
    {"darkslategray",0x2F4F4F},{"slategray",   0x708090},
    {"steelblue",   0x4682B4}, {"cadetblue",   0x5F9EA0},
    {"powderblue",  0xB0E0E6}, {"lightblue",   0xADD8E6},
    {"skyblue",     0x87CEEB}, {"deepskyblue", 0x00BFFF},
    {"dodgerblue",  0x1E90FF}, {"royalblue",   0x4169E1},
    {"mediumblue",  0x0000CD}, {"midnightblue",0x191970},
    {"chartreuse",  0x7FFF00}, {"springgreen", 0x00FF7F},
    {"mediumspringgreen",0x00FA9A},{"turquoise",0x40E0D0},
    {"mediumturquoise",0x48D1CC},{"paleturquoise",0xAFEEEE},
    {"darkcyan",    0x008B8B}, {"lightcyan",   0xE0FFFF},
    {"mediumaquamarine",0x66CDAA},{"aquamarine",0x7FFFD4},
    {"lightseagreen",0x20B2AA},{"mediumseagreen",0x3CB371},
    {"seagreen",    0x2E8B57}, {"darkseagreen",0x8FBC8F},
    {"yellowgreen", 0x9ACD32}, {"olivedrab",   0x6B8E23},
    {"darkolivegreen",0x556B2F},{"limegreen",  0x32CD32},
    {"forestgreen", 0x228B22}, {"darkkhaki",   0xBDB76B},
    {"goldenrod",   0xDAA520}, {"darkgoldenrod",0xB8860B},
    {"palegoldenrod",0xEEE8AA},{"peachpuff",   0xFFDAB9},
    {"moccasin",    0xFFE4B5}, {"navajowhite", 0xFFDEAD},
    {"mistyrose",   0xFFE4E1}, {"lightsalmon", 0xFFA07A},
    {"darksalmon",  0xE9967A}, {"tomato",      0xFF6347},
    {"orangered",   0xFF4500}, {"hotpink",     0xFF69B4},
    {"deeppink",    0xFF1493}, {"palevioletred",0xDB7093},
    {"mediumvioletred",0xC71585},{"orchid",    0xDA70D6},
    {"plum",        0xDDA0DD}, {"thistle",     0xD8BFD8},
    {"mediumorchid",0xBA55D3}, {"darkorchid",  0x9932CC},
    {"darkviolet",  0x9400D3}, {"blueviolet",  0x8A2BE2},
    {"mediumpurple",0x9370DB}, {"slateblue",   0x6A5ACD},
    {"mediumslateblue",0x7B68EE},{"darkslateblue",0x483D8B},
    {"transparent", 0x000001}, /* treat transparent as near-black, not 0 */
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
    /* Named color (skip for functional notation) */
    if (value[i] != '#' && !css_startswith_ci(value + i, "rgb") &&
        !css_startswith_ci(value + i, "hsl")) {
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
    /* hsl(h, s%, l%) or hsla(...) */
    if (css_startswith_ci(value + i, "hsl")) {
        /* Parse h (0-360), s (0-100%), l (0-100%) */
        while (value[i] && value[i] != '(') i++;
        if (!value[i]) return 0;
        i++;
        /* H */
        while (css_is_space(value[i])) i++;
        uint32_t H = 0;
        while (value[i] >= '0' && value[i] <= '9') { H = H*10+(uint32_t)(value[i++]-'0'); }
        while (value[i] && value[i] != ',' && value[i] != ' ') i++;
        while (css_is_space(value[i]) || value[i] == ',') i++;
        /* S */
        uint32_t S = 0;
        while (value[i] >= '0' && value[i] <= '9') { S = S*10+(uint32_t)(value[i++]-'0'); }
        while (value[i] && value[i] != ',' && value[i] != ' ') i++;
        while (css_is_space(value[i]) || value[i] == ',') i++;
        /* L */
        uint32_t L = 0;
        while (value[i] >= '0' && value[i] <= '9') { L = L*10+(uint32_t)(value[i++]-'0'); }
        /* HSL to RGB conversion (integer math) */
        if (S == 0) {
            uint32_t v = (L * 255u) / 100u;
            return (v << 16) | (v << 8) | v;
        }
        {
            /* C = (1 - |2L - 1|) * S */
            uint32_t L2 = L > 50 ? (L - 50)*2 : (50 - L)*2;
            uint32_t C = ((100u - L2) * S) / 100u; /* C in [0,100] */
            uint32_t X = C * (uint32_t)(60u - (H % 120u > 60u ? H % 120u - 60u : 60u - H % 120u)) / 60u;
            uint32_t m_l = L > C/2 ? L - C/2 : 0;
            uint32_t r1, g1, b1;
            uint32_t h6 = H / 60u;
            if      (h6 == 0) { r1=C; g1=X; b1=0; }
            else if (h6 == 1) { r1=X; g1=C; b1=0; }
            else if (h6 == 2) { r1=0; g1=C; b1=X; }
            else if (h6 == 3) { r1=0; g1=X; b1=C; }
            else if (h6 == 4) { r1=X; g1=0; b1=C; }
            else              { r1=C; g1=0; b1=X; }
            uint32_t R = ((r1 + m_l) * 255u) / 100u;
            uint32_t G = ((g1 + m_l) * 255u) / 100u;
            uint32_t B = ((b1 + m_l) * 255u) / 100u;
            if (R > 255) R = 255;
            if (G > 255) G = 255;
            if (B > 255) B = 255;
            return (R << 16) | (G << 8) | B;
        }
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
    } else if (css_streq_ci(name, "margin") || css_streq_ci(name, "padding")) {
        /* Shorthand: parse up to 4 space-separated values; apply left (index 3 or 1) */
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
        /* CSS shorthand: 1 val = all; 2 vals = top/bottom left/right;
         * 3 vals = top left/right bottom; 4 vals = top right bottom left */
        uint32_t left_val = (nvals == 1) ? vals[0] :
                            (nvals == 2) ? vals[1] :
                            (nvals == 3) ? vals[1] : vals[3];
        uint32_t top_val  = (nvals >= 1) ? vals[0] : 0;
        uint32_t bot_val  = (nvals == 1) ? vals[0] :
                            (nvals == 2) ? vals[0] :
                            (nvals == 3) ? vals[2] :
                            (nvals == 4) ? vals[2] : 0;
        if (left_val > 0 && (int)left_val > node->indent)
            node->indent = (int)left_val;
        /* Both margin and padding shorthand set vertical spacing */
        if (top_val > 0 && top_val < 128) node->css_margin_top = (uint8_t)top_val;
        if (bot_val > 0 && bot_val < 128) node->css_margin_bottom = (uint8_t)bot_val;
    } else if (css_streq_ci(name, "margin-top") ||
               css_streq_ci(name, "padding-top")) {
        uint32_t px = 0; const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && px < 128) node->css_margin_top = (uint8_t)px;
    } else if (css_streq_ci(name, "margin-bottom") ||
               css_streq_ci(name, "padding-bottom")) {
        uint32_t px = 0; const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) px = px * 16;
        if (px > 0 && px < 128) node->css_margin_bottom = (uint8_t)px;
    } else if (css_streq_ci(name, "margin-left") ||
               css_streq_ci(name, "padding-left")) {
        uint32_t px = 0;
        const char *p = value;
        while (*p >= '0' && *p <= '9') { px = px * 10 + (uint32_t)(*p - '0'); p++; }
        if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem"))
            px = px * 16;
        if (px > 0 && (int)px > node->indent)
            node->indent = (int)px;
    } else if (css_streq_ci(name, "border") ||
               css_streq_ci(name, "border-top") || css_streq_ci(name, "border-bottom") ||
               css_streq_ci(name, "border-left") || css_streq_ci(name, "border-right") ||
               css_streq_ci(name, "outline")) {
        /* Parse "Wpx style color" — extract first number as width, last token as color */
        if (css_streq_ci(value, "none") || css_streq_ci(value, "0")) {
            node->css_border_width = 0;
        } else {
            const char *p = value;
            uint32_t w = 0;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (uint32_t)(*p - '0'); p++; }
            if (w == 0) w = 1;
            if (w > 4) w = 4;
            node->css_border_width = (uint8_t)w;
            /* Try to parse color from remaining tokens */
            while (*p && *p != '#' && !(*p >= 'a' && *p <= 'z') &&
                   !(*p >= 'A' && *p <= 'Z')) p++;
            if (*p) {
                uint32_t bc = css_parse_color(p);
                if (bc) node->css_border_color = bc;
            }
            if (!node->css_border_color) node->css_border_color = 0x888888;
        }
    } else if (css_streq_ci(name, "border-width")) {
        uint32_t px = 0; const char *p = value;
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
        /* line-height: N (multiplier), Npx, Nem, or normal */
        if (!css_streq_ci(value, "normal")) {
            uint32_t whole = 0, frac = 0, frac_div = 1;
            const char *p = value;
            while (*p >= '0' && *p <= '9') { whole = whole * 10 + (uint32_t)(*p - '0'); p++; }
            if (*p == '.') {
                p++;
                while (*p >= '0' && *p <= '9') {
                    frac = frac * 10 + (uint32_t)(*p - '0'); frac_div *= 10; p++;
                }
            }
            uint32_t px;
            if (css_startswith_ci(p, "px")) {
                px = whole;  /* direct px value */
            } else if (css_startswith_ci(p, "em") || css_startswith_ci(p, "rem")) {
                px = whole * 16 + frac * 16 / frac_div;
            } else {
                /* Pure number: multiplier × 16px base font */
                px = whole * 16 + frac * 16 / frac_div;
            }
            if (px > 8 && px < 255) node->css_line_height = (uint8_t)px;
        }
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
        css_strip_important(value);
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
        /* Handle @-rules */
        if (css[pos] == '@') {
            size_t at_start = pos;
            int is_media = 0;
            int media_matches = 0;
            pos++; /* skip '@' */
            /* Check if it's @media */
            if (pos + 4 < len &&
                css_tolower(css[pos])   == 'm' &&
                css_tolower(css[pos+1]) == 'e' &&
                css_tolower(css[pos+2]) == 'd' &&
                css_tolower(css[pos+3]) == 'i' &&
                css_tolower(css[pos+4]) == 'a') {
                is_media = 1;
                pos += 5;
                /* Read media condition until '{' */
                size_t cond_start = pos;
                while (pos < len && css[pos] != '{' && css[pos] != ';') pos++;
                if (pos < len && css[pos] == '{') {
                    /* Check condition: match "screen", "all", or conditions with
                     * max-width >= 480 or min-width <= 800 (treat as small screen) */
                    char cond[128];
                    size_t clen = pos - cond_start;
                    if (clen >= 128) clen = 127;
                    kmemcpy(cond, css + cond_start, clen); cond[clen] = '\0';
                    css_trim(cond);
                    /* Accept: screen, all, empty, or max-width queries */
                    if (!cond[0] || css_streq_ci(cond, "screen") ||
                        css_streq_ci(cond, "all") ||
                        css_startswith_ci(cond, "screen") ||
                        /* max-width condition: always accept (we're always small) */
                        kstrlen(cond) > 0) {
                        /* Check for min-width > 1024 — skip those (desktop-only) */
                        const char *mw = cond;
                        int skip_desktop = 0;
                        while (*mw) {
                            if (css_startswith_ci(mw, "min-width")) {
                                /* Parse px value */
                                const char *p2 = mw + 9;
                                while (*p2 && (*p2 < '0' || *p2 > '9') &&
                                       *p2 != ')') p2++;
                                uint32_t px = 0;
                                while (*p2 >= '0' && *p2 <= '9') {
                                    px = px * 10 + (uint32_t)(*p2 - '0'); p2++;
                                }
                                if (px > 1024) { skip_desktop = 1; break; }
                            }
                            mw++;
                        }
                        media_matches = !skip_desktop;
                    }
                }
            } else {
                /* Non-media @-rule: skip to '{...}' or ';' */
                while (pos < len && css[pos] != '{' && css[pos] != ';') pos++;
            }
            (void)at_start;
            if (pos < len && css[pos] == '{') {
                if (is_media && media_matches) {
                    /* Parse inner rules recursively by advancing past '{' */
                    pos++; /* enter block */
                    /* Continue normal parsing inside (loop handles nested depth via selector scan) */
                    continue; /* re-enter loop inside the @media block */
                } else {
                    /* skip entire block */
                    int depth = 1;
                    pos++;
                    while (pos < len && depth > 0) {
                        if (css[pos] == '{') depth++;
                        else if (css[pos] == '}') depth--;
                        pos++;
                    }
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
                css_strip_important(p->value);
                if (p->name[0] == '-' && p->name[1] == '-') {
                    /* CSS custom property: store in var table */
                    if (out->var_count < CSS_MAX_VARS) {
                        struct css_var *cv = &out->vars[out->var_count++];
                        size_t vnl = kstrlen(p->name);
                        size_t vvl = kstrlen(p->value);
                        if (vnl >= CSS_VAR_NAME_MAX) vnl = CSS_VAR_NAME_MAX - 1;
                        if (vvl >= CSS_VAR_VALUE_MAX) vvl = CSS_VAR_VALUE_MAX - 1;
                        kmemcpy(cv->name, p->name, vnl); cv->name[vnl] = '\0';
                        kmemcpy(cv->value, p->value, vvl); cv->value[vvl] = '\0';
                    }
                    /* Don't store in rule props */
                } else if (p->name[0]) {
                    rule->prop_count++;
                }
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
        case HTML_NODE_TAG_BODY:       return "body";
        case HTML_NODE_TAG_HTML:       return "html";
        case HTML_NODE_TAG_HEAD:       return "head";
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
    char stripped[CSS_SELECTOR_MAX];
    size_t pos = 0;
    size_t len = 0;
    if (!sel || !node) return 0;
    /* :root → treat as html selector */
    if (css_streq_ci(sel, ":root")) return node->type == HTML_NODE_TAG_HTML;
    /* Strip pseudo-classes/elements (:hover, ::before, :nth-child(), etc.)
     * so a:hover still applies the base a{} properties */
    {
        size_t slen = kstrlen(sel);
        size_t si = 0, di = 0;
        while (si < slen && di < CSS_SELECTOR_MAX - 1) {
            if (sel[si] == ':') {
                /* skip to end of this token (stop at '.', '#', '[', or end) */
                while (si < slen && sel[si] != '.' && sel[si] != '#' &&
                       sel[si] != '[') si++;
            } else if (sel[si] == '[') {
                while (si < slen && sel[si] != ']') si++;
                if (si < slen) si++; /* skip ']' */
            } else {
                stripped[di++] = sel[si++];
            }
        }
        stripped[di] = '\0';
        sel = stripped;
    }
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
    /* Match first class */
    if (cls_part[0] && !css_has_class(node->class_list, cls_part)) return 0;
    /* Match any additional chained classes (.foo.bar.baz) still remaining in sel */
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

/* Expand var(--name) or var(--name, fallback) using stylesheet variables.
 * Writes resolved value into out (max out_len). Returns 1 if expanded. */
static int css_expand_var(const struct css_stylesheet *css,
                          const char *value, char *out, size_t out_len) {
    const char *p;
    size_t ol = 0;
    if (!css || !value || !out || out_len == 0) return 0;
    p = value;
    while (*p && ol + 1 < out_len) {
        /* Look for var( */
        if (p[0] == 'v' && p[1] == 'a' && p[2] == 'r' && p[3] == '(') {
            const char *inner = p + 4;
            while (*inner == ' ') inner++;
            if (inner[0] == '-' && inner[1] == '-') {
                /* Extract var name (up to comma or closing paren) */
                const char *nstart = inner;
                size_t ni2 = 0;
                char varname[CSS_VAR_NAME_MAX];
                while (*inner && *inner != ',' && *inner != ')') inner++;
                ni2 = (size_t)(inner - nstart);
                if (ni2 >= CSS_VAR_NAME_MAX) ni2 = CSS_VAR_NAME_MAX - 1;
                kmemcpy(varname, nstart, ni2); varname[ni2] = '\0';
                css_trim(varname);
                /* Find in var table */
                const char *resolved = NULL;
                for (int vi = 0; vi < css->var_count; vi++) {
                    if (css_streq_ci(css->vars[vi].name, varname)) {
                        resolved = css->vars[vi].value; break;
                    }
                }
                /* Extract fallback (after comma, if any) */
                const char *fallback = NULL;
                char fb_buf[CSS_VAR_VALUE_MAX];
                if (!resolved && *inner == ',') {
                    inner++; /* skip comma */
                    while (*inner == ' ') inner++;
                    const char *fb_start = inner;
                    while (*inner && *inner != ')') inner++;
                    size_t fb_len = (size_t)(inner - fb_start);
                    if (fb_len > 0 && fb_len < CSS_VAR_VALUE_MAX) {
                        kmemcpy(fb_buf, fb_start, fb_len);
                        fb_buf[fb_len] = '\0';
                        css_trim(fb_buf);
                        fallback = fb_buf;
                    }
                }
                /* Skip to closing ')' */
                while (*inner && *inner != ')') inner++;
                if (*inner == ')') inner++;
                const char *use = resolved ? resolved : fallback;
                if (use) {
                    size_t rlen = kstrlen(use);
                    if (ol + rlen >= out_len) rlen = out_len - ol - 1;
                    kmemcpy(out + ol, use, rlen);
                    ol += rlen;
                }
                p = inner;
                continue;
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
                /* Expand CSS custom property references */
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
