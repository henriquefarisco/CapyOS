#ifndef APPS_BROWSER_DIMENSIONS_H
#define APPS_BROWSER_DIMENSIONS_H

/*
 * include/apps/browser_dimensions.h (Etapa 2.f, 2026-05-02)
 *
 * Constantes canonicas das dimensoes do browser CapyOS, usadas tanto
 * pelo engine ring 3 (`userland/bin/capybrowser/main.c`) quanto pelo
 * chrome ring 0 (`src/apps/browser_app/browser_app.c`).
 *
 * Antes deste header, as dimensoes 480x360 e a altura da URL bar 24
 * estavam duplicadas em ambos os lados, criando o risco de drift
 * silencioso: se um lado bumpasse para 640x480 e o outro nao, o
 * `EVENT_FRAME` chegaria com width/height inconsistente com o que o
 * `chrome.last_frame` espera, e o blit seria clipado/exibiria lixo.
 *
 * Regra para mudar:
 *   1. Edite UMA das constantes BROWSER_FRAME_* aqui.
 *   2. Garanta que `BROWSER_FRAME_PIXEL_BYTES + BROWSER_FRAME_HDR_BYTES`
 *      cabe em `CHROME_RUNTIME_EVENT_BUF_MAX` (ver
 *      `include/apps/browser_chrome_runtime.h`). Se nao, bump o scratch
 *      em paralelo.
 *   3. Re-rode `make test` (cobre runtime) e o smoke `browser-smoke`
 *      (cobre engine) para garantir que ambos os lados leem do header
 *      atualizado.
 */

/* Framebuffer do engine: BGRA little-endian, sem padding entre linhas
 * (stride = W*4).
 *
 * 2026-05-02: separado em MAX (cap do framebuffer estatico no .bss
 * do engine) vs DEFAULT (viewport inicial). Antes existia apenas um
 * tamanho fixo 480x360, e o engine sempre rasterizava nesse tamanho
 * mesmo quando o usuario aumentava a janela do browser -- o
 * conteudo ficava encalhado em 480x360 no canto superior-esquerdo
 * da janela, com bg_color preenchendo o resto. Bumpando o cap para
 * 1024x768 e implementando o protocolo BROWSER_IPC_RESIZE,
 * o engine rasteriza no tamanho exato da viewport ate esse cap.
 *
 * Custos do bump:
 *   - .bss do engine: 1024*768*4 = 3 MiB (era 675 KiB).
 *   - event_scratch + last_frame_storage da chrome runtime: 2x ~3 MiB
 *     porque CHROME_RUNTIME_EVENT_BUF_MAX e dimensionado pelo MAX.
 *   - Total kernel-side: ~6 MiB extras, compativel com QEMU >= 64 MiB.
 *
 * MAX_W x MAX_H deve continuar cabendo em CHROME_RUNTIME_EVENT_BUF_MAX
 * (12 B header + W*H*4). Bumpar aqui exige bumpar o scratch em
 * paralelo (ver `include/apps/browser_chrome_runtime.h`). */
#define BROWSER_FRAME_MAX_W        1024u
#define BROWSER_FRAME_MAX_H        768u
#define BROWSER_FRAME_MAX_STRIDE   (BROWSER_FRAME_MAX_W * 4u)
#define BROWSER_FRAME_MAX_PIXELS   (BROWSER_FRAME_MAX_STRIDE * BROWSER_FRAME_MAX_H)

/* Viewport inicial do engine quando a chrome ainda nao mandou um
 * RESIZE. Combinado com `BROWSER_APP_WIN_*` abaixo de modo que a
 * primeira janela ja chega na resolucao certa para o primeiro
 * FRAME, sem flash de blank. Aumentar aqui muda a janela default
 * mas nao o cap. */
#define BROWSER_FRAME_DEFAULT_W    480u
#define BROWSER_FRAME_DEFAULT_H    360u

/* Aliases legados: muito codigo le `BROWSER_FRAME_W/H/STRIDE/...`
 * esperando "o tamanho do framebuffer". A semantica nova e que esse
 * tamanho corresponde ao MAX (capacidade do .bss). Caller que ainda
 * quer o default precisa usar `BROWSER_FRAME_DEFAULT_*`. */
#define BROWSER_FRAME_W            BROWSER_FRAME_MAX_W
#define BROWSER_FRAME_H            BROWSER_FRAME_MAX_H
#define BROWSER_FRAME_STRIDE       BROWSER_FRAME_MAX_STRIDE
#define BROWSER_FRAME_PIXEL_BYTES  BROWSER_FRAME_MAX_PIXELS

/* Header do payload de EVENT_FRAME: nav_id u32 + width u16 + height
 * u16 + stride u32 = 12 bytes. Depois vem `BROWSER_FRAME_PIXEL_BYTES`
 * de BGRA. */
#define BROWSER_FRAME_HDR_BYTES    12u
#define BROWSER_FRAME_TOTAL_BYTES  (BROWSER_FRAME_HDR_BYTES + BROWSER_FRAME_PIXEL_BYTES)

/* URL bar do chrome: 32 px de altura abaixo da area do frame, com
 * toolbar (Back/Forward/Reload/Home/Go) + status + texto + cursor.
 * Janela INICIAL = frame default + URL bar. Apos isso o usuario
 * pode redimensionar via grip-zones do compositor ate
 * `BROWSER_FRAME_MAX_*` + URLBAR_H.
 *
 * Etapa F4 toolbar (2026-05-03): bump 24 -> 32 para acomodar
 * botoes 22 px de altura com 5 px de padding top/bottom. Layout
 * dentro da barra esta documentado em
 * `src/apps/browser_app/toolbar.c`. */
#define BROWSER_APP_URLBAR_H       32u
#define BROWSER_APP_WIN_W          BROWSER_FRAME_DEFAULT_W
#define BROWSER_APP_WIN_H          (BROWSER_FRAME_DEFAULT_H + BROWSER_APP_URLBAR_H)

#endif /* APPS_BROWSER_DIMENSIONS_H */
