#ifndef APPS_BROWSER_CHROME_RUNTIME_H
#define APPS_BROWSER_CHROME_RUNTIME_H

/*
 * Browser chrome runtime (F3.3d).
 *
 * Camada de orquestracao entre o dispatcher do chrome (browser_chrome)
 * e os pipes do kernel que ligam o chrome ao engine ring 3
 * (capybrowser). Mantem-se livre de inline asm e syscalls diretos:
 * `chrome_runtime_set_pipe_ops()` injeta `pipe_write`/`pipe_read` na
 * runtime, o que permite testar a logica em host sem o kernel real.
 *
 * Arquitetura:
 *
 *   chrome (kernel-side compositor)
 *     |
 *     |  request_pipe (chrome write -> engine read)
 *     v
 *   capybrowser (ring 3, fd 0 = request read, fd 1 = response write)
 *     |
 *     |  response_pipe (engine write -> chrome read)
 *     v
 *   chrome
 *
 * O chrome possui dois pipe ids opacos. As funcoes de envio chamam
 * o write_fn injetado; as funcoes de poll chamam o read_fn injetado;
 * nenhuma referencia direta a `pipe.h` aparece no nucleo da runtime.
 *
 * `pipe_read` retorna -1 quando vazio sem EOF (would-block); 0 em EOF;
 * >0 com bytes lidos. A runtime trata cada caso explicitamente.
 */

#include "apps/browser_chrome.h"
#include "apps/browser_ipc.h"
#include <stddef.h>
#include <stdint.h>

/* Funcoes injetaveis: assinatura igual ao kernel (`pipe.h`). */
typedef int (*chrome_runtime_pipe_write_fn)(int pipe_id,
                                            const void *buf,
                                            size_t len);
typedef int (*chrome_runtime_pipe_read_fn)(int pipe_id,
                                           void *buf,
                                           size_t len);

/* Codigos de retorno dos pollers. */
enum chrome_runtime_poll_status {
    CHROME_RUNTIME_POLL_NO_DATA       = 0, /* would-block; nada feito */
    CHROME_RUNTIME_POLL_EVENT_HANDLED = 1, /* um evento decodificado e despachado */
    CHROME_RUNTIME_POLL_ENGINE_EOF    = 2, /* read retornou 0; engine saiu */
    CHROME_RUNTIME_POLL_PROTOCOL_ERR  = 3, /* header invalido / payload inconsistente */
    CHROME_RUNTIME_POLL_RATE_LIMITED  = 4  /* engine excedeu burst budget; retry next tick */
};

/* Etapa 5 hardening (2026-05-03): rate limiting de eventos vindos do
 * engine. Cada chamada a chrome_runtime_tick reseta o contador; entre
 * ticks (tipicamente 10 ms @ 100Hz), poll_event admite ate
 * CHROME_RUNTIME_INCOMING_RATE_MAX eventos. Excedeu? Retorna
 * RATE_LIMITED ate proximo tick. Defesa contra engine comprometido
 * spammando frames para travar o dispatch loop do chrome.
 *
 * Default 64 eventos por tick = 6400 eventos/segundo de teto. Em uso
 * normal o engine nao chega perto disto (typicamente 1 EVENT_FRAME
 * por navegacao + alguns FETCH_REQUEST). */
#define CHROME_RUNTIME_INCOMING_RATE_MAX  64u

/* Etapa 5 hardening (2026-05-03): URL whitelist policy (opt-in).
 * Quando instalada, `chrome_runtime_send_navigate` chama o callback
 * antes de escrever no pipe; retorno 0 (deny) faz a navegacao
 * fracassar com -1 e incrementa `total_url_blocked`. Sem policy
 * instalada (default), todos os URLs sao permitidos.
 *
 * Useful for kiosk-mode browsing, parental control, or testing.
 * O contrato e simples (ponteiro + tamanho); a politica fica no
 * caller (browser_app pode ler de config, ou um teste pode hardcode). */
typedef int (*chrome_runtime_url_policy_fn)(const char *url, uint32_t url_len);
void chrome_runtime_set_url_policy(chrome_runtime_url_policy_fn fn);

#define CHROME_RUNTIME_REQUEST_BUF_MAX  (BROWSER_IPC_HEADER_SIZE + BROWSER_CHROME_URL_MAX + 16u)
/* F3.3f: o engine ring 3 (slice 4-final) emite EVENT_FRAME com
 * 480x360 BGRA (= 675 KiB) + frame header (12 B) por padrao no
 * 2026-05-02 (rev pos primeiro UX feedback: janela 192x128 era
 * pequena demais para mostrar conteudo legivel). Dimensionamos
 * o scratch para 768 KiB para acomodar o payload com folga,
 * sem chegar perto do BROWSER_IPC_MAX_PAYLOAD (1 MiB). Reduzir
 * aqui obrigaria a fragmentar EVENT_FRAME em tiles (slice
 * futura). */
/* 2026-05-02: bumped to fit BROWSER_FRAME_MAX (1024x768 BGRA + 12 B
 * header = 3 MiB + 12 B). Round up to 4 MiB for headroom and easy
 * page alignment. The struct chrome_runtime now reserves
 * 2 * 4 MiB = 8 MiB (event_scratch + last_frame_storage) per
 * instance; only one instance lives in `browser_app.c::g_app` so
 * the kernel-side cost is bounded. */
#define CHROME_RUNTIME_EVENT_BUF_MAX    (4u * 1024u * 1024u)

/* F3.3f triagem 2026-05-02: o dispatcher do chrome aliasa
 * `last_frame.pixels` dentro de `event_scratch` (zero-copy). Como o
 * scratch e reaproveitado entre polls, qualquer evento subsequente
 * (STATUS, LOG, etc.) corrompe o frame. A runtime copia os pixels
 * recem-decodificados para `last_frame_storage` IMEDIATAMENTE apos
 * o dispatch e re-aponta `last_frame.pixels` para esse buffer
 * dedicado. O contrato fica: "pixels validos ate o proximo FRAME
 * substituir", em vez de "ate o proximo evento de qualquer tipo".
 * O tamanho corresponde ao maior payload de pixels possivel dentro
 * de um evento (event_scratch sans header de 12 bytes). */
#define CHROME_RUNTIME_FRAME_STORAGE_MAX (CHROME_RUNTIME_EVENT_BUF_MAX - 12u)

/* Etapa 5 hardening (2026-05-03): inclui ring buffer dedicado de
 * audit log; ver browser_chrome_audit.h para o contrato. */
#include "apps/browser_chrome_audit.h"

struct chrome_runtime {
    struct browser_chrome chrome;

    /* Identificadores opacos dos pipes (passados ao write_fn/read_fn). */
    int request_pipe_id;   /* chrome write -> engine read */
    int response_pipe_id;  /* engine write -> chrome read */

    /* Pid do engine. 0 enquanto nenhum engine ativo. */
    uint32_t engine_pid;
    int engine_alive;       /* 1 enquanto chrome acredita que engine esta vivo */

    /* Telemetria */
    uint32_t total_requests_sent;
    uint32_t total_events_polled;
    uint32_t total_pongs_received;
    uint32_t total_engine_eofs;

    /* Buffer scratch para um unico evento; reaproveitado entre polls. */
    uint8_t event_scratch[CHROME_RUNTIME_EVENT_BUF_MAX];

    /* Storage dedicado para o ultimo frame decodificado. Quebra o
     * alias com `event_scratch`: ver nota acima na declaracao de
     * CHROME_RUNTIME_FRAME_STORAGE_MAX. */
    uint8_t last_frame_storage[CHROME_RUNTIME_FRAME_STORAGE_MAX];

    /* Telemetria de frames: `total_frames_persisted` conta cada
     * REPAINT_FRAME que copiou pixels com sucesso para o storage;
     * `total_frames_dropped` conta os que nao couberam (pixel_bytes >
     * sizeof storage) e tiveram `last_frame.pixels` zerado para
     * evitar mostrar lixo. */
    uint32_t total_frames_persisted;
    uint32_t total_frames_dropped;

    /* Etapa 5 hardening (2026-05-03): rate limiting incoming events.
     *   - incoming_in_window: eventos admitidos no tick atual.
     *   - total_incoming_drops: contador acumulado de RATE_LIMITED. */
    uint32_t incoming_in_window;
    uint32_t total_incoming_drops;

    /* Etapa 5 hardening (2026-05-03): URL whitelist enforcement.
     * total_url_blocked conta NAVIGATEs rejeitados pela policy. */
    uint32_t total_url_blocked;

    /* Etapa 5 hardening observability (2026-05-03): bytes acumulados
     * recebidos do engine (header + payload por evento admitido). Util
     * para detectar engine vazando memoria via spam de eventos. Sem
     * enforcement (audit log so). */
    uint64_t total_event_bytes_received;

    /* Etapa 5 hardening (2026-05-03): ring buffer de audit log. Ver
     * browser_chrome_audit.h. Cada send/poll significativo grava uma
     * entrada (NAV, RATE_DROP, POLICY_DENY, ENGINE_EOF, PROTOCOL,
     * FETCH). Tamanho fixo 32 entradas. */
    struct chrome_audit_state audit;
};

/* Injeta os ponteiros de funcao usados por send/poll. Em producao
 * sao `pipe_write` e `pipe_read` do kernel; em testes apontam para
 * mocks que escrevem em buffers em memoria. NULL desativa I/O e
 * causa send/poll a falharem com graca. */
void chrome_runtime_set_pipe_ops(chrome_runtime_pipe_write_fn w,
                                 chrome_runtime_pipe_read_fn r);

/* F3.3f: injeta uma funcao `yield` que a runtime chama quando
 * bate em would-block NO MEIO de um payload IPC ja iniciado.
 * Sem yield, o read parcial vira protocol-error (o que e o que
 * os testes host querem, porque la o mock de pipe ou tem tudo
 * ou EOF). No kernel, a yield aponta para `task_yield()` e
 * permite transportar payloads maiores que `PIPE_BUF_SIZE`
 * (ex.: EVENT_FRAME de 96 KiB por 4 KiB de pipe). Um NULL
 * preserva o comportamento antigo (fail-fast). */
typedef void (*chrome_runtime_yield_fn)(void);
void chrome_runtime_set_yield_op(chrome_runtime_yield_fn y);

/* Inicializa a runtime apos o chrome ter spawnado o engine.
 * `request_pipe_id` e o pipe-id que o chrome USA para ESCREVER
 * comandos (sera lido pelo engine via fd 0). `response_pipe_id` e
 * o pipe-id que o chrome USA para LER eventos do engine (escritos
 * pelo engine via fd 1). `engine_pid` e o pid retornado por
 * `process_create()`/spawn. `now_ticks` alimenta o watchdog. */
void chrome_runtime_init(struct chrome_runtime *rt,
                         int request_pipe_id,
                         int response_pipe_id,
                         uint32_t engine_pid,
                         uint64_t now_ticks);

/* Constroi e envia uma request NAVIGATE para o engine. Retorna 0 em
 * sucesso, -1 em erro (write_fn nao injetado, broken pipe ou URL
 * grande demais). Atualiza estado do chrome via
 * `browser_chrome_record_navigate_sent()`. */
int chrome_runtime_send_navigate(struct chrome_runtime *rt,
                                 const char *url,
                                 size_t url_len);

/* Envia CANCEL/SHUTDOWN/PING. PING usa nonce alocado pelo watchdog
 * e marca-o como em voo. Retorna 0 ok / -1 erro. */
int chrome_runtime_send_cancel(struct chrome_runtime *rt);
int chrome_runtime_send_shutdown(struct chrome_runtime *rt);
int chrome_runtime_send_ping(struct chrome_runtime *rt, uint64_t now_ticks);

/* 2026-05-02: notifica o engine que a viewport mudou de tamanho.
 * O engine clampa para [1..BROWSER_FRAME_MAX_W] x [1..BROWSER_FRAME_MAX_H]
 * e usa as novas dimensoes na proxima rasterizacao. Sem este send,
 * apos o usuario redimensionar a janela do browser pelo grip do
 * compositor, o engine continuaria emitindo frames no tamanho
 * default (480x360) e o conteudo nao se adaptaria a janela maior.
 *
 * Payload IPC: 4 bytes BE = width:u16 + height:u16. Retorna 0 ok / -1
 * erro (broken pipe, valor 0 em algum eixo). */
int chrome_runtime_send_resize(struct chrome_runtime *rt,
                               uint16_t width, uint16_t height);

/* Etapa 3 seção b (2026-05-02): envia CLICK ao engine. As coordenadas
 * sao relativas a viewport do frame (nao da janela) -- o chamador ja
 * descontou a URL bar e outros enfeites do chrome. O engine faz
 * hit-testing contra o layout atual da pagina e, se (x,y) cai em
 * um elemento com `href`, dispara uma nova NAVIGATE para essa URL.
 * Botoes: 1 = LMB, 2 = RMB, 3 = MMB.
 *
 * Payload IPC: 5 bytes BE = x:u16 + y:u16 + button:u8. Retorna 0
 * ok / -1 erro (pipe quebrado ou rt == NULL). */
int chrome_runtime_send_click(struct chrome_runtime *rt,
                              uint16_t x, uint16_t y, uint8_t button);

/* Etapa 3 seção e (2026-05-02): envia SCROLL delta ao engine. O
 * engine acumula o delta em um offset interno e re-rasteriza o
 * documento atual com essa translacao aplicada. Positive delta
 * rola para baixo (conteudo sobe), negative rola para cima.
 *
 * Payload IPC: 4 bytes BE = delta_y:i32. Retorna 0 ok / -1 erro. */
int chrome_runtime_send_scroll(struct chrome_runtime *rt,
                               int32_t delta_y);

/* Etapa 3 seção c (2026-05-03): envia KEY ao engine. O engine
 * roteia a tecla ao input atualmente focado (g_focused_input_idx);
 * sem foco, ignora silenciosamente. Comportamento por keycode:
 *   - 0x08 / 0x7F (BS / DEL): remove o ultimo caractere
 *   - 0x09 (TAB): avanca foco para o proximo INPUT
 *   - 0x0D / 0x0A (Enter): submit do form atual
 *   - 32..126 (printable): append ao value
 *   - outros: ignorados (browser_app trata hotkeys F5/F6/F7/Esc)
 *
 * `mods` reservado para Shift/Ctrl/Alt; o engine atual nao usa.
 *
 * Payload IPC: 5 bytes BE = keycode:u32 + mods:u8. Retorna 0 ok /
 * -1 erro (rt == NULL ou pipe quebrado). */
int chrome_runtime_send_key(struct chrome_runtime *rt,
                            uint32_t keycode, uint8_t mods);

/* Etapa 3 seção b (2026-05-02): pede ao engine para re-navegar a
 * URL atual. Util para botao "reload" e para force-refresh apos
 * erro transient de rede. Engine guarda a ultima URL navegada em
 * estado interno; sem ela (primeira tick antes de NAVIGATE), este
 * helper vira no-op no lado engine.
 *
 * Payload IPC: vazio. Retorna 0 ok / -1 erro. */
int chrome_runtime_send_reload(struct chrome_runtime *rt);

/* Etapa 3 seção b-polish (2026-05-03): navega para a entrada
 * anterior/seguinte no historico mantido pelo engine ring 3. No-op
 * silencioso no engine se nao houver historico (inicio/fim da
 * pilha). Do lado chrome, este helper apenas emite o frame IPC;
 * toda a logica de "qual URL" vive no engine que guarda o ring
 * em `.bss`. Chrome nao precisa saber onde o usuario esta no
 * historico -- ele so envia BACK/FORWARD e recebe NAV_STARTED com
 * a URL destino.
 *
 * Payload IPC: vazio. Retorna 0 ok / -1 erro. */
int chrome_runtime_send_back(struct chrome_runtime *rt);
int chrome_runtime_send_forward(struct chrome_runtime *rt);

/* F3.3c slice 5c: drain the pending fetch from the chrome (set by
 * EVENT_FETCH_REQUEST), resolve it via the built-in page table
 * (`browser_chrome_resolve_local`), and send the answer back to
 * the engine as a FETCH_RESPONSE frame.
 *
 * Returns:
 *    1 if a request was drained and the response was queued ok;
 *    0 if no fetch is pending (no-op);
 *   -1 if a request was drained but the write failed (engine
 *      treated as dead, `rt->engine_alive` cleared).
 *
 * The resolver is canned and never fails -- unknown URLs map to
 * a 404 result, which the chrome still forwards as a valid
 * FETCH_RESPONSE so the engine can render an error page. */
int chrome_runtime_dispatch_pending_fetch(struct chrome_runtime *rt);

/* Le um unico frame IPC do engine (se houver). Retorna um codigo de
 * `chrome_runtime_poll_status`. Quando retorna EVENT_HANDLED,
 * `*out_actions` recebe a mascara devolvida pelo dispatcher
 * (REPAINT_FRAME, UPDATE_TITLE, etc.). */
int chrome_runtime_poll_event(struct chrome_runtime *rt,
                              uint64_t now_ticks,
                              uint32_t *out_actions);

/* Avanca a logica de tempo do chrome:
 *  - chama `browser_watchdog_tick`
 *  - se watchdog pede PING, envia
 *  - se watchdog pede kill, sinaliza para o caller (retorna 1)
 *
 * Retorna:
 *    0  nada a fazer alem do que ja fez
 *    1  caller deve matar o engine via `process_kill(engine_pid, 9)`
 *       e respawnar; depois chamar `chrome_runtime_record_restart`.
 *   -1  erro de I/O ao tentar enviar ping (broken pipe). Indica que
 *       o engine provavelmente ja morreu; caller deve respawnar. */
int chrome_runtime_tick(struct chrome_runtime *rt, uint64_t now_ticks);

/* Notifica a runtime que o engine foi morto/saiu e novos pipe ids +
 * pid foram alocados. Reinicia watchdog e flag engine_alive. Estado
 * de navegacao (current_url, current_title) e preservado. */
void chrome_runtime_record_restart(struct chrome_runtime *rt,
                                   int new_request_pipe_id,
                                   int new_response_pipe_id,
                                   uint32_t new_engine_pid,
                                   uint64_t now_ticks);

#endif /* APPS_BROWSER_CHROME_RUNTIME_H */
