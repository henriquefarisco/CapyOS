/* include/apps/browser_app_nav.h
 *
 * Etapa F4 nav (2026-05-03): helpers de navegacao do browser_app
 * extraidos para manter browser_app.c sob 900 linhas. Cobre:
 *   - Normalizacao de URL (prefixar `http://` quando o usuario
 *     digita "example.com" sem esquema, strip whitespace).
 *   - Detector de morte do engine (process_stats lookup).
 *
 * Sem libc, sem alloc. O scratch da normalizacao e estatico
 * (.bss) compartilhado entre chamadas, mas como o browser_app
 * roda no kernel single-threaded e o resultado eh consumido
 * imediatamente em chrome_runtime_send_navigate(), nao ha race.
 */
#ifndef APPS_BROWSER_APP_NAV_H
#define APPS_BROWSER_APP_NAV_H

#include <stdint.h>
#include <stddef.h>

/* Normaliza `src`/`src_len` retornando ponteiro NUL-terminado para
 * a URL pronta para o engine. Em sucesso, *out_len recebe o
 * comprimento. Em falha (vazio, oversized, etc) retorna NULL.
 *
 * Regras:
 *   - "file://..." / "http://..." / "https://..." passam direto.
 *   - URL sem esquema ganha "http://" automatico.
 *   - Whitespace leading/trailing eh stripped.
 *   - O ponteiro retornado eh valido ate a proxima chamada (scratch
 *     interno reutilizavel). */
const char *browser_app_nav_normalize(const char *src, uint16_t src_len,
                                       uint16_t *out_len);

/* Detecta morte externa do engine via process_stats. Recebe o PID
 * para nao acoplar a header com o estado global do browser_app.
 * Retorna 1 se o engine_pid nao esta mais RUNNING (zombie/unused
 * ou slot reapado), 0 caso contrario. */
int browser_app_nav_engine_is_dead(uint32_t engine_pid);

#endif /* APPS_BROWSER_APP_NAV_H */
