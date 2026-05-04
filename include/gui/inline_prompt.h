/* include/gui/inline_prompt.h
 *
 * Etapa UX W7-ish (2026-05-03): popup modal de prompt de texto
 * (single-line). Usado pelo file_manager para Rename, e pelo desktop
 * para New File/New Folder com nome customizado.
 *
 * Design (segregado, singleton estatico):
 *   - inline_prompt_show(title, default_text, x, y, on_submit, ctx)
 *     cria a janela; Enter -> on_submit(text, ctx); Esc -> cancela.
 *   - inline_prompt_handle_key(keycode, ch) consome teclas quando
 *     o popup esta aberto (chamar do desktop key dispatcher antes
 *     do focused window).
 *   - inline_prompt_handle_click consome cliques fora (cancela) e
 *     dentro (no-op por enquanto; futura focus-on-click).
 *
 * Limites:
 *   - 1 prompt aberto por vez.
 *   - Texto max 64 chars.
 */
#ifndef GUI_INLINE_PROMPT_H
#define GUI_INLINE_PROMPT_H

#include <stdint.h>

#define INLINE_PROMPT_TEXT_MAX  64u
#define INLINE_PROMPT_TITLE_MAX 32u
#define INLINE_PROMPT_WIDTH    240u
#define INLINE_PROMPT_HEIGHT    62u

/* Callback chamado quando o usuario pressiona Enter; recebe o texto
 * digitado (NUL-terminado, len <= INLINE_PROMPT_TEXT_MAX-1) e o ctx
 * registrado em show(). Tudo dentro do popup eh fechado antes do
 * callback retornar para evitar reentrancia. */
typedef void (*inline_prompt_submit_fn)(const char *text, void *ctx);

/* Mostra o popup; default_text aparece pre-preenchido no campo
 * (usuario pode editar). Retorna 0 ok, -1 erro. */
int inline_prompt_show(const char *title, const char *default_text,
                       int32_t screen_x, int32_t screen_y,
                       inline_prompt_submit_fn on_submit, void *ctx);

/* Fecha sem callback. Idempotente. */
void inline_prompt_close(void);

/* Retorna 1 se ha um prompt aberto. */
int inline_prompt_is_open(void);

/* Handler de tecla; retorna 1 se consumiu (caller deve interromper
 * o dispatch). Apenas Enter/Esc/Backspace/printable ASCII sao
 * consumidos. */
int inline_prompt_handle_key(uint32_t keycode, char ch);

/* Handler de click; retorna 1 se consumiu. Click fora -> fecha. */
int inline_prompt_handle_click(int32_t screen_x, int32_t screen_y);

#endif /* GUI_INLINE_PROMPT_H */
