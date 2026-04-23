#ifndef APPS_CSS_PARSER_INTERNAL_H
#define APPS_CSS_PARSER_INTERNAL_H

#include "apps/css_parser.h"
#include "util/kstring.h"

#include <stddef.h>
#include <stdint.h>

int css_is_space(char c);
char css_tolower(char c);
int css_streq_ci(const char *a, const char *b);
int css_startswith_ci(const char *s, const char *prefix);
size_t css_skip_space(const char *s, size_t pos, size_t len);
size_t css_skip_comment(const char *s, size_t pos, size_t len);
void css_trim(char *s);
void css_strip_important(char *s);
void css_apply_prop(const char *name, const char *value, struct html_node *node);
int css_selector_matches(const char *selector, const struct html_node *node);

#endif /* APPS_CSS_PARSER_INTERNAL_H */
