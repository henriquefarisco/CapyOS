/*
 * src/kernel/browser_smoke.c (F3.3e scaffolding)
 *
 * Implementacao do smoke kernel-side. Vide
 * include/kernel/browser_smoke.h.
 *
 * UNIT_TEST: este TU nao e compilado no binario de testes host
 * (depende de scheduler/task/pipe reais). A logica testavel ja
 * esta totalmente coberta por test_browser_chrome_runtime e
 * test_browser_e2e (108 asserts entre os dois).
 */

#include "kernel/browser_smoke.h"
#include "kernel/browser_engine_spawn.h"
#include "apps/browser_chrome_runtime.h"
#include "kernel/pipe.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "kernel/user_task_init.h"
#include <stddef.h>
#include <stdint.h>

/* Helper: dbgcon_putc esta declarado como inline em
 * include/arch/x86_64/kernel_main_internal.h. Replicamos aqui via
 * outb 0xE9 para nao puxar o internal header (que tem deps de
 * arch). */
static inline void bs_putc(uint8_t c) {
    __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0xE9));
}

static void bs_write(const char *s) {
    if (!s) return;
    while (*s) bs_putc((uint8_t)*s++);
}

static void bs_write_dec(uint32_t v) {
    char buf[12];
    int i = (int)sizeof(buf);
    buf[--i] = '\0';
    if (v == 0u) {
        buf[--i] = '0';
    } else {
        while (v > 0u && i > 0) {
            buf[--i] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    bs_write(&buf[i]);
}

/* === State =============================================================== */

static struct chrome_runtime g_smoke_rt;

/* === Poller task =========================================================
 *
 * Roda em modo kernel. Le eventos do engine, dispara comandos e
 * imprime markers. O scheduler preemptivo intercala com o engine
 * ring 3 a cada APIC tick. */
static void browser_smoke_poller_task(void *arg) {
    struct chrome_runtime *rt = (struct chrome_runtime *)arg;

    bs_write("[browser-smoke] poller-start\n");

    if (chrome_runtime_send_navigate(rt, "test://smoke", 12) != 0) {
        bs_write("[browser-smoke] FAIL navigate-send\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    bs_write("[browser-smoke] navigate-sent\n");

    int got_started = 0;
    int got_frame = 0;
    int got_ready = 0;
    int got_pong = 0;
    int sent_ping = 0;
    int sent_shutdown = 0;
    uint64_t tick = 0u;
    /* Limite: cada iteracao yielda; sob preemptive scheduler a 100Hz
     * isso da ~10s antes de timeout, suficiente para o stub que
     * responde NAVIGATE em <1ms. */
    uint32_t idle_iters = 0u;
    const uint32_t IDLE_LIMIT = 100000u;

    for (;;) {
        uint32_t actions = 0u;
        int s = chrome_runtime_poll_event(rt, tick, &actions);
        if (s == CHROME_RUNTIME_POLL_EVENT_HANDLED) {
            idle_iters = 0u;
            switch (rt->chrome.status) {
                case BROWSER_CHROME_STATUS_LOADING:
                    if (!got_started) {
                        bs_write("[browser-smoke] event NAV_STARTED\n");
                        got_started = 1;
                    }
                    break;
                case BROWSER_CHROME_STATUS_READY:
                    if (!got_ready) {
                        bs_write("[browser-smoke] event NAV_READY\n");
                        got_ready = 1;
                    }
                    break;
                default: break;
            }
            if (!got_frame && rt->chrome.last_frame.width > 0u) {
                bs_write("[browser-smoke] event FRAME w=");
                bs_write_dec(rt->chrome.last_frame.width);
                bs_write(" h=");
                bs_write_dec(rt->chrome.last_frame.height);
                bs_write("\n");
                got_frame = 1;
            }
            /* Pong reconhecido pelo watchdog: estado volta a IDLE. */
            if (sent_ping && !got_pong
                && rt->total_pongs_received > 0u) {
                bs_write("[browser-smoke] event PONG\n");
                got_pong = 1;
            }
            /* Apos READY, manda PING para validar watchdog. */
            if (got_ready && !sent_ping) {
                if (chrome_runtime_send_ping(rt, tick) == 0) {
                    bs_write("[browser-smoke] ping-sent\n");
                    sent_ping = 1;
                }
            }
            /* Apos PONG, manda SHUTDOWN. */
            if (got_pong && !sent_shutdown) {
                if (chrome_runtime_send_shutdown(rt) == 0) {
                    bs_write("[browser-smoke] shutdown-sent\n");
                    sent_shutdown = 1;
                }
            }
        } else if (s == CHROME_RUNTIME_POLL_ENGINE_EOF) {
            bs_write("[browser-smoke] engine-eof\n");
            break;
        } else if (s == CHROME_RUNTIME_POLL_PROTOCOL_ERR) {
            bs_write("[browser-smoke] FAIL protocol-err\n");
            for (;;) __asm__ volatile("cli; hlt");
        } else {
            /* NO_DATA */
            if (++idle_iters > IDLE_LIMIT) {
                bs_write("[browser-smoke] FAIL timeout\n");
                for (;;) __asm__ volatile("cli; hlt");
            }
        }
        chrome_runtime_tick(rt, tick++);
        task_yield();
    }

    if (got_started && got_frame && got_ready && got_pong && sent_shutdown) {
        bs_write("[browser-smoke] OK\n");
    } else {
        bs_write("[browser-smoke] FAIL incomplete: started=");
        bs_write_dec((uint32_t)got_started);
        bs_write(" frame=");
        bs_write_dec((uint32_t)got_frame);
        bs_write(" ready=");
        bs_write_dec((uint32_t)got_ready);
        bs_write(" pong=");
        bs_write_dec((uint32_t)got_pong);
        bs_write("\n");
    }

    /* Halta o sistema de forma deterministica; o harness QEMU le
     * o debugcon e termina via `-no-reboot -no-shutdown`. */
    for (;;) __asm__ volatile("cli; hlt");
}

/* === Entry point ========================================================= */

__attribute__((noreturn))
static void browser_smoke_halt(void) {
    for (;;) __asm__ volatile("hlt");
}

void kernel_boot_run_browser_smoke(void) {
    struct browser_engine_spawn_result r;
    int rc = browser_engine_spawn(&r);
    if (rc != BROWSER_ENGINE_SPAWN_OK) {
        bs_write("[browser-smoke] FAIL spawn rc=");
        /* rc e negativo; emite o valor absoluto. */
        bs_write_dec((uint32_t)(-rc));
        bs_write("\n");
        browser_smoke_halt();
    }
    bs_write("[browser-smoke] spawn pid=");
    bs_write_dec(r.engine_pid);
    bs_write(" req=");
    bs_write_dec((uint32_t)r.request_pipe_id);
    bs_write(" resp=");
    bs_write_dec((uint32_t)r.response_pipe_id);
    bs_write("\n");

    chrome_runtime_init(&g_smoke_rt, r.request_pipe_id, r.response_pipe_id,
                        r.engine_pid, 0u);
    chrome_runtime_set_pipe_ops(pipe_write, pipe_read);

    /* Arma engine main_thread para o trampoline x64_user_first_dispatch.
     * RIP/RSP foram primados por elf_load_into_process. */
    if (!r.engine_main_thread) {
        bs_write("[browser-smoke] FAIL no main_thread\n");
        browser_smoke_halt();
    }
    user_task_arm_for_first_dispatch(r.engine_main_thread,
                                     r.engine_main_thread->context.rip,
                                     r.engine_main_thread->context.rsp);
    scheduler_add(r.engine_main_thread);

    /* Cria e enfileira o kernel poller. */
    struct task *poller = task_create_kernel("browser-poller",
                                             browser_smoke_poller_task,
                                             &g_smoke_rt);
    if (!poller) {
        bs_write("[browser-smoke] FAIL task_create_kernel\n");
        browser_smoke_halt();
    }
    scheduler_add(poller);
    bs_write("[browser-smoke] tasks-armed\n");
    /* Boot CPU bloqueia em hlt; APIC tick preempta para poller/engine. */
    browser_smoke_halt();
}
