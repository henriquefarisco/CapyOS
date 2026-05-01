/*
 * src/apps/browser_chrome/watchdog.c (F3.3d)
 *
 * Implementacao da maquina de estado de watchdog do browser engine.
 * Pura: sem syscalls, sem dependencia de kernel/userland alem de
 * <stdint.h>. Toda decisao de tempo e tomada via `now_ticks` injetado
 * pelo caller (chrome ou test harness).
 *
 * Contrato em include/apps/browser_watchdog.h.
 */

#include "apps/browser_watchdog.h"

void browser_watchdog_init(struct browser_watchdog *w, uint64_t now_ticks) {
    if (!w) return;
    w->state = BROWSER_WATCHDOG_IDLE;
    w->in_flight_nonce = 0u;
    w->next_nonce = 1u; /* nonce 0 reservado para "nenhum" */
    w->last_pong_tick = now_ticks;
    w->last_ping_tick = now_ticks;
    w->consecutive_misses = 0u;
    w->total_kills = 0u;
}

uint32_t browser_watchdog_alloc_nonce(struct browser_watchdog *w) {
    if (!w) return 0u;
    uint32_t n = w->next_nonce++;
    if (w->next_nonce == 0u) {
        /* Wrap: pula 0 (sentinela). */
        w->next_nonce = 1u;
    }
    return n;
}

int browser_watchdog_should_send_ping(const struct browser_watchdog *w,
                                      uint64_t now_ticks) {
    if (!w) return 0;
    if (w->state == BROWSER_WATCHDOG_KILL_REQUESTED) return 0;
    if (w->state == BROWSER_WATCHDOG_PING_IN_FLIGHT) return 0;

    uint64_t elapsed = now_ticks - w->last_ping_tick;
    return elapsed >= (uint64_t)BROWSER_WATCHDOG_PING_INTERVAL_TICKS;
}

void browser_watchdog_record_ping(struct browser_watchdog *w,
                                  uint32_t nonce,
                                  uint64_t now_ticks) {
    if (!w) return;
    if (w->state == BROWSER_WATCHDOG_KILL_REQUESTED) return;
    w->state = BROWSER_WATCHDOG_PING_IN_FLIGHT;
    w->in_flight_nonce = nonce;
    w->last_ping_tick = now_ticks;
}

void browser_watchdog_record_pong(struct browser_watchdog *w,
                                  uint32_t nonce,
                                  uint64_t now_ticks) {
    if (!w) return;
    if (w->state == BROWSER_WATCHDOG_KILL_REQUESTED) return;
    if (w->state != BROWSER_WATCHDOG_PING_IN_FLIGHT) {
        /* Pong sem ping em voo: ignora (poderia ser pong de ping
         * anterior que ja expirou). */
        return;
    }
    if (nonce != w->in_flight_nonce) {
        /* Nonce nao bate: pong atrasado, ignora. */
        return;
    }
    /* Pong valido: engine vivo. */
    w->state = BROWSER_WATCHDOG_IDLE;
    w->in_flight_nonce = 0u;
    w->last_pong_tick = now_ticks;
    w->consecutive_misses = 0u;
}

void browser_watchdog_tick(struct browser_watchdog *w, uint64_t now_ticks) {
    if (!w) return;
    if (w->state != BROWSER_WATCHDOG_PING_IN_FLIGHT) return;

    uint64_t elapsed = now_ticks - w->last_ping_tick;
    if (elapsed < (uint64_t)BROWSER_WATCHDOG_PONG_TIMEOUT_TICKS) {
        return;
    }
    /* Pong expirou. Conta como uma falha e libera o slot para o
     * proximo PING ser enviado (a menos que a politica diga matar). */
    w->consecutive_misses++;
    w->in_flight_nonce = 0u;
    if (w->consecutive_misses >= BROWSER_WATCHDOG_MAX_MISSED_PONGS) {
        w->state = BROWSER_WATCHDOG_KILL_REQUESTED;
    } else {
        /* Volta para idle para que should_send_ping possa autorizar
         * o proximo PING quando o intervalo passar. Forcamos
         * last_ping_tick para que o intervalo seja contado a partir
         * deste timeout (caso contrario o proximo PING dispararia
         * imediato e o sistema spamearia o engine que ja esta lento). */
        w->state = BROWSER_WATCHDOG_IDLE;
        w->last_ping_tick = now_ticks;
    }
}

int browser_watchdog_should_kill(const struct browser_watchdog *w) {
    if (!w) return 0;
    return w->state == BROWSER_WATCHDOG_KILL_REQUESTED;
}

void browser_watchdog_record_restart(struct browser_watchdog *w,
                                     uint64_t now_ticks) {
    if (!w) return;
    w->total_kills++;
    w->state = BROWSER_WATCHDOG_IDLE;
    w->in_flight_nonce = 0u;
    w->last_pong_tick = now_ticks;
    w->last_ping_tick = now_ticks;
    w->consecutive_misses = 0u;
}

uint32_t browser_watchdog_total_kills(const struct browser_watchdog *w) {
    if (!w) return 0u;
    return w->total_kills;
}
