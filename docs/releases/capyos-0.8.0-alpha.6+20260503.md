# CapyOS 0.8.0-alpha.6+20260503

**Data:** 2026-05-03
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha a **Etapa 2 (Estabilidade pré-JS)** do roteiro de
maturação do browser ring-3, descrita em `docs/plans/historical/f3-3f-browser-desktop-wiring.md`
(arquivado em 2026-05-03 após consolidação em
`docs/plans/historical/f3-browser-delivered.md`).
O navegador agora abre com janela legível (480×384 px), tem throughput
de IPC suficiente para frames reais (pipe 64 KiB), pode ser encerrado
deterministicamente pelo Task Manager, e expõe os logs do engine ring-3
diretamente ao debugcon do operador.

Esta release também unifica a nomenclatura de planos: todos os docs
ativos passam a usar **Etapa N** + **Seção a/b/c…** como vocabulário
único, eliminando a mistura prévia de `Slice 5d`, `Fase F3.3f`,
`M4.1`, `Sessão 3`, etc. Os identificadores históricos permanecem
referidos por nome curto entre crases (ex.: `M5`, `F3`) apenas como
*tag arqueológico* — sem novas tarefas usando essa forma.

## Etapas entregues nesta release

### Etapa 2 — Estabilidade pré-JS do browser

| Seção | Item | Antes | Depois |
|---|---|---|---|
| **a** | Pipe kernel | `PIPE_BUF_SIZE = 4 KiB` (~170 round-trips por frame 480×360) | `PIPE_BUF_SIZE = 64 KiB` (~11 round-trips por frame; **16×** menos latência) |
| **b** | Janela do browser | 192×148 px (ilegível) | **480×384 px** (frame 480×360 + URL bar 24 px) |
| **c** | Engine framebuffer | 192×128 BGRA = 96 KiB | **480×360 BGRA = 675 KiB** |
| **d** | Chrome scratch buffer | 128 KiB | **768 KiB** |
| **e** | Task Manager Kill | Não fechava a janela: `process_kill` só liberava FDs em `process_destroy`, chamado apenas para órfãos; engine do browser tem pai → ZOMBIE com pipes abertos → chrome polling sem fim | `process_kill` agora libera **todos os FDs imediatamente** mesmo para processos com pai; `process_fd_free` fecha pipe ends / VFS files (mirror de `sys_close`); janela fecha em ~1 frame |
| **f** | Detecção de morte externa do engine | Browser_app não percebia kill externo; janela ficava "viva" sem responder | Novo `engine_is_dead()` no início de `browser_app_tick`; checa `process_stats_get(engine_pid)`; fecha janela imediatamente; marker debugcon `[D]` |
| **g** | Visibilidade de logs do engine | `EVENT_LOG` chegava ao chrome mas era apenas armazenado; sem caminho para o operador | Browser_app encaminha `BROWSER_CHROME_ACTION_LOG_FORWARD` ao **debugcon** via `outb 0xE9` no formato `<msg>\n`; agora qualquer crash ou estado anômalo do engine ring-3 fica visível no QEMU `-debugcon stdio` |
| **h** | **`sys_read`/`sys_write` ignoravam pipes em fd 0/1/2** (bug crítico): browser engine instala pipes em fd 0 (request, READ) e fd 1 (response, WRITE), mas o syscall verificava `fd == 0 → stdin_buf` / `fd == 1\|2 → debugcon` ANTES da tabela de fd do processo. Resultado: engine bloqueava em `capy_read(0)` lendo do teclado vazio e `capy_write(1)` ia para debugcon — **chrome nunca via NAVIGATE/FRAME/qualquer evento** | Reordenado em `src/kernel/syscall.c:34-99,102-164`: tabela de fd do processo agora wins, com fallback legacy stdin_buf/debugcon só quando `slot->type == FD_TYPE_FREE`. Cobertura: `tests/test_syscall_pipe_priority.c` (8 asserts: pipe-wins-em-0/1/2 + fallback-stdin/debugcon). Sem este fix a página inicial nunca carregava e Enter na URL bar não disparava nada |
| **i** | Janelas do sistema não eram redimensionáveis | Apenas o canto inferior-direito ativava resize; bordas e edges declaradas no enum (`WM_DRAG_RESIZE_RIGHT/BOTTOM`) nunca implementadas. Apps sem `on_resize` callback → buffer realocado fica em branco até próximo evento de paint | `wm_handle_mouse_down` agora hit-testa 3 zonas (`right`, `bottom`, `corner`) com grip de 6 px; `wm_handle_mouse_move` calcula tamanho absoluto contra drag-start (sem drift); todas as 7 janelas (browser, terminal, task manager, file manager, calculator, text editor, settings) ganharam `on_resize` que repinta o conteúdo no novo buffer; floor centralizado em `WM_MIN_WINDOW_W/H = 120×80 px` |
| **j** | **`process_current()` retornava sempre NULL** (ROOT CAUSE crítico do "browser não carrega"): `current_proc` era setado APENAS em `process_system_init` (NULL) e em `process_set_current`, e este último JAMAIS era chamado em código de produção. O scheduler atualizava `task_current` mas nunca o `current_proc`. Toda syscall que dependia de `process_current()` (`sys_read`/`write`/`open`/`close`/`fork`/`exec`/`pipe`/`brk`, fault classifier, vmm AS lookup) silenciosamente via NULL e ou retornava -1 ou caía em fallbacks legacy. Sintoma observado: engine do browser instala pipe em fd 0 mas `capy_read(0)` caía em `stdin_buf` (sempre vazio) e o NAVIGATE nunca era processado; `capy_write(1)` ia pra debugcon. **Smokes existentes (fork/exec/pipe) "passavam" por coincidência** — markers eram emitidos via os próprios fallbacks legacy | `process_current()` agora resolve **dinamicamente** via `task_current()`: percorre `proc_table` procurando o slot cujo `main_thread` é a task corrente (O(128), mas só syscall slow path). `current_proc` mantém-se como override TEST-ONLY. `process_create.ppid` e `process_exit.exit_code/state` migrados para `process_current()` (antes liam `current_proc` direto e por isso ppid era sempre 0 e `process_exit` halt-loopava). Cobertura: `tests/test_process_current_dynamic.c` (6 asserts cobrindo dynamic resolve + UNUSED skip + multi-process aliasing + null-task + override). Sem este fix, **nenhum** dos outros sub-fixes (h, i, k) se manifestava — engine continuaria isolado do chrome |
| **k** | Conteúdo do browser **não se adaptava** ao tamanho da janela | Engine ring 3 rasterizava sempre em 480×360 fixo (`ENGINE_FB_W/H` cabledados); janela maior → frame ficava encalhado no canto superior-esquerdo com `bg_color` preenchendo o resto. `BROWSER_IPC_RESIZE` estava no enum mas o engine ignorava silenciosamente | **Protocolo RESIZE end-to-end**: (1) `BROWSER_FRAME_MAX_W/H` bumpado para **1024×768** (de 480×360); (2) engine ring 3 ganhou globais `g_vw/g_vh` que default = `BROWSER_FRAME_DEFAULT_*` (480×360) e atualizam no handler de `BROWSER_IPC_RESIZE` (clamp `[1..MAX]`); (3) `emit_real_frame` rasteriza no `g_vw/g_vh` corrente e envia só `12 + vw*vh*4` bytes (não o MAX inteiro); (4) `chrome_runtime_send_resize(rt, w, h)` exposto no header; (5) `browser_app_on_resize` envia RESIZE seguido de NAVIGATE re-issue para o engine emitir um frame fresco no novo tamanho; (6) `paint_urlbar` agora posiciona-se em `surface.height - URLBAR_H` (não no fixo 360 antigo); (7) blit clipa pelo `surface.height - URLBAR_H` corrente. Custo: chrome scratch sobe de 768 KiB para 4 MiB (cabe 1024×768 BGRA + header). Cobertura: `test_browser_chrome_runtime` ganhou 3 testes para `send_resize` (encoding BE, zero-dim refusado, engine-dead refusado) |

## Etapas anteriores consolidadas (referência)

- **Etapa 0** (alpha.5 e anteriores): boot UEFI, kernel x86_64, M4 processos
  + scheduler preemptivo + CoW + TSS, M5 fork/exec/wait/pipe + capysh
  ring 3, M6 política de senha + auditoria + privilégio + journal CAPYFS,
  M7 WAL + recovery + update transacional, M8 browser kernel-side com
  budget + cache + DNS, W1/W2/W3 polish UX.
- **Etapa 1** (alpha.5+20260501 + sessões 1-4 do f3-3f):
  Seção a → protocolo IPC binário; Seção b → engine ring-3 stub; Seção c →
  chrome scaffolding (watchdog + dispatcher + runtime); Seção d → smoke
  spawn QEMU; Seção e → migração html_viewer para userland; Seção f →
  desktop wiring (menu Browser → spawn ring-3 + janela compositor);
  Seção g → fix #GP no ELF load (page-by-page copy); Seção h → fix yield
  infinito em `read_full` (cap 4096 yields).

## Etapa 3 — Roteiro pós-Etapa 2 (não entregue ainda)

Pré-requisito declarado para qualquer tentativa de adicionar JS:

| Seção | Item | Bloqueador |
|---|---|---|
| **a** | Image rendering inline (PNG/JPEG) | Decoders já existem em `src/gui/core/`; precisa wire no engine raster |
| **b** | Hyperlink click → navigate | Compositor precisa propagar coords de mouse à browser_app |
| **c** | Form inputs (text fields) | Engine precisa receber CLICK + KEY routing direcionado a regiões |
| **d** | Tabelas + listas + box model CSS | Layout do `libcapyhtml` precisa expandir além de h1+p+ul |
| **e** | Scrolling vertical | Engine precisa enviar viewport offset; chrome precisa propagar wheel |
| **f** | Fonte real (não 8×8) | Glyph atlas + bitmap fonts no `libcapyhtml` |

Nenhum item da Etapa 3 deve começar antes que **todos os fixes da Etapa 2
sejam validados em QEMU pelo operador**.

## Compatibilidade

- Configs `config.ini` antigos com `theme=rosa` ou `theme=pink` continuam
  funcionando (aliases legados → canonical `love` introduzido na alpha.5).
- Pipes criados antes do bump (4 KiB) não existem persistidos em disco;
  todos os pipes são in-memory e recriados no boot.
- Binários ELF userland antigos (com BSS pequena, framebuffer 192×128)
  continuam funcionando — o blit do browser_app respeita
  `last_frame.width/height` que é dinâmico.

## Regressões potenciais

- **`.bss` do kernel cresceu ~2 MiB** por causa do bump de `PIPE_BUF_SIZE`
  (32 pipes × 64 KiB). Validado que cabe no espaço de imagem do kernel
  na ISO atual.
- **`g_app` no `browser_app.c`** cresceu ~640 KiB por causa do scratch
  bumpado para 768 KiB (componente de `chrome_runtime`). Total `.bss`
  do kernel agora maior; verificar se há aviso no link no próximo
  build cross-toolchain.

## Validação

### Hardening pós-Etapa 2 (2026-05-02 PM)

Após o pacote inicial da Etapa 2, fizemos uma rodada de regressão para
auditar se cada fix segue as melhores práticas de um sistema robusto
em vez de patch mínimo. Resultado:

| Item | Antes | Depois |
|---|---|---|
| **Constantes FD** | `FD_TYPE_PIPE/FILE` e `FD_PIPE_FLAG_*` duplicadas em 3 TUs (`process.c`, `syscall.c`, `browser_engine_spawn.c`) — risco de drift | Centralizadas em `src/kernel/process.h`; demais TUs incluem o header |
| **Tests de FD release** | Sem cobertura host para o invariante "kill libera todos os FDs imediatamente" | 4 novos tests em `tests/test_process_destroy.c` cobrindo pipe r/w, dupla-kill, idempotência |
| **`compositor.c`** | Excedia o limite de 900 linhas do layout-audit (910 linhas) — bloqueio do `--strict` | Split em 3 TUs: `compositor.c` (CRUD/lifecycle, 444 lines), `compositor_theme.c` (paleta, 148), `compositor_render.c` (render path, 331); estado privado em `internal/compositor_internal.h` |
| **`last_frame.pixels`** | Aliased dentro de `event_scratch` da chrome runtime; próximo poll sobrescrevia silenciosamente; UB se browser_app demorasse para blittar | Storage dedicado `last_frame_storage` (768 KiB) na `chrome_runtime`; copy pós-dispatch + telemetria `total_frames_persisted/dropped`; 2 novos regression tests no `test_browser_chrome_runtime` |
| **Log forward** | 3 inline `__asm__ outb` duplicados; sem cap defensivo no length | `debugcon_putc` helper único; `browser_app_log_forward()` com cap em `BROWSER_CHROME_LOG_MSG_MAX (192)` + sufixo `[T]` em truncamento |
| **Dimensões do browser** | `BROWSER_APP_FRAME_W=480` em `browser_app.c` e `ENGINE_FB_W=480` em `userland/bin/capybrowser/main.c` — risco de drift silencioso | Header canônico `include/apps/browser_dimensions.h`; ambos os lados consomem `BROWSER_FRAME_W/H/STRIDE/...` |
| **Registry de marcadores debugcon** | 17+ tags em `bapp_mark` sem documentação central | `docs/reference/debugcon-markers.md` consolida bapp_mark + browser-smoke + drivers; obrigatório atualizar no PR |

### Suítes verdes (host, macOS)

- `make test`: **76 + 567 + 71 + … = 100% verde** (+2 regressões novas:
  `test_frame_pixels_survive_subsequent_poll`,
  `test_two_consecutive_frames_overwrite_storage`).
- `make layout-audit --strict`: ok (compositor split entrega
  444+331+148 ≤ 900 cada, sem warnings).
- `make version-audit`: alinhado em `0.8.0-alpha.6+20260503`.
- `make boot-perf-baseline-selftest`: ok.
- `clang -target x86_64-unknown-linux-gnu -fsyntax-only -Werror`: limpo
  em `compositor.c`, `compositor_theme.c`, `compositor_render.c`,
  `runtime.c`, `browser_app.c`, `userland/bin/capybrowser/main.c`.

### Suítes que dependem de cross-toolchain (Linux CI)

Estas etapas só rodam na esteira Linux (precisam de
`gcc-x86-64-linux-gnu`, `binutils-x86-64-linux-gnu`, `gnu-efi`, `xorriso`,
`qemu-system-x86`, `ovmf`):

- `make all64 TOOLCHAIN64=host` — build kernel x86_64 (job
  `release-gates`).
- `make iso-uefi TOOLCHAIN64=host` — produz `CapyOS-Installer-UEFI.iso`.
- `make verify-release-checksums TOOLCHAIN64=host` — `sha256sum -c`.
- `make smoke-x64-iso TOOLCHAIN64=host` — boot QEMU end-to-end (job
  `qemu-smoke`).

A `release-gates` engatilha apenas após `make test + layout-audit +
version-audit + boot-perf` passarem; `qemu-smoke` engatilha apenas após
`release-gates`. Toda a pipeline está espelhada em
`.github/workflows/ci.yml` e foi exercitada localmente até onde a
toolchain de host permite.

## Status do pacote de atualização

- ✅ **Pode ser fechado manualmente.** Todos os fixes funcionais da
  Etapa 2 estão entregues; o hardening pós-Etapa 2 (lista acima) também
  está fechado e coberto por tests.
- ✅ **Versão e changelog** estão alinhados (`VERSION.yaml`, `README.md`,
  `include/core/version.h`, esta release note).
- ✅ **CI local** valida tudo o que cabe em macOS sem cross-toolchain;
  o restante é deterministicamente reproduzido pela esteira Linux.

## Pendente para a próxima release

- **Smoke QEMU `smoke-x64-browser-spawn`** rodando no boot real para
  validar a sequência de markers `[O][1][2][3][4][5][K][h][H][F]` +
  `[D]` ao testar Task Manager kill em hardware emulado. Já deterministicamente
  validado pelo `test_browser_e2e` host (48 asserts), mas o boot real
  ainda não foi capturado nesta release.
- **Captura de screenshots** novos com a janela 480×384 em uma futura
  versao de CapyUI (`docs/screenshots/CapyUI/v2/`).
- **Etapa 3 (JS engine inicial)**: bloqueada até hoje pelos pré-requisitos
  da seção "Etapa 3 — Roteiro pós-Etapa 2". Com a Etapa 2 fechada e
  hardenizada, a Etapa 3 fica desbloqueada para iniciar `Seção a`
  (rendering inline de imagens) ou diretamente o engine JS, sem mais
  pré-requisitos de robustez.

Versao alinhada: `0.8.0-alpha.6+20260503`
