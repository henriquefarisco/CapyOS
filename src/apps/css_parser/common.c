#include "internal/css_parser_internal.h"

struct css_named_color {
    const char *name;
    uint32_t rgb;
};

int css_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

char css_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int css_streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a || *b) {
        if (css_tolower(*a) != css_tolower(*b)) return 0;
        a++;
        b++;
    }
    return 1;
}

int css_startswith_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (css_tolower(*s) != css_tolower(*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

size_t css_skip_space(const char *s, size_t pos, size_t len) {
    while (pos < len && css_is_space(s[pos])) pos++;
    return pos;
}

size_t css_skip_comment(const char *s, size_t pos, size_t len) {
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

void css_trim(char *s) {
    size_t len = kstrlen(s);
    size_t start = 0;
    if (!s || !s[0]) return;
    while (start < len && css_is_space(s[start])) start++;
    while (len > start && css_is_space(s[len - 1])) len--;
    if (start > 0 && len > start) kmemmove(s, s + start, len - start);
    s[len > start ? len - start : 0] = '\0';
}

void css_strip_important(char *s) {
    size_t len;
    if (!s) return;
    len = kstrlen(s);
    while (len > 0 && css_is_space(s[len - 1])) len--;
    if (len >= 10) {
        const char *tail = s + len - 10;
        if (css_tolower(tail[0]) == '!' &&
            css_tolower(tail[1]) == 'i' && css_tolower(tail[2]) == 'm' &&
            css_tolower(tail[3]) == 'p' && css_tolower(tail[4]) == 'o' &&
            css_tolower(tail[5]) == 'r' && css_tolower(tail[6]) == 't' &&
            css_tolower(tail[7]) == 'a' && css_tolower(tail[8]) == 'n' &&
            css_tolower(tail[9]) == 't') {
            len -= 10;
            while (len > 0 && css_is_space(s[len - 1])) len--;
        }
    }
    s[len] = '\0';
}

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
    {"transparent", 0x000001},
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
    while (css_is_space(value[i])) i++;
    if (value[i] != '#' && !css_startswith_ci(value + i, "rgb") &&
        !css_startswith_ci(value + i, "hsl")) {
        for (size_t n = 0; g_named_colors[n].name; n++) {
            if (css_streq_ci(value + i, g_named_colors[n].name))
                return g_named_colors[n].rgb;
        }
        return 0;
    }
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
    if (css_startswith_ci(value + i, "hsl")) {
        while (value[i] && value[i] != '(') i++;
        if (!value[i]) return 0;
        i++;
        while (css_is_space(value[i])) i++;
        uint32_t h = 0;
        while (value[i] >= '0' && value[i] <= '9') { h = h * 10 + (uint32_t)(value[i++] - '0'); }
        while (value[i] && value[i] != ',' && value[i] != ' ') i++;
        while (css_is_space(value[i]) || value[i] == ',') i++;
        uint32_t s = 0;
        while (value[i] >= '0' && value[i] <= '9') { s = s * 10 + (uint32_t)(value[i++] - '0'); }
        while (value[i] && value[i] != ',' && value[i] != ' ') i++;
        while (css_is_space(value[i]) || value[i] == ',') i++;
        uint32_t l = 0;
        while (value[i] >= '0' && value[i] <= '9') { l = l * 10 + (uint32_t)(value[i++] - '0'); }
        if (s == 0) {
            uint32_t v = (l * 255u) / 100u;
            return (v << 16) | (v << 8) | v;
        }
        {
            uint32_t l2 = l > 50 ? (l - 50) * 2 : (50 - l) * 2;
            uint32_t c = ((100u - l2) * s) / 100u;
            uint32_t x = c * (uint32_t)(60u - (h % 120u > 60u ? h % 120u - 60u : 60u - h % 120u)) / 60u;
            uint32_t m_l = l > c / 2 ? l - c / 2 : 0;
            uint32_t r1, g1, b1;
            uint32_t h6 = h / 60u;
            if (h6 == 0) { r1 = c; g1 = x; b1 = 0; }
            else if (h6 == 1) { r1 = x; g1 = c; b1 = 0; }
            else if (h6 == 2) { r1 = 0; g1 = c; b1 = x; }
            else if (h6 == 3) { r1 = 0; g1 = x; b1 = c; }
            else if (h6 == 4) { r1 = x; g1 = 0; b1 = c; }
            else { r1 = c; g1 = 0; b1 = x; }
            uint32_t r = ((r1 + m_l) * 255u) / 100u;
            uint32_t g = ((g1 + m_l) * 255u) / 100u;
            uint32_t b = ((b1 + m_l) * 255u) / 100u;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            return (r << 16) | (g << 8) | b;
        }
    }
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
