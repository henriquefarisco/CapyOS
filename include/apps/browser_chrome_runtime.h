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
    CHROME_RUNTIME_POLL_PROTOCOL_ERR  = 3  /* header invalido / payload inconsistente */
};

#define CHROME_RUNTIME_REQUEST_BUF_MAX  (BROWSER_IPC_HEADER_SIZE + BROWSER_CHROME_URL_MAX + 16u)
#define CHROME_RUNTIME_EVENT_BUF_MAX    (1024u + BROWSER_IPC_HEADER_SIZE) /* 1 KiB de payload + header */

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
};

/* Injeta os ponteiros de funcao usados por send/poll. Em producao
 * sao `pipe_write` e `pipe_read` do kernel; em testes apontam para
 * mocks que escrevem em buffers em memoria. NULL desativa I/O e
 * causa send/poll a falharem com graca. */
void chrome_runtime_set_pipe_ops(chrome_runtime_pipe_write_fn w,
                                 chrome_runtime_pipe_read_fn r);

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
