/* src/lang/app_language.c
 *
 * Etapa F4 i18n (2026-05-03): impl do contrato em
 * include/lang/app_language.h. Singleton fino sobre session_active.
 * Sem libc.
 */
#include "lang/app_language.h"
#include "auth/session.h"

#include <stddef.h>

const char *app_current_language(void) {
    struct session_context *sess = session_active();
    if (!sess) return "pt-BR";
    const char *lang = session_language(sess);
    if (!lang || !lang[0]) return "pt-BR";
    return lang;
}
