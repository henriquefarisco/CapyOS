#ifndef APPS_BROWSER_APP_H
#define APPS_BROWSER_APP_H

/*
 * browser_app (F3.3f): ponte entre a camada logica `chrome_runtime`
 * (ring 0) e a janela compositor que o usuario ve no desktop.
 *
 * Responsabilidades:
 *   1. `browser_app_open()` spawna o engine `/bin/capybrowser` em
 *      ring 3 via `browser_engine_spawn`, inicializa a
 *      `chrome_runtime` com os dois pipes, cria uma janela
 *      compositor e inicia uma navegacao para
 *      `file://capyos/welcome` como home-page.
 *   2. `browser_app_tick()` e chamada uma vez por `desktop_run_frame`.
 *      Drena eventos IPC do engine (ate NO_DATA), resolve fetches
 *      pendentes via `chrome_runtime_dispatch_pending_fetch`, e quando
 *      um `EVENT_FRAME` chega copia os pixels BGRA do
 *      `chrome.last_frame` para a superficie da janela (centralizando
 *      e preenchendo o resto com a cor de fundo da paleta).
 *   3. `browser_app_close()` envia SHUTDOWN ao engine e limpa estado.
 *      E chamado tanto pelo usuario (fechar a janela) quanto por
 *      watchdog (engine morto).
 *
 * Desenho deliberadamente minimo: sem barra de endereco, sem botoes,
 * sem historico. A pagina inicial e fixa em `file://capyos/welcome`
 * (servida pelo resolver embutido do chrome). Interacao via teclado /
 * navegacao multi-pagina ficam para sub-slices futuras.
 */

#include <stdint.h>

/* Abre (ou traz ao foco) a janela do browser. Se ja estiver aberta,
 * apenas requests foco. Idempotente. */
void browser_app_open(void);

/* Fecha a janela, envia SHUTDOWN ao engine e libera recursos.
 * Seguro chamar mesmo com a janela fechada. */
void browser_app_close(void);

/* 1 se a janela esta visivel, 0 caso contrario. */
int browser_app_is_open(void);

/* Tick per-frame. Chamada pelo `desktop_run_frame` em todo quadro;
 * barata quando o browser esta fechado (single load + branch). */
void browser_app_tick(uint64_t now_ticks);

#endif /* APPS_BROWSER_APP_H */
