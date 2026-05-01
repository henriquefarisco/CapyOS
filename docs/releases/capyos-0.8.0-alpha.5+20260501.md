# CapyOS 0.8.0-alpha.5+20260501

Data: 2026-05-01
Canal: develop
Base: M5 userland (pos-M4) + polish UX pos-M5 (W1/W2/W3) + consolidacao do roadmap em plano linear unico

## Destaques

- **M5 userland completo** sobre a infraestrutura do M4 (CoW + scheduler
  preemptivo + IRET sintetico + TSS/RSP0). Sistema agora roda multiplos
  processos em ring 3 com fork/exec/wait/pipe reais e isolamento de crash:
  - `SYS_FORK` com retomada correta do filho via `user_task_arm_for_fork`
    (CoW dispara em primeira escrita pos-fork).
  - `SYS_EXEC` com `process_exec_replace` (tear-down do AS atual + carga
    de novo ELF) e registry `embedded_progs` para binarios userland.
  - `SYS_WAIT` / `SYS_EXIT` com block/unblock cooperativo no scheduler.
  - `SYS_PIPE` com FD type `FD_TYPE_PIPE`, busy-yield em empty/non-EOF,
    full-buffer detection, broken-pipe detection, fork inheritance.
  - Isolamento de crash multi-processo: dispatcher kill-path
    (`process_exit(128+vector)` em `ARCH_FAULT_KILL_PROCESS`).
  - Shell `capysh` ring 3 (banner, prompt, builtins
    `help/echo/pid/ppid/exectarget/exit/clear`) com stdin via ring buffer
    SPSC de 256 bytes alimentado pelo IRQ do teclado (dual-feed: TTY echo
    + `user_stdin_publish`).
  - 84 novos host asserts (frame builder + embedded_progs + pipe +
    stdin_buf).
  - 6 smokes QEMU adicionados: `smoke-x64-fork-cow`, `smoke-x64-exec`,
    `smoke-x64-fork-wait`, `smoke-x64-pipe`, `smoke-x64-fork-crash`,
    `smoke-x64-capysh`.

- **W1 - TTY polish (`clear` context-aware)**: `shell_clear_screen()` agora
  roteia via callback paired (terminal widget do desktop ou `vga_clear`
  fallback). `cmd_mess` passou a usar a abstracao em vez de `vga_clear`
  direto. Builtin `clear` adicionado em `capysh` ring 3 emitindo
  `\x1b[2J\x1b[H`.

- **W2 - Task manager auto-refresh + Kill button**:
  `task_manager_tick()` invocado por frame em `desktop_run_frame` invalida
  a janela a cada ~0.5s (30 frames). Botao Kill funcional via
  `process_kill(pid, SIGKILL=9)`. Footer rotula "Kill" para tasks/processes
  e "Restart" para services.

- **W3 - Browser responsiveness (core)**: yield cooperativo no `html_parse`
  (a cada 1024 iteracoes consulta `hv_nav_budget_blocked()` e chama
  `task_yield()`). Timeout duro de 30s via `html_viewer_tick()` chamado de
  `desktop_run_frame` (drain async sem precisar interacao do usuario;
  `hv_nav_budget_cancel("timeout")` quando excede deadline). Carregamento
  de pagina deixa de congelar o desktop e sites pesados podem ser
  abortados sem reboot. W3.4 (mover html_viewer para processo userland
  isolado) deferido para F3 do master plan.

- **Consolidacao de planos**: 9 documentos paralelos em `docs/plans/active/`
  unificados em [`docs/plans/active/capyos-master-plan.md`](../plans/active/capyos-master-plan.md)
  com plano linear unico **F1-F10**:
  - F1 - este release (alpha.5).
  - F2 - DHCP smoke VMware+E1000 + assinatura Ed25519 dos checksums.
  - F3 - Browser em processo userland isolado + watchdog (M8.2 + W3.4).
  - F4 - Sockets userland + TLS (libcapy-net + libcapy-tls).
  - F5 - Update real via GitHub Releases.
  - F6 - Sessao grafica completa (mouse fim-a-fim, login GUI, dispatcher).
  - F7 - Apps basicos (file_manager, text_editor, settings, etc.).
  - F8 - Package manager + SDK + ABI estavel.
  - F9 - JS engine sandboxed.
  - F10 - CapyLang.
  Os 9 planos antigos foram movidos para `docs/plans/historical/` com
  rastreabilidade explicita no Apendice A do master plan.

## Userland API e ABI

- `userland/lib/capylibc/syscall_stubs.S`:
  - `capy_fork()` (SYS_FORK=12), `capy_exec()` (SYS_EXEC=11),
    `capy_wait()` (SYS_WAIT=13), `capy_pipe()` (SYS_PIPE=15).
  - `capy_fork` salva callee-saved no user stack antes do syscall e
    restaura apos retorno (necessario para que o filho retomando com
    mesmo RSP encontre os valores via CoW).
- `userland/include/capylibc/capylibc.h`: declaracoes correspondentes.
- `userland/bin/capysh/main.c`: shell ring 3.
- `userland/bin/exectarget/main.c`: helper de validacao do exec.

## Kernel

- `src/kernel/process.c`: `process_exec_replace`, FD inheritance no fork
  (~150 linhas adicionadas).
- `src/kernel/syscall.c`: SYS_FORK, SYS_EXEC, SYS_WAIT, SYS_PIPE com
  semantica documentada.
- `src/kernel/embedded_progs.c`: registry de binarios userland por path.
- `src/kernel/stdin_buf.c`: ring buffer SPSC com overflow tracking
  (`stdin_buf_dropped_total`).
- `src/kernel/user_task_init.c`: `user_task_arm_for_fork()` reusando o
  trampoline `x64_user_first_dispatch`.

## GUI / Apps

- `src/apps/task_manager.c`: `task_manager_tick()` + `task_manager_kill_selected()`.
- `src/gui/desktop/desktop.c`: integra `task_manager_tick()` e
  `html_viewer_tick()` no frame loop; instala `desktop_shell_clear()`
  como callback do shell.
- `src/apps/html_viewer/html_parser.c`: yield cooperativo
  (`HTML_PARSE_YIELD_EVERY = 1024`).
- `src/apps/html_viewer/async_runtime.c`: `html_viewer_tick()` com
  deadline de `HTML_VIEWER_NAV_TIMEOUT_TICKS = 3000` (30s @ 100Hz).

## Testes adicionados

- `tests/test_user_task_init.c`: 7 novos asserts (frame builder de fork).
- `tests/test_embedded_progs.c`: 16 asserts (lookup hit/miss/NULL/exact-match).
- `tests/test_pipe.c`: 12 asserts
  (create/write/read/EOF/broken-pipe/full-buffer).
- `tests/test_stdin_buf.c`: 19 asserts (init/FIFO/overflow/wrap-around).

## Smokes adicionados

- `smoke-x64-fork-cow` - pai e filho divergem apos escrita pos-fork.
- `smoke-x64-exec` - exec substitui AS com novo ELF.
- `smoke-x64-fork-wait` - pai bloqueia em wait, recolhe exit_code do filho.
- `smoke-x64-pipe` - pipe ring buffer + EOF + broken-pipe.
- `smoke-x64-fork-crash` - filho segfault, pai sobrevive, capy_wait
  retorna status >= 128 (128+vector).
- `smoke-x64-capysh` - HMP `sendkey` injection, valida prompt + builtins.

Os 6 smokes ainda aguardam validacao em CI antes de promover M4.1-M4.5
formalmente para `Implementado` no master plan (vao com a tag).

## Documentacao

- `docs/plans/active/capyos-master-plan.md` (NOVO) - fonte de verdade unica.
- `docs/plans/README.md` - reescrito para refletir a consolidacao.
- `docs/plans/STATUS.md` - aponta para o master plan unico; caminho
  critico simplificado para F1-F10.
- 9 planos antigos movidos para `docs/plans/historical/` (preservados
  para rastreabilidade).

## Validacao

```bash
make test                      # todos os asserts host passaram
make layout-audit              # sem warnings; nenhum monolito reintroduzido
make version-audit             # 0.8.0-alpha.5 alinhado em VERSION.yaml + version.h + README + release note
make all64                     # build x64 limpo
make iso-uefi                  # ISO UEFI gerada
```

Smokes pendentes de CI (executar antes de tagear):

```bash
make smoke-x64-fork-cow
make smoke-x64-exec
make smoke-x64-fork-wait
make smoke-x64-pipe
make smoke-x64-fork-crash
make smoke-x64-capysh
make smoke-x64-preemptive-all  # M4 final continua verde
```

## Proxima fase (F2)

- DHCP automatico no smoke oficial VMware+E1000 (fecha M2.1-M2.5).
- Assinatura Ed25519 dos checksums de release ponta-a-ponta (fecha M6.4).
- Detalhes em `docs/plans/active/capyos-master-plan.md` secao F2.

Versao alinhada: `0.8.0-alpha.5+20260501`
