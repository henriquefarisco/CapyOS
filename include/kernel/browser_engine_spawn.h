#ifndef KERNEL_BROWSER_ENGINE_SPAWN_H
#define KERNEL_BROWSER_ENGINE_SPAWN_H

/*
 * Helper kernel-side para spawnar o browser engine (capybrowser)
 * com dois pipes ja conectados aos fds 0 e 1 do processo ring 3.
 *
 * Resultado: o chrome (compositor kernel-side) recebe os pipe ids
 * que ele deve usar com `pipe_write`/`pipe_read` para falar com o
 * engine. A integracao com `browser_chrome_runtime` e direta:
 *
 *     struct browser_engine_spawn_result r;
 *     if (browser_engine_spawn(&r) != 0) { fail; }
 *     chrome_runtime_init(&rt, r.request_pipe_id, r.response_pipe_id,
 *                         r.engine_pid, apic_timer_ticks());
 *     chrome_runtime_set_pipe_ops(pipe_write, pipe_read);
 *     scheduler_add(r.engine_main_thread);
 */

#include <stdint.h>

struct task;
struct process;

struct browser_engine_spawn_result {
    int request_pipe_id;        /* chrome WRITE end (engine reads via fd 0) */
    int response_pipe_id;       /* chrome READ end (engine writes via fd 1) */
    uint32_t engine_pid;
    struct process *engine_proc;
    struct task *engine_main_thread; /* nao adicionado ao scheduler ainda */
};

enum browser_engine_spawn_status {
    BROWSER_ENGINE_SPAWN_OK         = 0,
    BROWSER_ENGINE_SPAWN_NO_BLOB    = -1, /* /bin/capybrowser nao encontrado */
    BROWSER_ENGINE_SPAWN_BAD_ELF    = -2,
    BROWSER_ENGINE_SPAWN_NO_PROCESS = -3, /* tabela cheia */
    BROWSER_ENGINE_SPAWN_NO_PIPE    = -4,
    BROWSER_ENGINE_SPAWN_LOAD_FAIL  = -5
};

/* Spawn um capybrowser. Em sucesso popula `*out` e retorna 0. Em
 * falha, qualquer recurso parcialmente alocado e liberado e o
 * codigo apropriado de `browser_engine_spawn_status` e devolvido.
 *
 * O caller assume ownership do processo + pipes; tipicamente:
 *   - chama `chrome_runtime_init()` com os ids retornados;
 *   - adiciona `engine_main_thread` ao scheduler.
 *
 * Em caso de morte do engine (EOF na pipe ou watchdog->kill), o
 * caller deve `process_kill` + chamar este helper de novo para
 * spawnar um substituto, depois `chrome_runtime_record_restart`. */
int browser_engine_spawn(struct browser_engine_spawn_result *out);

#endif /* KERNEL_BROWSER_ENGINE_SPAWN_H */
