/* include/apps/browser_app_homepage.h
 *
 * Etapa F4 homepage (2026-05-03): logica de homepage do browser
 * extraida em modulo dedicado para aliviar browser_app.c (que
 * passou do limite de 900 linhas com a infra de fallback offline).
 *
 * Responsabilidades deste modulo:
 *   1. Resolver a homepage configurada via /system/config.ini
 *      (`browser_homepage=`), com default code-side
 *      `https://wikipedia.org`.
 *   2. Disparar o NAVIGATE inicial na primeira tick apos
 *      browser_app_homepage_open().
 *   3. Detectar falha de navegacao (status FAILED no chrome) e
 *      cair UMA vez por sessao para a pagina embarcada
 *      `file://capyos/wikipedia`, garantindo que o usuario sempre
 *      veja conteudo (online ou offline).
 *
 * Estado e privado ao modulo (singleton estatico). browser_app.c
 * usa esta API via tick; o modulo nao toca compositor nem url_edit
 * (caller faz o sync de URL bar usando os retornos).
 */
#ifndef APPS_BROWSER_APP_HOMEPAGE_H
#define APPS_BROWSER_APP_HOMEPAGE_H

#include <stdint.h>

struct chrome_runtime;

/* Resolve a homepage do config.ini + zera flags de tick. Chamar
 * em browser_app_open() antes da primeira tick. */
void browser_app_homepage_open(void);

/* Idempotente. Zera flags para que a proxima abertura re-tente o
 * fallback se necessario. Chamar em browser_app_close(). */
void browser_app_homepage_close(void);

/* URL inicial resolvida (NUL-terminada). Sempre valida apos
 * browser_app_homepage_open(); pode apontar para o code-default se
 * o load das settings falhar. */
const char *browser_app_homepage_initial_url(void);
uint16_t    browser_app_homepage_initial_url_len(void);

/* URL de fallback offline (sempre `file://capyos/wikipedia`). */
const char *browser_app_homepage_fallback_url(void);

/* Resultado de browser_app_homepage_tick. */
enum {
    BROWSER_APP_HOMEPAGE_TICK_NOOP = 0,
    BROWSER_APP_HOMEPAGE_TICK_SENT_INITIAL = 1,
    BROWSER_APP_HOMEPAGE_TICK_SENT_FALLBACK = 2
};

/* Driver de tick: chama chrome_runtime_send_navigate quando
 * apropriado. Retorna BROWSER_APP_HOMEPAGE_TICK_SENT_FALLBACK se o
 * caller deve re-pintar a URL bar com browser_app_homepage_fallback_url(). */
int browser_app_homepage_tick(struct chrome_runtime *rt);

#endif /* APPS_BROWSER_APP_HOMEPAGE_H */
