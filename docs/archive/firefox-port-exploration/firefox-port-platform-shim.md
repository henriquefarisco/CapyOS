# Firefox port: plano da camada de shim de plataforma (F1+F4 unified)

**Status**: Sessao 7 (2026-05-05)
**Autor**: Cascade
**Documentos pais**:
- `docs/plans/active/firefox-port-roadmap.md` (visao alta)
- `docs/architecture/firefox-port-deep-dive.md` (analise tecnica)

Este documento e o **plano acionavel** para a camada mais critica
do port: o shim de plataforma que faz o Firefox enxergar o CapyOS
como "mais um Linux". Combina partes de F1 (POSIX musl), F3
(process model) e F4 (toolchain) que juntas habilitam o
**Marco M1: SpiderMonkey shell roda em CapyOS**.

## Pre-requisitos

- Capybrowser legacy erradicado (sessao 6, completo).
- VMM com fix do HUGE bit (sessao 5, completo).
- Workspace `/Volumes/Firefox` disponivel (sessao 7, completo).

## Estrategia: Linux ABI compatibility (Strategy A)

Replicar a interface POSIX/Linux que o musl libc espera, de forma
que `x86_64-unknown-linux-musl` produza binarios que rodam no
CapyOS. Isso permite:
- Usar `musl libc` upstream sem fork.
- Usar Rust/cargo com `--target=x86_64-unknown-linux-musl`.
- Usar Mozilla `mach build --target=x86_64-unknown-linux-musl`.
- Acompanhar releases nightly Firefox sem rebases custosos.

## Estrutura de tarefas

Cada tarefa e identificada como `S<numero>.<modulo>` para
referencia rapida. As tarefas formam um DAG de dependencias.
A coluna "Bloqueia" lista o que depende delas.

### Etapa S1: kernel syscall surface

| ID | Tarefa | Estimativa | Bloqueia |
|---|---|---|---|
| S1.1 | ✅ **DONE** (sessao 9) Dispatcher Linux-compat em `src/kernel/linux_compat/linux_syscall.c` com sparse table `LINUX_NR_MAX=512`, register/lookup/dispatch, weak module hooks (clock + random). 50+ NRs definidos em `linux_syscall_nrs.h`. **17/17 host asserts**. | ~~2-3 semanas~~ feito | tudo |
| S1.2 | ✅ **DONE** (sessoes 11+14) `mmap`/`munmap`/`mprotect`/`madvise`/`mremap` em `src/kernel/linux_compat/linux_mmap.c`. Suporta `MAP_ANONYMOUS\|MAP_PRIVATE\|MAP_FIXED` + `PROT_NONE\|READ\|WRITE\|EXEC`. fd=-1, offset=0 obrigatorios. madvise valida 23 advice values (DONTNEED/WILLNEED/HUGEPAGE/etc.) e retorna 0 (hints). mremap valida MAYMOVE/FIXED, MREMAP_FIXED requer MAYMOVE. MAP_SHARED/file-backed ainda pendentes. Boot wiring usa `vmm_register_anon_region` com bump pointer em 0x500000000000. **35/35 host asserts**. | ~~4-6 semanas~~ feito (parcial Marco M1) | F1.pthread, JIT |
| S1.3 | ✅ **DONE** (sessao 8) `clock_gettime(CLOCK_MONOTONIC/MONOTONIC_RAW/MONOTONIC_COARSE/BOOTTIME/REALTIME/REALTIME_COARSE)` em `src/kernel/linux_compat/linux_clock.c`; layering host-testavel; CPUTIME_ID retorna ENOSYS por enquanto. **22/22 host asserts** em `tests/test_linux_clock.c`. | ~~1 semana~~ feito | mozglue/misc |
| S1.4 | ✅ **DONE** (sessao 14, validation+stub) `clone`/`clone3`/`fork`/`vfork` em `src/kernel/linux_compat/linux_clone.c`. Validacao completa: flag mask whitelist (15 known bits), CSIGNAL low-byte stripping, CLONE_THREAD requer SIGHAND, SIGHAND requer VM (Linux invariants). clone3 valida ARGS_SIZE_VER0/1/2 (64/80/88 bytes). musl pthread_create flag pattern reconhecido como `LINUX_CLONE_PTHREAD_FLAGS`. Operacao retorna -ENOSYS ate `task_clone_thread` landar; userland (musl/SpiderMonkey) detects deterministico e fallback. **11/11 host asserts**. | ~~4-6 semanas~~ feito (validation+ENOSYS stub) | F1.pthread |
| S1.5 | ✅ **DONE** (sessao 12) `futex` em `src/kernel/linux_compat/linux_futex.c`. WAIT/WAKE/WAIT_BITSET/WAKE_BITSET/REQUEUE funcionais; PRIVATE_FLAG e CLOCK_REALTIME aceitos. FD/LOCK_PI/UNLOCK_PI/TRYLOCK_PI/CMP_REQUEUE/WAKE_OP -> -ENOSYS. Boot trampoline usa `task_block`/`task_unblock_channel` com uaddr como wait channel. Timeout ignorado por enquanto (espera task_sleep_until). **19/19 host asserts**. | ~~3-4 semanas~~ feito | F1.pthread |
| S1.6 | ✅ **DONE** (sessoes 13+44) `epoll_create1/ctl/wait/pwait` em `src/kernel/linux_compat/linux_epoll.c`. Tabela de 16 instances x 64 watch entries cada. fd encoding 0x6000+slot. CTL_ADD/MOD/DEL com EEXIST/ENOENT/event mask validation. wait com fd_ready callback (yield entre passes), EPOLLONESHOT desarma, pwait valida sigsetsize == 8. VFS generic `close` libera slot, `read`/`write` retornam -EINVAL e `lseek` -ESPIPE. **24 host asserts + 4 router regressions planejados/revisados**. | ~~3-4 semanas~~ feito | ipc/chromium |
| S1.7 | ✅ **DONE** (sessoes 12+14+41) `eventfd2` + `signalfd4` storage-only + `timerfd_create`/`settime`/`gettime`/read funcionais em `src/kernel/linux_compat/linux_eventfd.c`. eventfd2: 32 slots, semaphore mode, sentinel/overflow detection. signalfd4: 16 slots, cria/atualiza mascara, read -> -EAGAIN ate delivery real. timerfd: 16 slots (fd encoding 0x4800+), one-shot e periodic, ABSTIME flag, MONOTONIC/REALTIME/BOOTTIME, gettime retorna remaining time, settime arm/disarm via it_value=0, periodic read conta expirations elapsed. VFS router despacha read/write/close/lseek para eventfd/signalfd/timerfd ranges. **34 host asserts em `test_linux_eventfd.c` + 6 router regressions planejados/revisados**. | ~~2-3 semanas~~ feito | ipc/glue |
| S1.8 | ✅ **DONE** (sessao 9) `getrandom(buf, len, flags)` em `src/kernel/linux_compat/linux_random.c` delegando ao CSPRNG via callback injetavel. Suporta `GRND_NONBLOCK|RANDOM|INSECURE`, clipa em 33554431 (Linux 6.x cap). Registrado em `LINUX_NR_getrandom=318`. **12/12 host asserts**. | ~~1 semana~~ feito | NSS, JS GC seed |
| S1.9 | ✅ **DONE** (sessao 10, parcial) `prctl(PR_SET_NAME/PR_GET_NAME)` em `src/kernel/linux_compat/linux_process.c`. Cap de 16 bytes (Linux), trunca ate 15 chars + NUL no rename, lê 16 bytes NUL-padded no get. Outros ops -> -EINVAL. PR_SET_PDEATHSIG/PR_SET_DUMPABLE aguardam modelo de signals (S1.12). | ~~1-2 semanas~~ feito (parcial) | mozglue/misc |
| S1.10 | ✅ **DONE** (sessao 11) `set_tid_address(tidptr)` retorna gettid (Linux semantics, armazena ptr para futuro CLONE_CHILD_CLEARTID); `set_robust_list(head, len)` valida len == 24 (sizeof robust_list_head em x86_64) e armazena head, qualquer outro len -> -EINVAL. Estado em module-local (single-thread); migra para per-task quando S1.4 clone landar. **4/4 asserts** integrados em `test_linux_process`. | ~~1 semana~~ feito | F1.pthread |
| S1.11 | ✅ **DONE** (sessao 10) `sched_yield` chama `task_yield`, `sched_getaffinity` reporta single-CPU bitmap (bit 0 ligado), `sched_setaffinity` aceita qualquer mascara nao-vazia (no-op em single-CPU). cpusetsize validado (multiplo de 8 bytes). Quando MP existir basta extender o mask. | ~~1 semana~~ feito | js/src/threading |
| S1.12 | ✅ **DONE** (sessao 13, storage-only) `rt_sigaction`/`rt_sigprocmask`/`sigaltstack` em `src/kernel/linux_compat/linux_signal.c` armazenam handlers/mask/altstack (per-task quando S1.4 landar; module-local hoje). SIGKILL/SIGSTOP nao captureveis. sigsetsize=8 obrigatorio. SA flags validados. SS_DISABLE pula size check. rt_sigreturn -> -ENOSYS (sem delivery infra; musl panics se chegar aqui). Storage suficiente para SpiderMonkey/musl startup paths. **16/16 host asserts**. | ~~4-6 semanas~~ feito (storage-only) | crash report, GC barriers |
| S1.13 | ✅ **DONE** (sessoes 11+38+49, pipe2 funcional + dup3 stub) `pipe2` em `src/kernel/linux_compat/linux_fd.c` aceita `O_CLOEXEC\|O_NONBLOCK\|O_DIRECT` (mascara conhecida 0x84800), delega ao `pipe_create` nativo CapyOS. `pipe` delega para `pipe2(flags=0)`. `dup3` valida flag mask (so O_CLOEXEC permitido) e oldfd != newfd, mas trampoline retorna -EBADF ate fd table real existir. `linux_fd_install_ops(NULL)` agora limpa callbacks instalados para evitar hooks stale em reset/reconfiguracao. **host asserts +2 callback hygiene regressions planejados/revisados**. | ~~1 semana~~ feito (pipe2 funcional + dup3 stub) | ipc/chromium |
| S1.14 | ✅ **DONE** (sessao 12, validação) `accept4`/`recvmmsg`/`sendmmsg` em `src/kernel/linux_compat/linux_net.c`. Validacao completa de flags (SOCK_NONBLOCK/CLOEXEC para accept4; MSG_DONTWAIT/MSG_WAITFORONE para mmsg), fd >= 0, addrlen pareado com addr, vlen <= UIO_MAXIOV (1024). Operacao retorna -ENOSYS ate sockets BSD existirem (estamos sem socket layer ainda). **14/14 host asserts**. | ~~1 semana~~ feito (validacao + ENOSYS stub) | netwerk |
| S1.15 | ✅ **DONE** (sessoes 13+46, parcial) `memfd_create`/`pidfd_open` funcionais em `src/kernel/linux_compat/linux_memfd.c` (tabela 16 slots cada, fd encoding 0x5000/0x5800). memfd_create valida MFD_CLOEXEC/ALLOW_SEALING/HUGETLB/NOEXEC_SEAL/EXEC + name <= 249 chars. pidfd_open valida flags + pid existe via callback. pidfd_send_signal: sig=0 = probe (returns 0 if pid alive), sig != 0 -> -ENOSYS (signal delivery em S1.12 storage-only). VFS generic `close` libera slot; memfd `read`/`write`/`lseek` retornam -ENOSYS ate backing real, pidfd `read`/`write` retornam -EINVAL e `lseek` -ESPIPE. **20 host asserts + 4 router regressions planejados/revisados**. | ~~2-3 semanas~~ feito (parcial; memfd ftruncate/mmap/read/write em fase futura) | ipc/glue (sandbox) |
| S1.16 | ✅ **DONE** (sessoes 13+43, parcial) `inotify_init1`/`add_watch`/`rm_watch` em `src/kernel/linux_compat/linux_inotify.c`. Tabela 8 instances x 32 watches. fd encoding 0x7000+slot. add_watch aloca wd monotonico, valida event mask (>= 1 bit em IN_ALL_EVENTS), trunca path em 64 chars. Eventos nunca disparam (fs change notifier ainda nao existe); generic `read` via VFS retorna -EAGAIN, `write` -EINVAL, `lseek` -ESPIPE e `close` libera o slot. xpcom tolera (poll fallback). **20 host asserts + 4 router regressions planejados/revisados**. | ~~2-3 semanas~~ feito (storage-only; eventos disparam quando VFS notify landar) | xpcom prefs watcher |
| S1.17 | ✅ **DONE** (sessao 10) `prlimit64` em `linux_process.c`. RLIMIT_AS = 8 GiB, RLIMIT_NOFILE = 1024, RLIMIT_STACK soft=8 MiB hard=64 MiB (Linux defaults). Outros recursos retornam RLIM_INFINITY. Tentativa de set -> -EPERM (kernel policy fixed). pid != current -> -EPERM. | ~~1 semana~~ feito | F1, F3 |
| S1.18 | ✅ **DONE** (sessao 10) `gettid` em `linux_process.c` retorna `task_current()->pid` via accessor injetavel. (CapyOS hoje tem 1 thread por process, entao gettid == getpid; quando S1.4 clone landar com thread groups, tid divergira de pid sem mudar este modulo.) | ~~1 semana~~ feito | logging |

**Bonus syscalls (sessao 23, sem ID atribuido) --
stdio comfort:** apos sessao 22 ter declarado Marco
M1 ABI surface complete, estes 5 NRs nao bloqueiam
boot mas evitam degradacoes (printf single-byte
unbuffered, file-size queries falhando). 2 modulos
novos:
(a) **`linux_io.c`** (NRs 17, 18, 19, 20) --
`readv`/`writev` iteram iovec chamando linux_vfs_*
para cada elemento. Linux semantics: iovcnt=0 -> 0;
< 0 ou > IOV_MAX (1024) -> -EINVAL; NULL com count
-> -EFAULT; first-element error forwarded, later-
element error truncated to partial count, short
read/write stops iter. `pread64`/`pwrite64` fazem
save-pos + seek + IO + restore-pos via
linux_vfs_lseek. offset < 0 -> -EINVAL; ESPIPE
forwarded.
(b) **`linux_stat.c`** (NRs 4, 5, 6) -- `fstat`
sintetiza metadata: stdin/stdout/stderr -> S_IFCHR,
outros -> S_IFREG; size=0 default; nlink=1,
blksize=4096. Os 2 campos confiaveis para userland
(st_mode + st_size) saem corretos. `stat`/`lstat`
foram refinados na sessao 39: conhecidos pseudo paths
(`/`, `/dev`, `/proc`, `/tmp`, fixed `/dev/*` e
`/proc/*`) retornam metadata sintetica; desconhecidos
preservam -ENOSYS para fallback open+fstat ate namei.
**30 host asserts** (16 io + 14 stat). musl printf
agora pode usar buffered I/O via writev; SpiderMonkey
source-map readers via pread64.

**Bonus metadata refinement (sessao 39, sem ID atribuido) --
known-path stat family:** `stat`/`lstat`/`fstatat`/`statx`
agora compartilham uma projecao conservadora de metadata para
pseudo paths conhecidos. `linux_stat.c` reconhece dirs (`/`,
`/dev`, `/dev/shm`, `/proc`, `/proc/self`, `/tmp`), char devices
(`/dev/{null,zero,full,random,urandom}`) e fixed proc files
(`/proc/{cpuinfo,meminfo,version,uptime,loadavg}` +
`/proc/self/{maps,exe,cmdline,status}`). `lstat("/proc/self/exe")`
retorna S_IFLNK; `stat("/proc/self/exe")` segue para S_IFREG.
`fstatat(AT_FDCWD, path, ...)` delega para stat/lstat conforme
AT_SYMLINK_NOFOLLOW; `statx(AT_FDCWD, path, ...)` projeta a mesma
struct stat sintetica para struct statx e tambem respeita
AT_SYMLINK_NOFOLLOW. Unknown paths continuam -ENOSYS para preservar
fallback open+fstat ate um namei real.
Regressions planejadas/revisadas: 7 cenarios em `test_linux_stat.c`,
3 em `test_linux_at.c`, 2 em `test_linux_statx.c`. Validacao desta
sessao foi por revisao estatica, sem executar `make`, `git` ou
scripts, conforme instrucao do operador. **NR count permanece 252**.

**Bonus permission-probe refinement (sessao 40, sem ID atribuido) --
known-path access family:** `access`/`faccessat` agora usam
`linux_stat_path_is_known()` como fonte unica do mesmo conjunto
conservador de pseudo paths conhecidos usado por `stat`/`lstat`:
dirs (`/`, `/dev`, `/dev/shm`, `/proc`, `/proc/self`,
`/tmp`), char devices (`/dev/{null,zero,full,random,urandom}`) e
fixed proc files (`/proc/{cpuinfo,meminfo,version,uptime,loadavg}` +
`/proc/self/{maps,exe,cmdline,status}`). Qualquer modo R|W|X|F passa
para esses paths porque Marco M1 roda como effective root; unknown
paths continuam -ENOENT ate namei real. Regressions planejadas/
revisadas: +2 cenarios de `access` e +2 de `faccessat` em
`test_linux_at.c`. Validacao desta sessao foi por revisao estatica,
sem executar `make`, `git` ou scripts. **NR count permanece 252**.

**Bonus fd-readiness refinement (sessao 41, sem ID atribuido) --
signalfd4 storage-only + eventfd/timerfd VFS routing:** `signalfd4`
agora cria/atualiza um fd storage-only em `linux_eventfd.c`, valida
flags e `sizemask == 8`, remove SIGKILL/SIGSTOP da mascara armazenada
e retorna -EAGAIN em read ate existir signal delivery real. O VFS
router agora reconhece os ranges eventfd/signalfd/timerfd para
`read`/`write`/`close`/`lseek`: eventfd generic read/write funciona,
timerfd generic read retorna expirations e signalfd generic read
degrada para -EAGAIN. Regressions planejadas/revisadas: +8 cenarios
em `test_linux_eventfd.c` e +6 em `test_linux_vfs_router.c`.
Validacao desta sessao foi por revisao estatica, sem executar
`make`, `git` ou scripts. **NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 42, sem ID atribuido) --
memfd_secret VFS lifecycle:** `memfd_secret` ja alocava fds em
`linux_modern_misc.c`; agora esses fds tambem sao reconhecidos pelo
VFS router. `close(2)` libera o slot secretmem para reutilizacao, e
`read(2)`/`write(2)`/`lseek(2)` em fd vivo retornam -ENOSYS ate
existir backing/mmap real, evitando falso -EBADF. Regressions
planejadas/revisadas: +5 cenarios em `test_linux_modern_misc.c` e
+4 em `test_linux_vfs_router.c`. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 43, sem ID atribuido) --
inotify VFS lifecycle:** `inotify_init1` ja criava instancias
storage-only em `linux_inotify.c`; agora esses fds tambem passam pelo
VFS router. `close(2)` libera o slot, `read(2)` em fd vivo retorna
-EAGAIN ate existir fila de eventos do fs-notifier, `write(2)` retorna
-EINVAL e `lseek(2)` retorna -ESPIPE. Regressions planejadas/
revisadas: +5 cenarios em `test_linux_inotify.c` e +4 em
`test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 44, sem ID atribuido) --
epoll VFS lifecycle:** `epoll_create1` ja criava instancias
funcionais em `linux_epoll.c`; agora esses fds tambem passam pelo
VFS router. `close(2)` libera o slot, `read(2)`/`write(2)` em fd vivo
retornam -EINVAL e `lseek(2)` retorna -ESPIPE, mantendo
`epoll_wait`/`epoll_pwait` como caminho de consumo de eventos.
Regressions planejadas/revisadas: +5 cenarios em `test_linux_epoll.c`
e +4 em `test_linux_vfs_router.c`. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 45, sem ID atribuido) --
fanotify VFS lifecycle:** `fanotify_init` ja criava instancias
storage-only em `linux_fanotify.c`; agora esses fds tambem passam pelo
VFS router. `close(2)` libera o slot, `read(2)` em fd vivo retorna
-EAGAIN ate existir fila de eventos do fs-notifier, `write(2)` retorna
-EINVAL e `lseek(2)` retorna -ESPIPE. Regressions planejadas/
revisadas: +5 cenarios em `test_linux_fanotify.c` e +4 em
`test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 46, sem ID atribuido) --
memfd/pidfd VFS lifecycle:** `memfd_create` e `pidfd_open` ja criavam
fds storage-only em `linux_memfd.c`; agora esses ranges tambem passam
pelo VFS router. `close(2)` libera o slot dono, memfd generic
`read(2)`/`write(2)`/`lseek(2)` retornam -ENOSYS ate existir backing
real/ftruncate/mmap, enquanto pidfd generic `read(2)`/`write(2)`
retornam -EINVAL e `lseek(2)` retorna -ESPIPE. Regressions
planejadas/revisadas: +5 cenarios em `test_linux_memfd.c` e +4 em
`test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 47, sem ID atribuido) --
userfaultfd VFS lifecycle:** `userfaultfd` ja criava fds storage-only
em `linux_jit_aux.c`; agora o range `0xA000` tambem passa pelo VFS
router. `close(2)` libera o slot, `read(2)` em fd vivo retorna
-EAGAIN ate existir fila de eventos de page fault, `write(2)` retorna
-EINVAL e `lseek(2)` retorna -ESPIPE. Regressions planejadas/
revisadas: +5 cenarios em `test_linux_jit_aux.c` e +4 em
`test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd lifecycle refinement (sessao 48, sem ID atribuido) --
landlock ruleset VFS lifecycle:** `landlock_create_ruleset` ja criava
fds de ruleset em `linux_landlock.c`; agora o range `0xB000` tambem
passa pelo VFS router. `close(2)` libera o slot, `read(2)` e
`write(2)` em fd vivo retornam -EINVAL e `lseek(2)` retorna -ESPIPE.
Regressions planejadas/revisadas: +5 cenarios em
`test_linux_landlock.c` e +4 em `test_linux_vfs_router.c`. Validacao
desta sessao foi por revisao estatica, sem executar `make`, `git` ou
scripts.
**NR count permanece 252**.

**Bonus fd core hardening (sessao 49, sem ID atribuido) --
linux_fd callback hygiene:** `linux_fd_install_ops(NULL)` agora limpa o
bundle de callbacks instalado, alinhando `linux_fd` ao padrao de reset
dos demais modulos e evitando reutilizacao acidental de hooks antigos
de `pipe2`/`dup3` em testes, reconfiguracao ou boot parcial. Regressions
planejadas/revisadas: +2 cenarios em `test_linux_fd.c` cobrindo
limpeza de callbacks de pipe e dup3. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus exec-ext hardening (sessao 50, sem ID atribuido) --
linux_exec_ext callback hygiene:** `linux_exec_ext_install_ops(NULL)` e
`linux_exec_ext_reset_for_tests()` agora zeram o bundle de callbacks de
`close_range`, em vez de apenas desligar o flag de instalado. Isso
reduz risco de hooks stale em testes/reconfiguracao e deixa o modulo
simetrico com o hardening aplicado em `linux_fd`. Regressions
planejadas/revisadas: +2 cenarios em `test_linux_exec_ext.c` cobrindo
limpeza dos callbacks `close_one` e `set_cloexec_one`. Validacao desta
sessao foi por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd core hardening (sessao 51, sem ID atribuido) --
linux_dup callback hygiene:** `linux_dup_install_ops(NULL)` e
`linux_dup_reset_for_tests()` agora zeram o bundle de callbacks de
`dup`/`dup2`, em vez de apenas desligar o flag de instalado. Isso
alinha `linux_dup` com `linux_fd` e `linux_exec_ext`, reduzindo risco de
hooks stale em testes/reconfiguracao. Regressions planejadas/revisadas:
+2 cenarios em `test_linux_dup.c` cobrindo limpeza de callback `dup2`
via install NULL e limpeza de callbacks via reset. Validacao desta
sessao foi por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus process hardening (sessao 52, sem ID atribuido) --
linux_process callback hygiene:** `linux_process_install_ops(NULL)`
agora zera o bundle de callbacks de task accessors, alinhando o modulo
com `linux_fd`, `linux_exec_ext` e `linux_dup`. Isso evita reuso
acidental de accessors antigos em testes/reconfiguracao para `gettid`,
`sched_yield` e `sched_*affinity`. Regressions planejadas/revisadas:
+3 cenarios em `test_linux_process.c` cobrindo limpeza de callbacks de
gettid, yield e affinity. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd core hardening (sessao 53, sem ID atribuido) --
linux_memfd provider hygiene:** `linux_memfd_install_ops(NULL)` agora
zera o provider `pid_exists`, impedindo que callbacks antigos continuem
afetando `pidfd_open` e `pidfd_send_signal(sig=0)` apos
desinstalacao/reconfiguracao. Regressions planejadas/revisadas: +2
cenarios em `test_linux_memfd.c` cobrindo limpeza de provider para
`pidfd_open` e probe `pidfd_send_signal`. Validacao desta sessao foi
por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus fd core hardening (sessao 54, sem ID atribuido) --
linux_vfs provider hygiene:** `linux_vfs_install_ops(NULL)` agora zera
o bundle central de callbacks de file I/O (`open`, `close`, `read`,
`write`, `lseek`). Isso impede reuso acidental de providers antigos na
porta principal do VFS Linux durante testes/reconfiguracao e preserva
fallbacks deterministas `-ENOSYS`. Regressions planejadas/revisadas:
+4 cenarios em `test_linux_vfs.c` cobrindo limpeza de open, close,
read/write e lseek. Validacao desta sessao foi por revisao estatica,
sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus TLS hardening (sessao 55, sem ID atribuido) --
linux_arch_prctl provider hygiene:** `linux_arch_prctl_install_ops(NULL)`
e `linux_arch_prctl_reset_for_tests()` agora zeram o bundle de
callbacks de MSR/TLS (`SET/GET_FS`, `SET/GET_GS`), alem de desligar o
flag de instalado. Isso evita estado stale em reconfiguracao/testes no
syscall mais critico do bootstrap musl TLS. Regressions planejadas/
revisadas: +3 cenarios em `test_linux_arch_prctl.c` cobrindo limpeza de
callbacks SET, GET e reset. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus memory hardening (sessao 56, sem ID atribuido) --
linux_mmap provider hygiene:** `linux_mmap_install_ops(NULL)` agora
zera o bundle de callbacks do provider de memoria (`alloc_anon`,
`alloc_anon_at`, `free_pages`, `protect`, `remap`). Isso evita reuso
acidental de callbacks antigos nos caminhos criticos de `mmap`,
`munmap`, `mprotect` e `mremap`, incluindo o padrao SpiderMonkey JIT
`PROT_READ|WRITE|EXEC`. Regressions planejadas/revisadas: +4 cenarios
em `test_linux_mmap.c` cobrindo limpeza de callbacks de alloc, release/
protect, remap e reset. Validacao desta sessao foi por revisao estatica,
sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus sync hardening (sessao 57, sem ID atribuido) --
linux_futex provider hygiene:** `linux_futex_install_ops(NULL)` agora
zera o bundle de callbacks de sincronizacao (`atomic_load_u32`,
`block_on`, `wake`). Isso evita reutilizacao acidental de callbacks
antigos nos caminhos de pthread/mutex/condvar (`FUTEX_WAIT`, `WAKE` e
`REQUEUE`) durante testes/reconfiguracao. Regressions planejadas/
revisadas: +4 cenarios em `test_linux_futex.c` cobrindo limpeza de
WAIT, WAKE, REQUEUE e reset. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus path hardening (sessao 58, sem ID atribuido) --
linux_path provider hygiene:** `linux_path_install(NULL)` e
`linux_path_reset_for_tests()` agora zeram o provider
`resolve_proc_self_exe`, impedindo reutilizacao acidental de resolvers
antigos para `readlink("/proc/self/exe")` e
`readlinkat(AT_FDCWD, "/proc/self/exe", ...)`. Isso estabiliza o caminho
usado por musl, crash reporters, debuggers e profilers para descobrir o
binario corrente. Regressions planejadas/revisadas: +3 cenarios em
`test_linux_path.c` cobrindo limpeza de readlink, readlinkat e reset.
Validacao desta sessao foi por revisao estatica, sem executar `make`,
`git` ou scripts.
**NR count permanece 252**.

**Bonus network hardening (sessao 59, sem ID atribuido) --
linux_net provider hygiene:** `linux_net_install_ops(NULL)` agora zera
o bundle de callbacks de extensoes socket (`accept4`, `recvmmsg`,
`sendmmsg`). Isso garante que o boot sem socket layer (`linux_net_init_boot`
instala `NULL`) e reconfiguracoes/testes nao reutilizem callbacks antigos
e preservem o fallback deterministico `-ENOSYS`. Regressions planejadas/
revisadas: +4 cenarios em `test_linux_net.c` cobrindo limpeza de
accept4, recvmmsg, sendmmsg e reset. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus path-resolution hardening (sessao 60, sem ID atribuido) --
linux_openat2 provider hygiene:** `linux_openat2_install_ops(NULL)` e
`linux_openat2_reset_for_tests()` agora zeram o bundle de callbacks de
path-resolution endurecido (`openat2`, `faccessat2`), alem de desligar
o flag de instalado. Isso evita reuso de providers antigos em caminhos
de sandbox Firefox (`RESOLVE_BENEATH`, `RESOLVE_NO_SYMLINKS`) e probes
bubblewrap `faccessat2(AT_EACCESS)`. Regressions planejadas/revisadas:
+3 cenarios em `test_linux_openat2.c` cobrindo limpeza de openat2,
faccessat2 e reset. Validacao desta sessao foi por revisao estatica,
sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus process-VM hardening (sessao 61, sem ID atribuido) --
linux_proc_vm provider hygiene:** `linux_proc_vm_install_ops(NULL)` e
`linux_proc_vm_reset_for_tests()` agora zeram o bundle de callbacks de
introspeccao process-VM (`read_self`, `write_self`, `current_pid`),
alem de desligar o flag de instalado. Isso impede reuso de providers
antigos nos caminhos de profiler/debugger (`process_vm_readv`,
`process_vm_writev`) e preserva os fallbacks self/foreign deterministas.
Regressions planejadas/revisadas: +3 cenarios em
`test_linux_proc_vm.c` cobrindo limpeza de readv/current_pid,
writev/current_pid e reset. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus zero-copy hardening (sessao 62, sem ID atribuido) --
linux_pipe_zero provider hygiene:** `linux_pipe_zero_install_ops(NULL)` e
`linux_pipe_zero_reset_for_tests()` agora zeram o bundle de callbacks de
zero-copy (`splice`, `tee`, `vmsplice`), alem de desligar o flag de
instalado. Isso evita reuso de providers antigos nos caminhos de cache/
IPC de alta performance e preserva o fallback deterministico `-ENOSYS`
para read/write ou writev. Regressions planejadas/revisadas: +4
cenarios em `test_linux_pipe_zero.c` cobrindo limpeza de splice, tee,
vmsplice e reset. Validacao desta sessao foi por revisao estatica, sem
executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus resource-limit hardening (sessao 63, sem ID atribuido) --
linux_rlimit_legacy provider hygiene:**
`linux_rlimit_legacy_install_ops(NULL)` e
`linux_rlimit_legacy_reset_for_tests()` agora zeram o bundle de callbacks
de limites de recurso (`get_limit`, `set_limit`), alem de desligar o flag
de instalado. Isso evita reuso de providers antigos nos probes de
`RLIMIT_NOFILE`, `RLIMIT_STACK` e `RLIMIT_AS`, preservando defaults
sinteticos e `setrlimit` no-op deterministico. Regressions planejadas/
revisadas: +3 cenarios em `test_linux_rlimit_legacy.c` cobrindo limpeza
de getrlimit, setrlimit e reset. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus filesystem-stats hardening (sessao 64, sem ID atribuido) --
linux_statfs provider hygiene:** `linux_statfs_install_providers(NULL)` e
`linux_statfs_reset_for_tests()` agora zeram o bundle de providers de
estatisticas de filesystem (`total_blocks`, `total_files`), alem de
desligar o flag de instalado. Isso impede reuso de providers antigos em
checks de espaco livre do Firefox, SQLite WAL e probes GIO/gvfs,
preservando o fallback tmpfs sintetico deterministico. Regressions
planejadas/revisadas: +2 cenarios em `test_linux_statfs.c` cobrindo
limpeza por install NULL em `statfs` e por reset em `fstatfs`. Validacao
desta sessao foi por revisao estatica, sem executar `make`, `git` ou
scripts.
**NR count permanece 252**.

**Bonus system-info hardening (sessao 65, sem ID atribuido) --
linux_sysinfo provider hygiene:** `linux_sysinfo_install(NULL)` e
`linux_sysinfo_reset_for_tests()` agora zeram o bundle de providers de
informacao de sistema (`total_ram_bytes`, `free_ram_bytes`,
`uptime_seconds`, `nproc`), alem de desligar o flag de instalado. Isso
impede reuso de providers antigos em heuristicas de memoria/uptime do
Firefox `nsSystemInfo`, musl `pthread_create` e crash-report metadata,
preservando defaults deterministicos (`mem_unit=1`, `procs=1`, demais
campos zero). Regressions planejadas/revisadas: +1 cenario novo de reset
e reforco do cenario install NULL em `test_linux_sysinfo.c`, com
contadores garantindo que callbacks antigos nao sejam invocados.
Validacao desta sessao foi por revisao estatica, sem executar `make`,
`git` ou scripts.
**NR count permanece 252**.

**Bonus filesystem-mutation hardening (sessao 66, sem ID atribuido) --
linux_fs_mut provider hygiene:** `linux_fs_mut_install_ops(NULL)` e
`linux_fs_mut_reset_for_tests()` agora zeram o bundle de callbacks de
mutacao de filesystem (`mkdir`, `rmdir`, `unlink`, `rename`), alem de
desligar o flag de instalado. Isso impede reuso de providers antigos em
operacoes destrutivas de profile/cache, preservando o fallback
deterministico `-ENOSYS` ate tmpfs/namei instalar hooks reais.
Regressions planejadas/revisadas: cenario install NULL reforcado para
cobrir todos os callbacks e +1 cenario de reset em `test_linux_fs_mut.c`.
Validacao desta sessao foi por revisao estatica, sem executar `make`,
`git` ou scripts.
**NR count permanece 252**.

**Bonus filesystem-metadata hardening (sessao 67, sem ID atribuido) --
linux_fs_meta provider hygiene:** `linux_fs_meta_install_ops(NULL)` e
`linux_fs_meta_reset_for_tests()` agora zeram o bundle de callbacks de
metadados de filesystem (`chmod_path`, `chmod_fd`, `chown_path`,
`chown_fd`), alem de desligar o flag de instalado. Isso impede reuso de
providers antigos em lockdown de permissao de profile/cache e hardening de
arquivos temporarios, preservando o fallback deterministico `-ENOSYS` ate
tmpfs/namei instalar metadados reais. Regressions planejadas/revisadas:
cenario install NULL reforcado para cobrir chmod/fchmod/chown/fchown e +1
cenario de reset em `test_linux_fs_meta.c`. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus working-directory hardening (sessao 68, sem ID atribuido) --
linux_chdir provider hygiene:** `linux_chdir_install_ops(NULL)` e
`linux_chdir_reset_for_tests()` agora zeram o bundle de callbacks de cwd
(`chdir_path`, `chdir_fd`), alem de desligar o flag de instalado. Isso
impede reuso de providers antigos em mudancas de diretorio de trabalho,
preservando o fallback deterministico `-ENOSYS` ate o modelo per-task cwd
e tmpfs/namei instalarem estado real. Regressions planejadas/revisadas:
cenario install NULL reforcado para cobrir `chdir`/`fchdir` e +1 cenario
de reset em `test_linux_chdir.c`. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus sendfile hardening (sessao 69, sem ID atribuido) --
linux_advise provider hygiene:** `linux_advise_install_ops(NULL)` e
`linux_advise_reset_for_tests()` agora zeram o bundle de callbacks de
kernel-copy (`sendfile`), alem de desligar o flag de instalado. Isso
impede reuso de providers antigos no caminho de copia de dados e preserva
o fallback deterministico `-ENOSYS` para read/write em userland quando nao
ha backend real. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_advise.c` cobrindo install NULL e reset com preservacao de
offset e sem chamada ao callback antigo. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus legacy-time hardening (sessao 70, sem ID atribuido) --
linux_time_legacy provider hygiene:**
`linux_time_legacy_install_ops(NULL)` e
`linux_time_legacy_reset_for_tests()` agora zeram o bundle de callbacks de
tempo legado (`now_seconds`), alem de desligar o flag de instalado. Isso
impede reuso de providers antigos em `time(2)` e preserva fallback
deterministico para epoch `0` quando nao ha provider real; `getcpu`
continua single-CPU `0/0`. Regressions planejadas/revisadas: reforco de
contadores em `test_linux_time_legacy.c` e +1 cenario de reset garantindo
que callbacks antigos nao sejam invocados. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus sandbox hardening (sessao 71, sem ID atribuido) --
linux_sandbox provider hygiene:** `linux_sandbox_install_ops(NULL)` e
`linux_sandbox_reset_for_tests()` agora zeram o bundle de callbacks de
sandbox (`chroot`), alem de desligar o flag de instalado. Isso impede
reuso de provider antigo em superficie de sandbox/chroot e preserva o
fallback seguro de single-root no-op para caminhos bem formados quando nao
ha provider real. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_sandbox.c` cobrindo install NULL e reset sem chamada ao
callback antigo. Validacao desta sessao foi por revisao estatica, sem
executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus time-setter hardening (sessao 72, sem ID atribuido) --
linux_settod provider hygiene:** `linux_settod_install_ops(NULL)` e
`linux_settod_reset_for_tests()` agora zeram o bundle de callbacks de
time-of-day setter (`set_seconds`), alem de desligar o flag de instalado.
Isso impede reuso de provider antigo em mutacao de wall-clock/CAP_SYS_TIME
e preserva fallback no-op `0` para chamadas bem formadas quando nao ha RTC
real. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_settod.c` cobrindo install NULL e reset sem chamada ao provider
antigo. Validacao desta sessao foi por revisao estatica, sem executar
`make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus link hardening (sessao 73, sem ID atribuido) --
linux_link provider hygiene:** `linux_link_install_ops(NULL)` e
`linux_link_reset_for_tests()` agora zeram o bundle de callbacks de
hard/soft-link (`hard_link`, `sym_link`), alem de desligar o flag de
instalado. Isso impede reuso de provider antigo em mutacoes de link usadas
por cache/update patterns e preserva fallback deterministico `-ENOSYS` ate
tmpfs/namei instalar links reais. Regressions planejadas/revisadas:
cenario install NULL reforcado para cobrir `link`/`symlink` e +1 cenario
de reset em `test_linux_link.c`. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus timestamp hardening (sessao 74, sem ID atribuido) --
linux_utime provider hygiene:** `linux_utime_install_ops(NULL)` e
`linux_utime_reset_for_tests()` agora zeram o bundle de callbacks de
timestamp (`utime_path`, `utime_fd`, `now`), alem de desligar o flag de
instalado. Isso impede reuso de provider antigo em updates de metadados
temporais usados por cache HTTP, build tooling e compatibilidade libc, e
preserva fallback deterministico `-ENOSYS` ate tmpfs/namei instalar
timestamps reais. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_utime.c` cobrindo install NULL e reset para path/fd, incluindo
garantia de que a fonte `UTIME_NOW` antiga nao e' chamada. Validacao desta
sessao foi por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus interval-timer hardening (sessao 75, sem ID atribuido) --
linux_itimer provider hygiene:** `linux_itimer_install_ops(NULL)` e
`linux_itimer_reset_for_tests()` agora zeram o bundle de callbacks de
ticks (`now_ticks`), alem de desligar o flag de instalado e resetar o
estado storage-only de alarm/itimers. Isso impede reuso de provider antigo
em `times(2)` e preserva fallback deterministico para tick `0` enquanto
per-task CPU accounting real nao existe. Regressions planejadas/revisadas:
reforco de contadores em `test_linux_itimer.c` e +2 cenarios cobrindo
install NULL e reset sem chamada ao callback antigo. Validacao desta
sessao foi por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus copy-range hardening (sessao 76, sem ID atribuido) --
linux_lock provider hygiene:** `linux_lock_install_ops(NULL)` e
`linux_lock_reset_for_tests()` agora zeram o bundle de callbacks de
kernel-copy (`copy_file_range`), alem de desligar o flag de instalado;
`reset_for_tests()` preserva o reset da tabela storage-only de flock. Isso
impede reuso de provider antigo em caminho de copia eficiente e preserva
fallback deterministico `-ENOSYS` para read/write em userland quando nao ha
backend real. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_lock.c` cobrindo install NULL e reset sem chamada ao callback
antigo. Validacao desta sessao foi por revisao estatica, sem executar
`make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus capability hardening (sessao 77, sem ID atribuido) --
linux_caps provider hygiene:** `linux_caps_install_ops(NULL)` e
`linux_caps_reset_for_tests()` agora zeram o bundle de callbacks de
capabilities (`get_caps`, `set_caps`), alem de desligar o flag de
instalado. Isso impede reuso de provider antigo em uma superficie direta de
seguranca usada por sandbox Firefox/bubblewrap e preserva fallback
deterministico root-with-all-caps/no-op set enquanto credenciais por tarefa
nao existem. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_caps.c` cobrindo install NULL e reset sem chamada aos callbacks
antigos. Validacao desta sessao foi por revisao estatica, sem executar
`make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus signal hardening (sessao 78, sem ID atribuido) --
linux_kill provider hygiene:** `linux_kill_install_ops(NULL)` e
`linux_kill_reset_for_tests()` agora zeram o bundle de callbacks de entrega
de sinais (`getpid`, `deliver`), alem de desligar o flag de instalado.
Isso impede reuso de provider antigo em self-signal/signal-delivery e
preserva fallback deterministico single-task/no-op para `kill`, `tgkill` e
`tkill` quando o backend real de sinais nao esta instalado. Regressions
planejadas/revisadas: +2 cenarios em `test_linux_kill.c` cobrindo install
NULL e reset sem chamada ao callback antigo. Validacao desta sessao foi por
revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus durability hardening (sessao 79, sem ID atribuido) --
linux_sync provider hygiene:** `linux_sync_install_ops(NULL)` e
`linux_sync_reset_for_tests()` agora zeram o bundle de callbacks de
durabilidade (`sync_all`, `sync_fs`, `sync_fd`), alem de desligar o flag de
instalado. Isso impede reuso de provider antigo em flush/fsync paths de
SQLite/cache Firefox e preserva fallback deterministico RAM-only no-op
quando nao ha backend persistente real. Regressions planejadas/revisadas:
+2 cenarios em `test_linux_sync.c` cobrindo install NULL e reset para
`sync`, `syncfs`, `fsync` e `fdatasync` sem chamada ao callback antigo.
Validacao desta sessao foi por revisao estatica, sem executar `make`,
`git` ou scripts.
**NR count permanece 252**.

**Bonus heap hardening (sessao 80, sem ID atribuido) --
linux_brk provider hygiene:** `linux_brk_install_ops(NULL)` e
`linux_brk_reset_for_tests()` agora zeram o bundle de callbacks de reserva
de heap (`reserve_pages`), alem de desligar o flag de instalado e preservar
o reset do break/committed para `LINUX_BRK_BASE`. Isso impede reuso de
provider antigo em grow de heap e preserva a semantica Linux de falha de
`brk`: retornar o break atual sem chamar VMM antigo quando nao ha provider.
Regressions planejadas/revisadas: +2 cenarios em `test_linux_brk.c`
cobrindo install NULL e reset sem chamada ao callback antigo. Validacao
desta sessao foi por revisao estatica, sem executar `make`, `git` ou
scripts.
**NR count permanece 252**.

**Bonus truncate hardening (sessao 81, sem ID atribuido) --
linux_trunc provider hygiene:** `linux_trunc_install_ops(NULL)` e
`linux_trunc_reset_for_tests()` agora zeram o bundle de callbacks de resize
por fd (`ftruncate`), alem de desligar o flag de instalado. Isso impede
reuso de provider antigo em resizing de arquivos e preserva fallback
deterministico `-ENOSYS` ate tmpfs/VFS instalar resize real por fd.
Regressions planejadas/revisadas: reforco do cenario install NULL para
garantir zero chamadas antigas e +1 cenario reset em `test_linux_trunc.c`.
Validacao desta sessao foi por revisao estatica, sem executar `make`,
`git` ou scripts.
**NR count permanece 252**.

**Bonus eventfd hardening (sessao 82, sem ID atribuido) --
linux_eventfd provider hygiene:** `linux_eventfd_install_ops(NULL)` agora
zera o bundle de callbacks de alocacao de fd (`alloc_fd`) em vez de manter
um alocador antigo. Isso impede reuso de provider stale no caminho de
criacao de `eventfd`/`eventfd2` e preserva fallback deterministico para
fds baseados em slot (`LINUX_EVENTFD_FD_BASE + slot`) quando nao ha fd table
real instalada. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_eventfd.c` cobrindo install NULL e reset sem chamada ao
callback antigo. Validacao desta sessao foi por revisao estatica, sem
executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus seccomp hardening (sessao 83, sem ID atribuido) --
linux_seccomp provider hygiene:** `linux_seccomp_install_ops(NULL)` e
`linux_seccomp_reset_for_tests()` agora zeram o bundle de callbacks de
filtro (`install_filter`), alem de desligar o flag de instalado. Isso
impede reuso de provider antigo em `SECCOMP_SET_MODE_FILTER`, superficie
direta do sandbox Firefox/Chromium, e preserva fallback estrutural de
aceitar e descartar o filtro enquanto nao existe backend BPF real.
Regressions planejadas/revisadas: +2 cenarios em `test_linux_seccomp.c`
cobrindo install NULL e reset sem chamada ao callback antigo. Validacao
desta sessao foi por revisao estatica, sem executar `make`, `git` ou
scripts.
**NR count permanece 252**.

**Bonus epoll hardening (sessao 84, sem ID atribuido) --
linux_epoll provider hygiene:** `linux_epoll_install_ops(NULL)` agora zera
o bundle de callbacks de wait (`fd_ready`, `yield`) em vez de preservar
callbacks antigos. Isso impede reuso de readiness/yield provider stale em
`epoll_wait`/`epoll_pwait`, caminho central para libevent/Chromium IPC, e
preserva fallback deterministico de zero eventos quando nao ha readiness
oracle instalado. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_epoll.c` cobrindo install NULL e reset sem chamada aos callbacks
antigos. Validacao desta sessao foi por revisao estatica, sem executar
`make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus process-lifecycle hardening (sessao 85, sem ID atribuido) --
linux_exit provider hygiene:** `linux_exit_install_ops(NULL)` e
`linux_exit_reset_for_tests()` agora zeram o bundle de callbacks de
terminacao de tarefa (`exit_task`), alem de desligar o flag de instalado.
Isso impede reuso de provider antigo no caminho noreturn de `exit` e
`exit_group`, preservando fallback sentinela `-ENOSYS` em tests quando nao
ha backend de task lifecycle instalado. Regressions planejadas/revisadas:
+2 cenarios em `test_linux_exit.c` cobrindo install NULL e reset sem
chamada ao callback antigo. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus process-group hardening (sessao 86, sem ID atribuido) --
linux_pgrp provider hygiene:** `linux_pgrp_install_ops(NULL)` e
`linux_pgrp_reset_for_tests()` agora zeram o bundle de callbacks de pid
(`getpid`), alem de desligar o flag de instalado. Isso impede reuso de
provider antigo nos caminhos de grupo/sessao de processo (`setpgid`,
`getpgid`, `setsid`, `getsid`) e preserva fallback single-task
deterministico com pid/sid/pgid default 1 quando nao ha task provider
instalado. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_pgrp.c` cobrindo install NULL e reset sem chamada ao callback
antigo. Validacao desta sessao foi por revisao estatica, sem executar
`make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus entropy-source hardening (sessao 87, sem ID atribuido) --
linux_random source hygiene:** `linux_random_install_source(NULL)` agora tem
regression guard explicito garantindo limpeza da fonte de entropia instalada
e fallback `-EAGAIN` em `getrandom` sem chamada a fonte antiga. Isso protege
uma superficie direta de seguranca/criptografia usada por NSS, SpiderMonkey
seed/GC e geracao de tokens, preservando comportamento deterministico quando
o CSPRNG ainda nao foi instalado no boot/test setup. Regressions
planejadas/revisadas: +1 cenario em `test_linux_random.c` cobrindo install
NULL da source. Validacao desta sessao foi por revisao estatica, sem
executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus epoll readiness refinement (sessao 88, sem ID atribuido) --
eventfd/timerfd readiness:** `linux_epoll_init_boot()` agora instala
`linux_eventfd_family_poll_events()` como provider real de `fd_ready`.
Com isso, `epoll_wait`/`epoll_pwait` passam a observar `EPOLLIN` quando
um `eventfd` tem counter > 0 e quando um `timerfd` armado expira; `eventfd`
tambem reporta `EPOLLOUT` enquanto uma escrita de 1 nao causaria overflow.
`signalfd` continua storage-only e retorna sem readiness ate existir signal
delivery real; outras classes de fd preservam fallback zero-event. Regressions
planejadas/revisadas: +2 cenarios em `test_linux_epoll.c` cobrindo
readiness de eventfd e timerfd. Validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus epoll readiness contract coverage (sessao 89, sem ID atribuido) --
eventfd EPOLLOUT/drain:** cobertura host de `epoll` agora fixa dois
contratos complementares de `linux_eventfd_family_poll_events()`: eventfd
gravavel gera `EPOLLOUT`, e eventfd com `EPOLLIN` deixa de ficar ready
apos `linux_eventfd_read()` drenar o counter em modo normal. Isso reduz
risco de regressao em loops async estilo libevent/Firefox que dependem de
writability e de re-arm natural apos consumo. Regressions planejadas/
revisadas: +2 cenarios em `test_linux_epoll.c`; validacao desta sessao foi
por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus epoll timerfd contract coverage (sessao 90, sem ID atribuido) --
one-shot drain/periodic rearm:** cobertura host de `epoll` agora fixa dois
contratos de timerfd sobre `linux_eventfd_family_poll_events()`: timerfd
one-shot deixa de gerar `EPOLLIN` depois de `linux_timerfd_read()` consumir
a expiracao e desarmar o timer, enquanto timerfd periodico reprograma a
proxima expiracao e volta a gerar `EPOLLIN` quando o clock alcanca o novo
deadline. Isso reforca compatibilidade com runtimes que usam timerfd como
fonte de wakeup em loops async. Regressions planejadas/revisadas: +2
cenarios em `test_linux_epoll.c`; validacao desta sessao foi por revisao
estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus eventfd poll oracle coverage (sessao 91, sem ID atribuido) --
direct provider contract:** `test_linux_eventfd.c` agora fixa diretamente o
contrato de `linux_eventfd_family_poll_events()`, alem da cobertura indireta
via `epoll`: eventfd vazio reporta `EPOLLOUT`, eventfd com counter reporta
`EPOLLIN|EPOLLOUT`, leitura normal drena `EPOLLIN`, signalfd storage-only
permanece sem readiness e timerfd one-shot passa de sem readiness para
`EPOLLIN` ao expirar e volta a zero depois do read/disarm. Regressions
planejadas/revisadas: +3 cenarios em `test_linux_eventfd.c`; validacao
desta sessao foi por revisao estatica, sem executar `make`, `git` ou
scripts.
**NR count permanece 252**.

**Bonus eventfd poll oracle edge coverage (sessao 92, sem ID atribuido) --
saturation/semaphore:** cobertura direta do provider agora fixa os cantos de
readiness que evitam loops async incorretos: eventfd saturado em
`UINT64_MAX-1` continua readable (`EPOLLIN`) mas nao writable (`EPOLLOUT`),
e eventfd em modo `EFD_SEMAPHORE` mantem `EPOLLIN` apos a primeira leitura
quando ainda ha unidades no counter, limpando apenas quando a ultima unidade
e consumida. Regressions planejadas/revisadas: +2 cenarios em
`test_linux_eventfd.c`; validacao desta sessao foi por revisao estatica,
sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus pipe readiness for epoll (sessao 93, sem ID atribuido):**
`pipe_poll_events()` agora expoe readiness generica no core de pipe, sem
acoplar `pipe.c` diretamente ao Linux ABI shim. `linux_epoll_init_boot()`
converte esses bits para `EPOLLIN`/`EPOLLOUT`/`EPOLLERR`/`EPOLLHUP` depois
do oracle `linux_eventfd_family_poll_events()`, permitindo que loops async
observem pipes legiveis/gravaveis junto de eventfd/timerfd. Regressions
planejadas/revisadas: +2 em `test_pipe.c` para read/write/full/HUP e +2 em
`test_linux_epoll.c` para `EPOLLIN`/`EPOLLOUT` via epoll. Validacao desta
sessao foi por revisao estatica, sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus epoll pipe edge readiness (sessao 94, sem ID atribuido):**
`epoll_wait` agora propaga `EPOLLERR` e `EPOLLHUP` reportados pelo provider
mesmo quando esses bits nao estao no mask registrado, alinhando o contrato
visivel com Linux e reduzindo risco de loops async presos em pipes fechados.
A cobertura host tambem fixa que write end de pipe cheio nao gera
`EPOLLOUT`, read end com writer fechado gera `EPOLLHUP`, e write end com
reader fechado gera `EPOLLERR`. Regressions planejadas/revisadas: +3 em
`test_linux_epoll.c`; validacao desta sessao foi por revisao estatica, sem
executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus epoll pipe ONESHOT edge coverage (sessao 95, sem ID atribuido):**
cobertura host agora fixa que `EPOLLHUP` e `EPOLLERR` reportados pelo
provider nao reativam entradas ja desarmadas por `EPOLLONESHOT`. Isso
mantem a compatibilidade Linux-like de ERR/HUP unmasked sem criar wakeups
espurios depois do primeiro disparo. Regressions planejadas/revisadas: +2
em `test_linux_epoll.c`; validacao desta sessao foi por revisao estatica,
sem executar `make`, `git` ou scripts.
**NR count permanece 252**.

**Bonus syscall completion (sessao 38, sem ID atribuido) --
legacy pipe:** `pipe` (NR 22) agora e' registrado em
`linux_fd_register_syscalls` e delega para o caminho ja existente
`linux_pipe2(fds, 0)`. Isso remove a unica entrada pendente visivel
na tabela Marco M1 sem duplicar implementacao, preservando a
semantica Linux de par read/write em `fds[0..1]`, erros `-EFAULT`,
`-ENOSYS` e `-EMFILE`, e o hook de flags com mascara zero. Host
regressions adicionadas em `tests/test_linux_fd.c`: wrapper basico,
NULL fds e no-ops. `tests/test_linux_syscall.c` tambem spot-checka
que NR 22 e' registrado no boot table. Validacao desta sessao foi
por revisao estatica de codigo, sem executar `make`, `git` ou script,
conforme instrucao do operador. **NR count permanece 252**.

**Bonus syscalls (sessao 37, sem ID atribuido) --
seccomp/BPF/ptrace + fanotify + io_uring + modern
misc:** apos sessao 36 ter wirado proc_vm + openat2
+ pkey + landlock, ainda faltavam quatro nichos
hot-path para userland Linux: seccomp/BPF/ptrace
(Firefox content sandbox installs seccomp BPF
filter antes de exec'ing renderer; Chromium-derived
sandbox usa bpf para compile filter; crash
reporters usam ptrace para core dump), fanotify
(snap-packaged Firefox e auditd usam para monitor
file access), io_uring (Firefox necko HTTP/3 e
SpiderMonkey IOUringJobBackend probes para opt
into kernel-side completion-driven I/O), modern
misc (musl 1.2.4+ pthread tenta futex_waitv,
chrony usa clock_adjtime, libsecret usa
memfd_secret).
4 modulos novos:
(a) **`linux_seccomp.c`** (NRs 317, 321, 101) --
seccomp/bpf/ptrace. seccomp 4-op (STRICT/FILTER/
GET_ACTION_AVAIL/GET_NOTIF_SIZES); FILTER
provider-injectable; GET_ACTION valida 8 RET_*
actions; GET_NOTIF_SIZES retorna Linux x86_64
sizes. bpf cmd whitelist [0, 40); -ENOSYS default.
ptrace TRACEME -> 0; foreign pid -ESRCH; pid==0
self -EPERM.
(b) **`linux_fanotify.c`** (NRs 300, 301) --
fanotify_init/mark. 8-slot fd table (FD_BASE
0xC000); init flags whitelist (13 bits); mark
flags whitelist (11 bits); ADD/REMOVE/FLUSH
mutex; FLUSH ignora pathname.
(c) **`linux_io_uring.c`** (NRs 425, 426, 427)
-- io_uring_setup/enter/register. setup: entries
pow-of-2 [1, 4096]; -ENOSYS default. enter: fd<0
-EBADF; flags whitelist; sigsz != 8 com sig
-EINVAL. register: opcode [0, 32) whitelist.
(d) **`linux_modern_misc.c`** (NRs 449, 305, 447)
-- futex_waitv/clock_adjtime/memfd_secret.
futex_waitv: nr_futexes [1, 128]; clockid
REALTIME/MONOTONIC; -ENOSYS para musl fallback.
clock_adjtime: modes 12 bits whitelist; ->
TIME_OK. memfd_secret: 8-slot fd table (FD_BASE
0xD000) com VFS close/read/write/lseek reconhecendo fd vivo
desde sessao 42.
**68 host asserts** (+19 seccomp, +14 fanotify,
+15 io_uring, +20 modern_misc) + 4 router regressions planejados/
revisados. **NR count: 241
-> 252.**

**Bonus syscalls (sessao 36, sem ID atribuido) --
process VM + hardened path resolution + memory
protection keys + Landlock sandbox:** apos sessao
35 ter wirado JIT auxiliary + namespaces + exec
ext + pipe zero-copy, ainda faltavam quatro
nichos hot-path para userland Linux: process VM
+ kcmp (Firefox profiler usa process_vm_readv
para sample stacks de outros threads;
Chromium-derived sandboxes usam kcmp para detect
fd identity; debuggers usam process_vm_writev),
hardened path resolution (Firefox content sandbox
usa openat2 com RESOLVE_BENEATH para safely abrir
profile files sem traversing symlinks; bubblewrap
usa faccessat2), memory protection keys
(SpiderMonkey W^X JIT usa pkey_mprotect via
PKRU para flip code RW <-> RX sem TLB shootdown),
Landlock sandbox (Firefox 5.13+ content sandbox
usa landlock_create_ruleset + restrict_self).
4 modulos novos:
(a) **`linux_proc_vm.c`** (NRs 310, 311, 312)
-- process_vm_readv/process_vm_writev/kcmp.
self peer detection (pid==0 ou current_pid()
hook); foreign pid em readv -ESRCH, em writev
-EPERM. iovcnt > IOV_MAX (1024) -EINVAL.
kcmp type whitelist (8 types); KCMP_FILE
compara fds structurally.
(b) **`linux_openat2.c`** (NRs 437, 439) --
openat2/faccessat2. open_how versioned via
size; size<24 -EINVAL; resolve flag whitelist;
BENEATH | IN_ROOT mutually exclusive.
faccessat2 mode/flags whitelists. Provider-
injectable; openat2 default -ENOSYS.
(c) **`linux_pkey.c`** (NRs 329, 330, 331) --
pkey_alloc/free/mprotect. 16-slot table; keys
0/1 reserved. pkey_alloc access_rights
whitelist; pkey_mprotect addr page-aligned;
pkey=-1 = default key. Marco M1 sem PKRU
programming -> validation only.
(d) **`linux_landlock.c`** (NRs 444, 445, 446)
-- create_ruleset/add_rule/restrict_self. 16-
slot ruleset fd table (FD_BASE 0xB000); ABI
v4. VERSION query pattern; handled_access_fs
15-bit whitelist; all-zero access -ENOMSG.
add_rule type whitelist (PATH_BENEATH/
NET_PORT). ENOMSG (42) added to errno.
**66 host asserts** (+16 proc_vm, +17 openat2,
+17 pkey, +16 landlock). **NR count: 230 ->
241.**

**Bonus syscalls (sessao 35, sem ID atribuido) --
JIT auxiliary + namespaces + exec extensions +
pipe zero-copy:** apos sessao 34 ter wirado sandbox
surface + mincore + NUMA + settimeofday, ainda
faltavam quatro nichos hot-path para userland
Linux: JIT auxiliary (membarrier/userfaultfd/
sched_rr_get_interval que SpiderMonkey JIT, WASM
page-fault handler, e musl pthread time-slice
sizing usam), namespaces (unshare/mount/umount2
que Firefox content sandbox e bubblewrap usam),
exec extensions (execveat que musl posix_spawn
usa para TOCTOU-safe exec, close_range que
Firefox sandbox usa para scrub fds antes de
renderer exec -- security-critical), pipe zero-
copy (splice/tee/vmsplice que musl/Firefox cache
usam para evitar userspace bounce).
4 modulos novos:
(a) **`linux_jit_aux.c`** (NRs 324, 323, 148) --
membarrier/userfaultfd/sched_rr_get_interval.
membarrier QUERY reporta SUPPORTED bitmask;
PRIVATE_EXPEDITED requer REGISTER first
(-EPERM otherwise per Linux 4.16). userfaultfd
16-slot fd table (FD_BASE 0xA000); flags
whitelist USER_MODE_ONLY|NONBLOCK|CLOEXEC.
sched_rr_get_interval default 100 ms slice.
(b) **`linux_namespace.c`** (NRs 272, 165, 166)
-- unshare/mount/umount2. unshare CLONE_*
whitelist com THREAD/SIGHAND/VM invariants.
mount fstype whitelist (tmpfs/proc/devpts/sysfs
/none); BIND/MOVE/REMOUNT bypass fstype.
umount2 4-flag whitelist (MNT_FORCE/DETACH/
EXPIRE/UMOUNT_NOFOLLOW). Marco M1 no-op
success quando well-formed.
(c) **`linux_exec_ext.c`** (NRs 322, 436) --
execveat/close_range. execveat valida dirfd/
path/flags -> -ENOSYS (exec subsystem nao
landed). close_range provider-injectable; cap
last em 4096 (sane bound vs ~0u); CLOEXEC
delega a set_cloexec_one callback.
(d) **`linux_pipe_zero.c`** (NRs 275, 276, 278)
-- splice/tee/vmsplice. fd<0 -EBADF; flags
whitelist MOVE|NONBLOCK|MORE|GIFT; vmsplice
nr_segs > 1024 -EINVAL. Provider injection;
default -ENOSYS forca userland fallback.
**61 host asserts** (+14 jit_aux, +20 namespace,
+12 exec_ext, +15 pipe_zero). **NR count: 219
-> 230.**

**Bonus syscalls (sessao 34, sem ID atribuido) --
sandbox surface + memory residency + NUMA policy
+ legacy time setter:** apos sessao 33 ter wirado
sched priorities + POSIX timers + time/getcpu,
ainda faltavam quatro nichos hot-path para
userland Linux: sandbox surface (chroot/personality
/setfsuid/setfsgid que Firefox content sandbox usa
apos seccomp filter installation), memory residency
(mincore que Firefox JIT IonMonkey usa em trampoline
buffer page antes de flip RX, glibc posix_spawn
probes COW safety), NUMA memory policy
(get_mempolicy/set_mempolicy/mbind que Firefox
WebRender usa para topology detection e
SpiderMonkey GC para hot-page binding), legacy
time setter (settimeofday que musl clock_settime
fallback usa em older kernels).
4 modulos novos:
(a) **`linux_sandbox.c`** (NRs 161, 135, 122, 123)
-- chroot/personality/setfsuid/setfsgid. chroot:
NULL -EFAULT, empty -ENOENT, provider-injectable;
default no-op (Marco M1 single-root). personality:
QUERY sentinel (0xFFFFFFFF) read-only; aceita
any persona bits (Linux liberal). setfsuid/
setfsgid: -1 = probe (no change), >=0 stores;
returns prev always.
(b) **`linux_mincore.c`** (NR 27) -- mincore.
addr page-aligned (4 KiB) ou -EINVAL; length=0
-> 0; overflow -> -ENOMEM; NULL vec -> -EFAULT.
Marco M1 no swap -> all pages resident -> bit 0
set em cada byte vec.
(c) **`linux_numa.c`** (NRs 239, 238, 237) --
get_mempolicy/set_mempolicy/mbind. Modes
DEFAULT/PREFERRED/BIND/INTERLEAVE/LOCAL; BIND/
INTERLEAVE/PREFERRED requerem nodemask. Marco
M1 single-NUMA -> get retorna DEFAULT com bit 0
em nodemask; set/mbind validam + no-op.
(d) **`linux_settod.c`** (NR 164) -- settimeofday.
NULL tv com NULL tz -> 0 no-op; tz ignored desde
2.6.x; tv_usec [0,1e6) e tv_sec >=0 validated.
Provider-injectable; default no-op success.
**44 host asserts** (+13 sandbox, +8 mincore,
+15 numa, +8 settod). **NR count: 210 -> 219.**

**Bonus syscalls (sessao 33, sem ID atribuido) --
real-time scheduler priorities + POSIX timers +
legacy time + getcpu:** apos sessao 32 ter wirado
rlimit legacy + capabilities + itimers + flock,
ainda faltavam tres nichos hot-path para userland
Linux: real-time scheduler priorities (Firefox
audio thread queries sched_get_priority_max
(SCHED_FIFO) antes de bumping para evitar audio
glitches; compositor usa sched_setscheduler para
elevar priority; musl pthread_getschedparam le
via sched_getscheduler), POSIX timers (Firefox
profiler usa timer_create com SIGEV_THREAD para
sample stacks at fixed intervals; SpiderMonkey
GC heuristics usa timer_create com
CLOCK_MONOTONIC para incremental marking; musl
timer_create e' implementado via raw syscall sem
userspace fallback), legacy time + getcpu
(time(NULL) que musl fallback usa quando vDSO
unavailable, getcpu que sched_getcpu usa para
profiler CPU labels).
3 modulos novos:
(a) **`linux_sched_prio.c`** (NRs 142, 143, 144,
145, 146, 147) -- sched_setscheduler/
getscheduler/setparam/getparam/get_priority_max/
get_priority_min. Linux policies SCHED_OTHER/
FIFO/RR/BATCH/IDLE/DEADLINE; FIFO/RR -> [1,99]
priority, outros -> 0. get_priority_max retorna
99 para FIFO/RR, 0 outros. setscheduler armazena
policy+priority em module-local state. NULL
param -EFAULT; pid<0 em getscheduler -EINVAL;
unknown policy/invalid priority -EINVAL.
(b) **`linux_posix_timer.c`** (NRs 222, 223,
224, 225, 226) -- timer_create/settime/gettime/
getoverrun/delete. 16-slot table com 1-based
ids; clockid whitelist (8 valores incluindo
REALTIME/MONOTONIC/COARSE/BOOTTIME); sigev_notify
whitelist (SIGNAL/NONE/THREAD/THREAD_ID).
timer_settime valida tv_nsec [0,1e9), tv_sec
>=0; TIMER_ABSTIME flag honored; old_value
populado retorna previous. Slot exhaustion ->
-EAGAIN. Marco M1 stores spec mas no actual
fires ate' per-task signal subsystem landar.
(c) **`linux_time_legacy.c`** (NRs 201, 309) --
time/getcpu. time: provider-injectable
now_seconds() (default 0); writes seconds via
tloc pointer; return value e *tloc identicos
(Linux). getcpu: cpu=0, node=0 (single-CPU);
third arg unused desde 2.6.24; NULL pointers
silently accepted.
**46 host asserts** (+20 sched_prio, +18
posix_timer, +8 time_legacy). **NR count: 199
-> 210.**

**Bonus syscalls (sessao 32, sem ID atribuido) --
resource limits legacy + capabilities + interval
timers + advisory locking:** apos sessao 31 ter
wirado xattr family + statfs + fadvise/fallocate/
sendfile, ainda faltavam quatro nichos hot-path
para userland Linux: resource limits legacy (que
bash startup probes via getrlimit, musl
pthread_create usa para sizing thread stacks via
RLIMIT_STACK; modern userland ja' usa prlimit64
mas legacy paths ainda hit), capabilities (que
Firefox sandbox usa para drop ALL caps antes de
exec content processes; libcap probes via
cap_get_proc; bubblewrap/firejail capget para
determinar current set), interval timers (que
musl sigtimedwait fallback usa para bound wait,
Firefox compositor watchdog usa ITIMER_REAL para
detectar frozen render thread, bash/ps/time(1)
usam times para reporting), advisory locking
(flock que Firefox profile lock .parentlock usa,
SQLite usa como fcntl record lock fallback em
tmpfs, Firefox cache usa copy_file_range).
4 modulos novos:
(a) **`linux_rlimit_legacy.c`** (NRs 97, 160) --
getrlimit/setrlimit. Resource [0..NLIMITS=16) ou
-EINVAL; NULL buf -EFAULT; setrlimit cur > max
com handling de INFINITY -> -EINVAL. Synthesised
defaults sem provider: NOFILE 1024/4096, STACK
8 MiB/INFINITY, NPROC 1024/1024, CORE
0/INFINITY, outros INFINITY/INFINITY (mirror
Linux initrd profile). Provider injection via
`linux_rlimit_legacy_install_ops`.
(b) **`linux_caps.c`** (NRs 125, 126) -- capget/
capset. Linux capability ABI v1/v2/v3 honored;
unknown version rewrites hdr->version=v3 e
retorna -EINVAL (Linux probe behaviour). NULL
hdr -> -EFAULT antes do version check; NULL
data com valid version -> -EFAULT. capget
pid<0 -> -EINVAL. capset only-self (pid != 0
-> -EPERM per Linux 2.6.25). Default Marco M1:
root-with-all-caps. Provider injection via
`linux_caps_install_ops`.
(c) **`linux_itimer.c`** (NRs 36, 37, 38, 100)
-- alarm/getitimer/setitimer/times. alarm:
storage-only state machine retornando prev
(Linux); SIGALRM nao firado (signal subsystem
storage-only). getitimer/setitimer: 3-slot
table para REAL/VIRTUAL/PROF; which whitelist
[0,3); tv_usec [0, 1e6) e tv_sec >=0
validados; setitimer com old_value retorna
previous antes de overwrite. times: returns
provider tick count (NULL ops -> 0); buf
populado com zeros (no per-task acct ainda).
Provider injection via
`linux_itimer_install_ops`.
(d) **`linux_lock.c`** (NRs 73, 326) -- flock/
copy_file_range. flock: 32-slot per-fd state
machine; LOCK_SH/EX/UN modes mutually exclusive;
LOCK_SH | LOCK_EX -> -EINVAL; unknown bits
fora known set -> -EINVAL; 33 distinct fds ->
-ENOLCK (fail-closed). Single-proc world means
LOCK_NB never blocks. copy_file_range: fd<0
-EBADF; flags != 0 -> -EINVAL; provider
injection via `linux_lock_install_ops`; default
-ENOSYS forca userland fallback. Static-init
bug fixed via `ensure_initialised()` helper.
**58 host asserts** (+14 rlimit_legacy, +13
caps, +16 itimer, +15 lock). **NR count: 189
-> 199.**

**Bonus syscalls (sessao 31, sem ID atribuido) --
extended attributes + filesystem stats + file
advise/preallocation/sendfile:** apos sessao 30
ter wirado timestamp mutations + identity changes
+ working directory, ainda faltavam tres nichos
hot-path para userland Linux: extended attributes
(xattr family que Firefox quarantine usa para
download tracking via user.xdg.origin.url, SELinux/
AppArmor probes, musl cp -a), filesystem stats
(statfs que Firefox usa para free-space check
antes de downloads, SQLite usa para WAL space
pressure detect), file advise/preallocation/
sendfile (SQLite posix_fadvise para database
page-cache hints, Firefox fallocate para download
space reservation, musl sendfile para zero-copy
fallback).
3 modulos novos:
(a) **`linux_xattr.c`** (NRs 188, 189, 190, 191,
192, 193, 194, 195, 196, 197, 198, 199) -- full
xattr family. Marco M1 sem xattr storage; Linux
convention para "filesystem nao suporta xattrs":
setxattr family -> -EOPNOTSUPP, getxattr family
-> -ENODATA (attribute missing), listxattr ->
0 attrs, removexattr -> -ENODATA. Validation:
NULL/empty path; NULL/empty name (-ERANGE); name
> 255 chars (-ENAMETOOLONG); value size > 64 KiB
(-E2BIG); flag bits fora CREATE|REPLACE
(-EINVAL); fd<0 (-EBADF). 12 NRs total via
fd-based, follow-symlink, e l-form (no-follow)
variantes.
(b) **`linux_statfs.c`** (NRs 137, 138) -- statfs/
fstatfs com 120-byte Linux struct sintetizado.
f_type=TMPFS_MAGIC (0x01021994), f_bsize=4096,
f_blocks=16384 default (provider-injectable),
f_files=1024, f_namelen=255. f_fsid e f_spare
zerados. Provider injection via
`linux_statfs_install_providers`.
(c) **`linux_advise.c`** (NRs 221, 285, 40) --
posix_fadvise/fallocate/sendfile. fadvise64
advice whitelist [0..5], offset/len >= 0,
retorna 0 (advisory no-op). fallocate mode
whitelist + PUNCH_HOLE requer KEEP_SIZE; basic
-> -EOPNOTSUPP (tmpfs sem preallocation).
sendfile via provider injection
(`linux_advise_install_ops`); default -ENOSYS
forca userland a read+write fallback.
ENODATA (61) adicionado em linux_errno.h.
**50 host asserts** (+23 xattr, +10 statfs, +17
advise). **NR count: 172 -> 189.**

**Bonus syscalls (sessao 30, sem ID atribuido) --
timestamp mutations + identity changes + working
directory:** apos sessao 29 ter wirado filesystem
metadata mutations + links + durability barriers,
ainda faltavam tres nichos hot-path para userland
Linux: timestamp mutations (utime family que Firefox
HTTP cache precisa para preservar Last-Modified
header; cache index detecta stale entries via mtime),
identity changes (setuid/setresuid family que dynamic
linker invoca via initgroups em setuid binaries),
working directory mutations (chdir/fchdir que Firefox
profile setup usa antes de carregar componentes com
paths relativos).
3 modulos novos:
(a) **`linux_utime.c`** (NRs 280, 132, 235, 261) --
utimensat/utime/utimes/futimesat. utimensat e' a
forma moderna; legacy delegam para utimensat com
NULL buf (populated buf -> -ENOSYS). Honra Linux
UTIME_NOW e UTIME_OMIT sentinels; ambos UTIME_OMIT
-> 0 fast path; UTIME_NOW expandido contra
injectable now() callback. Form fd-based via NULL
path; AT_FDCWD com NULL path -> -EFAULT.
AT_SYMLINK_NOFOLLOW honored.
(b) **`linux_setid.c`** (NRs 105, 106, 117, 119,
118, 120) -- setuid/setgid/setresuid/setresgid/
getresuid/getresgid. Marco M1 single-root (uid=
gid=0). setuid(0) -> 0; outros -> -EPERM.
setresuid/setresgid honram Linux `(uid_t)-1`
sentinel "no change" per-component. getresuid/
getresgid retornam (0,0,0); pointers obrigatorios
(NULL -> -EFAULT).
(c) **`linux_chdir.c`** (NRs 80, 81) -- chdir/
fchdir via provider injection
(`linux_chdir_install_ops`). Validation: NULL ->
-EFAULT, empty -> -ENOENT, fd<0 -> -EBADF, sem
ops -> -ENOSYS. Provider rc forwarded verbatim.
**44 host asserts** (+17 utime, +18 setid, +9
chdir). **NR count: 160 -> 172.**

**Bonus syscalls (sessao 29, sem ID atribuido) --
filesystem metadata mutations + links + durability
barriers:** apos sessao 28 ter wirado filesystem
mutations + memory locking + supplementary groups,
ainda faltavam tres nichos hot-path para userland
Linux: filesystem metadata mutations (chmod/chown
family que Firefox precisa para profile permission
lockdown), hard- e soft-links (link/symlink que
Firefox usa em atomic cache update pattern), e
durability barriers (sync/fsync que SQLite invoca
em WAL checkpoint para places.sqlite/cookies.sqlite).
3 modulos novos:
(a) **`linux_fs_meta.c`** (NRs 90, 91, 268, 92, 93,
94, 260) -- chmod/fchmod/fchmodat/chown/fchown/
lchown/fchownat via provider injection
(`linux_fs_meta_install_ops`). Validation:
NULL/empty path -> -EFAULT/-ENOENT; fd<0 -> -EBADF;
chmod clampa mode em 07777; lchown -> chown_path
com follow=0; fchmodat rejeita TODOS os flags
(Linux fs/namei.c contract); fchownat aceita
NOFOLLOW|EMPTY_PATH whitelist; AT_EMPTY_PATH com
AT_FDCWD -> -EINVAL; uid/gid forwarded verbatim
incluindo `(uid_t)-1` sentinel. Sem ops ->
-ENOSYS.
(b) **`linux_link.c`** (NRs 86, 265, 88, 266) --
link/linkat/symlink/symlinkat via provider injection
(`linux_link_install_ops`). link -> linkat com
flags=0; linkat valida AT_SYMLINK_FOLLOW|
AT_EMPTY_PATH whitelist; symlink -> symlinkat com
AT_FDCWD; symlinkat valida target NULL -> -EFAULT,
empty -> -ENOENT (Linux: empty target invalido).
AT_FDCWD only para dirfds.
(c) **`linux_sync.c`** (NRs 162, 306, 74, 75) --
sync/syncfs/fsync/fdatasync. CapyOS sem persistent
backing store; durability trivialmente satisfeita.
sync() -> 0; syncfs/fsync/fdatasync(-1) -> -EBADF;
provider opcional via `linux_sync_install_ops` com
data_only flag distinguindo fsync (=0) de fdatasync
(=1).
**42 host asserts** (+18 fs_meta, +13 link, +11
sync). **NR count: 145 -> 160.**

**Bonus syscalls (sessao 28, sem ID atribuido) --
filesystem mutations + memory locking +
supplementary groups:** apos sessao 27 ter wirado
system info + scheduling + sessions, ainda faltavam
tres nichos hot-path para userland Linux:
filesystem mutations (mkdir/rename family que Firefox
usa em profile setup e cache rotation), memory
locking (mlock que SpiderMonkey JIT precisa para W^X
pages e que musl pthread bring-up usa em TLS), e
supplementary group credentials (getgroups/setgroups
que dynamic linker invoca em setuid program path
via initgroups). 3 modulos novos:
(a) **`linux_fs_mut.c`** (NRs 83, 258, 84, 87, 263,
82, 264, 316) -- mkdir/mkdirat/rmdir/unlink/
unlinkat/rename/renameat/renameat2 via provider
injection (`linux_fs_mut_install_ops`). Validation:
NULL/empty path -> -EFAULT/-ENOENT; mkdirat/etc.
exigem AT_FDCWD (others -ENOTDIR); unlinkat com
AT_REMOVEDIR rota para rmdir; renameat2 valida
flag whitelist (NOREPLACE|EXCHANGE|WHITEOUT) e
mutex NOREPLACE+EXCHANGE -> -EINVAL. Sem ops
instalado -> -ENOSYS (tmpfs hooks quando landarem).
(b) **`linux_mlock.c`** (NRs 149, 150, 151, 152) --
mlock/munlock/mlockall/munlockall com no-op success
(CapyOS sem swap). Validation: addr+len wrap ->
-EINVAL; len==0 -> 0 (Linux short-circuit);
mlockall(0) -> -EINVAL; flags fora MCL_CURRENT|
FUTURE|ONFAULT -> -EINVAL.
(c) **`linux_creds.c`** (NRs 115, 116) -- getgroups/
setgroups com Marco M1 zero-supplementary-groups
semantics. getgroups(0, NULL) -> 0 (Linux count
query idiom); getgroups(size>0, NULL) -> -EFAULT.
setgroups valida size <= NGROUPS_MAX (65536); root
tem CAP_SETGID implicito entao any well-formed list
-> 0 no-op.
**39 host asserts** (+21 fs_mut, +10 mlock, +8
creds). **NR count: 131 -> 145.**

**Bonus syscalls (sessao 27, sem ID atribuido) --
system info + scheduling + sessions:** apos sessao
26 ter wirado process control + truncation, ainda
faltavam tres nichos hot-path para userland Linux:
timing primitives (clock_nanosleep), system metadata
(sysinfo, getrusage), scheduling priority hints
(getpriority, setpriority) e POSIX session/process
group control (setpgid, getpgid, getpgrp, setsid,
getsid). Sem clock_nanosleep, musl
`pthread_cond_timedwait` quebra. Sem sysinfo,
Firefox `nsSystemInfo` falha. Sem setpgid/setsid,
shell job control e daemonization quebram. 3
modulos novos + 1 extension:
(a) **`linux_sysinfo.c`** (NRs 99, 98) -- sysinfo
com struct 112 bytes (Linux x86_64 ABI verbatim;
inicial 64 bytes errado, corrigido por struct-size
sanity test); injectable providers; getrusage
zero-filled aceita SELF/CHILDREN/THREAD.
(b) **`linux_priority.c`** (NRs 140, 141) --
getpriority retorna 20 - g_nice (Linux encoding);
setpriority clampa em [-20, +19]; PRIO_PROCESS/PGRP/
USER aceitos.
(c) **`linux_pgrp.c`** (NRs 109, 121, 111, 112,
124) -- setpgid self-only (others -EPERM); getpgid/
getsid com -ESRCH para non-self; setsid first call
ok, second -EPERM (already leader); provider
injection.
(d) **linux_clock.c extension** (NR 230) --
clock_nanosleep com flags={0, TIMER_ABSTIME} e
clockid validation; CPUTIME -> -EOPNOTSUPP; modo
absoluto/relativo via mesmo spin-wait do nanosleep.
**37 host asserts** (+6 clock_nanosleep, +11
sysinfo, +8 priority, +12 pgrp). **NR count:
121 -> 131.**

**Bonus syscalls (sessao 26, sem ID atribuido) --
process control + truncation:** apos sessao 25 ter
fechado o nicho de filesystem comfort, ainda faltavam
hot-path syscalls de process control (wait4, waitid,
kill, tgkill, tkill) e file resize (truncate,
ftruncate) que userland chama frequentemente:
musl `popen()`/busybox `system()`/shell job control
falham fail-fast sem -ECHILD; `abort()` precisa de
kill(self, SIGABRT); tmpfs/log rotation precisa de
ftruncate. 3 modulos novos:
(a) **`linux_wait.c`** (NRs 61, 247) -- wait4/waitid
retornam -ECHILD (documented Linux answer para "no
children"). Validation completa de options/idtype.
Quando task_clone_thread + child-tracking landar,
hooks no wait queue real.
(b) **`linux_kill.c`** (NRs 62, 234, 200) --
kill/tgkill/tkill com self-signal funcional (sig==0
alive probe; sig real delega ao
linux_kill_install_ops({.deliver}) se instalado,
senao no-op). pid==0/-1 -> 0 silencioso (no peers).
Outros pids -> -ESRCH. Validacao 0..LINUX_NSIG.
(c) **`linux_trunc.c`** (NRs 76, 77) -- truncate
path-based -> -ENOSYS apos validacao (musl fall
back para open()+ftruncate). ftruncate fd-based
delega para linux_trunc_install_ops({.ftruncate}),
tmpfs pode instalar real resize hook.
**34 host asserts** (10 wait + 14 kill + 10 trunc).
**NR count: 113 -> 121.**

**Bonus syscalls (sessao 25, sem ID atribuido) --
filesystem comfort:** apos sessao 24 ter wirado
path/getcwd/readlink/getdents64/statx, faltava o
nicho de access/faccessat/fstatat/dup/dup2/umask que
musl/glibc chamam constantemente antes do open.
Sem eles userland degrada significativamente:
`./configure` scripts e dynamic linker abortam ao
probar paths via access; musl `stat()` (que
internamente chama fstatat AT_FDCWD) quebra; shells
stdio redirect (2>&1, <, >) e popen() falham sem
dup/dup2; musl `__init_libc` aborta logo na startup
sem umask. 3 modulos novos:
(a) **`linux_at.c`** (NRs 21, 269, 262) -- access/
faccessat aceitam o known pseudo path set via
`linux_stat_path_is_known()` desde sessao 40, retornando 0 para qualquer modo R|W|X|F
(Marco M1 roda como effective root). Paths fora do
set -> -ENOENT. fstatat com path vazio (com ou sem
AT_EMPTY_PATH) projeta o linux_fstat sintetico no
struct stat; AT_FDCWD+path delega para `linux_stat`/`linux_lstat`
em known pseudo paths desde sessao 39; unknown paths
continuam -ENOSYS para fallback open+fstat; dirfd>=0+path
-> -ENOTDIR.
(b) **`linux_dup.c`** (NRs 32, 33) -- dup/dup2 com
validation completa (oldfd/newfd negativos -> EBADF).
**dup2(fd, fd) e' funcional**: retorna newfd sem
chamar ops. Provider injection via
linux_dup_install_ops para quando fd table unificada
existir; sem ops -> -ENOSYS.
(c) **`linux_umask.c`** (NR 95) -- modulo trivial,
default 0022, retorna previous, clamp a low 9 bits.
Sempre sucesso (Linux: umask nunca falha).
**33 host asserts** (18 at + 9 dup + 6 umask).
**NR count: 110 -> 113.**

**Bonus syscalls (sessao 24, sem ID atribuido) --
path/metadata polish:** apos sessao 23 ter wirado
fstat, userland frequentemente passa por
`getcwd`/`readlink`/`statx`/`getdents64` ANTES do
open. Sem eles userland degrada de forma assimetrica
(getcwd ENOSYS quebra shells; readlink falhando faz
gdb/profilers abortar; getdents64 ENOSYS faz
opendir/readdir falhar em vez de retornar lista vazia;
statx ENOSYS forca fallback open+fstat custoso). 3
modulos novos:
(a) **`linux_path.c`** (NRs 79, 89, 267) -- `getcwd`
retorna sempre "/" (Marco M1 sem cwd-per-process;
-EINVAL/-ERANGE/-EFAULT corretos). `readlink` especializa
`/proc/self/exe` via provider injetavel; outros paths
-> -EINVAL ("not a symlink", semantica Linux para
regulares). `readlinkat` com AT_FDCWD delega; outros
dirfd -> -ENOTDIR.
(b) **`linux_statx.c`** (NR 332) -- `struct linux_statx`
de 256 bytes ABI Linux x86_64. statx com path vazio
projeta o fstat sintetico nos campos statx (mode/
blksize/nlink/size/blocks); stx_mask clampado a
LINUX_STATX_SUPPORTED. Desde sessao 39, AT_FDCWD+path
tambem projeta os known pseudo paths de `linux_stat`;
unknown paths continuam -ENOSYS para fallback. dirfd>=0
com path -> -ENOTDIR.
(c) **`linux_dirent.c`** (NR 217) -- `getdents64`
retorna sempre 0 (EOF) para fd valido. Userland le
"diretorio vazio" e prossegue. struct dirent64
declarada para uso futuro.
**30 host asserts** (14 path + 10 statx + 6 dirent).
**NR count: 105 -> 110.**

**Bonus syscalls (sessao 22, sem ID atribuido) -- ABI
surface complete:** apos sessao 21, restavam apenas
ioctl + fcntl no COMPAT.md como MISSING. Estes nao
bloqueiam musl boot mas sao chamados durante stdio
init de cada FILE*. Esta sessao fecha o nicho:
(a) **`linux_ioctl.c`** (NR 16) -- retorna -ENOTTY
para qualquer fd nao-negativo e qualquer cmd. Linux
semantics para non-tty fds. musl stdio init chama
`ioctl(fd, TCGETS, &t)` para detectar tty-ness;
-ENOTTY -> bloco-buffered mode. fd < 0 -> -EBADF.
(b) **`linux_fcntl.c`** (NR 72) -- subset funcional.
F_GETFD/F_SETFD com tabela hash 256-bucket por fd
(collision = latest wins). F_GETFL retorna O_RDWR
| (writable flags). F_SETFL honra so O_APPEND|
O_NONBLOCK|O_DIRECT|O_NOATIME|O_ASYNC; immutable
bits silently dropped. F_DUPFD/F_DUPFD_CLOEXEC/
F_GETLK/F_SETLK/F_SETLKW -> -ENOSYS. Unknown cmd
-> -EINVAL. fd < 0 -> -EBADF.
**21 host asserts** (8 ioctl + 13 fcntl). **0 gaps
remaining**: Marco M1 ABI surface complete; vendoring
upstream musl-1.2.5 (S3.1) e o proximo passo natural.

**Bonus syscalls (sessao 21, sem ID atribuido) -- musl
boot blockers:** apos sessao 20, `COMPAT.md` listava
5 gaps que bloqueavam musl boot. Esta sessao fecha 4
deles via 3 modulos novos (ioctl/fcntl ficam, mas
sao tolerados pelo musl como no-ops no startup):
(a) **`linux_brk.c`** (NR 12) -- heap region tracker
com base em `0x0000_6000_0000_0000` e cap 256 MiB.
Linux semantics: brk(0) retorna current; brk(addr)
success retorna addr, failure retorna o break velho
unchanged (Linux nao retorna -errno de brk).
Production grow chama `vmm_register_anon_region` com
USER+WRITE+NX; shrink so retracts o live break sem
liberar frames. Provider injection para host tests.
(b) **`linux_arch_prctl.c`** (NR 158) -- TLS setup
critico. ARCH_SET_FS escreve IA32_FS_BASE MSR;
ARCH_GET_FS le ela; ARCH_SET_GS / GET_GS operam no
IA32_KERNEL_GS_BASE (shadow do user gs durante
syscall, ja que kernel fez swapgs no entry). Boot
wiring usa wrmsr/rdmsr inline asm guard-ed por
defined(__x86_64__). Unknown op -> -EINVAL; null
ptr em GET -> -EFAULT.
(c) **`linux_exit.c`** (NRs 60, 231) -- exit +
exit_group delegam a `task_exit(code)` (noreturn)
via callback. Single-thread model: exit_group
identico a exit. Tests usam setjmp/longjmp.
**27 host asserts** novos (11 brk + 10 arch_prctl +
6 exit). **musl boot path agora unblocked**.

**Bonus syscalls (sessao 20, sem ID atribuido):** musl
bring-up exigia 10 NRs adicionais que nao tinham ID
proprio mas eram criticos. Wired em 3 modulos existentes:
(a) **`linux_process`** -- `getpid` (alias para `gettid`
ate S1.4 thread groups), `getppid` (retorna 1 = init's
pid), `getuid`/`geteuid`/`getgid`/`getegid` (todos
retornam 0/root para Marco M1), `uname` (struct utsname
com sysname=Linux, machine=x86_64, release=6.5.0-capyos,
domainname="(none)"; cada campo zero-padded em 65
bytes); (b) **`linux_clock`** -- `gettimeofday` delega a
`clock_gettime(REALTIME)` com fallback para MONOTONIC
sem epoch; `nanosleep` com validacao Linux-faithful e
spin-busy-wait contra MONOTONIC (substituido por
`task_sleep_until` no futuro); (c) **`linux_vfs`** --
`openat(AT_FDCWD, ...)` delega a `linux_vfs_open`,
dirfd >= 0 -> -ENOTDIR. **18 host asserts** novos
(8 process + 7 clock + 3 vfs).

**Total S1**: ~36-50 semanas (8-12 meses) com 1 dev focado.
Em paralelo com outras tarefas.

### Etapa S2: pseudo-filesystems

| ID | Tarefa | Estimativa | Bloqueia |
|---|---|---|---|
| S2.1 | ✅ **DONE** (sessoes 12+16) `/proc/self/maps` formatter em `linux_proc.c` + backend de fd em `linux_procfs` que renderiza no open. Sessao 16: `open("/proc/self/maps")` flui via `linux_vfs_router` -> `linux_procfs_open` -> `render_maps` -> `linux_proc_format_maps`. Provider injetavel (boot wira placeholder vazio ate vmm_anon_region walker landar). **6/6 formatter asserts** (linux_proc) + **3/3 procfs asserts** (open + read + lseek). | ~~2 semanas~~ feito (formatter + backend) | ipc/chromium process_util_linux.cc |
| S2.2 | ✅ **DONE** (sessoes 13+16) `/proc/self/exe` formatter em `linux_proc.c` + backend procfs. Sessao 16: routing real via VFS. NULL path -> '/unknown'. Provider injetavel (placeholder retorna '/unknown' ate ELF metadata landar). **4/4 formatter asserts** + cobertura via tests procfs (open/read/lseek). | ~~1 semana~~ feito (formatter + backend) | xpcom (XRE_GetBinaryPath) |
| S2.3 | ✅ **DONE** (sessoes 12+16) `/proc/self/cmdline` formatter em `linux_proc.c` + backend procfs com `cmdline` provider injetavel. Sessao 16: `open("/proc/self/cmdline")` flui via VFS router. argv joined por NULs (Linux byte stream); placeholder retorna empty argv ate exec metadata landar. **5/5 formatter asserts** + cobertura via tests procfs. | ~~1 semana~~ feito (formatter + backend) | logging, crash report |
| S2.4 | ✅ **DONE** (sessoes 10+16) `/proc/cpuinfo` formatter em `linux_cpuinfo.c` + backend procfs com `cpuinfo` provider injetavel. Sessao 16: `open("/proc/cpuinfo")` flui via VFS router; placeholder em boot fornece 1 entry com baseline x86_64 flags (fpu, tsc, cmov, lm) ate cpuid harvest landar. **11/11 formatter asserts** (linux_cpuinfo) + cobertura via tests procfs/router (read /proc/cpuinfo verifica `fpu`/`avx` tokens). | ~~1-2 semanas~~ feito (formatter + backend) | gfx (capability detection) |
| S2.5 | ✅ **DONE** (sessoes 11+16) `/proc/meminfo` formatter em `linux_proc.c` + backend procfs com `meminfo` provider injetavel. Sessao 16: `open("/proc/meminfo")` flui via VFS router; placeholder retorna zeros ate PMM stats getter landar. **5/5 formatter asserts** + cobertura via tests procfs (read verifica `MemTotal:` header). | ~~1 semana~~ feito (formatter + backend) | xpcom (sysinfo) |
| S2.6 | ✅ **DONE** (sessoes 11+16) `/proc/<pid>/status` formatter em `linux_proc.c` + backend procfs (`/proc/self/status` apenas; numeric `<pid>` aguarda cross-process introspection). Sessao 16: `open("/proc/self/status")` flui via VFS router; placeholder retorna `{name=capy, state=R, pid=1}` ate task metadata landar. **6/6 formatter asserts** + cobertura via tests procfs. | ~~1 semana~~ feito (formatter + backend self-only) | telemetry (opcional) |
| S2.7 | ✅ **DONE** (sessoes 9+15) Pseudo-`/dev` em `linux_devfs.c` com lookup path-baseado + fd API (`linux_devfs_open/close/read_fd/write_fd/lseek_fd`, fd encoding 0x8000). `/dev/{null,zero,full,urandom,random}` semantics Linux 6.x: urandom delega CSPRNG, full -> -ENOSPC em write, char devs lseek = 0. Sessao 15 adicionou roteamento real: `linux_open("/dev/urandom")` agora flui end-to-end via `linux_vfs_router` -> `linux_devfs_open`. **31/31 host asserts** (17 id-based + 14 fd-based). | ~~1 semana~~ feito | NSS fallback |
| S2.8 | ✅ **DONE** (sessao 9, parcial) `/dev/null` + `/dev/zero` cobertos pelo `linux_devfs.c` (junto com S2.7). `/dev/tty` aguarda modelo de sessao/pty. | ~~1 semana~~ feito (parcial) | tudo |
| S2.9 | ✅ **DONE** (sessao 18) `tmpfs` em `/tmp/` em `src/kernel/linux_compat/linux_tmpfs.c`. In-memory filesystem com flat namespace (sem subdirectorios), 16 files x 4 KiB content cada (alocacao inline para evitar dependencia em page allocator). 32 handles independentes (refcount per-file). POSIX semantics: O_CREAT/O_EXCL/O_TRUNC/O_APPEND, unlink+close orphan-frees slot, O_RDONLY rejeita write -> EBADF, O_WRONLY rejeita read -> EBADF, O_APPEND sempre escreve no end. Routing real via `linux_vfs_router` -> `/tmp/<name>` -> `linux_tmpfs_open`. read/write/lseek/close end-to-end via VFS. fd encoding 0xA000. **35/35 host asserts** + 5 router asserts. Compartilhamento entre processos NAO landa nesta sessao (espera shared address space ou tmpfs como backing de mmap MAP_SHARED). | ~~3-4 semanas~~ feito (sem cross-process sharing) | ipc/glue (shm) |
| S2.10 | ✅ **DONE** (sessoes 14+15) Storage table para POSIX shm em `linux_shm.c`. shm_open/unlink/truncate/size/close. Ate 16 named objects, names <= 63 chars, sizes <= 64 MiB. POSIX semantics: O_CREAT/O_EXCL/O_TRUNC, unlink+close orphan-frees slot. **Sessao 15 wirou o roteamento**: `open("/dev/shm/<name>", ...)` flui via `linux_vfs_router` -> `linux_shm_open` end-to-end. read/write/lseek de fds shm ficam para quando S2.9 (tmpfs/backing pages) landar -- shm Linux espera mmap path. **15/15 host asserts**. | ~~2-3 semanas~~ feito (kernel API + routing) | ipc/glue/SharedMemoryPlatform_posix.cpp |

**Bonus paths (sessao 17, sem ID atribuido):** `/proc/version`,
`/proc/uptime`, `/proc/loadavg` (formatters em `linux_proc.c` +
roteamento via `linux_procfs`). Userland (musl `__libc_get_version_string`,
glibc, JS shell) checa `/proc/version` para "Linux version" prefix;
`uptime`/`loadavg` consumidos por `procps` e xpcom `nsSystemInfo`.
`/proc/uptime` placeholder retorna `linux_clock_gettime(MONOTONIC)`
real. **18/18 asserts** (11 formatter + 7 procfs).

**Total S2**: ~14-19 semanas (3-5 meses).

### Etapa S3: musl libc port

| ID | Tarefa | Estimativa | Bloqueia |
|---|---|---|---|
| S3.1 | Vendor `musl-1.2.5` (or latest) em `userland/lib/musl/` | 1 semana | tudo |
| S3.2 | ✅ **DONE** (sessao 19, seed) Arch adapter `userland/lib/musl/arch/x86_64/syscall_arch.h` com inline asm para __syscall0..__syscall6 (rax=NR, rdi/rsi/rdx/r10/r8/r9 = a1..a6, rcx+r11+memory clobbers). Compila para x86_64-linux-gnu com `-Wall -Werror` no host arm64 via `-target` (cross-validation real, nao apenas syntax). VDSO_USEFUL=0 (sem vDSO ainda). Acompanha `userland/lib/musl/README.md` (estrategia + roadmap), `userland/lib/musl/COMPAT.md` (matriz de 105 NRs com state WIRED/STUB/MISSING), e `docs/plans/active/musl-port-strategy.md` (decision log + risk mitigation). | ~~2 semanas~~ feito (seed) | musl roda |
| S3.3 | Configurar musl build: `./configure --prefix=/usr --disable-shared --target=x86_64-capyos` | 1-2 semanas | sysroot |
| S3.4 | Validar test suite musl (LIBC_TESTS) | 4-6 semanas | qualidade |
| S3.5 | Implementar `libc.so` versao dinamica + ld-musl-x86_64.so.1 | 4-6 semanas | F3 (dynamic loading) |
| S3.6 | Substituir capylibc por musl em `userland/bin/capysh` etc. | 2-3 semanas | regression test |
| S3.7 | Manter capylibc como compatibility shim (ou deprecate) | 1 semana | -- |

**Total S3**: ~15-21 semanas (3-5 meses).

### Etapa S4: toolchain

| ID | Tarefa | Estimativa | Bloqueia |
|---|---|---|---|
| S4.1 | Cross-toolchain `x86_64-unknown-linux-musl-gcc-13` (do crosstool-NG ou musl.cc pre-built) | 1 semana | musl |
| S4.2 | Validar com `gcc --target=x86_64-unknown-linux-musl -static helloworld.c` que roda em CapyOS | 1 semana | sanity |
| S4.3 | Cross-toolchain Rust: `rustup target add x86_64-unknown-linux-musl` | 1 semana | Stylo, WebRender |
| S4.4 | Validar com `cargo build --target=x86_64-unknown-linux-musl --release` em projeto Rust dummy | 1 semana | sanity |
| S4.5 | Ajustar PATH/sysroot env nas docs do CapyOS | 1 semana | DX |
| S4.6 | Habilitar `-shared`/`-fPIC`/dynamic linking no toolchain (post-S3.5) | 2-3 semanas | F3 |

**Total S4**: ~7-10 semanas (1.5-2.5 meses).

### Etapa S5: pthread + TLS no userland

| ID | Tarefa | Estimativa | Bloqueia |
|---|---|---|---|
| S5.1 | musl ja prove pthread; validar que `clone(CLONE_THREAD)` funciona end-to-end | 2-3 semanas | tudo |
| S5.2 | TLS via `__thread` -- requer ELF TLS sections (PT_TLS), `arch_prctl(ARCH_SET_FS)` para fs_base | 4-6 semanas | C++ runtime |
| S5.3 | `pthread_setname_np` mapeado para `prctl(PR_SET_NAME)` (S1.9) | 1 semana | mozglue |
| S5.4 | `pthread_getaffinity_np`/`pthread_setaffinity_np` mapeado para sched_*affinity (S1.11) | 1 semana | js/src/threading |
| S5.5 | Stress test: 256 threads concorrentes com pthread_mutex + pthread_cond | 2-3 semanas | qualidade |

**Total S5**: ~10-15 semanas (2.5-4 meses).

### Etapa S6: SpiderMonkey standalone build (Marco M1)

| ID | Tarefa | Estimativa | Bloqueia |
|---|---|---|---|
| S6.1 | Build SpiderMonkey shell standalone: `cd /Volumes/Firefox/js/src && ../../mach build js/src` no host (validar) | 1 semana | sanity |
| S6.2 | Cross-build SpiderMonkey: `--target=x86_64-unknown-linux-musl` apontando sysroot CapyOS | 4-6 semanas | M1 |
| S6.3 | Resolver symbols faltantes iterativamente (cada link error -> patch musl ou kernel) | 4-8 semanas | M1 |
| S6.4 | Rodar `js -e 'print(1+1)'` em CapyOS e ver "2" no debugcon | 2 semanas | M1 |
| S6.5 | Rodar test262 (subset) no shell SpiderMonkey CapyOS | 4-6 semanas | confidence |

**Total S6**: ~15-23 semanas (4-6 meses) -- **Marco M1**.

## Cronograma sequencial

```
Mes 1-3:    S1.1, S1.2, S1.3 (mmap + clock_gettime + dispatcher)
Mes 2-4:    S1.4, S1.5 (clone + futex)
Mes 3-5:    S1.6, S1.7, S1.8 (epoll + eventfd + getrandom)
Mes 4-6:    S2.1-S2.8 (pseudo-fs)
Mes 5-7:    S3.1-S3.4 (musl static)
Mes 6-8:    S4.1-S4.5 (toolchain)
Mes 7-10:   S5.1-S5.5 (pthread userland)
Mes 9-13:   S6.1-S6.4 (SpiderMonkey M1)
Mes 12+:    S2.9-S2.10, S3.5-S3.7, S5.x extras (paralelizado)
```

**Marco M1 estimado: 13 meses** com 1 dev focado.

## Estrutura de arquivos no CapyOS apos este plano

```
src/
├── kernel/
│   ├── syscall.c                    # estender com linux_syscall[]
│   ├── linux_compat/                # NEW
│   │   ├── linux_syscall_table.c   # tabela NR_<X> -> handler
│   │   ├── linux_mmap.c            # SYS_mmap/munmap/mprotect/madvise
│   │   ├── linux_clone.c           # SYS_clone (pthread)
│   │   ├── linux_futex.c           # SYS_futex
│   │   ├── linux_epoll.c           # SYS_epoll_*
│   │   ├── linux_eventfd.c         # SYS_eventfd2 etc.
│   │   ├── linux_signal.c          # SYS_rt_sigaction etc.
│   │   ├── linux_prctl.c           # SYS_prctl
│   │   ├── linux_sched.c           # SYS_sched_*
│   │   ├── linux_inotify.c
│   │   └── linux_proc_self.c       # /proc/self/* generators
│   └── ...
├── fs/
│   ├── procfs/                     # NEW
│   │   ├── procfs.c                # /proc impl
│   │   ├── proc_self.c             # /proc/self/{maps,exe,cmdline,status}
│   │   ├── proc_cpuinfo.c
│   │   ├── proc_meminfo.c
│   │   └── ...
│   ├── tmpfs/                      # NEW
│   │   └── tmpfs.c                 # /tmp + /dev/shm
│   ├── devfs/                      # extender com /dev/{urandom,null,zero,tty}
│   └── ...
└── ...

userland/
├── lib/
│   ├── capylibc/                   # KEEP como compat shim (ou deprecate)
│   ├── musl/                       # NEW: vendored musl-1.2.5+
│   │   ├── arch/x86_64/syscall_arch.h
│   │   ├── src/
│   │   ├── include/
│   │   └── ...
│   └── musl-build/                 # NEW: built musl artifacts
│       ├── lib/libc.a
│       ├── lib/libc.so.musl-x86_64
│       ├── lib/libpthread.a
│       └── include/
├── bin/
│   ├── capysh/                     # KEEP
│   ├── hello/                      # KEEP
│   ├── exectarget/                 # KEEP
│   ├── firefox/                    # NEW: stub Marker para futuro
│   └── js/                         # NEW: SpiderMonkey shell (Marco M1)
└── sdk/                            # NEW: cross-build sysroot
    └── x86_64-capyos-musl/
        ├── lib/                    # musl + libstdc++
        ├── include/
        └── bin/

docs/
├── plans/active/
│   ├── firefox-port-roadmap.md     # visao alta
│   ├── firefox-port-platform-shim.md  # ESTE arquivo
│   └── ...
└── architecture/
    ├── firefox-port-deep-dive.md   # analise tecnica
    ├── linux-syscall-compat.md     # NEW: documentar Linux ABI
    └── ...
```

## Estrategia de testes

Cada tarefa S<X> tem testes:
1. **Unit test (host)** -- testa logica da API em userland
2. **Smoke test (CapyOS QEMU)** -- binario real exercita a syscall
3. **Comparacao Linux** -- mesmo binario em Linux deve produzir
   o mesmo output (regressao se diverge)

Exemplo S1.5 (futex):
- Test 1: pthread_mutex em musl userland (host) -> OK
- Test 2: rodar binario com 4 threads + mutex contention em
  CapyOS, debugcon log mostra ordering correto
- Test 3: rodar mesmo binario em Linux x86_64; comparar logs

## Riscos e mitigacoes

| Risco | Mitigacao |
|---|---|
| Linux syscall numbers diferem do CapyOS | Tabela `linux_syscall[]` traduz; capylibc continua usando syscalls nativos CapyOS |
| Estruturas (`struct stat`, `struct timespec`) tem layout diferente | Forcar layout Linux na ABI Linux-compat; capylibc tem layout proprio |
| musl assume `vdso` para clock_gettime | Implementar vdso minimo OU forcar musl a usar syscall (slowdown ~2x mas funciona) |
| TLS via `arch_prctl(ARCH_SET_FS)` pode conflitar com TSS GS no CapyOS | Reservar `%fs:0` para userland TLS; `%gs:0` continua kernel CPU-local |
| Stub `_unimplemented.cpp` causa crashes em runtime | Documentar quais primitives sao stub; rodar Firefox com `MOZ_FORCE_DISABLE_E10S=1` initialmente |
| musl pode ter bugs em cross-build de Stylo (Rust+C++ FFI) | Comecar com `--without-rust` e adicionar Stylo apenas em S6.5 |
| C++ runtime (libstdc++) pode requerer mais syscalls que esperamos | Strace de helloworld C++ na primeira semana de S6 |
| RLIMIT_NOFILE default baixo no CapyOS quebra Firefox (precisa ~4096) | S1.17 expoe setrlimit; CapyOS aumenta default para 4096 ou 8192 |

## Validacao Marco M1

Criterios de aceitacao para "SpiderMonkey roda em CapyOS":

1. Binary `/usr/bin/js` (ou `userland/bin/js/js.elf`) compila e linka.
2. ELF tem `INTERP /lib/ld-musl-x86_64.so.1`.
3. Kernel CapyOS carrega ELF + executa ld-musl + executa main.
4. `js -e 'print(1+1)'` imprime `2`.
5. `js -e 'print(JSON.stringify(Array.from({length:10}, (_,i) => i*i)))'`
   imprime `[0,1,4,9,16,25,36,49,64,81]`.
6. test262 subset (1k tests) passa pelo menos 95%.
7. Stack trace de uma exception cobre stack walking corretamente
   (testa S1.12 + libgcc unwind).

## Proxima sessao (apos S1.1+S1.2+S1.3 terem comecado)

A proxima sessao pode focar em:
- **Implementar S1.1+S1.2+S1.3** -- linux_syscall_table.c +
  linux_mmap.c + linux_clock.c (~3 semanas)
- **OU** preparar smoke test mais elaborado para validar que
  o stack atual (process model + scheduler + IPC) suporta o
  load esperado pelo Firefox.
- **OU** comecar S2 (procfs) que e independente de S1 e libera
  paralelismo.

## Referencias adicionais

- musl libc roadmap: <https://musl.libc.org/roadmap.html>
- Linux syscall numbers (x86_64):
  `/Volumes/Firefox/third_party/lss/linux_syscall_support.h`
- musl arch x86_64 syscalls:
  upstream `musl-1.2.x/arch/x86_64/syscall_arch.h`
- Mozilla searchfox (codebase search):
  <https://searchfox.org/>
