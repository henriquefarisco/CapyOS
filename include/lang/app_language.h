/* include/lang/app_language.h
 *
 * Etapa F4 i18n (2026-05-03): helper compartilhado para descobrir
 * o idioma da sessao ativa e fazer seleao por idioma. Apps GUI
 * (settings, file_manager, text_editor, taskbar, desktop_icons,
 * etc.) chamam estas funcoes para localizar suas labels.
 *
 * Por que existir esta camada:
 *   - `session_active()` + `session_language()` ja existem, mas
 *     apps GUI tendem a duplicar a logica defensiva (NULL checks,
 *     fallback para "pt-BR"). Centralizar evita bugs.
 *   - Macro `APP_T(pt, en, es)` empacota localization_select para
 *     reduzir verbosidade nas chamadas de paint.
 */
#ifndef LANG_APP_LANGUAGE_H
#define LANG_APP_LANGUAGE_H

#include "lang/localization.h"

/* Retorna o idioma ativo da sessao (3 caracteres no minimo, ex.
 * "pt-BR", "en", "es"). Nunca retorna NULL: cai em "pt-BR" se a
 * sessao for NULL ou se o idioma estiver vazio. */
const char *app_current_language(void);

/* Macro de conveniencia para selecionar o texto certo conforme o
 * idioma da sessao. Usar como:
 *   font_draw_string(s, f, x, y, APP_T("Tema:", "Theme:", "Tema:"), col);
 *
 * Notas:
 *   - O lookup de idioma acontece a cada chamada (cheap: lookup de
 *     ponteiro + 1 strcmp). Para loops apertados, cachear localmente
 *     com `const char *lang = app_current_language();` e usar
 *     `localization_select(lang, ...)` direto. */
#define APP_T(pt_br, en, es) \
    localization_select(app_current_language(), (pt_br), (en), (es))

#endif /* LANG_APP_LANGUAGE_H */
