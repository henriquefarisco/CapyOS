/* include/gui/desktop_icons.h
 *
 * Etapa UX W7-ish (2026-05-03): visualizacao de arquivos/pastas no
 * desktop (sob o wallpaper, sobre as janelas decoradas via window
 * z-order). Layout em grid:
 *   - Cell 80×80 px
 *   - Coluna esquerda, em sequencia para baixo
 *   - Margem superior 16 px (deixa headroom para futuras barras)
 *   - Margem inferior = altura do taskbar
 *
 * Ciclo de vida:
 *   - desktop_icons_init(path): primeira chamada -- carrega lista
 *     de entries via vfs_listdir. path tipicamente "~/Desktop"
 *     (cai pra home se nao existir).
 *   - desktop_icons_paint(surface): callback para o compositor;
 *     desenha icones sobre o wallpaper.
 *   - desktop_icons_handle_click(sx, sy, screen_h): hit-test +
 *     dispatch (open/select).
 *   - desktop_icons_handle_context(sx, sy, screen_h): mostra menu
 *     de contexto na posicao do cursor.
 *   - desktop_icons_refresh(path): recarrega listagem.
 *
 * Limites:
 *   - Maximo DESKTOP_ICONS_MAX = 64 itens.
 *   - Singleton estatico (so um desktop por vez).
 */
#ifndef GUI_DESKTOP_ICONS_H
#define GUI_DESKTOP_ICONS_H

#include <stdint.h>

struct gui_surface;

#define DESKTOP_ICONS_MAX     64u
#define DESKTOP_ICONS_NAME_MAX 32u
#define DESKTOP_ICON_CELL_W   80u
#define DESKTOP_ICON_CELL_H   80u
#define DESKTOP_ICON_PAD_TOP  16u
#define DESKTOP_ICON_PAD_LEFT 16u

void desktop_icons_init(const char *path, uint32_t taskbar_height);

/* Recarrega a listagem do path atual (para chamar apos
 * criar/deletar/renomear). */
void desktop_icons_refresh(void);

/* Pinta os icones no surface do wallpaper. Chamado pelo compositor
 * via comp_desktop_paint_cb. */
void desktop_icons_paint(struct gui_surface *surface);

/* Hit-test: retorna o indice do icone clicado (>= 0) ou -1.
 * `screen_h` eh a altura total da tela (para nao desenhar sob o
 * taskbar). */
int desktop_icons_hit_test(int32_t sx, int32_t sy);

/* Click esquerdo: seleciona; segundo click no mesmo icone abre
 * (text editor para .txt/.md, file_manager para diretorios). */
void desktop_icons_handle_click(int32_t sx, int32_t sy);

/* Right-click: mostra context_menu apropriado (sobre icone ou
 * area vazia). */
void desktop_icons_handle_context(int32_t sx, int32_t sy);

/* Limpa selecao quando user clica fora. */
void desktop_icons_clear_selection(void);

#endif /* GUI_DESKTOP_ICONS_H */
