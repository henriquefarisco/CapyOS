/* include/gui/context_menu.h
 *
 * Etapa UX W7-ish (2026-05-03): popup de menu de contexto generico
 * para click-direito. Usado pelo file_manager (e potencialmente
 * desktop/text_editor) para mostrar acoes (Open, Rename, Delete,
 * New File/Folder, Save, etc.) na posicao do cursor.
 *
 * Design (segregado, sem libc):
 *   - Owner registra um set de items (label + action_id) e chama
 *     `context_menu_show(items, count, screen_x, screen_y, on_pick)`.
 *   - O modulo cria/reusa uma janela compositor undecorada com
 *     corner_radius=4, border do tema, hover por linha e labels truncadas.
 *   - Click: invoca `on_pick(action_id, ctx)` e fecha o popup.
 *   - Click fora ou ESC: fecha sem callback.
 *   - Mouse-move: atualiza hover, repaint da linha.
 *
 * Limites:
 *   - Maximo 8 items por menu (CONTEXT_MENU_MAX_ITEMS).
 *   - Apenas 1 menu pode estar aberto por vez (singleton estatico).
 *   - Items de label vazio sao tratados como separadores.
 */
#ifndef GUI_CONTEXT_MENU_H
#define GUI_CONTEXT_MENU_H

#include <stdint.h>

#define CONTEXT_MENU_MAX_ITEMS  8u
#define CONTEXT_MENU_LABEL_MAX  32u
#define CONTEXT_MENU_WIDTH      160u
#define CONTEXT_MENU_ITEM_H     24u
#define CONTEXT_MENU_SEP_H      8u

struct context_menu_item {
  char     label[CONTEXT_MENU_LABEL_MAX]; /* "" => separador */
  uint16_t action_id;                      /* livre para o caller */
  uint8_t  enabled;                        /* 0 => grayed-out */
  uint8_t  reserved;
};

/* Callback chamado quando o usuario seleciona um item. action_id e
 * o mesmo passado em context_menu_item; ctx e o passado a show(). */
typedef void (*context_menu_pick_fn)(uint16_t action_id, void *ctx);

/* Mostra o popup com os `count` itens em (screen_x, screen_y). Se
 * count > CONTEXT_MENU_MAX_ITEMS, items extras sao ignorados.
 * Coordenadas sao clampadas para nao ultrapassar a tela.
 * Retorno: 0 ok, -1 erro (count <= 0 ou create falhou). */
int context_menu_show(const struct context_menu_item *items, uint32_t count,
                      int32_t screen_x, int32_t screen_y,
                      context_menu_pick_fn on_pick, void *ctx);

/* Fecha qualquer popup aberto (sem callback). Idempotente. */
void context_menu_close(void);

/* Routing de eventos. Chamados pelo desktop loop quando o popup esta
 * visivel:
 *   - context_menu_handle_click: retorna 1 se o click foi consumido.
 *   - context_menu_handle_hover: idempotente, atualiza hover_index. */
int context_menu_handle_click(int32_t screen_x, int32_t screen_y);
void context_menu_handle_hover(int32_t screen_x, int32_t screen_y);

/* Retorna 1 se ha um popup aberto. */
int context_menu_is_open(void);

#endif /* GUI_CONTEXT_MENU_H */
