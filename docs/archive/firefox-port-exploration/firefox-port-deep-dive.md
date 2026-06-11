# Firefox port deep-dive: analise tecnica baseada no codigo fonte

**Data**: Sessao 7 (2026-05-05)
**Autor**: Cascade
**Workspace de referencia**: `/Volumes/Firefox` (clone de
mozilla-firefox/firefox upstream)
**Documento pai**: `docs/plans/active/firefox-port-roadmap.md`

Este documento mapeia, modulo a modulo, o que cada componente do
Firefox precisa do CapyOS (Strategy A: Linux ABI compatibility).
Cada secao cita arquivos reais do upstream com `path:linha`.

## Visao geral da arvore Firefox

A arvore Mozilla tem ~30M LOC organizados como:

```
/Volumes/Firefox/
├── mach                        # Build CLI (Python)
├── moz.configure              # Configure top-level
├── build/moz.configure/        # Target detection, toolchain probing
├── python/mozbuild/            # Build engine (Python)
├── mozglue/                    # Glue layer com a libc da plataforma
├── mfbt/                       # Mozilla Framework Based on Templates
├── xpcom/                      # Cross-Platform COM (servico registry)
├── ipc/                        # Inter-Process Communication
│   ├── chromium/              # Chromium IPC base (forked)
│   ├── glue/                  # Mozilla extensions (CrossProcessMutex etc.)
│   └── ipdl/                  # IDL para mensagens IPC
├── js/                         # SpiderMonkey JavaScript engine
├── dom/                        # DOM implementation
├── layout/                     # CSS layout engine
├── gfx/                        # Graphics
│   ├── 2d/                    # Cairo/Skia 2D backend
│   ├── webrender_bindings/    # WebRender (Rust) bindings
│   ├── wr/swgl/               # Software OpenGL (Rust+C++)
│   └── thebes/                # Legacy gfx layer
├── widget/                     # Platform widget abstraction
│   ├── headless/              # Headless backend (sem display) -- BASE
│   ├── gtk/                   # Linux GTK
│   ├── windows/               # Windows
│   ├── cocoa/                 # macOS
│   └── android/               # Android
├── netwerk/                    # Networking (Necko)
├── security/                   # NSS (Network Security Services)
├── media/                      # Audio/video
│   └── libcubeb/              # Audio abstraction
├── modules/                    # libpref, libjar, libtelemetry, etc.
├── toolkit/                    # XUL Toolkit (UI base)
├── browser/                    # Firefox UI specifically
├── third_party/                # Vendored deps (skia, harfbuzz, ffi, etc.)
└── servo/                      # Servo crates (Stylo: rust CSS engine)
```

Para o port CapyOS, **interessam-nos** principalmente:
`mozglue/`, `mfbt/`, `xpcom/`, `ipc/`, `js/`, `gfx/`, `widget/`,
`netwerk/`, `media/libcubeb/`, e o build system.

O Firefox UI (`browser/`, `toolkit/`) nao requer port -- compila
verbatim sobre o resto.

## 1. Build system: mach + moz.configure + moz.build

### 1.1 Entry point: `mach`

`/Volumes/Firefox/mach` e um shell wrapper que invoca
`mach.py`. Toda a configuracao do build acontece via Python
declarativo em `moz.configure` (top level) e
`build/moz.configure/*.configure` (modulos).

Para Strategy A, invocacao tipica seria:

```bash
./mach configure \
  --target=x86_64-unknown-linux-musl \
  --with-app=browser \
  --enable-release \
  --disable-tests \
  --disable-debug \
  --without-wasm-sandboxed-libraries
```

### 1.2 Detecao de target: `init.configure`

`/Volumes/Firefox/build/moz.configure/init.configure:797-825`
define `target_variables` que extrai `OS_TARGET`,
`OS_ARCH`, `INTEL_ARCHITECTURE`, etc. do triplet.

`/Volumes/Firefox/build/moz.configure/init.configure:455-548`
implementa `split_triplet()` que mapeia o triplet
(`x86_64-unknown-linux-musl`) para `(cpu, kernel, os)`. O case
relevante para Strategy A:

```python
elif os.startswith("linux"):
    canonical_os = "GNU"
    canonical_kernel = "Linux"
```

ou seja, `target.kernel == "Linux"` e `target.os == "GNU"` --
dispara TODOS os branches `if CONFIG["TARGET_KERNEL"] == "Linux":`
nas moz.builds.

### 1.3 Constantes: `python/mozbuild/mozbuild/configure/constants.py`

Define os enums `Kernel.POSSIBLE_VALUES` (linha 49) e
`OS.POSSIBLE_VALUES` (linha 33) que limitam quais plataformas
o moz.configure aceita. Para Strategy B (futuro), adicionar
`"CapyOS"` aqui.

`kernel_preprocessor_checks` (linha 139) define quais macros C
o build define no compilador para cada kernel. Para Linux:
`__linux__`. Para CapyOS Strategy B: definir `__CapyOS__`.

## 2. mozglue/: glue com a libc da plataforma

`/Volumes/Firefox/mozglue/` e a primeira camada que toca a libc.
E o ponto de port mais critico.

### 2.1 mozglue/misc/: primitives de threading e tempo

| Arquivo | API exposta | Backend POSIX |
|---|---|---|
| `Mutex.h` | `mozilla::detail::MutexImpl` | pthread_mutex_t |
| `Mutex_posix.cpp` | impl POSIX | `pthread_mutex_init`/`lock`/`unlock`/`destroy` |
| `Mutex_windows.cpp` | impl Windows | CRITICAL_SECTION |
| `Mutex_noop.cpp` | impl no-op | nada (single-threaded) |
| `ConditionVariable_posix.cpp` | pthread_cond_t | `pthread_cond_*` |
| `RWLock_posix.cpp` | pthread_rwlock_t | `pthread_rwlock_*` |
| `TimeStamp_posix.cpp` | `TimeStamp::Now()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `TimeStamp_darwin.cpp` | idem | `mach_absolute_time` |
| `TimeStamp_windows.cpp` | idem | `QueryPerformanceCounter` |
| `Now.cpp` | `Now()` helper | varia por plataforma |
| `StackWalk.cpp` | unwinding (crash report) | varia |
| `Printf.cpp` | safe sprintf | sem deps |

**Strategy A:** F1 deve fornecer
`pthread_mutex_*`/`pthread_cond_*`/`pthread_rwlock_*`/`clock_gettime`.
Os arquivos `_posix.cpp` compilam verbatim.

**Strategy B:** criariamos `Mutex_capyos.cpp`,
`ConditionVariable_capyos.cpp` etc., apontando para primitives
nativas (mais eficientes mas mais trabalho de manutencao).

### 2.2 mozglue/build/: linker glue + symbol versioning

`mozglue/build/moz.build` define se mozglue e
`SharedLibrary("mozglue")` ou `Library("mozglue")` baseado em
`OS_TARGET`. Para Linux/musl, o mozglue normalmente e
estatico (Library). CapyOS pode usar o mesmo padrao.

`BionicGlue.cpp` (Android-only) provida workarounds para
bionic libc. CapyOS nao precisa.

### 2.3 mozglue/linker/: dynamic linker custom (Android)

Apenas Android usa este linker custom (porque o linker do bionic
nao expoe APIs que o Firefox precisa). CapyOS pode usar o ld.so
do musl diretamente.

## 3. mfbt/: Mozilla Framework Based on Templates

`/Volumes/Firefox/mfbt/` e portavel quase 100% (header-only
+ alguns .cpp de utilities). Depende apenas de:
- `<stdint.h>`, `<stddef.h>`, `<string.h>`, `<stdlib.h>`,
  `<assert.h>`
- `<atomic>`, `<chrono>`, `<thread>` (libstdc++ ou libc++)

**Conclusao:** mfbt compila verbatim assim que F1 + libstdc++
estiverem operacionais.

## 4. xpcom/: Cross-Platform COM

`xpcom/` e o sistema de servicos do Firefox (registro de
componentes, IDL bindings, factory pattern, refcount).

### 4.1 xpcom/io/: file/stream APIs

| Arquivo | API exposta | Backend |
|---|---|---|
| `nsLocalFileUnix.cpp` | `nsIFile` | open/read/write/stat/dirent POSIX |
| `nsLocalFileWin.cpp` | idem | CreateFile etc. |
| `nsLocalFileCommon.cpp` | parts comuns | --|
| `SpecialSystemDirectory.cpp` | `~/.mozilla` etc. | varia |
| `nsAnonymousTemporaryFile.cpp` | tmp file | mkstemp etc. |
| `nsPipe3.cpp` | `nsIPipe` (in-memory) | sem deps SO |
| `crc32c.c` | CRC32C (hardware-accel) | x86 SSE 4.2 ou software |

**Strategy A:** `nsLocalFileUnix.cpp` (76KB) compila verbatim
sob musl quando F1.fs + F1.dirent + F1.stat estiverem prontos.
`SpecialSystemDirectory` precisa de pequena adaptacao para
mapear "~/.mozilla" no FS do CapyOS.

### 4.2 xpcom/threads/: threading abstrations

`xpcom/threads/nsThread.cpp` e o nucleo. Usa
`mfbt::dom::Mutex` e `mfbt::dom::CondVar` que sao definidas em
mozglue. Compila com Strategy A sem mudanca.

`xpcom/threads/MainThreadIdlePeriod.cpp` -- idle scheduler.

### 4.3 xpcom/base/: smart pointers, refcount, weak ptr

Quase 100% header-only. Compila verbatim.

## 5. ipc/: inter-process communication

### 5.1 ipc/chromium/: Chromium IPC base (forked)

Veja `/Volumes/Firefox/ipc/chromium/moz.build:8-83` para a lista
completa de fontes. Os arquivos que entram no build sob Linux
(Strategy A):
- `src/base/condition_variable_posix.cc`
- `src/base/lock_impl_posix.cc`
- `src/base/platform_thread_posix.cc`
- `src/base/process_util_posix.cc`
- `src/base/thread_local_posix.cc`
- `src/base/waitable_event_posix.cc`
- `src/base/string16.cc`
- `src/chrome/common/ipc_channel_posix.cc`
- `src/base/process_util_linux.cc`
- `src/base/set_process_title_linux.cc`
- `src/base/time_posix.cc`
- `src/base/message_pump_libevent.cc`
- `src/third_party/libevent/*` (se nao usar systemwide libevent)

Estes arquivos compilam quando o CapyOS expoe:
- pthreads (subset listado em F.6 do roadmap)
- AF_UNIX socketpair com SCM_RIGHTS para passagem de FDs
- `epoll_create1`/`epoll_ctl`/`epoll_wait` (libevent backend)
- `getpid`/`fork`/`execve`/`waitpid`
- `/proc/self/maps` para enumerar regions de memoria
  (`process_util_linux.cc` parseia)

### 5.2 ipc/glue/: Mozilla-specific

| Arquivo | Funcao | Strategy A |
|---|---|---|
| `MessageChannel.cpp` (85KB) | dispatch core | sem deps SO |
| `MessageLink.cpp` | wire format | sem deps SO |
| `MessagePump.cpp` | event loop | usa libevent (POSIX) |
| `NodeController.cpp` (32KB) | Mojo Ports | sem deps SO |
| `SharedMemoryPlatform_posix.cpp` (15KB) | shm_open + mmap | F1.shm + F1.mmap |
| `SharedMemoryPlatform_mach.cpp` | macOS | --|
| `SharedMemoryPlatform_windows.cpp` | Windows | --|
| `CrossProcessMutex_posix.cpp` | pthread mutex em shm | F1.shm |
| `CrossProcessMutex_unimplemented.cpp` | stub | usar enquanto shm nao existe |
| `ProcessUtils_linux.cpp` | misc Linux | --|
| `ProcessUtils_none.cpp` | stub | usar enquanto process API nao tem |
| `ForkServer.cpp` | fork-server (Linux) | precisa de fork+execve |
| `ForkServiceChild.cpp` | client do fork-server | --|
| `GeckoChildProcessHost.cpp` (69KB) | spawn de processos filhos | fork+execve |
| `FileDescriptorShuffle.cpp` | dup2/close para FD setup | F1.dup2 |

**Marco intermediario:** comecar com
`CrossProcessMutex_unimplemented.cpp` e
`ProcessUtils_none.cpp`. Firefox roda single-process (ele tem
modo `MOZ_FORCE_DISABLE_E10S=1`) e essas primitives nao sao
chamadas. Implementacoes reais entram quando habilitamos
multi-process.

### 5.3 ipc/ipdl/: IDL compiler

`/Volumes/Firefox/ipc/ipdl/` contem um compilador (Python) que
le `.ipdl`/`.ipdlh` files e gera bindings C++. Roda no host
(Linux/macOS de quem builda), nao precisa rodar no CapyOS.

## 6. js/: SpiderMonkey

### 6.1 js/src/threading/: thread primitives

Este modulo e independente de mozglue e tem seu proprio set
de implementacoes. Backends:
- `posix/PosixThread.cpp` (4KB) -- pthread_create/join
- `posix/CpuCount.cpp` -- sysconf(_SC_NPROCESSORS_ONLN)
- `posix/ThreadPlatformData.h` -- pthread_t opaque
- `windows/WindowsThread.cpp`
- `noop/NoopThread.cpp` -- WASI, single-threaded

Strategy A: posix branch compila verbatim.

### 6.2 js/src/jit/: JIT (BaselineJIT, Ion, CacheIR)

A JIT precisa de:
- `mmap(addr, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0)`
- `mprotect(addr, size, PROT_READ|PROT_EXEC)` para selar
- `__builtin___clear_cache(start, end)` (sem op em x86, no-op
  em ARM)

CapyOS deve permitir `PROT_EXEC` em paginas anonimas para
SpiderMonkey rodar. Sem JIT (`--disable-jit`), SpiderMonkey
roda em modo interpretado (10x mais lento mas funciona).

### 6.3 js/src/gc/: Garbage Collector

Generational, incremental, multi-arena. Usa:
- `mmap` para heap regions
- `madvise(MADV_DONTNEED)` para devolver paginas
- pthread mutexes para arena locks
- `pthread_setname_np` para threads do GC

### 6.4 js/src/util/: utilidades portaveis

`Utility.cpp`/`UtilityBackend*.cpp` definem allocators e
abstractions. Quase 100% portavel sob libc-musl.

### 6.5 js/src/wasm/: WebAssembly

Cranelift (Rust) + Baseline JIT (C++). Precisa de SIMD intrinsics
da plataforma; SpiderMonkey tem fallback escalar.

## 7. gfx/: graphics stack

### 7.1 gfx/wr/swgl/: Software OpenGL (CRITICO)

`/Volumes/Firefox/gfx/wr/swgl/README.md` descreve um software
rasterizer Rust+C++ single-threaded. Substitui Mesa swrast.

```
Software OpenGL implementation for WebRender
This is a relatively simple single threaded software rasterizer
designed for use by WebRender. It will shade one quad at a time
using a 4xf32 vector with one vertex per lane.
```

Build deps: clang ou clang-cl + Rust.

**Implicacao para CapyOS:** WebRender + SWGL sao a stack grafica
default no port. Nao precisa de driver GPU.

### 7.2 gfx/wr/: WebRender (Rust)

`gfx/wr/webrender/` e o WebRender principal. Compila para
Rust crate `webrender`. Compila com `rustc
--target=x86_64-unknown-linux-musl` se sysroot estiver
configurado.

`gfx/webrender_bindings/` faz a ponte C++ <-> Rust.

### 7.3 gfx/2d/: backend 2D para rasterizacao

`gfx/2d/` define `mozilla::gfx::DrawTarget` que e a abstracao
2D. Backends:
- Skia (default) -- precisa de fonts + harfbuzz
- Cairo (legacy)
- D2D (Windows)

Para CapyOS comecar com Skia (vem como third_party verbatim).

### 7.4 gfx/skia/, harfbuzz/, ots/, qcms/

Todos third_party vendored. Compilam com Strategy A.
- `gfx/skia/` -- 2D rendering
- `gfx/harfbuzz/` -- text shaping (precisa de FreeType)
- `gfx/ots/` -- font sanitization
- `gfx/qcms/` -- color management

## 8. widget/: platform abstraction

### 8.1 widget/headless/: backend ideal para CapyOS

`/Volumes/Firefox/widget/headless/` (~5 arquivos, ~25KB total)
implementa o minimo:
- `HeadlessWidget.cpp` -- janela sem display real
- `HeadlessCompositorWidget.cpp` -- compositor target
- `HeadlessClipboard.cpp` -- clipboard em memoria
- `HeadlessSound.cpp` -- audio mute (no-op)
- `HeadlessScreenHelper.cpp` -- screen 1920x1080 default

**Estrategia:** copiar essa estrutura para `widget/capyos/` e
substituir HeadlessCompositorWidget para escrever no
framebuffer do CapyOS via syscall.

### 8.2 widget/gtk/: backend Linux completo (referencia, NAO copiar)

`widget/gtk/` tem ~1MB de codigo (GTK3+Wayland+X11). Util como
referencia mas nao queremos depender de GTK no CapyOS.

`nsWindow.cpp` (266KB) trata X11+Wayland+InputMethod+
WindowManager hints. CapyOS comeca sem nada disso.

### 8.3 widget/headless/HeadlessCompositorWidget.cpp

Le este arquivo:

```cpp
class HeadlessCompositorWidget : public CompositorWidget,
                                  public CompositorWidgetDelegate {
 public:
  // ...
  void* GetNativeData(uint32_t aDataType) override { return nullptr; }
  uintptr_t GetWidgetKey() override { return (uintptr_t)mWidget; }
  // ...
};
```

A conexao com o framebuffer do CapyOS acontece via custom
override de `EndRemoteDrawingInRegion` que emite uma syscall
custom `SYS_FB_FLIP` (a definir).

## 9. netwerk/: networking (Necko)

Stack completa: HTTP/1.1, HTTP/2, HTTP/3, WebSocket, FTP (legacy),
DNS, cookies, cache.

### 9.1 netwerk/base/: backbone

Usa nsISocketTransport sobre TCP. Backends:
- `netwerk/socket/nsSocketProviderService.cpp`
- `netwerk/sctp/` (WebRTC)

Strategy A: requer F2.socket (full POSIX socket API).

### 9.2 security/nss/: TLS

NSS e a biblioteca de crypto/TLS da Mozilla. ~500KB. Vendored
em `security/nss/`. Compila com Strategy A se C++ runtime
estiver completo.

`security/nss/lib/freebl/` faz crypto. Tem assembly otimizada
para x86_64 (SSE/AVX).

### 9.3 netwerk/dns/

Resolver DNS. Usa `getaddrinfo` da libc se disponivel, ou
falsa direto via UDP/TCP. Strategy A: depende de F1.netdb.

## 10. media/: audio/video

### 10.1 media/libcubeb/

Audio abstraction. Veja `cubeb_*.{c,cpp}` por backend.

CapyOS comeca com:
- Implementacao stub `media/libcubeb/src/cubeb_capyos.c`
  (baseado em `cubeb_oss.c`).
- Ou fallback `media/libcubeb/src/cubeb_null.c` (silencio).

### 10.2 dom/media/, media/libpng, libjpeg, libdav1d

Video/image decoders. Vendored. Compilam com Strategy A.
`libdav1d` (AV1 decoder) tem assembly otimizada x86_64.

## 11. servo/ + intl/: Stylo + ICU

### 11.1 servo/components/style/

Stylo (CSS engine em Rust, do projeto Servo). Compila com
toolchain Rust + sysroot musl. ~200K LOC Rust.

### 11.2 intl/icu/

International Components for Unicode. ~1MB. Compila com
Strategy A se libstdc++ + libc estiverem operacionais.

## 12. third_party/: vendored deps

Quase 100% portavel:
- `third_party/libwebp/` -- WebP decoder
- `third_party/lss/` -- Linux Syscall Support (assembly direta)
- `third_party/sqlite/` -- SQLite database
- `third_party/zlib/` -- zlib
- `third_party/python/*` -- build helpers (rodam no host)

`third_party/lss/` e curioso: usa syscall direta no Linux
(sem libc). Para Strategy A funciona se CapyOS expuser a
mesma syscall numbering do Linux. Caso contrario, modificar
ele para usar a libc do CapyOS.

## Resumo de dependencias por modulo

| Modulo Firefox | Strategy A pre-req | Esforco port |
|---|---|---|
| mozglue/misc | F1.pthread + F1.time | nenhum |
| mfbt | F1.libc + libstdc++ | nenhum |
| xpcom/io | F1.fs + F2.vfs | nenhum |
| xpcom/threads | F1.pthread | nenhum |
| ipc/chromium | F1.pthread + F1.unix-socket + F1.epoll + F2.proc | nenhum |
| ipc/glue | F1.shm + F1.mmap (parcial: stubs aceitos) | baixo |
| js/src/threading | F1.pthread + F1.cpucount | nenhum |
| js/src/jit | F1.mmap PROT_EXEC | nenhum |
| js/src/gc | F1.mmap + F1.madvise | nenhum |
| gfx/wr/swgl | rustc + clang | nenhum |
| gfx/wr (WebRender) | rustc + sysroot musl | nenhum |
| gfx/2d (Skia) | F1.libc completo | nenhum |
| widget/capyos (NEW) | compositor CapyOS + input pipe | **alto** -- 3-4 meses |
| netwerk | F1.socket + F1.netdb + NSS | nenhum |
| security/nss | F1.libc completo | nenhum |
| media/libcubeb | F6.audio CapyOS driver | medio -- backend novo |
| servo/components/style | rustc | nenhum |
| browser/, toolkit/ | tudo acima | nenhum |

## Apendice A: lista de syscalls Linux que Firefox usa

Esta lista e derivada de strace de uma execucao tipica do
Firefox (about:blank renderizado em Wayland) e da analise
estatica do codigo.

### Memoria
- `mmap` / `mmap2` -- alocacao de heap, JIT, shared mem
- `munmap`, `mprotect`, `madvise`, `mremap`
- `brk` (legacy malloc), `sbrk`

### Tempo
- `clock_gettime(CLOCK_MONOTONIC)` -- TimeStamp
- `clock_gettime(CLOCK_REALTIME)` -- wall clock
- `clock_nanosleep`, `nanosleep`
- `gettimeofday` (legacy)

### Threading
- `clone(CLONE_THREAD|CLONE_VM|...)` -- pthread_create
- `futex(FUTEX_WAIT|FUTEX_WAKE)` -- mutex/cond underneath
- `set_robust_list`, `get_robust_list`
- `set_tid_address`
- `sched_yield`, `sched_getaffinity`, `sched_setaffinity`
- `prctl(PR_SET_NAME)`

### File I/O
- `open`, `openat`, `creat`, `close`
- `read`, `write`, `pread64`, `pwrite64`, `readv`, `writev`
- `lseek`, `_llseek`
- `stat`, `lstat`, `fstat`, `statx`, `newfstatat`
- `access`, `faccessat`
- `mkdir`, `mkdirat`, `rmdir`, `unlink`, `unlinkat`, `rename`,
  `renameat`
- `chmod`, `fchmod`, `fchmodat`, `chown`, `fchown`, `lchown`
- `dup`, `dup2`, `dup3`
- `pipe`, `pipe2`
- `fcntl(F_GETFL|F_SETFL|F_DUPFD|F_GETFD|F_SETFD|F_SETLK|F_GETLK)`
- `ioctl` (varios subcomandos: TIOCGWINSZ, FIONREAD, etc.)

### Process
- `fork`, `vfork`, `execve`, `execveat`, `exit_group`
- `waitpid`, `wait4`, `waitid`
- `kill`, `tgkill`
- `getpid`, `getppid`, `gettid`, `getuid`, `geteuid`,
  `getgid`, `getegid`, `setuid`, `setgid`
- `prlimit64`, `getrlimit`, `setrlimit`
- `getcwd`, `chdir`, `fchdir`

### Networking
- `socket`, `socketpair`, `bind`, `listen`, `accept`,
  `accept4`, `connect`
- `send`, `sendto`, `sendmsg`, `recv`, `recvfrom`, `recvmsg`
- `shutdown`
- `setsockopt`, `getsockopt`, `getsockname`, `getpeername`

### IPC
- `epoll_create1`, `epoll_ctl`, `epoll_wait`, `epoll_pwait`
- `eventfd2`, `signalfd4`, `timerfd_create`,
  `timerfd_settime`
- `inotify_init1`, `inotify_add_watch`, `inotify_rm_watch`
- `pidfd_open` (newer kernels)
- `memfd_create`
- `shm_open` (POSIX, atraves da libc)
- `msgget`/`semget` (legacy SysV, raro)

### Signals
- `rt_sigaction`, `rt_sigprocmask`, `rt_sigreturn`
- `rt_sigsuspend`, `rt_sigtimedwait`
- `tkill`, `tgkill`, `kill`
- `sigaltstack`

### Random
- `getrandom`, `getentropy`
- `/dev/urandom`, `/dev/random` (read)

### Filesystem watching
- `inotify_*` (Firefox watcha prefs)

Esta lista e ~80 syscalls. Para Strategy A, **F1 + F2 + F3
devem implementar este set**. musl ja faz wrapping de tudo
isso, entao se musl roda, Firefox roda.

## Apendice B: dependencias de C++ runtime

Firefox usa C++17/20 com:
- libstdc++ (ou libc++) -- containers, smart ptr, atomic,
  thread, chrono, optional, variant, string_view
- exception handling (-fexceptions)
- RTTI (-frtti)
- TLS (`thread_local`)
- C++ atomics
- C++ coroutines (Firefox 130+)

GCC/clang com sysroot musl ja providam tudo isso. Verificar
que o build usa `-std=c++17` ou `-std=c++20` quando aplicavel.

## Apendice C: patches Mozilla esperados (Strategy A)

Mesmo Strategy A precisa de pequenos patches:

1. **Detector capabilities runtime.** Firefox em alguns lugares
   testa `__GLIBC__`. Substituir por checks de feature.

2. **Sandbox.** `security/sandbox/linux/` usa namespaces +
   seccomp-bpf. Sem support nativo, desabilitar via
   `--disable-sandbox` (perda de seguranca em troca de
   funcionalidade).

3. **`/proc/self/maps` parser.** Em
   `ipc/chromium/src/base/process_util_linux.cc` se assume
   formato Linux. Se CapyOS expuser /proc compativel, OK.

4. **`prctl(PR_SET_DUMPABLE)`, `PR_SET_PDEATHSIG`.** Usados em
   Linux para crash report e parent-death signal. CapyOS pode
   stub.

Total estimado: **20-50 patches pequenos** (vs 1000+ para
Strategy B).

## Apendice D: ferramentas para investigar Firefox

- `/Volumes/Firefox/AGENTS.md` -- guia oficial para agentes IA
  (incluindo Cascade) trabalharem no Firefox.
- `searchfox.org` -- search engine semantica do Firefox
  (usa a CLI `searchfox-cli` mencionada em AGENTS.md).
- `./mach` -- toda interacao com o build/test/run.
- `treeherder.mozilla.org` -- CI dashboard.

## Mudancas de plano vs sessao 6

| Topico | Sessao 6 | Sessao 7 (apos clonar Firefox) |
|---|---|---|
| Estrategia de port | Strategy B (CapyOS native) | Strategy A (Linux ABI) |
| Backend grafico | Mesa swrast (~5M LOC port) | SWGL (vem com Firefox) |
| Backend widget | Re-fork GTK | Estender HeadlessWidget |
| musl libc | Portar from-scratch | Reusar Linux ABI |
| Cronograma F1-F7 | 36-60 meses | 27-42 meses |
| Numero de patches | 100s a 1000s | 20-50 |
| Manutencao continua | Cara | Barata (acompanha nightly) |

A diferenca de 25-30% no cronograma decorre de:
- SWGL elimina F5.gpu (-6 meses)
- HeadlessWidget reduz F5.widget (-8 meses)
- musl reuse reduz F1+F4 combinados (-4 meses)
- cubeb_null/cubeb_oss reduz F6 (-3 meses)

## Proximos passos

1. **Finalizar erradicacao do capybrowser** (Parte D do roadmap,
   sessao 6) -- ja completa em commits anteriores.
2. **Iniciar F1.pthread** -- portar pthread minimal para
   `userland/lib/capylibc/`. Plano detalhado em
   `docs/plans/active/firefox-port-platform-shim.md`.
3. **Iniciar F1.time** -- `clock_gettime` em
   `userland/lib/capylibc/`.
4. **Marco M1**: SpiderMonkey shell roda em CapyOS.
