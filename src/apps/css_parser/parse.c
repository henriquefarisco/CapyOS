#include "internal/css_parser_internal.h"

int css_parse(const char *css, size_t len, struct css_stylesheet *out) {
    size_t pos = 0;
    if (!css || !out) return -1;
    kmemzero(out, sizeof(*out));
    while (pos < len) {
        struct css_rule *rule = NULL;
        size_t sel_start, sel_end;
        size_t prev = pos;
        pos = css_skip_space(css, pos, len);
        pos = css_skip_comment(css, pos, len);
        if (pos == prev && pos < len) {
            if (!css_is_space(css[pos]) &&
                !(css[pos] == '/' && pos + 1 < len && css[pos + 1] == '*'))
                pos++;
            continue;
        }
        if (pos >= len) break;
        if (css[pos] == '@') {
            int is_media = 0;
            int media_matches = 0;
            pos++;
            if (pos + 4 < len &&
                css_tolower(css[pos]) == 'm' &&
                css_tolower(css[pos + 1]) == 'e' &&
                css_tolower(css[pos + 2]) == 'd' &&
                css_tolower(css[pos + 3]) == 'i' &&
                css_tolower(css[pos + 4]) == 'a') {
                is_media = 1;
                pos += 5;
                size_t cond_start = pos;
                while (pos < len && css[pos] != '{' && css[pos] != ';') pos++;
                if (pos < len && css[pos] == '{') {
                    char cond[128];
                    size_t clen = pos - cond_start;
                    if (clen >= sizeof(cond)) clen = sizeof(cond) - 1;
                    kmemcpy(cond, css + cond_start, clen);
                    cond[clen] = '\0';
                    css_trim(cond);
                    if (!cond[0] || css_streq_ci(cond, "screen") ||
                        css_streq_ci(cond, "all") ||
                        css_startswith_ci(cond, "screen") ||
                        kstrlen(cond) > 0) {
                        const char *mw = cond;
                        int skip_desktop = 0;
                        while (*mw) {
                            if (css_startswith_ci(mw, "min-width")) {
                                const char *p2 = mw + 9;
                                while (*p2 && (*p2 < '0' || *p2 > '9') && *p2 != ')') p2++;
                                uint32_t px = 0;
                                while (*p2 >= '0' && *p2 <= '9') {
                                    px = px * 10 + (uint32_t)(*p2 - '0');
                                    p2++;
                                }
                                if (px > 1024) { skip_desktop = 1; break; }
                            }
                            mw++;
                        }
                        media_matches = !skip_desktop;
                    }
                }
            } else {
                while (pos < len && css[pos] != '{' && css[pos] != ';') pos++;
            }
            if (pos < len && css[pos] == '{') {
                if (is_media && media_matches) {
                    pos++;
                    continue;
                }
                {
                    int depth = 1;
                    pos++;
                    while (pos < len && depth > 0) {
                        if (css[pos] == '{') depth++;
                        else if (css[pos] == '}') depth--;
                        pos++;
                    }
                }
            } else if (pos < len) {
                pos++;
            }
            continue;
        }
        sel_start = pos;
        while (pos < len && css[pos] != '{') pos++;
        if (pos >= len) break;
        sel_end = pos;
        pos++;
        if (out->rule_count >= CSS_MAX_RULES) {
            while (pos < len && css[pos] != '}') pos++;
            if (pos < len) pos++;
            continue;
        }
        rule = &out->rules[out->rule_count++];
        kmemzero(rule, sizeof(*rule));
        {
            size_t slen = sel_end > sel_start ? sel_end - sel_start : 0;
            if (slen >= CSS_SELECTOR_MAX) slen = CSS_SELECTOR_MAX - 1;
            kmemcpy(rule->selector, css + sel_start, slen);
            rule->selector[slen] = '\0';
            css_trim(rule->selector);
        }
        while (pos < len && css[pos] != '}') {
            size_t n_start, n_end, v_start, v_end;
            pos = css_skip_space(css, pos, len);
            pos = css_skip_comment(css, pos, len);
            if (pos >= len || css[pos] == '}') break;
            n_start = pos;
            while (pos < len && css[pos] != ':' && css[pos] != '}') pos++;
            if (pos >= len || css[pos] == '}') break;
            n_end = pos++;
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
                    if (out->var_count < CSS_MAX_VARS) {
                        struct css_var *cv = &out->vars[out->var_count++];
                        size_t vnl = kstrlen(p->name);
                        size_t vvl = kstrlen(p->value);
                        if (vnl >= CSS_VAR_NAME_MAX) vnl = CSS_VAR_NAME_MAX - 1;
                        if (vvl >= CSS_VAR_VALUE_MAX) vvl = CSS_VAR_VALUE_MAX - 1;
                        kmemcpy(cv->name, p->name, vnl);
                        cv->name[vnl] = '\0';
                        kmemcpy(cv->value, p->value, vvl);
                        cv->value[vvl] = '\0';
                    }
                } else if (p->name[0]) {
                    rule->prop_count++;
                }
            }
        }
        if (pos < len && css[pos] == '}') pos++;
    }
    return 0;
}
