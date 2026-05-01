#ifndef APPS_BROWSER_WATCHDOG_H
#define APPS_BROWSER_WATCHDOG_H

/*
 * Browser engine watchdog (F3.3d).
 *
 * Maquina de estado pura que decide quando enviar PING ao engine
 * (capybrowser) e quando matar/reiniciar o processo. Implementacao
 * deliberadamente sem syscalls: o caller injeta `now_ticks` e
 * consome decisoes via `should_send_ping()` / `should_kill()`.
 *
 * Politica:
 *   - PING_INTERVAL_TICKS  : intervalo entre PINGs consecutivos.
 *   - PONG_TIMEOUT_TICKS   : tempo maximo entre PING enviado e PONG
 *                            recebido para considerar que o engine
 *                            ainda esta vivo.
 *   - MAX_MISSED_PONGS     : quantos PINGs consecutivos podem falhar
 *                            antes do watchdog declarar travamento.
 *
 * Uso esperado pelo chrome:
 *   1. browser_watchdog_init(&w);
 *   2. A cada tick do compositor:
 *        browser_watchdog_tick(&w, now);
 *        if (browser_watchdog_should_send_ping(&w, now)) {
 *            send_ping(nonce); browser_watchdog_record_ping(&w, nonce, now);
 *        }
 *        if (browser_watchdog_should_kill(&w, now)) {
 *            process_kill(engine_pid, SIGKILL); spawn_engine();
 *            browser_watchdog_record_restart(&w, now);
 *        }
 *   3. No EVENT_PONG: browser_watchdog_record_pong(&w, nonce, now);
 *
 * Decisoes sao idempotentes: chamar `should_send_ping` varias vezes
 * sem record_ping nao acumula PINGs em voo. record_ping atualiza o
 * nonce em voo e o deadline.
 */

#include <stdint.h>

/* Defaults: 5s entre PINGs, 1s para PONG, 2 PINGs perdidos = kill.
 * Em ticks @ 100Hz (apic_timer_ticks). */
#define BROWSER_WATCHDOG_PING_INTERVAL_TICKS  500u
#define BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS   100u
#define BROWSER_WATCHDOG_MAX_MISSED_PONGS     2u

enum browser_watchdog_state {
    BROWSER_WATCHDOG_IDLE             = 0, /* nunca enviou ping ou pong recebido */
    BROWSER_WATCHDOG_PING_IN_FLIGHT   = 1, /* ping enviado, aguardando pong */
    BROWSER_WATCHDOG_KILL_REQUESTED   = 2  /* watchdog pediu kill+restart */
};

struct browser_watchdog {
    enum browser_watchdog_state state;
    uint32_t in_flight_nonce;     /* nonce do ultimo PING enviado */
    uint32_t next_nonce;          /* monotonico, gerado em record_ping */
    uint64_t last_pong_tick;      /* ultimo PONG valido recebido */
    uint64_t last_ping_tick;      /* ultimo PING enviado */
    uint32_t consecutive_misses;  /* PINGs consecutivos sem PONG */
    uint32_t total_kills;         /* contador de kills disparados (telemetria) */
};

/* Inicializa estado do watchdog. now_ticks e o tempo "agora" no momento
 * da inicializacao (geralmente o spawn do engine). */
void browser_watchdog_init(struct browser_watchdog *w, uint64_t now_ticks);

/* Retorna 1 se o chrome deve enviar um PING agora. Idempotente.
 * O chrome tipicamente chama isto a cada frame; vai retornar 1 apenas
 * quando o intervalo desde o ultimo PING (ou desde init) ultrapassou
 * BROWSER_WATCHDOG_PING_INTERVAL_TICKS E nao ha PING em voo. */
int browser_watchdog_should_send_ping(const struct browser_watchdog *w,
                                      uint64_t now_ticks);

/* Marca que o chrome enviou um PING com `nonce` em `now_ticks`.
 * Atualiza estado para PING_IN_FLIGHT. O caller eh responsavel por
 * gerar o nonce; sugestao: usar browser_watchdog_alloc_nonce(). */
void browser_watchdog_record_ping(struct browser_watchdog *w,
                                  uint32_t nonce,
                                  uint64_t now_ticks);

/* Aloca um nonce monotonico unico. Pode ser usado pelo chrome para
 * obter um nonce sem manter contador proprio. */
uint32_t browser_watchdog_alloc_nonce(struct browser_watchdog *w);

/* Notifica que o chrome recebeu um PONG com `nonce`. Se for o nonce
 * do PING em voo, considera o engine vivo, reseta consecutive_misses
 * e volta para IDLE. Se nao, ignora silenciosamente (PONG atrasado
 * de um PING anterior; o watchdog ja avancou). */
void browser_watchdog_record_pong(struct browser_watchdog *w,
                                  uint32_t nonce,
                                  uint64_t now_ticks);

/* Avanca o tempo: se ha PING em voo e o deadline de PONG passou,
 * incrementa consecutive_misses; se ultrapassar MAX_MISSED_PONGS,
 * entra em KILL_REQUESTED. Idempotente; chame a cada frame. */
void browser_watchdog_tick(struct browser_watchdog *w, uint64_t now_ticks);

/* Retorna 1 se o watchdog quer que o chrome mate e reinicie o engine.
 * Idempotente. */
int browser_watchdog_should_kill(const struct browser_watchdog *w);

/* Reseta estado apos kill+restart bem-sucedido. Incrementa total_kills. */
void browser_watchdog_record_restart(struct browser_watchdog *w,
                                     uint64_t now_ticks);

/* Telemetria: numero acumulado de restarts. */
uint32_t browser_watchdog_total_kills(const struct browser_watchdog *w);

#endif /* APPS_BROWSER_WATCHDOG_H */
