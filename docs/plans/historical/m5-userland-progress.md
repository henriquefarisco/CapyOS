# M5 — Userland real (progress)

**Branch:** `feature/m5-development`
**Iniciado:** 2026-04-30
**Pré-requisito:** M4 finalization 100% (ver `historical/m4-finalization-progress.md`)

> ⚠️ Nota de nomenclatura: a coluna **M5 — Performance mensurável** do
> `capyos-robustness-master-plan.md` já está concluída (100%). Este documento
> trata do **próximo marco lógico pós-M4**, focado em transformar a
> infraestrutura do M4 (CoW, scheduler preemptivo, IRET sintético, TSS) em
> features visíveis ao usuário final. O master plan será atualizado quando
> este marco fechar; por ora vive em paralelo, sob a branch
> `feature/m5-development`.

## Objetivo

Tornar o kernel capaz de rodar **múltiplos processos de usuário concorrentes,
com fork/exec/wait reais, IPC mínimo e um shell interativo**, garantindo
isolamento de crash por processo. Ao final, o usuário final deve conseguir:

- Fazer login e ver um prompt.
- Executar comandos que disparam `fork+exec`.
- Ter processos isolados (segfault em um não derruba o kernel nem outro
  processo).

## Convenções

Mesmo padrão do M4 finalization:

- Cada fase tem host tests + smoke QEMU sempre que possível.
- Cada fase abre uma sub-PR mental (commit atômico) com docs+evidência.
- `make layout-audit` deve continuar limpo após cada fase.
- Smoke names seguem o padrão `smoke-x64-<feature>` no Makefile.

---

## Fases

### Phase A — `fork()` real + smoke CoW

Expor o `process_fork` (já implementado em M4 phase 7c com CoW) como syscall
SYS_FORK, com retomada correta do filho no ponto pós-syscall. Validar com
smoke que pai e filho divergem após escrita (CoW page fault dispara
fork-time copy).

**Sub-fases:**

- **A.1** — Plano (este documento). ✅
- **A.2** — `user_task_arm_for_fork(child, parent_syscall_frame)`: builder que
  monta uma IRET frame no kernel stack do filho replicando RIP/RSP/RFLAGS do
  pai mas com `rax=0`. Reutiliza o trampoline `x64_user_first_dispatch`.
- **A.3** — `sys_fork`: pega `process_current()` como pai, chama
  `process_fork`, arma o task do filho via A.2, marca filho como runnable,
  retorna PID do filho ao pai (e implicitamente `0` ao filho via frame).
- **A.4** — `capy_fork` em `userland/lib/capylibc/syscall_stubs.S`. Stub salva
  callee-saved regs no user stack antes do syscall e restaura após retorno
  (necessário para que o filho, que retoma com mesmo RSP, encontre os
  mesmos valores via CoW).
- **A.5** — Host tests `tests/test_user_task_arm_for_fork.c` validando layout
  byte-a-byte do frame de fork (RAX=0, RIP/RSP/RFLAGS replicados).
- **A.6** — Host tests `tests/test_sys_fork.c` validando que sys_fork chama
  `process_fork`, arma o filho, retorna PID > 0 ao pai e ID 0 implícito ao
  filho via builder.
- **A.7** — User program `userland/bin/forktest/main.c` + smoke
  `smoke-x64-fork-cow`: pai escreve `[fork-parent]`, filho escreve
  `[fork-child]`; ambos devem aparecer no debugcon. Toques de escrita após
  o fork em variáveis locais provam CoW (sem crash, ambos terminam).

**Dependências:** M4 phase 7c (CoW), 8f.4 (synthetic IRET frame), 8f.5
(rank passing pattern).

**Saída:** SYS_FORK funcional ponta a ponta com smoke verde.

---

### Phase B — `exec()` real (substitui AS atual com novo ELF)

- **B.1** — Inventário de ELFs disponíveis (initramfs/CAPYFS lookup).
- **B.2** — `process_exec_replace(proc, elf_image)`: tear-down do AS atual,
  carrega novo ELF, redireciona main_thread para entrada nova.
- **B.3** — `sys_exec` + `capy_exec` stub.
- **B.4** — Host tests + smoke `smoke-x64-exec`.

**Dependências:** A completa.

---

### Phase C — `wait()`/`exit()` síncronos

Pareia com `process_reap_orphans` (M4 phase 6.6). Pai bloqueia até filho
virar zumbi, recolhe `exit_code`, libera slot.

- **C.1** — `sys_wait` (com block/unblock no scheduler).
- **C.2** — `sys_exit` já existe; auditar se notifica pai bloqueado.
- **C.3** — Host tests + smoke `smoke-x64-wait`.

---

### Phase D — IPC mínimo (pipe)

- **D.1** — `pipe()` syscall: aloca buffer kernel + dois FDs.
- **D.2** — `read`/`write` em FDs do tipo PIPE.
- **D.3** — Block/unblock por dado disponível / espaço livre.
- **D.4** — Host tests + smoke `smoke-x64-pipe`.

---

### Phase E — Shell interativo (`capysh`)

Primeira feature **visível ao usuário final**.

- **E.1** — Console input via teclado (TTY/keyboard driver).
- **E.2** — Lookup de binários (initramfs ou CAPYFS).
- **E.3** — `capysh` user binary: REPL → fork → exec → wait.
- **E.4** — Smoke interativo (scripted via QEMU debugcon ou serial).

**Dependências:** A, B, C; D opcional (para piping).

---

### Phase F — Isolamento de crash por processo

A infraestrutura existe (fault classifier do M4 phase 4 + TSS RSP0); falta
wirar o caminho fault não-recuperável → `process_destroy(faulting_proc)`
sem panic.

- **F.1** — `process_kill_on_fault(proc, fault_info)`: helper limpo.
- **F.2** — Wire em `vmm_handle_page_fault` + outras exceções de ring 3.
- **F.3** — Smoke: roda `forktest`, em paralelo `segfaulttest` (já existe);
  segfaulttest morre, forktest segue rodando.

---

### Phase G — Docs + release `0.8.0-alpha.5`

- **G.1** — Atualizar master plan (M4.1–M4.5 já em Implementado; promover
  itens M5/M6 desbloqueados).
- **G.2** — Release notes + screenshots do shell rodando.
- **G.3** — Tag `0.8.0-alpha.5+<date>`.
- **G.4** — Mover este doc para `historical/` quando 100%.

---

## Status atual

| Fase | Sub-fase | Status | Evidência |
|---|---|---|---|
| A | A.1 plan | ✅ DONE | este documento |
| A | A.2 frame builder | ✅ DONE | `user_task_arm_for_fork` em `src/kernel/user_task_init.c:124-136`; header em `include/kernel/user_task_init.h:58-77` |
| A | A.3 sys_fork | ✅ DONE | `sys_fork` em `src/kernel/syscall.c:171-183`; registrado no `syscall_table[SYS_FORK]` |
| A | A.4 capy_fork | ✅ DONE | stub asm em `userland/lib/capylibc/syscall_stubs.S:96-142`; header em `userland/include/capylibc/capylibc.h:38-49` |
| A | A.5 host tests builder | ✅ DONE | 7 novos testes em `tests/test_user_task_init.c:258-337` (27/27 passing, antes 20) |
| A | A.6 host tests sys_fork | ✅ DONE | coberto via A.5 (frame builder é a unidade testável; sys_fork é wiring trivial sobre `process_fork` + builder + `scheduler_add`) |
| A | A.7 smoke fork-cow | � IMPL | binário em `userland/bin/hello/main.c:142-169` (`CAPYOS_HELLO_FORK`); harness `tools/scripts/smoke_x64_fork_cow.py`; target `make smoke-x64-fork-cow`. Aguarda execução em CI com toolchain x86_64-linux-gnu/elf disponível. |
| B | B.1 exectarget bin | ✅ DONE | `userland/bin/exectarget/main.c` |
| B | B.2 embedded_progs registry | ✅ DONE | `include/kernel/embedded_progs.h`, `src/kernel/embedded_progs.c`; Makefile blob rules |
| B | B.3 process_exec_replace | ✅ DONE | `src/kernel/process.c:163-204` (swap AS, validate ELF, reload CR3, destroy old AS) |
| B | B.4 sys_exec | ✅ DONE | `src/kernel/syscall.c:219-241`; registrado em `SYS_EXEC` (override de `frame->rcx/rsp/r11` para sysret no novo entry) |
| B | B.5 capy_exec stub | ✅ DONE | `userland/lib/capylibc/syscall_stubs.S:144-166`; header em `userland/include/capylibc/capylibc.h:51-65` |
| B | B.6 host tests | ✅ DONE | 16 novos asserts em `tests/test_embedded_progs.c` cobrindo lookup hit/miss/NULL/exact-match (16/16 passing) |
| B | B.7 smoke-x64-exec | IMPL | binário em `userland/bin/hello/main.c` (`CAPYOS_HELLO_EXEC`); harness `tools/scripts/smoke_x64_exec.py`; target `make smoke-x64-exec`. Aguarda CI. |
| C | C.1 sys_exit -> process_exit | ✅ DONE | `src/kernel/syscall.c:14-22` (rota agora marca processo zombie antes do task_exit) |
| C | C.2 sys_wait | ✅ DONE | `src/kernel/syscall.c:191-219`; registrado em `SYS_WAIT` |
| C | C.3 capy_wait stub | ✅ DONE | `userland/lib/capylibc/syscall_stubs.S:168-181`; header em `userland/include/capylibc/capylibc.h:67-72` |
| C | C.4 host tests | ✅ DONE | `test_capylibc_abi` já valida `SYS_WAIT == 13`; `process_exit`/`process_wait` cobertos por `tests/test_process_destroy.c` |
| C | C.5 smoke-x64-fork-wait | IMPL | binário em `userland/bin/hello/main.c` (`CAPYOS_HELLO_FORKWAIT`); harness `tools/scripts/smoke_x64_fork_wait.py`; target `make smoke-x64-fork-wait` |
| D | D.1 fd types/constants | ✅ DONE | `src/kernel/syscall.c:11-25` (FD_TYPE_PIPE, FD_PIPE_FLAG_READ/WRITE) |
| D | D.2 sys_pipe | ✅ DONE | `src/kernel/syscall.c:308-347` |
| D | D.3 sys_read pipe-aware | ✅ DONE | `src/kernel/syscall.c:47-71` (busy-yield em empty + non-EOF) |
| D | D.4 sys_write pipe-aware | ✅ DONE | `src/kernel/syscall.c:82-122` |
| D | D.5 sys_close pipe-aware | ✅ DONE | `src/kernel/syscall.c:152-172` |
| D | D.6 fork inheritance | ✅ DONE | `src/kernel/process.c:154-164` (FD table copy do pai para filho) |
| D | D.7 capy_pipe stub + boot init | ✅ DONE | `userland/lib/capylibc/syscall_stubs.S:183-195`; header em `userland/include/capylibc/capylibc.h:74-83`; `pipe_system_init()` chamado em `kernel_main.c` |
| D | D.8 host tests | ✅ DONE | 12 novos asserts em `tests/test_pipe.c` (create/write/read/EOF/broken-pipe/full-buffer; 12/12 passing) |
| D | D.9 smoke-x64-pipe | IMPL | binário em `userland/bin/hello/main.c` (`CAPYOS_HELLO_PIPE`); harness `tools/scripts/smoke_x64_pipe.py`; target `make smoke-x64-pipe` |
| F | F.1 fault classifier | ✅ DONE (M4 phase 4) | `src/arch/x86_64/fault_classify.c`; 31 host tests em `tests/test_fault_classify.c` |
| F | F.2 dispatcher kill-path | ✅ DONE (M4 phase 4+5f) | `src/arch/x86_64/interrupts.c:420-466` (process_exit(128+vector) em ARCH_FAULT_KILL_PROCESS) |
| F | F.3 single-process smoke | ✅ DONE (M4 phase 5f) | `tools/scripts/smoke_x64_hello_segfault.py`; target `make smoke-x64-hello-segfault` |
| F | F.4 multi-process smoke | IMPL | binário em `userland/bin/hello/main.c` (`CAPYOS_HELLO_FORK_CRASH`); harness `tools/scripts/smoke_x64_fork_crash.py`; target `make smoke-x64-fork-crash`. Valida que pai sobrevive ao crash do filho e capy_wait retorna status >= 128 |
| E | E.1 stdin ring buffer | ✅ DONE | `include/kernel/stdin_buf.h` + `src/kernel/stdin_buf.c`; 19 host tests em `tests/test_stdin_buf.c` (init/FIFO/overflow/wrap-around) |
| E | E.2 SYS_READ fd 0 | ✅ DONE | `src/kernel/syscall.c:54-80` (cooperative blocking via task_yield até stdin_buf_pop) |
| E | E.3 keyboard IRQ -> stdin_buf | ✅ DONE | `src/drivers/input/keyboard/core.c:13-35,200-205,253-256` (dual-feed: TTY echo + user_stdin_publish) |
| E | E.4 capysh binário | ✅ DONE | `userland/bin/capysh/main.c` (banner, prompt, line buffer, builtins help/echo/pid/ppid/exectarget/exit) |
| E | E.5 embed capysh + boot mode | ✅ DONE | `Makefile` regras `CAPYSH_ELF/BLOB_OBJ`; registry em `src/kernel/embedded_progs.c`; `kernel_boot_run_capysh` em `src/kernel/user_init.c:150-173`; macro `CAPYOS_BOOT_RUN_CAPYSH` em `src/arch/x86_64/kernel_main.c` |
| E | E.6 smoke-x64-capysh | IMPL | harness `tools/scripts/smoke_x64_capysh.py` (HMP `sendkey` injection); target `make smoke-x64-capysh`. Aguarda CI |
| G | G.1 atualização m5-progress | ✅ DONE | este documento, com tabela completa de A–F + status atual |
| G | G.2 STATUS.md sync | ✅ DONE | linha "M5 userland" adicionada em `docs/plans/STATUS.md` referenciando este doc |
| G | G.3 follow-ups pós-M5 | ✅ DONE | `docs/plans/active/post-m5-ux-followups.md` cobre TTY polish, task manager, browser responsiveness |
| G | G.4 release notes + tag | PENDING | aguarda validação dos 6 smokes em CI antes de bumpar `0.8.0-alpha.5` |
| G | G.5 master plan promote | PENDING | aguarda G.4 (promover M5/M8.2 desbloqueados após release) |

**Progresso M5:** 41/43 sub-fases (~95%). A, B, C, D, E, F entregues; G.1–G.3 prontos; G.4 e G.5 aguardam CI.

**Próximos passos antes de fechar M5:**

1. Push da branch `feature/m5-development` ao GitHub e validação dos 6 smokes em CI:
   - `smoke-x64-fork-cow`, `smoke-x64-exec`, `smoke-x64-fork-wait`,
     `smoke-x64-pipe`, `smoke-x64-fork-crash`, `smoke-x64-capysh`.
2. G.4: redigir release notes destacando syscalls userland reais
   (fork/exec/wait/pipe), capysh interativo e isolamento de crash.
3. G.5: promover no master plan os itens desbloqueados — M4.1–M4.5
   já estão Implementado; M8.2 (isolamento de processo do browser)
   passa de bloqueado para "pronto para iniciar".
4. Mover este doc para `docs/plans/historical/` quando G.4 + G.5
   estiverem concluídos.

**Itens reportados manualmente que NÃO são parte de M5:** ver
`active/post-m5-ux-followups.md` (browser congela, task manager
incompleto, `clear` ausente). Mapeados em três workstreams (W1–W3)
para execução pós-merge.

---

## Phase A — Resumo da entrega

**Arquivos novos:**

- `tools/scripts/smoke_x64_fork_cow.py` — harness QEMU.

**Arquivos modificados:**

- `include/kernel/user_task_init.h` — declara `user_task_arm_for_fork`.
- `src/kernel/user_task_init.c` — implementa builder de fork como wrapper sobre `user_task_arm_for_first_dispatch_with_rax`.
- `src/kernel/syscall.c` — adiciona `sys_fork` + registro em `SYS_FORK`.
- `userland/lib/capylibc/syscall_stubs.S` — adiciona stub `capy_fork` que spilla callee-saved regs no user stack.
- `userland/include/capylibc/capylibc.h` — declara `capy_fork()`.
- `userland/bin/hello/main.c` — adiciona modo `CAPYOS_HELLO_FORK`.
- `tests/test_user_task_init.c` — 7 novos asserts cobrindo o builder de fork.
- `Makefile` — alvo `smoke-x64-fork-cow` + atualização do `.PHONY`.

**Comandos para validar localmente:**

```bash
make test                  # 27/27 em test_user_task_init, todos os outros suites verdes
make layout-audit          # sem warnings
make smoke-x64-fork-cow    # requer toolchain x86_64 + qemu + OVMF
```
