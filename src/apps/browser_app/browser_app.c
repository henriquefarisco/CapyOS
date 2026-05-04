/*
 * src/apps/browser_app/browser_app.c (F3.3f)
 *
 * Ponte kernel-side entre a chrome runtime (que drena o IPC com o
 * engine ring 3 capybrowser) e a janela do compositor que o
 * usuario enxerga. Cobertura desta slice:
 *
 *   1. Spawn/Shutdown do engine + janela compositor.
 *   2. Blit BGRA do `chrome.last_frame` na area principal da
 *      superficie (linhas 0..FRAME_H-1).
 *   3. URL bar editavel pelo usuario:
 *        - texto inicial = home-page (`file://capyos/welcome`);
 *        - caracteres printaveis (32..126) entram no buffer;
 *        - Backspace (0x08) apaga o ultimo byte;
 *        - Enter (\n/\r) dispara NAVIGATE com o buffer atual;
 *        - Esc (0x1B) fecha o browser.
 *      A barra e desenhada na faixa de 20 px abaixo do frame.
 *   4. Respawn on-the-fly quando o watchdog mata o engine ou a
 *      pipe quebra: novo spawn + `chrome_runtime_record_restart`.
 *      Janela permanece aberta; status reflete "LOADING (restart)".
 *
 * Interacao teclado passa pelo roteamento padrao do desktop
 * (`desktop_handle_input`): a janela focada recebe `on_key` com
 * keycode = ASCII ou KEY_* para teclas especiais.
 *
 * Contrato publico: include/apps/browser_app.h.
 */

#include "apps/browser_app.h"
#include "apps/browser_app_homepage.h"
#include "apps/browser_app_nav.h"
#include "apps/browser_app_toolbar.h"
#include "apps/browser_app_url_edit.h"
#include "apps/browser_chrome_runtime.h"
#include "apps/browser_chrome.h"
#include "apps/browser_dimensions.h"
#include "kernel/browser_engine_spawn.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include "kernel/process_iter.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "kernel/user_task_init.h"
#include "arch/x86_64/apic.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include "drivers/input/keyboard_layout.h"

#include <stddef.h>
#include <stdint.h>

/* Helper de debugcon (porta 0xE9). Centralizar a inline asm em UM
 * lugar evita duplicacao e facilita migrar (ex.: usar `kdebug_putc`
 * do kernel quando a sessao virar built-in). Custo zero: o
 * compilador inlineia para 1 outb. */
static inline void debugcon_putc(uint8_t c) {
    __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0xE9));
}

/* Debug markers via debugcon. Boot QEMU com `-debugcon stdio` para
 * ver. Cada chamada emite 3 bytes: '[' + tag + ']'. Use isso para
 * bisetar onde o open/tick falha. */
static inline void bapp_mark(char tag) {
    debugcon_putc((uint8_t)'[');
    debugcon_putc((uint8_t)tag);
    debugcon_putc((uint8_t)']');
}

/* Etapa 2.f: encaminha uma mensagem de log do engine para o
 * debugcon. Bytes nao-printaveis viram '?' para nao corromper o
 * fluxo do operador. O cap defensivo `BROWSER_CHROME_LOG_MSG_MAX`
 * protege contra `last_log_msg_len` corrompido (cao morto, bug
 * futuro no dispatcher) que poderia causar leitura fora do array
 * `last_log_msg[BROWSER_CHROME_LOG_MSG_MAX]`. Se o cap dispara,
 * marcamos com `[T]` para o operador notar trunc. */
static void browser_app_log_forward(const char *msg, uint16_t len) {
    uint16_t cap = (uint16_t)BROWSER_CHROME_LOG_MSG_MAX;
    int truncated = 0;
    if (len > cap) {
        len = cap;
        truncated = 1;
    }
    debugcon_putc((uint8_t)'<');
    for (uint16_t i = 0; i < len; ++i) {
        uint8_t c = (uint8_t)msg[i];
        if (c < 32u || c > 126u) c = (uint8_t)'?';
        debugcon_putc(c);
    }
    if (truncated) {
        debugcon_putc((uint8_t)'[');
        debugcon_putc((uint8_t)'T');
        debugcon_putc((uint8_t)']');
    }
    debugcon_putc((uint8_t)'>');
    debugcon_putc((uint8_t)'\n');
}

/* Etapa 2.f, 2026-05-02: dimensoes do browser saem de
 * `include/apps/browser_dimensions.h` para garantir que engine ring 3
 * (`userland/bin/capybrowser/main.c`) e chrome ring 0 (este arquivo)
 * leiam exatamente os mesmos valores. Tudo o que era
 * `BROWSER_APP_FRAME_*` aqui agora vem do header canonico via
 * `BROWSER_FRAME_*` (frame W/H/stride) e `BROWSER_APP_*` (janela e
 * URL bar). */
#define BROWSER_APP_FRAME_W   BROWSER_FRAME_W
#define BROWSER_APP_FRAME_H   BROWSER_FRAME_H

/* Cor de fundo (usada se o tema nao foi aplicado ainda). */
#define BROWSER_APP_SURFACE_BG 0xFFF0F0F0u

/* Etapa F4 homepage (2026-05-03): a logica da homepage (resolucao
 * via /system/config.ini, default `https://wikipedia.org`, fallback
 * automatico para `file://capyos/wikipedia` em FAILED) vive em
 * `src/apps/browser_app/homepage.c` (header
 * `include/apps/browser_app_homepage.h`). browser_app.c apenas
 * chama browser_app_homepage_{open,close,tick} no lugar certo. */

/* F3.3g + Etapa F4 nav (2026-05-03): URL normalization e detector
 * de morte do engine moveram para `src/apps/browser_app/nav.c`
 * (header `include/apps/browser_app_nav.h`) para manter o
 * monolito sob 900 linhas. Aqui ficam apenas as chamadas. */

/* Etapa 3 seção c (2026-05-03): destinos possiveis do foco de
 * teclado. URL bar e default na criacao da janela; clicar dentro
 * do frame transfere foco para a pagina (engine roteia ao input
 * focado dentro do doc). */
enum browser_app_focus {
    BROWSER_APP_FOCUS_URLBAR = 0,
    BROWSER_APP_FOCUS_PAGE   = 1
};

struct browser_app_state {
    uint8_t active;             /* 0/1 */
    uint8_t pipe_ops_installed; /* 0/1 */
    /* Etapa 3 seção c (2026-05-03): foco do teclado. Atualizado por
     * cliques e pelo usuario (Esc volta o foco para URL bar). */
    uint8_t focus_target;       /* enum browser_app_focus */
    /* Etapa F4 homepage (2026-05-03): home_sent + home_fallback_used
     * migraram para o modulo dedicado homepage.c. */

    struct chrome_runtime runtime;
    struct gui_window *window;
    uint32_t window_id;

    /* URL bar editavel pelo usuario. A URL realmente navegada
     * esta em `runtime.chrome.current_url` apos Enter. */
    struct url_edit url;

    /* Telemetria */
    uint32_t frames_repainted;
    uint32_t fetches_dispatched;
    uint32_t engine_respawns;
};

static struct browser_app_state g_app;

/* === Helpers ============================================================ */

static void install_pipe_ops_once(void) {
    if (g_app.pipe_ops_installed) return;
    chrome_runtime_set_pipe_ops(pipe_write, pipe_read);
    /* F3.3f: sem yield_op, `read_full` falha com -2 assim que o
     * pipe drena mid-payload (EVENT_FRAME 96 KiB vs pipe 4 KiB). */
    chrome_runtime_set_yield_op(task_yield);
    g_app.pipe_ops_installed = 1u;
}

/* Etapa F4 toolbar (2026-05-03): forward-decl. Definicoes ficam
 * proximas dos demais helpers de tick (dispatch_toolbar_action e
 * navigate_url_bar) para preservar a estrutura "paint -> input ->
 * lifecycle"; a referencia anterior em browser_app_on_mouse precisa
 * deste prototipo para nao falhar com -Werror=implicit. */
static void dispatch_toolbar_action(enum browser_app_toolbar_action a);
static void navigate_url_bar(void);

/* === Pintura =========================================================== */

/* Limpa a area da URL bar para a cor `bg`. A URL bar fica nos
 * ULTIMOS BROWSER_APP_URLBAR_H pixels da janela atual, nao em uma
 * coordenada fixa: depois do bump do framebuffer cap, o cap fixo
 * antigo `BROWSER_APP_FRAME_H` (= 768) faria a URL bar sumir para
 * fora de qualquer janela < 768 px, que e o caso comum. */
static void paint_urlbar_bg(struct gui_surface *s, uint32_t bg) {
    if (!s || !s->pixels) return;
    if (s->height <= BROWSER_APP_URLBAR_H) return;
    uint32_t y0 = s->height - BROWSER_APP_URLBAR_H;
    for (uint32_t y = y0; y < s->height; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels
                                      + (size_t)y * (size_t)s->pitch);
        for (uint32_t x = 0u; x < s->width; ++x) row[x] = bg;
    }
}

/* Desenha a URL bar (fundo + toolbar + status + texto + cursor).
 * Etapa F4 toolbar (2026-05-03): refatorado para chamar o paint
 * dos botoes via browser_app_toolbar_paint, e usar
 * browser_app_toolbar_url_region para saber onde o texto da URL
 * comeca/termina (de modo que o cursor nao se sobreponha aos
 * botoes Back/Forward/Reload/Home na esquerda nem ao Go na direita). */
static void paint_urlbar(void) {
    struct gui_window *win = g_app.window;
    if (!win || !win->surface.pixels) return;

    const struct gui_theme_palette *theme = compositor_theme();
    const struct font *font = font_default();
    if (!theme || !font) return;

    uint32_t bar_bg   = theme->terminal_bg;
    uint32_t text_col = theme->text;
    uint32_t muted    = theme->text_muted;
    uint32_t accent   = theme->accent;

    paint_urlbar_bg(&win->surface, bar_bg);

    uint32_t bar_top = (win->surface.height > BROWSER_APP_URLBAR_H)
                       ? (win->surface.height - BROWSER_APP_URLBAR_H)
                       : 0u;

    /* Botoes Back/Forward/Reload/Home/Go. */
    browser_app_toolbar_paint(&win->surface, theme, font, bar_top);

    /* Texto centralizado verticalmente. URL bar = 32 px; glyph 8 px;
     * y = bar_top + (32 - 8)/2 = bar_top + 12. */
    int32_t y = (int32_t)bar_top + 12;

    /* Status letter entre o ultimo botao esquerdo e o texto da URL.
     * Posicao fixa em x = 4*28 + 4 = 116. */
    char st_buf[2];
    st_buf[1] = '\0';
    uint32_t st_col = muted;
    switch (g_app.runtime.chrome.status) {
        case BROWSER_CHROME_STATUS_LOADING:   st_buf[0] = 'L'; st_col = accent; break;
        case BROWSER_CHROME_STATUS_READY:     st_buf[0] = 'R'; st_col = text_col; break;
        case BROWSER_CHROME_STATUS_FAILED:    st_buf[0] = 'F'; st_col = 0xFFB23131u; break;
        case BROWSER_CHROME_STATUS_CANCELLED: st_buf[0] = 'X'; st_col = muted; break;
        case BROWSER_CHROME_STATUS_IDLE:
        default:                              st_buf[0] = 'I'; st_col = muted; break;
    }
    font_draw_string(&win->surface, font, 116, y, st_buf, st_col);

    /* URL bar de texto: regiao calculada pelo toolbar.
     * Etapa F4 toolbar (2026-05-03): cursor agora respeita a
     * regiao do texto -- nao desenha em cima dos botoes Go. */
    int32_t text_x = 132;
    int32_t text_w = (int32_t)win->surface.width;
    browser_app_toolbar_url_region(win->surface.width, &text_x, &text_w);
    font_draw_string(&win->surface, font, text_x, y, g_app.url.buf, text_col);

    uint32_t cursor_x = (uint32_t)text_x
                         + (uint32_t)g_app.url.cursor * font->glyph_width;
    int32_t text_end = text_x + text_w;
    if ((int32_t)cursor_x + 1 < text_end &&
        cursor_x + 1u < win->surface.width) {
        for (uint32_t cy = 0u; cy < font->glyph_height; ++cy) {
            uint32_t py = (uint32_t)y + cy;
            if (py >= win->surface.height) break;
            uint32_t *row = (uint32_t *)((uint8_t *)win->surface.pixels
                                          + (size_t)py
                                              * (size_t)win->surface.pitch);
            row[cursor_x] = text_col;
        }
    }
}

/* Copia os pixels do chrome.last_frame para a area principal da
 * janela. Faz clip contra as dimensoes da superficie. */
static void copy_last_frame_to_window(void) {
    struct gui_window *win = g_app.window;
    if (!win || !win->surface.pixels) return;

    const struct browser_chrome_frame_meta *f = &g_app.runtime.chrome.last_frame;
    if (f->pixels == (const uint8_t *)0 || f->width == 0u || f->height == 0u) {
        return;
    }

    uint32_t dst_w = win->surface.width;
    uint32_t src_w = f->width;
    uint32_t src_h = f->height;
    uint32_t copy_w = (src_w < dst_w) ? src_w : dst_w;
    /* 2026-05-02: a area do frame se estende ate URL_BAR pixels do
     * fundo da janela atual, em vez do antigo cap fixo de
     * BROWSER_APP_FRAME_H (480x360). Apos o bump do framebuffer cap
     * para 1024x768 e a propagacao do RESIZE, o frame pode chegar
     * com qualquer tamanho ate o MAX; precisamos respeitar a janela
     * VISIVEL (`win->surface.height - URL_BAR_H`), nao o cap MAX. */
    uint32_t avail_h = (win->surface.height > BROWSER_APP_URLBAR_H)
                       ? (win->surface.height - BROWSER_APP_URLBAR_H)
                       : 0u;
    uint32_t copy_h = (src_h < avail_h) ? src_h : avail_h;

    uint8_t *dst = (uint8_t *)win->surface.pixels;
    const uint8_t *src = f->pixels;
    for (uint32_t yy = 0u; yy < copy_h; ++yy) {
        uint8_t *drow = dst + (size_t)yy * (size_t)win->surface.pitch;
        const uint8_t *srow = src + (size_t)yy * (size_t)f->stride;
        for (uint32_t x = 0u; x < copy_w * 4u; ++x) {
            drow[x] = srow[x];
        }
    }

    /* Se a URL bar ainda nao foi pintada para este estado, repinta
     * tambem para refletir o novo status. Barato. */
    paint_urlbar();

    struct gui_rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = (int32_t)win->surface.width;
    rect.height = (int32_t)win->surface.height;
    compositor_invalidate_rect(win->id, &rect);
}

/* === Spawn / shutdown do engine ======================================== */

/* Spawna um novo engine, preenche a runtime, agenda a task. Retorna
 * 0 em sucesso, -1 em falha (spawn nao criou processo). */
static int spawn_engine_and_bind(struct browser_engine_spawn_result *out_r) {
    if (browser_engine_spawn(out_r) != BROWSER_ENGINE_SPAWN_OK) return -1;

    uint64_t now = apic_timer_ticks();
    chrome_runtime_init(&g_app.runtime,
                        out_r->request_pipe_id, out_r->response_pipe_id,
                        out_r->engine_pid, now);
    if (out_r->engine_main_thread) {
        user_task_arm_for_first_dispatch(out_r->engine_main_thread,
                                         out_r->engine_main_thread->context.rip,
                                         out_r->engine_main_thread->context.rsp);
        scheduler_add(out_r->engine_main_thread);
    }
    return 0;
}

/* Fecha pipes + mata o processo do engine. Idempotente. */
static void teardown_current_engine(void) {
    if (g_app.runtime.request_pipe_id >= 0) {
        pipe_close_write(g_app.runtime.request_pipe_id);
    }
    if (g_app.runtime.response_pipe_id >= 0) {
        pipe_close_read(g_app.runtime.response_pipe_id);
    }
    if (g_app.runtime.engine_pid != 0u) {
        process_kill(g_app.runtime.engine_pid, 9);
    }
}

/* === Callbacks da janela =============================================== */

static void browser_app_on_close(struct gui_window *win) {
    (void)win;
    browser_app_close();
}

/* 2026-05-02: chrome must repaint AND notify the engine after the
 * compositor reallocates the window surface in response to a user
 * resize drag.
 *
 * Steps:
 *  1. Compute the new viewport size = window_h - URL_BAR. The URL
 *     bar always sits at the bottom 24 px of the window so the
 *     frame area is everything above it. Clamp width/height to
 *     [1..BROWSER_FRAME_MAX_*]; the engine will clamp again
 *     defensively.
 *  2. Send BROWSER_IPC_RESIZE so the engine knows the new dims.
 *     It uses these on the next emit_real_frame / emit_stub_frame.
 *  3. Re-issue the current URL as a NAVIGATE so the user sees the
 *     content adapt immediately rather than waiting for them to
 *     press Enter. If the URL bar is empty (cleared by the user),
 *     skip the navigate.
 *  4. Repaint the URL bar + last frame so the visible state is
 *     coherent until the next FRAME arrives.
 *
 * Without (2)+(3), the engine kept rasterising at the original
 * 480x360 even after the user grew the window: the frame stayed in
 * the upper-left corner with bg padding around -- exactly the bug
 * the user reported as "conteudo nao se adapta ao tamanho". */
static void browser_app_on_resize(struct gui_window *win,
                                  uint32_t w, uint32_t h) {
    (void)win;
    if (!g_app.active) return;

    /* Compute the engine viewport from the new window size. */
    uint32_t vw = w;
    uint32_t vh = (h > BROWSER_APP_URLBAR_H) ? (h - BROWSER_APP_URLBAR_H) : 1u;
    if (vw == 0u) vw = 1u;
    if (vh == 0u) vh = 1u;
    if (vw > BROWSER_FRAME_MAX_W) vw = BROWSER_FRAME_MAX_W;
    if (vh > BROWSER_FRAME_MAX_H) vh = BROWSER_FRAME_MAX_H;

    /* Notify engine. Failure (-1) means the engine is dead or the
     * pipe is broken; we still repaint the URL bar locally so the
     * user can keep typing / press Esc to close. */
    (void)chrome_runtime_send_resize(&g_app.runtime,
                                      (uint16_t)vw, (uint16_t)vh);

    /* Trigger a re-render at the new size by re-navigating to the
     * current URL. The engine will emit a fresh FRAME at the new
     * viewport in response. F3.3g: mesma normalizacao do Enter para
     * que o resize no meio de uma navegacao HTTP nao vire 404.
     * Etapa F4 nav (2026-05-03): normalizacao agora vive em nav.c. */
    if (g_app.url.len > 0u) {
        uint16_t nlen = 0u;
        const char *nav_url = browser_app_nav_normalize(g_app.url.buf,
                                                         g_app.url.len,
                                                         &nlen);
        if (nav_url) {
            (void)chrome_runtime_send_navigate(&g_app.runtime,
                                                nav_url, nlen);
        } else {
            (void)chrome_runtime_send_navigate(&g_app.runtime,
                                                g_app.url.buf,
                                                g_app.url.len);
        }
    }

    paint_urlbar();
    copy_last_frame_to_window();
}

/* Etapa 3 seção b (2026-05-02): CLICK -> engine. Traduz (x,y) do
 * espaco da janela (onde o compositor entrega eventos) para o
 * espaco da viewport do engine (onde o layout/raster vivem).
 * Quatro casos:
 *   1. clique dentro da area do frame: envia CLICK ao engine com
 *      (x, y) relativos a (0,0) da viewport. O engine traduz para
 *      doc-space somando seu proprio scroll_offset e faz hit-test.
 *   2. clique dentro da URL bar: foca a bar (futuro: posicionar
 *      cursor). Por enquanto so re-pinta para garantir que o
 *      cursor apareca piscando.
 *   3. clique fora de ambos (entre frame e URL bar): ignora.
 *   4. button == 0 (move sem click): ignora (ha infra para hover
 *      em EVENT_CURSOR, mas esta slice nao emite hover).
 *
 * O compositor entrega (x, y) relativo ao topo-esquerdo da
 * janela, nao da superficie de conteudo. Como capyos nao tem
 * "non-client area" separada (o titulo ja e pintado fora da
 * surface), (x,y) ja vale como coordenada da surface e nao
 * precisamos subtrair offset de titulo. */
static void browser_app_on_mouse(struct gui_window *win,
                                  int32_t x, int32_t y, uint8_t btns) {
    (void)win;
    if (!g_app.active) return;
    /* btns == 0 em move puro (sem click). O compositor na versao
     * atual nao distingue move x click consistentemente; aqui
     * tratamos qualquer bit de botao como "press". LMB == bit 0. */
    if ((btns & 0x01u) == 0u) return;

    if (x < 0 || y < 0) return;
    struct gui_window *w = g_app.window;
    if (!w || !w->surface.pixels) return;

    uint32_t surf_w = w->surface.width;
    uint32_t surf_h = w->surface.height;
    if ((uint32_t)x >= surf_w || (uint32_t)y >= surf_h) return;

    uint32_t frame_h = (surf_h > BROWSER_APP_URLBAR_H)
                      ? (surf_h - BROWSER_APP_URLBAR_H) : 0u;
    if ((uint32_t)y < frame_h) {
        /* Click dentro da area do frame: repassa ao engine.
         * Etapa 3 seção c (2026-05-03): muda foco para a pagina. O
         * engine ja roda hit-test interno e atualiza
         * g_focused_input_idx; aqui apenas redirecionamos teclas
         * subsequentes via send_key em vez do URL bar editor. */
        bapp_mark('C');
        g_app.focus_target = (uint8_t)BROWSER_APP_FOCUS_PAGE;
        (void)chrome_runtime_send_click(&g_app.runtime,
                                         (uint16_t)x, (uint16_t)y,
                                         /*button=*/1u);
        return;
    }
    /* Etapa F4 toolbar (2026-05-03): click na URL bar; primeiro
     * checa hit-test em algum botao do toolbar (Back/Forward/
     * Reload/Home/Go). Se acertou, dispatch da acao + return. Senao
     * (clique no campo de texto da URL), apenas troca o foco
     * para a URL bar e re-pinta. */
    enum browser_app_toolbar_action ta =
        browser_app_toolbar_hit_test(x, y, surf_w, surf_h);
    if (ta != BROWSER_APP_TOOLBAR_NONE) {
        g_app.focus_target = (uint8_t)BROWSER_APP_FOCUS_URLBAR;
        dispatch_toolbar_action(ta);
        return;
    }
    /* Click na URL bar: foco volta para URL bar e re-pinta para
     * enfatizar. Etapa 3 seção c (2026-05-03): foco explicito permite
     * o usuario alternar URL bar <-> pagina sem F5/Esc. */
    g_app.focus_target = (uint8_t)BROWSER_APP_FOCUS_URLBAR;
    paint_urlbar();
}

/* Etapa 3 seção e (2026-05-02): SCROLL -> engine. `delta` positivo
 * = roda do mouse para baixo = conteudo sobe. O compositor entrega
 * o delta em "ticks de roda"; convertemos para pixels usando
 * `BROWSER_APP_SCROLL_STEP_PX` (48 px por tick, ~4 linhas de
 * texto). O engine faz o clamp final contra a altura do doc,
 * entao nao precisamos conhecer `content_height_px` aqui. */
#define BROWSER_APP_SCROLL_STEP_PX 48

/* Etapa F4 cursors (2026-05-03): I-beam sobre a regiao do texto
 * da URL bar; ARROW caso contrario. Hit-test em toolbar.c. */
static enum comp_cursor_kind browser_app_on_cursor(struct gui_window *win,
                                                    int32_t lx, int32_t ly) {
    (void)win;
    if (!g_app.window) return COMP_CURSOR_ARROW;
    if (browser_app_toolbar_is_url_text_region(lx, ly,
                                                g_app.window->surface.width,
                                                g_app.window->surface.height))
        return COMP_CURSOR_TEXT;
    return COMP_CURSOR_ARROW;
}

static void browser_app_on_scroll(struct gui_window *win, int32_t delta) {
    (void)win;
    if (!g_app.active) return;
    if (delta == 0) return;
    int32_t delta_px = delta * BROWSER_APP_SCROLL_STEP_PX;
    bapp_mark('S');
    (void)chrome_runtime_send_scroll(&g_app.runtime, delta_px);
}

/* Etapa F4 toolbar (2026-05-03): submete a URL atualmente no bar
 * ao engine via NAVIGATE. Normalizacao delegada ao modulo nav.c.
 * Usada tanto pelo Enter quanto pelo botao Go do toolbar. No-op
 * se o bar estiver vazio. Repinta a URL bar ao final para
 * refletir o `LOADING` que o chrome.status assume na proxima tick. */
static void navigate_url_bar(void) {
    if (g_app.url.len == 0u) return;
    uint16_t nlen = 0u;
    const char *nav_url = browser_app_nav_normalize(g_app.url.buf,
                                                     g_app.url.len, &nlen);
    if (!nav_url) {
        nav_url = g_app.url.buf;
        nlen = g_app.url.len;
    }
    bapp_mark('N');
    (void)chrome_runtime_send_navigate(&g_app.runtime, nav_url, nlen);
    paint_urlbar();
}

/* Etapa F4 toolbar (2026-05-03): dispatcher de cliques em botoes
 * do toolbar. Acoes mapeiam para chamadas IPC do chrome_runtime
 * (back/forward/reload), exceto Home/Go que sao locais (definir
 * URL bar e chamar navigate_url_bar). */
static void dispatch_toolbar_action(enum browser_app_toolbar_action a) {
    switch (a) {
        case BROWSER_APP_TOOLBAR_BACK:
            bapp_mark('B');
            (void)chrome_runtime_send_back(&g_app.runtime);
            return;
        case BROWSER_APP_TOOLBAR_FORWARD:
            bapp_mark('f');
            (void)chrome_runtime_send_forward(&g_app.runtime);
            return;
        case BROWSER_APP_TOOLBAR_RELOAD:
            bapp_mark('R');
            (void)chrome_runtime_send_reload(&g_app.runtime);
            return;
        case BROWSER_APP_TOOLBAR_HOME:
            bapp_mark('h');
            url_edit_set(&g_app.url, browser_app_homepage_initial_url());
            paint_urlbar();
            navigate_url_bar();
            return;
        case BROWSER_APP_TOOLBAR_GO:
            bapp_mark('G');
            navigate_url_bar();
            return;
        case BROWSER_APP_TOOLBAR_NONE:
        default:
            return;
    }
}

static void browser_app_on_key(struct gui_window *win,
                                uint32_t keycode, uint8_t mods) {
    (void)win;
    (void)mods;
    if (!g_app.active) return;

    /* Esc: fecha quando URL bar focada (preserva comportamento
     * legacy); volta foco para URL bar quando pagina focada
     * (Etapa 3 seção c, 2026-05-03). Permite ao usuario
     * "deselecionar" um input no doc sem fechar a janela. */
    if (keycode == 0x1Bu) {
        if (g_app.focus_target == (uint8_t)BROWSER_APP_FOCUS_PAGE) {
            g_app.focus_target = (uint8_t)BROWSER_APP_FOCUS_URLBAR;
            paint_urlbar();
            return;
        }
        browser_app_close();
        return;
    }

    /* Etapa 3 seção b-polish (2026-05-03): hotkeys de navegacao
     * SEMPRE atuam, independente do foco. Mesmo digitando em um
     * input do form, F5 ainda recarrega a pagina (perdendo o que
     * estava sendo digitado, comportamento esperado em browsers
     * reais). Posicionados ANTES da branch de foco para que
     * focus=PAGE nao engula F5/F6/F7. */
    if (keycode == KEY_F5) {
        bapp_mark('R');
        (void)chrome_runtime_send_reload(&g_app.runtime);
        return;
    }
    if (keycode == KEY_F6) {
        bapp_mark('B');
        (void)chrome_runtime_send_back(&g_app.runtime);
        return;
    }
    if (keycode == KEY_F7) {
        bapp_mark('f');
        (void)chrome_runtime_send_forward(&g_app.runtime);
        return;
    }

    /* Etapa 3 seção c (2026-05-03): se o foco e a pagina (usuario
     * clicou em um input do form), encaminha as teclas ao engine
     * via BROWSER_IPC_KEY. O engine roteia ao input focado, fazendo
     * o append/backspace/tab/enter. Hotkeys F5/F6/F7 + Esc ja foram
     * tratados acima, entao nao chegam aqui. */
    if (g_app.focus_target == (uint8_t)BROWSER_APP_FOCUS_PAGE) {
        bapp_mark('K');
        (void)chrome_runtime_send_key(&g_app.runtime, keycode, mods);
        return;
    }

    /* Enter submete a URL (navigate). F3.3g: normaliza antes de
     * enviar -- se o usuario digitou "example.com" sem esquema, o
     * engine recebe "http://example.com" e o stack HTTP consegue
     * resolver. Sem a normalizacao o engine retornaria 404 (URL
     * nao bate com `file://capyos/xyz` nem tem esquema HTTP).
     * Etapa F4 toolbar (2026-05-03): logica delegada a
     * navigate_url_bar() que e tambem usada pelo botao Go. */
    if (keycode == '\n' || keycode == '\r') {
        navigate_url_bar();
        return;
    }

    /* Backspace remove 1 byte antes do cursor. */
    if (keycode == 0x08u || keycode == 0x7Fu) {
        if (url_edit_backspace(&g_app.url)) paint_urlbar();
        return;
    }

    /* Setas e navegacao do cursor. */
    switch (keycode) {
        case KEY_LEFT:   if (url_edit_move_left(&g_app.url))  paint_urlbar(); return;
        case KEY_RIGHT:  if (url_edit_move_right(&g_app.url)) paint_urlbar(); return;
        case KEY_HOME:   if (url_edit_move_home(&g_app.url))  paint_urlbar(); return;
        case KEY_END:    if (url_edit_move_end(&g_app.url))   paint_urlbar(); return;
        case KEY_DELETE: if (url_edit_delete(&g_app.url))     paint_urlbar(); return;
        default: break;
    }

    /* Printavel ASCII entra no buffer. */
    if (keycode >= 32u && keycode < 127u) {
        if (url_edit_insert_char(&g_app.url, (char)keycode)) {
            paint_urlbar();
        }
    }
}

/* === Lifecycle ========================================================= */

void browser_app_open(void) {
    bapp_mark('O');
    if (g_app.active) {
        bapp_mark('o');
        if (g_app.window) {
            compositor_show_window(g_app.window_id);
            compositor_focus_window(g_app.window_id);
        }
        return;
    }

    install_pipe_ops_once();
    /* Etapa F4 homepage (2026-05-03): resolve a homepage cedo, antes
     * de spawnar o engine. Faz I/O no VFS uma vez por sessao do
     * browser; o resultado fica no modulo homepage. */
    browser_app_homepage_open();
    bapp_mark('1');

    struct browser_engine_spawn_result r;
    if (spawn_engine_and_bind(&r) != 0) {
        bapp_mark('!');
        return;
    }
    bapp_mark('2');

    g_app.window = compositor_create_window("CapyBrowser",
                                            /*x*/120, /*y*/80,
                                            BROWSER_APP_WIN_W,
                                            BROWSER_APP_WIN_H);
    if (!g_app.window) {
        bapp_mark('?');
        teardown_current_engine();
        return;
    }
    bapp_mark('3');
    g_app.window_id = g_app.window->id;
    g_app.window->on_close  = browser_app_on_close;
    g_app.window->on_key    = browser_app_on_key;
    g_app.window->on_resize = browser_app_on_resize;
    /* Etapa 3 seções b+e (2026-05-02): mouse/scroll entregam clicks
     * em links e rolam o conteudo via IPC CLICK/SCROLL. Sem esses
     * callbacks o compositor simplesmente descarta eventos de
     * mouse dentro da janela e o browser fica "read-only" pos-load. */
    g_app.window->on_mouse  = browser_app_on_mouse;
    g_app.window->on_scroll = browser_app_on_scroll;
    /* Etapa F4 cursors (2026-05-03): I-beam sobre a URL bar. */
    g_app.window->on_cursor_hint = browser_app_on_cursor;
    g_app.window->user_data = &g_app;
    compositor_show_window(g_app.window_id);
    compositor_focus_window(g_app.window_id);
    bapp_mark('4');

    /* Pre-preenche a URL bar com a home-page e pinta. */
    url_edit_set(&g_app.url, browser_app_homepage_initial_url());
    paint_urlbar();
    bapp_mark('5');

    g_app.active = 1u;
    g_app.frames_repainted = 0u;
    g_app.fetches_dispatched = 0u;
    g_app.engine_respawns = 0u;
    /* Etapa 3 seção c (2026-05-03): foco default na URL bar para
     * preservar o fluxo "abre browser, digita URL, Enter". So muda
     * para PAGE quando o usuario clica em um input dentro do doc. */
    g_app.focus_target = (uint8_t)BROWSER_APP_FOCUS_URLBAR;
    bapp_mark('K');
}

void browser_app_close(void) {
    if (!g_app.active) return;

    (void)chrome_runtime_send_shutdown(&g_app.runtime);
    teardown_current_engine();

    if (g_app.window) {
        uint32_t id = g_app.window_id;
        g_app.window = (struct gui_window *)0;
        g_app.window_id = 0u;
        compositor_destroy_window(id);
    }

    g_app.active = 0u;
    /* Etapa F4 homepage (2026-05-03): reset dos flags do modulo
     * homepage. Sem isso, fechar+reabrir o browser com a rede ainda
     * indisponivel nao re-disparia o fallback offline. */
    browser_app_homepage_close();
}

int browser_app_is_open(void) {
    return g_app.active ? 1 : 0;
}

/* === Tick ============================================================== */

/* F3.3f triagem 2026-05-01: respawn automatico foi removido para
 * evitar respawn-storm que congelava a tela quando a engine
 * crashava no first-dispatch. Politica atual: em EOF/protocol-err
 * ou watchdog kill, fechamos a janela e o usuario reabre pelo
 * menu manualmente. Quando a engine estiver estavel, podemos
 * re-introduzir respawn com backoff/limite. */

/* Etapa F4 nav (2026-05-03): detector de morte externa do engine
 * vive em src/apps/browser_app/nav.c. Aqui apenas chamamos com o
 * engine_pid atual. */

void browser_app_tick(uint64_t now_ticks) {
    if (!g_app.active) return;

    /* Politica de morte externa: se o engine foi morto por fora
     * (Task Manager, kill via shell, crash auto-reaped), fechamos
     * a janela imediatamente em vez de drenar o pipe morto. */
    if (browser_app_nav_engine_is_dead(g_app.runtime.engine_pid)) {
        bapp_mark('D');
        browser_app_close();
        return;
    }

    /* Etapa F4 homepage (2026-05-03): driver de homepage centralizado
     * em homepage.c. Manda o NAVIGATE inicial (na primeira tick) e,
     * se a homepage configurada falhar, dispara o fallback para
     * `file://capyos/wikipedia` UMA vez por sessao. Quando o
     * fallback acontece, atualizamos a URL bar localmente para
     * feedback rapido (o sync via UPDATE_STATUS chega logo depois). */
    int hp_rc = browser_app_homepage_tick(&g_app.runtime);
    if (hp_rc == BROWSER_APP_HOMEPAGE_TICK_SENT_INITIAL) {
        bapp_mark('H');
    } else if (hp_rc == BROWSER_APP_HOMEPAGE_TICK_SENT_FALLBACK) {
        bapp_mark('F');
        url_edit_set(&g_app.url, browser_app_homepage_fallback_url());
        paint_urlbar();
    }

    /* Etapa F4 cursors: sincroniza loading -> ampulheta sobre a janela. */
    if (g_app.window)
        g_app.window->loading =
            (g_app.runtime.chrome.status == BROWSER_CHROME_STATUS_LOADING);

    /* Drena eventos ate NO_DATA, com cap por tick para nao monopolizar
     * a CPU do desktop. 32 eventos/tick e suficiente para frame +
     * status + alguns logs; o resto fica para o proximo desktop frame
     * (~16ms). Sem cap, um engine bugado que emite eventos rapido
     * congela o compositor. */
    uint32_t events_this_tick = 0u;
    for (;;) {
        if (events_this_tick++ >= 32u) break;
        uint32_t actions = 0u;
        int st = chrome_runtime_poll_event(&g_app.runtime, now_ticks,
                                            &actions);
        if (st == CHROME_RUNTIME_POLL_NO_DATA) break;
        /* Etapa 5 hardening (2026-05-03): rate limiter da runtime
         * sinaliza que o engine excedeu o budget de eventos por tick.
         * Damos um break aqui para nao spinnar -- proximo
         * chrome_runtime_tick reseta a janela e voltamos a drenar. */
        if (st == CHROME_RUNTIME_POLL_RATE_LIMITED) {
            bapp_mark('R');
            break;
        }
        if (st == CHROME_RUNTIME_POLL_ENGINE_EOF ||
            st == CHROME_RUNTIME_POLL_PROTOCOL_ERR) {
            /* Engine morreu ou falou besteira. Fecha a janela em
             * vez de respawnar agressivamente (respawn-storm pode
             * congelar o desktop se a engine crasha consistentemente
             * no first-dispatch). O usuario re-abre pelo menu. */
            bapp_mark('X');
            browser_app_close();
            return;
        }
        if (actions & BROWSER_CHROME_ACTION_REPAINT_FRAME) {
            bapp_mark('F');
            copy_last_frame_to_window();
            g_app.frames_repainted++;
        }
        if (actions & BROWSER_CHROME_ACTION_FETCH_REQUESTED) {
            bapp_mark('Q');
            if (chrome_runtime_dispatch_pending_fetch(&g_app.runtime) > 0) {
                g_app.fetches_dispatched++;
                bapp_mark('q');
            }
        }
        if (actions & BROWSER_CHROME_ACTION_UPDATE_TITLE) {
            /* Etapa 3 seção b-polish (2026-05-03): engine emitiu
             * EVENT_TITLE com o `<title>` da pagina. O dispatcher do
             * chrome copiou para `chrome.current_title`. Propagamos
             * ao compositor para que a title bar reflita a pagina
             * ativa em vez de manter "CapyBrowser" para sempre.
             * `compositor_set_title` trunca silenciosamente se a
             * string for maior que o campo interno (64 bytes). */
            if (g_app.window_id != 0u) {
                bapp_mark('T');
                compositor_set_title(g_app.window_id,
                                     g_app.runtime.chrome.current_title);
            }
        }
        if (actions & BROWSER_CHROME_ACTION_UPDATE_STATUS) {
            /* Etapa 3 seção b (2026-05-02): o chrome pode ter mudado
             * de URL (ex. usuario clicou em link na pagina e o
             * engine disparou NAVIGATE para o href). Sincroniza a
             * URL bar com `chrome.current_url` quando houver
             * diferenca; senao, so re-pinta o status. Sem este
             * sync, o usuario via "Loading..." mas a bar continuava
             * com a URL antiga. */
            const char *cur = g_app.runtime.chrome.current_url;
            uint16_t cur_len = g_app.runtime.chrome.current_url_len;
            int differs = 0;
            if (cur_len != g_app.url.len) {
                differs = 1;
            } else {
                for (uint16_t i = 0; i < cur_len; ++i) {
                    if (cur[i] != g_app.url.buf[i]) { differs = 1; break; }
                }
            }
            if (differs && cur_len > 0u) {
                /* url_edit_set espera NUL-terminated; `current_url`
                 * e NUL-terminated pelo dispatcher do chrome. */
                url_edit_set(&g_app.url, cur);
            }
            paint_urlbar();
        }
        if (actions & BROWSER_CHROME_ACTION_LOG_FORWARD) {
            /* Etapa 2.f: encaminha mensagens de log do engine para
             * debugcon. Toda a logica (sanitizacao + cap defensivo
             * contra last_log_msg_len corrompido) esta em
             * `browser_app_log_forward`. Sem isso, qualquer crash ou
             * estado anomalo do engine fica invisivel ao operador. */
            browser_app_log_forward(g_app.runtime.chrome.last_log_msg,
                                    g_app.runtime.chrome.last_log_msg_len);
        }
    }

    /* Watchdog. Se pedir kill ou houver broken pipe, fecha. Mesma
     * politica do drain: nao tenta respawn automatico para evitar
     * loop infinito quando a engine crasha consistentemente. */
    int tr = chrome_runtime_tick(&g_app.runtime, now_ticks);
    if (tr == 1 || tr == -1) {
        bapp_mark('W');
        browser_app_close();
    }
}
