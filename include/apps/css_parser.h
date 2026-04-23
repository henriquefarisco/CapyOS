#ifndef APPS_CSS_PARSER_H
#define APPS_CSS_PARSER_H

#include "apps/html_viewer.h"
#include <stddef.h>
#include <stdint.h>

#define CSS_MAX_RULES        192
#define CSS_MAX_PROPS        24
#define CSS_SELECTOR_MAX     128
#define CSS_PROP_NAME_MAX    48
#define CSS_PROP_VALUE_MAX   128
#define CSS_STYLE_BUF_MAX    8192
#define CSS_MAX_VARS         32
#define CSS_VAR_NAME_MAX     64
#define CSS_VAR_VALUE_MAX    128

struct css_property {
    char name[CSS_PROP_NAME_MAX];
    char value[CSS_PROP_VALUE_MAX];
};

struct css_rule {
    char selector[CSS_SELECTOR_MAX];
    struct css_property props[CSS_MAX_PROPS];
    int prop_count;
};

struct css_var {
    char name[CSS_VAR_NAME_MAX];   /* --variable-name */
    char value[CSS_VAR_VALUE_MAX];
};

struct css_stylesheet {
    struct css_rule rules[CSS_MAX_RULES];
    int rule_count;
    struct css_var vars[CSS_MAX_VARS];
    int var_count;
};

/* Parse a CSS text block into a stylesheet. Returns 0 on success. */
int css_parse(const char *css, size_t len, struct css_stylesheet *out);

/* Parse a CSS inline style string (no selector, just "prop: val; ...").
 * Applies results directly to the given node. */
void css_apply_inline(const char *style_attr, struct html_node *node);

/* Apply a full stylesheet to every node in a document.
 * CSS properties are written into node fields (color, hidden, bold, etc.). */
void css_apply_to_doc(const struct css_stylesheet *css,
                      struct html_document *doc);

/* Parse a CSS color string into a 0x00RRGGBB value.
 * Returns 0 if the color could not be parsed (use 0 as "not set"). */
uint32_t css_parse_color(const char *value);

#endif /* APPS_CSS_PARSER_H */
