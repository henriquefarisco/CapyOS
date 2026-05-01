/*
 * src/kernel/browser_engine_spawn.c (F3.3d)
 *
 * Spawn helper que cria o processo capybrowser ring 3 com dois
 * pipes pre-conectados aos seus fds 0 e 1. Ver
 * include/kernel/browser_engine_spawn.h.
 *
 * UNIT_TEST: este TU nao e compilado no binario de testes host
 * (depende de `process_create`/`pipe_create` reais). A logica
 * pura testavel da runtime do chrome esta em
 * src/apps/browser_chrome/runtime.c.
 */

#include "kernel/browser_engine_spawn.h"
#include "kernel/embedded_progs.h"
#include "kernel/elf_loader.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include <stddef.h>
#include <stdint.h>

/* FD type constants espelhadas de src/kernel/syscall.c. Mantidas
 * como literais aqui porque syscall.c nao expoe header com elas
 * (sao detalhe interno da abi). Se forem mover para header
 * dedicado, consolidar la e remover daqui. */
#define FD_TYPE_PIPE      2
#define FD_PIPE_FLAG_READ  0x1u
#define FD_PIPE_FLAG_WRITE 0x2u

static void install_pipe_fd(struct process *proc, int fd_index,
                            int pipe_id, uint32_t flags) {
    proc->fds[fd_index].type = FD_TYPE_PIPE;
    proc->fds[fd_index].flags = flags;
    proc->fds[fd_index].private_data = (void *)(intptr_t)pipe_id;
    proc->fds[fd_index].offset = 0;
}

int browser_engine_spawn(struct browser_engine_spawn_result *out) {
    if (!out) return BROWSER_ENGINE_SPAWN_NO_PROCESS;

    /* 1. Resolve ELF do capybrowser. */
    const uint8_t *data = (const uint8_t *)0;
    size_t size = 0u;
    if (embedded_progs_lookup("/bin/capybrowser", &data, &size) != 0) {
        return BROWSER_ENGINE_SPAWN_NO_BLOB;
    }
    if (elf_validate(data, size) != 0) {
        return BROWSER_ENGINE_SPAWN_BAD_ELF;
    }

    /* 2. Cria os dois pipes ANTES do processo, para que o cleanup
     * em caso de falha possa fechar tudo de forma uniforme. */
    int req_fds[2];
    if (pipe_create(req_fds) != 0) {
        return BROWSER_ENGINE_SPAWN_NO_PIPE;
    }
    int resp_fds[2];
    if (pipe_create(resp_fds) != 0) {
        pipe_close_read(req_fds[0]);
        pipe_close_write(req_fds[0]);
        return BROWSER_ENGINE_SPAWN_NO_PIPE;
    }
    int request_pipe_id = req_fds[0];   /* chrome ESCREVE aqui */
    int response_pipe_id = resp_fds[0]; /* chrome LE aqui */

    /* 3. Cria processo e carrega ELF. */
    struct process *p = process_create("capybrowser", 0u, 0u);
    if (!p) {
        pipe_close_read(request_pipe_id);
        pipe_close_write(request_pipe_id);
        pipe_close_read(response_pipe_id);
        pipe_close_write(response_pipe_id);
        return BROWSER_ENGINE_SPAWN_NO_PROCESS;
    }
    if (elf_load_into_process(p, data, size) != 0) {
        process_destroy(p);
        pipe_close_read(request_pipe_id);
        pipe_close_write(request_pipe_id);
        pipe_close_read(response_pipe_id);
        pipe_close_write(response_pipe_id);
        return BROWSER_ENGINE_SPAWN_LOAD_FAIL;
    }

    /* 4. Instala fds 0/1 na tabela do engine.
     *   fd 0 = read end do request_pipe (engine LE comandos)
     *   fd 1 = write end do response_pipe (engine ESCREVE eventos)
     * fd 2 (stderr) fica livre por enquanto; o engine usa apenas
     * para logging via debugcon (capy_write 2 -> debug console). */
    install_pipe_fd(p, 0, request_pipe_id, FD_PIPE_FLAG_READ);
    install_pipe_fd(p, 1, response_pipe_id, FD_PIPE_FLAG_WRITE);

    /* 5. Resultado para o caller. NAO adicionamos ao scheduler;
     * o caller decide quando fazer scheduler_add(). */
    out->request_pipe_id = request_pipe_id;
    out->response_pipe_id = response_pipe_id;
    out->engine_pid = p->pid;
    out->engine_proc = p;
    out->engine_main_thread = p->main_thread;
    return BROWSER_ENGINE_SPAWN_OK;
}
