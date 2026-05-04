#ifndef APPS_BROWSER_APP_URL_EDIT_H
#define APPS_BROWSER_APP_URL_EDIT_H

/*
 * url_edit: pure-state editor da URL bar do browser_app.
 *
 * O buffer e fixo (`URL_EDIT_CAP = 512`), suficiente para
 * URLs realistas e bem dentro do limite de protocolo
 * (`BROWSER_CHROME_URL_MAX = 768`). Estado:
 *   - `buf`: caracteres ASCII printable (32..126), terminado em '\0';
 *   - `len`: comprimento atual (sem contar '\0');
 *   - `cursor`: posicao da insercao, 0 <= cursor <= len.
 *
 * As operacoes retornam 1 quando algo mudou (caller deve repaint),
 * 0 quando o estado nao mudou (op no-op por limite/borda).
 *
 * Modulo puro: sem dependencia de kernel, compositor ou IPC. Testado
 * em `tests/test_browser_app_url_edit.c`.
 */

#include <stddef.h>
#include <stdint.h>

#define BROWSER_APP_URL_EDIT_CAP 512u

struct url_edit {
    char buf[BROWSER_APP_URL_EDIT_CAP];
    uint16_t len;
    uint16_t cursor;
};

/* Reset + carrega `src` (truncado para CAP-1). cursor vai para o fim. */
void url_edit_set(struct url_edit *e, const char *src);

/* Limpa para string vazia. */
void url_edit_clear(struct url_edit *e);

/* Insere `ch` na posicao do cursor. Aceita apenas printable (32..126).
 * Retorna 1 em sucesso, 0 se cheio ou char nao aceito. */
int url_edit_insert_char(struct url_edit *e, char ch);

/* Remove o byte imediatamente antes do cursor (Backspace). */
int url_edit_backspace(struct url_edit *e);

/* Remove o byte na posicao do cursor (Delete/forward). */
int url_edit_delete(struct url_edit *e);

/* Cursor movement helpers. Retornam 1 se moveram. */
int url_edit_move_left(struct url_edit *e);
int url_edit_move_right(struct url_edit *e);
int url_edit_move_home(struct url_edit *e);
int url_edit_move_end(struct url_edit *e);

#endif /* APPS_BROWSER_APP_URL_EDIT_H */
