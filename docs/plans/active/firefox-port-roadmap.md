# Roadmap: Port do Firefox para o CapyOS + Erradicacao do Capybrowser

**Status**: Sessao 7 (2026-05-05) -- atualizado com analise do codigo
fonte real do Firefox apos o usuario adicionar `/Volumes/Firefox`
como workspace.
**Autor**: Cascade
**Trigger**: usuario decidiu que o capybrowser nao amadurecera o
suficiente para web real e quer migrar para um port do Firefox open
source. Antes do port, o capybrowser e suas dependencias diretas
sao removidos do sistema. Estruturas reusaveis para o Firefox sao
preservadas.

**Documentos relacionados (criados na sessao 7):**
- `docs/architecture/firefox-port-deep-dive.md` -- analise tecnica
  detalhada baseada no codigo fonte de cada componente (widget,
  ipc, gfx, js, mozglue, xpcom, media).
- `docs/plans/active/firefox-port-platform-shim.md` -- plano
  acionavel da camada de shim de plataforma (Fase F1+F4 unificada
  como "Strategy A: Linux ABI compatibility").

## Resumo executivo

### Parte A: Diagnostico do estado atual (2026-05-05)

O CapyOS tem uma stack de browser monolitica e custom-feita
(capybrowser engine ring 3 + browser_chrome runtime ring 0 +
browser_app shell + capyhtml parser/render + browser_ipc framing)
que nao escala para a web real. Os motivos:

1. **CSS minimo**: capyhtml suporta um subset de CSS 2.1 inline;
   nao ha cascade real, nao ha grid/flexbox/transforms.
2. **Sem JavaScript**: nao ha engine JS. A web moderna depende de
   JS em 99% dos sites uteis.
3. **Sem layout engine moderno**: nao ha box model completo (margin
   collapse, float, position fixed/sticky, etc.).
4. **Sem video/audio decode**: nao ha codec stack (ffmpeg/libdav1d).
5. **Sem WebRender ou aceleracao**: rasterizacao puramente
   software, sem GPU offload.
6. **TLS via BearSSL**: bom para HTTPS basico mas falta HTTP/2,
   HTTP/3, ALPN avancado, OCSP stapling, etc.
7. **DNS rudimentar**: sem DNS-over-HTTPS, sem cache TTL real.

### Parte B: O que falta no sistema para suportar Firefox real

Firefox e ~30M LOC de C++17/Rust/JS, requer um SDK POSIX completo
e uma stack grafica acelerada. O CapyOS hoje (alpha) tem ~6% das
fundacoes necessarias. As 7 fases abaixo descrevem **o que precisa
existir antes de tentar buildar Firefox**:

#### Fase F1: POSIX user-space completo (musl-equivalente)

Hoje o `userland/lib/capylibc/` e minimo (poucas funcoes). Firefox
exige:
- **stdio**: `fopen`/`fread`/`fwrite`/`fseek`/`ftell`/`fflush`/
  `fprintf`/`scanf`/`sprintf`/`vsnprintf`/`getc`/`ungetc`/etc.
- **string**: `strstr`/`strtok`/`strcasecmp`/`memmem`/etc.
- **stdlib**: `malloc`/`free`/`realloc`/`calloc` (heap real, nao
  o kheap kernel), `strtol`/`strtoul`/`strtod`/`atoi`/`atof`,
  `qsort`, `bsearch`, `getenv`/`setenv`/`environ`, `exit`/`atexit`.
- **time**: `time`/`gettimeofday`/`clock_gettime`/`localtime`/
  `mktime`/`strftime`.
- **errno**: `errno` thread-local (ou per-task), `strerror`.
- **math**: `libm` completo (`sin`/`cos`/`sqrt`/`pow`/`log`/
  `exp`/`floor`/`ceil`/`fabs`/etc.). Firefox usa muito.
- **stdint/stdbool/stddef**: ja temos basicos via `tools/host/include`.
- **locale**: `setlocale`, charset conversao basica (UTF-8).
- **dirent**: `opendir`/`readdir`/`closedir`/`scandir`.
- **sys/stat**: `stat`/`lstat`/`fstat`/`chmod`/`chown`.
- **sys/types**: `pid_t`/`uid_t`/`gid_t`/`off_t`/`size_t`/`ssize_t`.
- **fcntl**: `open` com `O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|
  O_APPEND|O_NONBLOCK`, `fcntl` (`F_GETFL`/`F_SETFL`/`F_DUPFD`).
- **unistd**: `read`/`write`/`close`/`lseek`/`pipe`/`dup`/`dup2`/
  `fork`/`exec*`/`wait`/`waitpid`/`getpid`/`getppid`/`getcwd`/
  `chdir`/`access`/`unlink`/`mkdir`/`rmdir`/`isatty`/`sleep`/
  `usleep`/`nanosleep`/`alarm`/`pause`.
- **sys/mman**: `mmap`/`munmap`/`mprotect`/`madvise`/`msync`.
  **CRITICO** -- Firefox usa mmap pesadamente para caches, e o
  V8/SpiderMonkey JS engine usa para JIT pages com `PROT_EXEC`.
- **pthread**: `pthread_create`/`pthread_join`/`pthread_mutex_*`/
  `pthread_cond_*`/`pthread_rwlock_*`/`pthread_key_*` (TLS),
  `pthread_once`. **CRITICO** -- Firefox e altamente
  multi-threaded (workers, layout, network, IPC).
- **signal**: `signal`/`sigaction`/`sigprocmask`/`raise`/`kill`/
  `siglongjmp`/`setjmp`/`longjmp`.
- **sys/socket**: AF_UNIX + AF_INET + AF_INET6, SOCK_STREAM +
  SOCK_DGRAM. `socket`/`bind`/`listen`/`accept`/`connect`/`send`/
  `recv`/`sendto`/`recvfrom`/`getsockname`/`getpeername`/
  `setsockopt`/`getsockopt`/`shutdown`.
- **netinet/in**: `htons`/`htonl`/`ntohs`/`ntohl`, `inet_pton`/
  `inet_ntop`.
- **netdb**: `getaddrinfo`/`freeaddrinfo` (pesado: precisa de DNS
  resolver real).
- **sys/select**, **sys/poll**, **sys/epoll**: I/O multiplexado.
  Firefox usa epoll no Linux, kqueue no BSD/macOS. Vamos
  implementar epoll-like.
- **sys/wait**: `wait`/`waitpid`/`waitid`/`WIFEXITED`/etc.
- **sys/uio**: `readv`/`writev` (scatter/gather; Firefox usa).
- **sys/ioctl**: muitas ioctls especificas. Vamos suportar o
  subset necessario (TIOCGWINSZ, FIONREAD, etc.).
- **sys/sysinfo** ou **sysconf**: `_SC_NPROCESSORS_ONLN`,
  `_SC_PAGESIZE`, etc.
- **sys/inotify** ou equivalent: filesystem watch (opcional, mas
  Firefox usa).
- **sys/random** ou **getrandom**: entropia para crypto.

Decisao tecnica: portar `musl libc` (BSD/MIT licensed) e adaptar
para syscalls do CapyOS. Estimativa: 4-6 meses para uma engenharia
focada.

#### Fase F2: VFS e filesystems

Hoje o `src/fs/` tem suporte parcial. Firefox precisa:
- **ext4 read+write** ou um filesystem comparavel (CapyFS proprio
  com features semelhantes). Hoje temos ext2 read-only.
- **/proc** (procfs) para cpuinfo/meminfo/maps/stat (Firefox le).
- **/sys** ou stub minimo.
- **/dev** com `/dev/null`, `/dev/zero`, `/dev/random`,
  `/dev/urandom`, `/dev/tty`, `/dev/dri/*` (se GPU).
- **tmpfs** em `/tmp` para shared memory entre processos
  Firefox.
- **shm fs** (POSIX shared memory) para `shm_open`.
- **Permissoes UNIX** (chmod/chown bits) -- ja temos o uid/gid
  fields em `struct process`, falta enforcement.

Estimativa: 3-4 meses.

#### Fase F3: Process model real

Hoje temos `process_create`/`process_fork`/`process_exec_replace`,
mas:
- **fork() real**: hoje e CoW parcial. Firefox usa fork+exec
  para spawn de filhos.
- **execve() POSIX**: argv/envp packing real no stack inicial.
  Hoje o stack inicial e quase vazio (so RAX=rank).
- **Dynamic linker (ld-musl-equivalente)**: Firefox tem ~50
  shared libraries (`libxul.so` e a principal, ~150 MB!).
  Precisa de:
  - PT_INTERP no ELF para invocar o dynamic linker
  - Lazy resolution (PLT/GOT)
  - `dlopen`/`dlsym`/`dlclose` para plugins
  - `LD_LIBRARY_PATH`, `LD_PRELOAD`
- **clone()** com flags Linux-compat (CLONE_VM, CLONE_FS,
  CLONE_FILES, CLONE_THREAD, etc.) -- pthreads no Linux usa
  clone(CLONE_THREAD).
- **Signal handling user-space** real: setjmp/sigaltstack,
  delivery em entrada de syscall, etc.
- **rlimit**: `getrlimit`/`setrlimit` (Firefox set RLIMIT_NOFILE).
- **Sandbox**: Firefox usa namespaces + seccomp. Nao precisamos
  reimplementar tudo, mas algum mecanismo de privilegio drop
  e necessario. Talvez via capabilities-like (cap_sys_*).

Estimativa: 6-8 meses (especialmente o dynamic linker).

#### Fase F4: Toolchain e linkagem dinamica

Hoje compilamos com `x86_64-elf-gcc -static`. Para Firefox:
- **Cross-toolchain `x86_64-capyos-musl-gcc`** com sysroot
  apontando para a libc capyos.
- **Cross-toolchain Rust** (`rustup target add
  x86_64-unknown-capyos`). Firefox tem ~30% Rust hoje
  (Stylo, WebRender, mp4parse, etc.).
- **Dynamic linking**: `-shared`, `-fPIC`, `.so` versioning.
- **Suporte a thread-local storage (TLS)**: `__thread`,
  `pthread_key_*`. SpiderMonkey usa TLS pesado.
- **C++ runtime**: `libstdc++` ou `libc++` portado (excecoes,
  RTTI, virtual dispatch). Firefox e majoritariamente C++.
- **C++17/20** features: structured bindings, `std::optional`,
  `std::variant`, coroutines (Firefox 130+ usa).
- **Build system**: a Mozilla build system (`mach build`)
  precisa ser convencida de que `--target=x86_64-capyos` e
  valido. Patches necessarios.

Estimativa: 8-12 meses.

#### Fase F5: Stack grafica acelerada

Hoje temos framebuffer linear + compositor 2D software.
Firefox precisa:
- **GPU 3D**: WebRender depende de OpenGL ES 3 ou WebGPU
  (Vulkan/Metal/D3D12). Sem driver real, podemos usar
  `swrast`/`llvmpipe` (Mesa software rasterizer) que
  implementa OpenGL inteiro em CPU. Performance:
  ~10-30 FPS em 1080p, suficiente para web basica.
- **Cairo/Skia**: Firefox usa Skia hoje (substituiu Cairo
  em 2D). Skia precisa de PNG/JPEG/WebP decoders, harfbuzz
  (text shaping), freetype (font hinting).
- **Wayland-equivalente** ou GTK abstrai: implementar um
  protocolo minimo (criar surface, comprar buffer, commit,
  receber input events). Firefox tem ports para Wayland,
  X11, Cocoa, Win32; podemos forkar o port Wayland e adaptar.
- **Fonts**: instalar pelo menos fonts-noto + fonts-dejavu
  + emoji para web normal.
- **DPI / HiDPI**: detectar e propagar.

Estimativa: 12-18 meses.

#### Fase F6: Audio, video, hardware extras

- **Audio output**: PulseAudio API ou ALSA (subset). Firefox
  usa cubeb (abstracao Mozilla) que tem backend para
  PulseAudio, ALSA, OSS, Cocoa, WASAPI. Vamos implementar
  um backend `cubeb_capyos.c`.
- **Video decode**: ffmpeg portado, libav1, libdav1d.
- **Camera/microfone**: V4L2-like subset (opcional).
- **GPU compute** (WebGPU): adia-se.

Estimativa: 6-9 meses.

#### Fase F7: Build, integracao, polish

- **mach configure** com `--target=x86_64-capyos`.
- **mozconfig** otimizado (LTO, PGO opcional).
- **Tests**: rodar Mochitest/xpcshell-tests subset.
- **Profile management**: `~/.mozilla/firefox/`.
- **Telemetry off**, **studies off** por privacy.
- **Update channel**: nightly (a comunidade nao vai dar
  ESR/release oficial; mantemos um fork patches).
- **Distribuicao**: ISO bootavel com Firefox preinstalado em
  `/usr/lib/firefox/` + symlink em `/usr/bin/firefox`.

Estimativa: 3-6 meses.

### Parte C: Estimativa total e alternativas

Total para Firefox: **~36-60 meses** com time pequeno
(2-4 engenheiros). Realista para CapyOS atual: **deferir e
considerar alternativas mais leves no curto prazo**:

| Alternativa | LOC | C++/Rust | JS | CSS3 | Esforco |
|---|---|---|---|---|---|
| **Firefox** | 30M | C++17 + Rust | SpiderMonkey full | CSS3 + WebExtensions | 36-60 meses |
| **Chromium** | 35M | C++20 | V8 full | CSS3 | comparable |
| **Servo** (Rust) | 1.3M | Rust | SpiderMonkey embed | CSS3 parcial | 18-30 meses |
| **WebKit** (port) | 8M | C++14 | JavaScriptCore | CSS3 | 24-36 meses |
| **NetSurf** | 50k | C99 | mujs (opcional) | CSS2.1 + alguns CSS3 | 6-9 meses |
| **Dillo** | 100k | C99 + FLTK | sem JS | CSS basico | 4-6 meses |
| **Lynx** (text) | 250k | C89 | sem | sem CSS | 2-3 meses |

Recomendacao tecnica para o CapyOS: comecar com **NetSurf** (C99
puro, leve, compativel com a stack atual de fonts/raster que ja
temos) e em paralelo construir as fundacoes (Fases F1-F4) que
permitirao o port do Firefox em medio prazo. NetSurf sera
substituido por Firefox quando F1-F7 completas.

Mas o usuario solicitou Firefox, entao o roadmap acima permanece
como objetivo final. **A erradicacao do capybrowser (Parte D
abaixo) e o pre-requisito imediato; depois F1-F7 sao executadas
sequencialmente.**

## Parte D: Plano de erradicacao do capybrowser

### Inventario do que sera removido (78 arquivos)

#### Codigo fonte (51 arquivos)
- `src/apps/browser_app/` (5 arquivos: browser_app.c, homepage.c,
  nav.c, toolbar.c, url_edit.c)
- `src/apps/browser_chrome/` (6 arquivos: chrome.c, runtime.c,
  runtime_image.c, fetch_resolver.c, audit_log.c, watchdog.c)
- `src/apps/browser_ipc/` (3 arquivos: codec.c, fetch.c, image.c)
- `src/kernel/browser_smoke.c`
- `src/kernel/browser_engine_spawn.c`
- `userland/bin/capybrowser/` (2 arquivos + diretorio:
  main.c, image_cache.c, image_cache.h)
- `userland/lib/capyhtml/` (4 src + 5 includes + README)

#### Headers (16 arquivos)
- `include/apps/browser_app.h`
- `include/apps/browser_app_homepage.h`
- `include/apps/browser_app_nav.h`
- `include/apps/browser_app_toolbar.h`
- `include/apps/browser_app_url_edit.h`
- `include/apps/browser_chrome.h`
- `include/apps/browser_chrome_runtime.h`
- `include/apps/browser_chrome_audit.h`
- `include/apps/browser_chrome_fetch_resolver.h`
- `include/apps/browser_dimensions.h`
- `include/apps/browser_ipc.h`
- `include/apps/browser_watchdog.h`
- `include/kernel/browser_engine_spawn.h`
- `include/kernel/browser_smoke.h`
- `userland/lib/capyhtml/include/capyhtml/{parser,render,raster,font,types}.h`

#### Tests (22 arquivos)
- 16 arquivos `test_browser_*.c` + 1 `.h`
- 1 arquivo `test_capybrowser_image_cache.c`
- 4 arquivos `test_capyhtml_*.c`

#### Tools/scripts (1 arquivo)
- `tools/scripts/smoke_x64_browser_spawn.py`

#### Docs (5 arquivos -- mover para historical)
- `docs/architecture/browser-ipc.md` -> `historical/`
- `docs/plans/active/f3-3c-html-viewer-userland-slicing.md`
  -> `historical/` (ja tem copia historical, deletar active)
- `docs/plans/active/f3-3f-browser-desktop-wiring.md`
  -> idem
- `docs/screenshots/CapyUI/v1/desktop-browser{1,2}.png`
  preservar (registro historico, nao afeta build)

### Estruturas que **PERMANECEM** (usadas pelo Firefox no futuro)

Estas areas sao explicitamente preservadas porque sao necessarias
para o port do Firefox (Fases F1-F7):

- **`src/memory/vmm.c` + `pmm.c`**: VMM core (fix do HUGE bit
  da sessao 5). Firefox fara mmap pesado.
- **`src/kernel/process.c` + `task.c` + `pipe.c`**: process model,
  scheduler, IPC. Firefox e multi-process e multi-thread.
- **`src/kernel/elf_loader.c` + `user_task_init.c` +
  `syscall.c`**: loader de ELF e syscalls. Vamos ESTENDER
  estes para suportar dynamic linking (PT_INTERP) e mais
  syscalls (mmap, pthread_create via clone, etc.).
- **`src/arch/x86_64/cpu/context_switch.S`,
  `interrupts_asm.S`, `syscall/syscall_entry.S`**: context
  switch, IDT, syscall entry. Firefox depende disso.
- **`src/gui/desktop/`, `src/gui/compositor/`,
  `src/gui/fbcon/`**: desktop runtime, compositor 2D,
  framebuffer console. Firefox renderiza dentro de uma
  janela do compositor.
- **`userland/lib/capylibc/`**: libc base. Sera ESTENDIDA
  para musl-equivalente (Fase F1).
- **`src/net/`** + **BearSSL**: stack TCP/UDP + TLS. Firefox
  usa.
- **`src/fs/`**: VFS, ext2 read-only. Sera ESTENDIDO para
  ext4 read+write + procfs + tmpfs (Fase F2).
- **`userland/bin/capysh/`** + **`hello/`** + **`exectarget/`**:
  shells e binarios de smoke. Firefox no futuro precisara
  de shell para debugging.
- **`src/config/system_settings.c`**: tem `browser_homepage`
  -- mantemos o campo (sera reusado pelo Firefox como
  homepage default).

### Sequencia de execucao (8 fases internas)

Cada fase e atomica: arquivos removidos + referencias
desligadas + build ainda compilavel (logicamente; o user roda
make depois). Apos cada fase, syntax-check via Python script
sobre TUs nao-tocadas para garantir nada externo quebrou.

#### E1: Desabilitar pontos de entrada
- `src/arch/x86_64/kernel_main.c`: remover include
  `kernel/browser_smoke.h` e o bloco
  `#ifdef CAPYOS_BOOT_RUN_BROWSER_SMOKE`.
- `src/gui/desktop/desktop.c`: remover include
  `apps/browser_app.h`, `menu_action_browser`, e a entrada
  do menu "Browser".
- `src/shell/commands/extended.c`: remover `cmd_open_browser`
  e a registracao `open-browser`.

#### E2: Remover testes (preserva infra de teste do kernel)
Remover os 22 arquivos de teste e suas referencias em
`Makefile` (variavel `TEST_SRCS`).

#### E3: Remover apps user-side (browser_app, browser_chrome,
browser_ipc)
Remover diretorios `src/apps/browser_app/`,
`src/apps/browser_chrome/`, `src/apps/browser_ipc/` e suas
referencias em `Makefile` (lista `CAPYOS64_OBJS`).

#### E4: Remover kernel hooks especificos
- `src/kernel/browser_smoke.c` + `include/kernel/browser_smoke.h`
- `src/kernel/browser_engine_spawn.c` +
  `include/kernel/browser_engine_spawn.h`
- Referencias em `Makefile`.

#### E5: Remover userland binaries e libs
- `userland/bin/capybrowser/` (binario + image cache)
- `userland/lib/capyhtml/` (parser, render, raster, font)
- Referencias em `Makefile` (`CAPYBROWSER_*`, `CAPYHTML_*`,
  blob target).

#### E6: Remover headers
Remover 16 headers em `include/apps/browser_*.h` e
`include/kernel/browser_*.h`.

#### E7: Remover embedded blob e clean Makefile
- `embedded_progs.c`/`.h`: remover linhas relacionadas a
  capybrowser (`g_test_capybrowser_*`,
  `_binary_capybrowser_elf_*`, prog_path "/bin/capybrowser",
  setter de teste).
- `Makefile`: remover targets `capybrowser-elf`,
  `capybrowser-blob`, `capyhtml-userland-syntax`,
  `smoke-x64-browser-spawn`, e variaveis
  `CAPYBROWSER_*`/`CAPYHTML_*`.
- `tools/scripts/smoke_x64_browser_spawn.py`: deletar.

#### E8: Atualizar docs e marcar deprecacao
- `docs/architecture/browser-ipc.md`: mover para
  `docs/archive/` com nota de deprecacao.
- `docs/plans/active/f3-3{c,f}-*.md`: deletar (copia ja
  esta em historical).
- `docs/plans/STATUS.md`: nova linha registrando a
  erradicacao.
- `docs/plans/active/capyos-master-plan.md`: nova entrada
  em "Entregas consolidadas".
- Criar este documento (`firefox-port-roadmap.md`) como
  ativo (marca a transicao).

### Validacao

Apos cada fase:
1. **Grep dangling references**: garantir que nenhum arquivo
   nao-removido referencia simbolos removidos.
2. **Syntax-check** TUs do kernel core via Python script
   freestanding x86_64 (nao roda make, mas valida sintaxe).
3. **Layout audit**: `python3 tools/scripts/audit_source_layout.py
   --strict` deve passar (todos os src/headers ainda sao
   bem-formados em arvores `kernel`/`apps`/`userland`).

### Riscos e mitigacoes

| Risco | Mitigacao |
|---|---|
| Quebrar boot por dangling reference em `kernel_main.c` | Fase E1 desabilita pontos de entrada primeiro; smoke flag desligada |
| Tests order-dependent quebram | Remover testes em E2 antes de remover codigo em E3 |
| `embedded_progs` deixa simbolos `_binary_capybrowser_elf_start` orfaos | E7 remove APOS E5 (que remove o blob target no Makefile) |
| Settings file tem `browser_homepage` que ficaria orfao | Mantemos o campo -- Firefox usara o mesmo |
| Compositor/desktop dependerem de browser_app | Fase E1 ja desliga; estruturas de janela/compositor permanecem |
| Conflito ao reintroduzir Firefox: nomes colidem | Firefox vivera em `userland/bin/firefox/` -- nomes nao colidem |

## Parte E: Pos-erradicacao -- proxima sessao

Apos completar D (8 fases), o sistema esta limpo e estavel.
A proxima sessao iniciara F1 (POSIX user-space completo) com
foco em portar musl libc para `userland/lib/capylibc/`. O
investimento em F1 e prerequisito de tudo o resto e nao depende
de hardware grafico, entao pode ser feito imediatamente.

## Parte F: Revisoes da sessao 7 (com codigo Firefox em maos)

Apos clonar `/Volumes/Firefox` (mozilla-firefox/firefox upstream)
exploramos os componentes-chave. As secoes abaixo refinam o plano
original com base no codigo real, identificando atalhos, decisoes
de estrategia, e tarefas concretas com referencias `path:linha`.

### F.1 Decisao: Strategy A (Linux ABI) vs Strategy B (CapyOS native)

**Strategy A: Linux ABI compatibility (RECOMENDADA).**
O Firefox detecta a plataforma via `target.kernel == "Linux"` +
`target.os == "GNU"` em `build/moz.configure/init.configure:797`.
Se o CapyOS expuser uma libc compativel com glibc/musl no nivel
de syscalls e signatures, o build do Firefox usa **TODO** o
caminho Linux/POSIX sem patches no upstream. O codigo Firefox ja
tem branches `_posix.cpp`, `_unimplemented.cpp`, `_none.cpp`
(exemplos:
`mozglue/misc/Mutex_posix.cpp`,
`ipc/glue/CrossProcessMutex_unimplemented.cpp:11`,
`ipc/glue/ProcessUtils_none.cpp`) que vamos reusar verbatim.

Vantagens:
- Reduz patches no upstream Firefox de centenas para dezenas.
- Permite acompanhar releases nightly sem rebases caros.
- musl libc roda 100% Linux ABI -> portar musl satisfaz F1.

Desvantagens:
- CapyOS precisa de todas as syscalls Linux que Firefox toca:
  `clock_gettime`, `mmap`/`munmap`/`mprotect`/`madvise`, `clone`
  (ou `pthread_create` no userland), `epoll_create1`/`epoll_ctl`/
  `epoll_wait`, `eventfd`, `signalfd`, `inotify_init1`,
  `getrandom`, `prlimit64`, `prctl(PR_GET_NAME)`,
  `pidfd_open`, `memfd_create`, `pipe2`, `dup3`, `accept4`,
  `recvmmsg`/`sendmmsg`.

**Strategy B: CapyOS como nova plataforma reconhecida.**
Adicionar `"CapyOS"` em
`python/mozbuild/mozbuild/configure/constants.py:Kernel` e
estender `split_triplet()` em
`build/moz.configure/init.configure:455`. Requer patchar dezenas
de moz.builds (`if CONFIG["TARGET_KERNEL"] == "CapyOS"`) ao longo
da arvore.

Vantagens:
- Nao precisa simular Linux ABI -- pode ter API mais idiomatica
  CapyOS.
- Sandbox/seguranca podem usar mecanismos nativos (capabilities,
  namespaces, ipc-token) sem fingir ser Linux.

Desvantagens:
- Centenas de patches no Firefox que precisam de manutencao
  continua. Cada release nightly potencialmente quebra builds.

**DECISAO: Strategy A para o objetivo "Firefox roda no CapyOS".**
Strategy B fica como objetivo de longo prazo (post-F7) caso o
projeto evolua para um CapyOS BSD-like com filosofia propria.

### F.2 widget/: HeadlessWidget como ponto de partida

O Firefox ja tem um backend minimalista em
`/Volumes/Firefox/widget/headless/` (
`HeadlessWidget.{cpp,h}`,
`HeadlessCompositorWidget.{cpp,h}`,
`HeadlessClipboard.{cpp,h}`,
`HeadlessSound.{cpp,h}`,
`HeadlessScreenHelper.{cpp,h}`).

`HeadlessWidget.cpp` tem **18KB** -- ordens de magnitude menor
que `widget/gtk/nsWindow.cpp` (266KB) ou `widget/windows/nsWindow.cpp`.
O CapyOS comeca DUPLICANDO `widget/headless/` para
`widget/capyos/`, e depois injeta:
1. Conexao com o compositor 2D do CapyOS
   (via `gui/core/compositor.c` e `drivers/console/fbcon.c`).
2. Pipe de input keyboard/mouse vindo do
   `gui/widgets/` do CapyOS.
3. `nsAppShell` + `nsToolkit` minimos.

Estimativa revisada: F5.widget passa de 12 meses (re-fork GTK
backend) para **~3-4 meses** (extender headless backend).

Referencia detalhada:
`docs/plans/active/firefox-port-platform-shim.md`.

### F.3 gfx/: SWGL elimina dependencia de Mesa/swrast

**Descoberta crucial:** Firefox tem
`/Volumes/Firefox/gfx/wr/swgl/` -- "Software OpenGL implementation
for WebRender", um rasterizador Rust+C++ single-threaded escrito
pela Mozilla. Le `gfx/wr/swgl/README.md:1-9`. Substitui Mesa
swrast/llvmpipe completamente.

Implicacoes para o roadmap:
- **F5.gpu sai do escopo critico.** Nao precisamos portar Mesa
  (~5M LOC). SWGL ja vem com Firefox e compila com a mesma
  toolchain Rust+clang do resto.
- **WebRender roda em SWGL backend** -- o pipeline grafico
  inteiro (composicao de layers, shaders WGSL/GLSL, texture
  atlas) executa em CPU sem driver GPU.
- Performance estimada: **30-60 FPS em 1080p** para web normal.
  Suficiente para o objetivo do CapyOS.

F5 nova estimativa: **6-9 meses** (em vez de 12-18) ja que SWGL
remove o capitulo "Mesa swrast port".

### F.4 ipc/: Chromium IPC e Mojo Ports tem branch "_unimplemented"

O Firefox usa Chromium IPC (forked) + Mojo Ports para multi-process.
Em
`/Volumes/Firefox/ipc/chromium/moz.build:64-83`,
o branch POSIX/non-Windows e:

```python
if CONFIG["TARGET_KERNEL"] != "WINNT":
    UNIFIED_SOURCES += [
        "src/base/condition_variable_posix.cc",
        "src/base/lock_impl_posix.cc",
        "src/base/platform_thread_posix.cc",
        "src/base/process_util_posix.cc",
        "src/base/string16.cc",
        "src/base/thread_local_posix.cc",
        "src/base/waitable_event_posix.cc",
        "src/chrome/common/ipc_channel_posix.cc",
    ]
```

Strategy A (Linux ABI) reusa todos esses verbatim.
Adicionalmente:
- `mozglue/misc/Mutex_posix.cpp` -- pthread_mutex_t.
- `mozglue/misc/ConditionVariable_posix.cpp` -- pthread_cond_t.
- `mozglue/misc/RWLock_posix.cpp` -- pthread_rwlock_t.
- `mozglue/misc/TimeStamp_posix.cpp` -- clock_gettime(CLOCK_MONOTONIC).
- `js/src/threading/posix/PosixThread.cpp` -- pthread_create/join.

**Tarefa F1.pthread:** implementar pthread minimal no
`userland/lib/capylibc/pthread/`. Subset minimo:
- pthread_create/join/detach/equal/self
- pthread_mutex_init/lock/unlock/destroy (recursive + normal)
- pthread_cond_init/wait/signal/broadcast/destroy
- pthread_rwlock_init/rdlock/wrlock/unlock/destroy
- pthread_key_create/getspecific/setspecific (TLS)
- pthread_once
- pthread_setname_np / pthread_getname_np

### F.5 ipc/: Cross-process primitives podem comecar como stub

`/Volumes/Firefox/ipc/glue/CrossProcessMutex_unimplemented.cpp`
prova que e aceitavel comecar com `MOZ_CRASH("...not allowed on
this platform")` para primitivas que so sao usadas em casos raros.

`/Volumes/Firefox/ipc/glue/ProcessUtils_none.cpp` faz o mesmo
para `SetThisProcessName()`.

**Implicacao:** o port pode ter uma fase de "Firefox roda mas
crashea se voce abre uma WebExtension". Isso e aceitavel para
**marco "Firefox carrega about:blank"**, e sera completado
incrementalmente.

### F.6 SpiderMonkey: pthread + clock_gettime suficientes

`js/src/threading/posix/PosixThread.cpp` mostra que SpiderMonkey
(o motor JavaScript) precisa apenas de:
- `pthread_attr_init`/`pthread_attr_setstacksize`/`pthread_create`
- `pthread_join`/`pthread_detach`/`pthread_equal`/`pthread_self`
- `pthread_setname_np` (Linux variant)

`js/src/threading/posix/CpuCount.cpp` precisa de
`sysconf(_SC_NPROCESSORS_ONLN)`.

`mfbt/TimeStamp_posix.cpp` precisa de
`clock_gettime(CLOCK_MONOTONIC)` ou
`clock_gettime(CLOCK_MONOTONIC_RAW)`.

**Conclusao:** SpiderMonkey nao requer trabalho de port custom
desde que F1.pthread + F1.time esteja completo. Esto e
significativo porque SpiderMonkey sozinho tem ~1M LOC.

### F.7 cubeb: backend `cubeb_oss.c` ou `cubeb_pulse.c` minimal

`/Volumes/Firefox/media/libcubeb/src/` tem backends modulares:
- `cubeb_alsa.c` (40KB)
- `cubeb_pulse.c`
- `cubeb_oss.c` (34KB) -- **mais simples, OSS API e
  read/write em /dev/dsp**
- `cubeb_jack.cpp`
- `cubeb_audiounit.cpp` (macOS)
- `cubeb_wasapi.cpp` (Windows)
- `cubeb_aaudio.cpp`/`cubeb_opensl.cpp` (Android)

**Estrategia:** comecar com **cubeb_null** (driver silencio)
para `marco "Firefox carrega about:blank com som mute"`. Depois
implementar `cubeb_capyos.c` baseado no driver de audio do
CapyOS (a ser construido em F6) seguindo o modelo simples do
`cubeb_oss.c`.

F6 nova estimativa: **3-5 meses** em vez de 6-9 (cubeb_null +
cubeb_capyos OSS-like).

### F.8 xpcom/: nsLocalFileUnix.cpp compila quase verbatim

`/Volumes/Firefox/xpcom/io/nsLocalFileUnix.cpp` (76KB) usa
APIs POSIX padrao:
- `open`/`read`/`write`/`close`/`lseek`
- `stat`/`lstat`/`fstat`/`chmod`
- `opendir`/`readdir`/`closedir`
- `mkdir`/`rmdir`/`unlink`/`rename`
- `realpath`/`readlink`/`symlink`/`link`
- `access`/`chown`
- `fchmod`/`fchown`/`futimes`

Strategy A: F1.fs (filesystem syscalls) e F2.vfs (VFS layer)
sao suficientes para esse arquivo compilar verbatim.

### F.9 Build system: Mach + moz.configure

`/Volumes/Firefox/mach` (entry point Python) +
`/Volumes/Firefox/moz.configure` (top-level) +
`/Volumes/Firefox/build/moz.configure/init.configure` (target
detection) sao os tres arquivos que controlam o build.

Para Strategy A, **NENHUMA modificacao** e necessaria nesses
arquivos. Configuramos o build com:
```
./mach configure \
  --target=x86_64-unknown-linux-musl \
  --with-libc=musl \
  --enable-application=browser \
  --enable-release \
  --disable-tests \
  --disable-debug
```

E o sysroot aponta para a build do CapyOS musl.

Para Strategy B (futuro), o trabalho seria estender:
- `python/mozbuild/mozbuild/configure/constants.py:49` (adicionar
  "CapyOS" em Kernel.POSSIBLE_VALUES)
- `build/moz.configure/init.configure:455` (split_triplet
  adicionar `elif os.startswith("capyos")`)
- `build/moz.configure/init.configure:870` (target_is_capyos)
- ~50 moz.builds que tem `if CONFIG["TARGET_KERNEL"] == ...`

### F.10 Cronograma revisado (Sessao 7)

| Fase | Estimativa Sessao 6 | Estimativa Sessao 7 | Justificativa |
|---|---|---|---|
| **F1 (POSIX musl)** | 4-6 meses | **3-5 meses** | musl ja existe; portar como sysroot e reusar Linux ABI direto |
| **F2 (VFS+filesystems)** | 3-4 meses | 3-4 meses | sem mudanca |
| **F3 (process model)** | 6-8 meses | **5-7 meses** | linker dinamico ja foi feito no musl-ldso, podemos reusar |
| **F4 (toolchain)** | 8-12 meses | **4-6 meses** | toolchain `x86_64-unknown-linux-musl` ja existe; so precisa apontar sysroot CapyOS |
| **F5 (graphics)** | 12-18 meses | **6-9 meses** | SWGL elimina Mesa; widget/headless reduz widget effort |
| **F6 (audio/video)** | 6-9 meses | **3-5 meses** | cubeb_null + cubeb_capyos minimal sao suficientes para v1 |
| **F7 (build/integracao)** | 3-6 meses | 3-6 meses | sem mudanca |
| **Total revisado** | **36-60 meses** | **27-42 meses** | Strategy A reduz 25-30% do esforco |

### F.11 Marcos intermediarios (Strategy A)

Definir marcos visiveis para acompanhar progresso:

1. **M1: SpiderMonkey shell roda em CapyOS.** Apenas
   `js/src/shell/js.cpp` linkado com capylibc-musl. Provido
   `pthread + mmap + clock_gettime + open/read/write`. Estimado:
   **F1 + 1 mes**.

2. **M2: Gecko core (xpcom + ipc) compila em modo standalone.**
   Sem widget, sem gfx, sem dom. Apenas xpcom service registry +
   threading. Estimado: **F1+F2+F3 + 2 meses**.

3. **M3: Firefox compila com `--target=x86_64-unknown-linux-musl
   --enable-application=browser`.** Pode nao linkar -- so o build
   passa. Estimado: **F4 + 1 mes**.

4. **M4: Firefox linka mas crash no startup.** Identifica os
   primeiros syscalls/symbols que faltam. Estimado: **M3 + 2 meses**.

5. **M5: about:blank renderiza** (sem rede, sem som, sem GPU).
   SWGL desenha o layout vazio. Estimado: **M4 + 4 meses**.

6. **M6: example.com carrega via HTTPS.** Stack rede + TLS +
   compositor + SWGL operacional. Estimado: **M5 + 3 meses**.

7. **M7: gmail.com login funciona.** Cookies, JS pesado, fontes
   web, renderiza UI complexa. Estimado: **M6 + 4 meses**.

8. **M8: youtube.com toca video.** Codec stack (libvpx/libdav1d/
   ffmpeg subset) + cubeb audio operacional. Estimado: **M7 + 6
   meses**.

## Cronograma sugerido

| Fase | Duracao realista | Pre-requisito |
|---|---|---|
| **D (erradicacao)** | 1 sessao (hoje) | -- |
| **F1 (POSIX musl)** | 4-6 meses | D |
| **F2 (VFS+filesystems)** | 3-4 meses | F1 |
| **F3 (process model)** | 6-8 meses | F1 |
| **F4 (toolchain)** | 8-12 meses | F1-F3 |
| **F5 (graphics stack)** | 12-18 meses | F4 |
| **F6 (audio/video)** | 6-9 meses | F4 |
| **F7 (build/integracao)** | 3-6 meses | F1-F6 |
| **Total** | **~36-60 meses** | |

## Referencias

- Mozilla source: <https://github.com/mozilla/gecko-dev>
- Firefox build docs: <https://firefox-source-docs.mozilla.org/setup/index.html>
- musl libc: <https://musl.libc.org/>
- Mesa swrast: <https://docs.mesa3d.org/drivers/swrast.html>
- Servo (alternativa Rust): <https://servo.org/>
- NetSurf (alternativa C99 leve): <https://www.netsurf-browser.org/>
