# F3.3c — `html_viewer` para userland: plano de fatiamento

**Branch:** `feature/m5-w4`
**Origem:** `feature/continue-development` @ `4b43669`
**Status:** 🟡 planejamento / fatiamento
**Substitui:** entrada genérica "F3.3c — migração do `html_viewer` para userland" em
[`active/capyos-master-plan.md`](capyos-master-plan.md) §F3.

> Este documento existe porque F3.3c, na forma "mover tudo de uma vez", é
> grande demais para uma única sessão (≈6.6k linhas em
> `src/apps/html_viewer/*.c` + dependências de kernel). O fatiamento abaixo
> permite progredir incrementalmente mantendo `make test`, `make
> layout-audit`, `make all64` e a esteira de smokes verdes em cada slice.

---

## 1. Inventário de acoplamento (snapshot de hoje)

`src/apps/html_viewer/internal/html_viewer_internal.h` puxa, em ordem:

| Header                                     | Categoria  | Disponível em userland hoje? |
|--------------------------------------------|------------|------------------------------|
| `apps/html_viewer.h`                       | público    | ✅ (já existe)               |
| `apps/css_parser.h`                        | puro       | 🟡 verificar (provável puro) |
| `drivers/input/keyboard_layout.h`          | kernel     | ❌                           |
| `gui/compositor.h`                         | kernel     | ❌                           |
| `gui/font.h`                               | kernel     | ❌                           |
| `gui/jpeg_loader.h` / `gui/png_loader.h`   | kernel/gui | ❌                           |
| `kernel/scheduler.h` / `kernel/worker.h`   | kernel     | ❌ (gated por `UNIT_TEST`)   |
| `memory/kmem.h`                            | kernel     | ❌ (precisa shim libc)       |
| `net/dns_cache.h` / `net/http.h` / `net/stack.h` | kernel | ❌                           |
| `security/tls.h`                           | kernel     | ❌                           |
| `util/kstring.h`                           | puro       | 🟡 já portável (header-only) |

Os módulos de `html_viewer/*.c` agrupam-se em camadas:

| Camada                    | Arquivos                                                             | Dependência forte |
|---------------------------|----------------------------------------------------------------------|-------------------|
| **A. Parser puro**        | `html_parser.c`, `html_tree_helpers.c`, `text_url_helpers.c`         | `kstring`, `kmem` |
| **B. Estado de navegação**| `navigation_state.c`, `navigation_budget.c`, `response_classification.c`, `forms_and_response.c` | `klog`, `apic_timer_ticks`, `kmem` |
| **C. Render lógico**      | `render_tree.c`, `render_primitives.c`                               | `font` (medida)   |
| **D. Recursos / fetch**   | `resource_loading.c`                                                 | `net/*`, `tls`, `dns_cache` |
| **E. UI / shell**         | `ui_input.c`, `ui_mouse.c`, `ui_runtime.c`, `ui_shell.c`             | `compositor`, `keyboard_layout` |
| **F. Async runtime**      | `async_runtime.c`, `app_entry_async.c`                               | `kernel/worker`, `apic_timer_ticks` |
| **G. API pública / glue** | `public_api.c`, `common.c`                                           | mistura B+E       |

A migração natural é **A → C → B → D → E/F**, deixando **G** como o último
adapter (porque é a fronteira que o capybrowser ring-3 chamará).

---

## 2. Slices propostos

Cada slice termina com `make test`, `make layout-audit`, `clang
-fsyntax-only -target x86_64-unknown-linux-gnu` limpos e (quando aplicável)
um host test novo. Cada slice é um commit revisável separadamente.

### Slice 1 — fronteira "puro" do parser (host-buildable hoje) — ✅ ENTREGUE

**Status:** ✅ implementado nesta branch (`feature/m5-w4`).

**Arquivos publicados:**

- `userland/lib/capyhtml/include/capyhtml/parser.h` — surface mínima
  com `capyhtml_parse`, `capyhtml_yield_fn`, forward decls.
- `userland/lib/capyhtml/src/parser.c` — stub TU (returns -1) para
  amarrar a regra de build sem mover código kernel-side ainda.
- `userland/lib/capyhtml/README.md` — inventário completo de 30+
  símbolos externos do `html_parser.c` classificados em (a) helpers
  puros porta direta, (b) helpers privados a copiar, (c) acoplamento
  kernel a substituir por callback.
- `Makefile` — target `make capyhtml-userland-syntax` invoca
  `clang -fsyntax-only -target x86_64-unknown-linux-gnu -ffreestanding
  -nostdinc` em `parser.c`. Validado local: exit 0.
- `.github/workflows/ci.yml` — job `release-gates` executa o target
  em todo PR, garantindo "userland nunca importa header de kernel".



**Escopo:**

- Identificar e listar (em `userland/lib/capyhtml/README.md`) o conjunto
  exato de TUs que compilam sem `gui/`, `kernel/*`, `drivers/*`, `net/*`,
  `security/*`. Hipótese inicial: `html_parser.c`, `html_tree_helpers.c`,
  `text_url_helpers.c`, mais um shim de `kstring`/`kmem`.
- Criar o esqueleto `userland/lib/capyhtml/` com:
  - `Makefile.frag` (a ser includado pelo Makefile raiz)
  - `include/capyhtml/parser.h` (público para o `capybrowser` ring 3)
  - `src/parser.c` consistindo, **por enquanto**, de um `#include
    "../../../src/apps/html_viewer/html_parser.c"` envolto em macros que
    desabilitam o yield kernel-side e substituem `kmemzero/kstrcpy` por
    builtins de `<string.h>`.
- **Não** mover o código ainda. O slice 1 só *prova* que o parser compila
  no target userland (`x86_64-elf-*` via toolchain do CI) sem depender de
  nada do kernel.

**Saída esperada:** `make capyhtml-userland-syntax` (target novo) faz
`clang -fsyntax-only -target x86_64-unknown-linux-gnu -Iuserland/include
-Iuserland/lib/capyhtml/include userland/lib/capyhtml/src/parser.c`.

**Estimativa:** 1 sessão.

### Slice 2 — parser MVP em `userland/lib/capyhtml/src/parser.c` — ✅ ENTREGUE

**Status:** ✅ implementado nesta branch (`feature/m5-w4`).

**Decisão de escopo (ajuste em relação ao plano original):** em vez de
mover `html_parser.c` integralmente (e arrastar 30+ helpers cross-TU),
o Slice 2 implementa um **parser MVP** novo, freestanding, que cobre o
conjunto representativo necessário para a Slice 4 demo. Slice 2b
amplia cobertura (CSS, forms, srcset, picture); Slice 6 ainda apaga o
parser kernel-side. A vantagem é que o ring 3 ganha um parser
testado **agora** sem mexer em nenhum arquivo do kernel.

**Arquivos publicados:**

- `userland/lib/capyhtml/include/capyhtml/types.h` — `struct
  capyhtml_document`, `struct capyhtml_node`, enum
  `capyhtml_node_type` (13 valores), caps de buffer dimensionados
  para a stack do `capybrowser`.
- `userland/lib/capyhtml/include/capyhtml/parser.h` — surface real
  com docstrings de contrato.
- `userland/lib/capyhtml/src/parser.c` — implementação freestanding
  (sem `<string.h>`, sem `<stdlib.h>`) com helpers `cap_*` portáteis
  (`cap_memset/memcmp/memmove/strlen`); cooperative yield via
  `capyhtml_yield_tick()` plumbed pelo file-static state.
- `tests/test_capyhtml_parser.c` — **19 asserts** locking: NULL
  safety, title harvest, fallback a primeiro H1, anchor href/text,
  UL/LI counts, void br/hr, entity decoding (&lt; &amp; &quot;),
  whitespace collapsing, head/script/style skipping, comment skip,
  doctype skip, yield callback fires.
- `tests/test_runner.c` — registra `test_capyhtml_parser_run` na
  suíte host (passa a executar em CI ao lado dos outros 28 jobs de
  teste).
- `Makefile` — `HOST_CFLAGS` ganha `-Iuserland/lib/capyhtml/include`
  e `TEST_SRCS` inclui o parser TU.

**Coverage MVP:**

- Block: `<h1>` `<h2>` `<h3>` `<p>` `<div>` `<ul>` `<li>`
- Inline: `<a href="...">` `<span>` `<strong>/<b>/<em>/<i>`
- Void: `<br>` `<hr>`
- Skip: `<script>` `<style>` `<svg>` `<iframe>` `<object>` `<embed>`
  `<template>` (e `<head>` walk-through com title-harvest)
- Title: `<title>` direto, fallback ao primeiro `<h1>`
- Entities: `&amp; &lt; &gt; &quot; &apos; &nbsp;`
- Comments e DOCTYPE descartados sem virar nó

**Validação local:** `make capyhtml-userland-syntax` ✅ exit 0;
`tests/test_capyhtml_parser` standalone ✅ 19/19 passes.



**Escopo:**

- Mover (não copiar) `html_parser.c` + helpers chamados de dentro dele
  (`hv_is_space`, `hv_streq_ci`, etc., que vivem em `text_url_helpers.c` /
  `html_tree_helpers.c`) para a nova lib.
- Trocar `task_yield()` por uma função-callback injetada
  (`capyhtml_yield_fn`) que o **kernel-side** continua passando para
  preservar o W3 yield. Em userland, o callback será no-op (o scheduler
  preempta sozinho).
- Substituir `kmemzero`/`kstrcpy`/`kbuf_append*` por equivalentes
  declarados num novo `userland/lib/capyhtml/src/portable_strings.h`
  (header-only, copia 1:1 da implementação inline).
- O kernel `html_viewer/html_parser.c` original passa a ser um forwarder
  de 5 linhas: `#include "capyhtml/parser.h"` + adapter de `op_budget` →
  `capyhtml_yield_fn`.
- Adicionar `tests/test_capyhtml_parser.c` (host) com 5–10 asserts de
  smoke do parser puro (parseio de `<h1>oi</h1><p>x</p>` produz 2 nós com
  os tipos corretos).

**Risco:** baixo — o parser já é puro, só precisa do shim de strings.

**Estimativa:** 1 sessão.

### Slice 3 — render lógico (camada C) — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Arquivos criados/atualizados:**

- `userland/lib/capyhtml/include/capyhtml/render.h` — API pública:
  `capyhtml_font_ops` (struct injetável com `measure_width`,
  `glyph_width_px`, `glyph_height_px`, `line_gap_px`),
  `capyhtml_cmd` (kind/scale/bold/underline/color_role/x/y/w/h +
  borrowed `text`/`href`), `capyhtml_render_result` e a função
  `capyhtml_layout()`.
- `userland/lib/capyhtml/src/render.c` — implementação freestanding:
  walk linear sobre `doc->nodes[]`, emite TEXT/BULLET/RULE com y
  monotonicamente crescente, viewport_w clampa a w intrínseca,
  cmd_capacity overflow seta `truncated`. Suporta H1/H2/H3, P, A,
  LI, BR, HR, TEXT free-floating; UL/SPAN/DIV são containers
  silenciosos.
- `tests/test_capyhtml_render.c` — **36 asserts host** cobrindo
  arg validation (NULL/zero), heading scale+bold, paragraph cor+
  intrinsic w, link underline+href, LI bullet+texto na mesma
  baseline com indent correto, viewport clamping, capacity
  overflow → truncated, total_height monotônico.
- `Makefile` — `CAPYHTML_RENDER_OBJ` adicionado ao link do
  capybrowser; `capyhtml-userland-syntax` agora valida `parser.c`
  + `render.c`; suite host registra `test_capyhtml_render.c`.
- `tests/test_runner.c` — `test_capyhtml_render_run()` registrado.

**Validação:**

- `make capyhtml-userland-syntax` ✅ (clang freestanding cross-target).
- Standalone host: parser 19/19 + render 36/36 = **55/55** passam.

**Não escopo (slice 3b futuro):**

- Word-wrap em runs longos (hoje viewport_w só clampa).
- Composição inline real (link dentro de parágrafo). O parser MVP
  já hoista `<a>` para nó separado, então o efeito visual atual é
  "link em própria linha"; correto, mas não composto.



**Escopo:**

- Mover `render_tree.c` + `render_primitives.c` para
  `userland/lib/capyhtml/src/render/`.
- Abstrair `font_default()` / `font_measure_string` atrás de uma struct
  `capyhtml_font_ops` injetada pelo caller. O kernel (compositor) implementa
  via `gui/font.h`; o capybrowser ring 3 implementa com a mesma fonte
  embarcada como recurso estático.
- Saída: `capyhtml_layout(doc, font_ops) -> render_list` callable de
  qualquer lado.

**Estimativa:** 1 sessão.

### Slice 4 (preview) — `capybrowser` consome `libcapyhtml` — ✅ ENTREGUE

**Status:** ✅ preview integrado nesta branch (`feature/m5-w4`).

**Decisão de escopo (preview):** o slice 4 completo prevê fetch real
+ render de pixels reais. O preview entregue aqui prova que o motor
ring 3 chama `capyhtml_parse` e o resultado atravessa o IPC até o
debugcon — eliminando a dúvida "a lib é mesmo consumível em ring 3".
Render real fica para o slice 4 final (após fetch).

**Arquivos atualizados:**

- `userland/bin/capybrowser/main.c` — `run_navigate` chama
  `capyhtml_parse()` numa página HTML embutida e emite um
  `EVENT_LOG` carregando `[capybrowser] parsed N nodes
  title=capyland`.
- `Makefile` — `USERLAND_CFLAGS` ganha `-Iuserland/lib/capyhtml/include`;
  `CAPYBROWSER_OBJS` linka `parser.o` da libcapyhtml.
- `userland/lib/capyhtml/include/capyhtml/types.h` — caps reduzidos
  (64 nodes × ~520 B = ~33 KB) para caber na stack ring 3.
- `include/apps/browser_chrome.h` — `struct browser_chrome` ganha
  `last_log_level`, `last_log_msg_len`, `last_log_msg[192]` para
  surfaced LOG payloads.
- `src/apps/browser_chrome/chrome.c` — `handle_log` captura o
  payload em `c->last_log_msg` antes de retornar `LOG_FORWARD`.
- `tests/test_browser_chrome.c` — **+5 asserts** locking o novo
  contrato (level, len, payload verbatim, truncamento de oversize,
  NUL terminator preservado).
- `src/kernel/browser_smoke.c` — quando o action mask traz
  `LOG_FORWARD`, o poller imprime `[browser-smoke] event LOG <msg>`
  no debugcon.
- `tools/scripts/smoke_x64_browser_spawn.py` — `SUCCESS_MARKERS`
  inclui `[capybrowser] parsed`, garantindo que o smoke falha se a
  integração libcapyhtml ↔ engine quebrar.

**Validação local:** `clang -fsyntax-only` limpo em `chrome.c`,
`browser_smoke.c`, `capybrowser/main.c` e `parser.c`;
`make capyhtml-userland-syntax` ✅; host test do parser ✅ 19/19.



**Escopo:**

- `userland/bin/capybrowser/main.c` deixa de devolver `EVENT_FRAME` com
  pixels solid-color. Em `NAVIGATE`:
  1. Chama um stub de fetch (slice 5 traz fetch real; aqui é uma página
     embutida `<h1>capyland</h1>`).
  2. Chama `capyhtml_parse` + `capyhtml_layout` da `libcapyhtml`.
  3. Renderiza a `render_list` num framebuffer 16×16 in-place (continua
     pequeno; a janela real entra em F3.3c+widget).
  4. Emite `EVENT_FRAME` com bytes reais do parser.
- Atualiza `tools/scripts/smoke_x64_browser_spawn.py` para validar um
  marcador adicional (`[browser-engine] parsed N nodes`).

**Estimativa:** 1 sessão.

### Slice 5 (protocol) — fetch IPC codec — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Escopo entregue:** apenas a *camada de protocolo*. O backend que
realmente resolve URLs (file://, in-memory, HTTP via `libcapy-net`)
fica para slice 5b. Esta entrega prova que o pipeline de
encode/decode é roundtrip-correto e tem cobertura host.

**Arquivos:**

- `include/apps/browser_ipc.h` — adicionados:
  `BROWSER_IPC_FETCH_RESPONSE = 0x0C` (chrome → engine),
  `BROWSER_IPC_EVENT_FETCH_REQUEST = 0x8B` (engine → chrome),
  enums `browser_ipc_fetch_status` (0/200/403/404/503) e
  `browser_ipc_fetch_method` (GET/POST), structs
  `browser_ipc_fetch_request` / `browser_ipc_fetch_response`,
  e os 4 helpers de encode/decode.
- `src/apps/browser_ipc/codec.c` — `kind_is_request`,
  `kind_is_event`, `kind_is_known` e `kind_min_payload`
  reconhecem os dois novos kinds. Min payload: 11 (request fixed
  prefix), 16 (response fixed prefix).
- `src/apps/browser_ipc/fetch.c` — implementação freestanding dos
  4 helpers, com validação completa de NULL/método inválido/
  buffer pequeno/payload truncado/`MAX_PAYLOAD` overflow. Decode
  retorna `borrowed pointers` para a URL e o body, evitando cópia.
- `tests/test_browser_ipc_fetch.c` — **40 asserts host** cobrindo
  roundtrip de request com URL, request vazia, validação de
  argumentos (NULL/método inválido/buffer pequeno), roundtrip de
  response com ctype+body, response 404 vazia, payload truncado
  rejeitado, predicates de direção, min_payload table.
- `Makefile` — `CAPYBROWSER_FETCH_OBJ` linkado no capybrowser
  (lado engine), suite host registra `test_browser_ipc_fetch.c`.
- `tests/test_runner.c` — `test_browser_ipc_fetch_run()` chamado.

**Validação:**

- 40/40 asserts host passam standalone.
- `clang -fsyntax-only` cross-target freestanding limpo em
  `fetch.c`, `codec.c`, `chrome.c`, `runtime.c` e
  `userland/bin/capybrowser/main.c` (ring 3).

### Slice 5b (chrome handler) — fetch dispatch + drain — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Escopo entregue:** lado chrome do fetch flow. O resolver real
(file://, in-memory table, eventually HTTP via `libcapy-net`) e
o uso pelo engine ficam para slice 5c.

**Arquivos:**

- `include/apps/browser_chrome.h` — adicionados:
  `BROWSER_CHROME_ACTION_FETCH_REQUESTED = (1u << 5)`, campos
  `pending_fetch_*` no struct, e duas APIs novas:
  `browser_chrome_take_pending_fetch()` (drena slot) e
  `browser_chrome_build_fetch_response_payload()` (wrapper sobre
  `browser_ipc_fetch_response_encode`).
- `src/apps/browser_chrome/chrome.c` — `handle_fetch_request()`:
  decodifica EVENT_FETCH_REQUEST via codec do slice 5, copia URL
  para storage chrome-owned (BROWSER_CHROME_URL_MAX bytes,
  trunca silenciosamente), e marca slot pendente. Recusa um
  segundo request enquanto o anterior não foi drenado (protocol
  error). Init agora limpa explicitamente `last_log_*` e
  `pending_fetch_*`. Dispatcher despacha o novo kind.
- `tests/test_browser_chrome_fetch.c` — **31 asserts host**:
  staging do request, drain e clear, NULL-safety, overlap
  rejeitado (slot single-shot), payload curto rejeitado,
  roundtrip do build_response_payload helper, buffer pequeno
  retorna 0.
- `Makefile` — adiciona `tests/test_browser_chrome_fetch.c` à
  suite host (chrome.c já era linkado por test_browser_chrome).
- `tests/test_runner.c` — `test_browser_chrome_fetch_run()`
  registrado.

**Validação:**

- 31/31 asserts standalone passam.
- Suite browser inteira (chrome + runtime + e2e + ipc + watchdog
  + fetch codec + chrome fetch) = **461/461** com este slice.
- Sem regressão em `test_browser_chrome` (71/71) — campos novos
  são inicializados explicitamente em `browser_chrome_init`.

### Slice 5c (resolver + runtime dispatch) — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Escopo entregue:** lado chrome do flow completo. Engine ainda
usa página hardcoded — atualização do capybrowser ring 3 fica
para slice 5d (precisa coordenar I/O bidirecional com PING/PONG
existente).

**Arquivos:**

- `include/apps/browser_chrome_fetch_resolver.h` — API nova:
  `browser_chrome_resolve_local()`, `_page_count()`, `_page_url()`.
  Struct `browser_chrome_fetch_result` com pointers borrowed para
  static const storage (sem alocação).
- `src/apps/browser_chrome/fetch_resolver.c` — tabela embutida com
  3 páginas (welcome, about, demo) servindo `text/html; charset=utf-8`,
  fallback 404 para qualquer URL desconhecida. Match exato por
  comprimento + bytes (nem prefix nem suffix). Sem libc.
- `include/apps/browser_chrome_runtime.h` — adiciona
  `chrome_runtime_dispatch_pending_fetch()` (drain → resolve →
  build_response → send_frame).
- `src/apps/browser_chrome/runtime.c` — implementação: drena via
  `browser_chrome_take_pending_fetch`, resolve via resolver,
  encoda payload em scratch 2 KiB, manda FETCH_RESPONSE pelo
  request pipe. Body grande demais → fallback 503 com body vazio.
  Pipe quebrado → `engine_alive=0` + retorna -1.
- `tests/test_browser_fetch_resolver.c` — **27 asserts host**:
  cada página known resolve com 200+body+ctype, URL desconhecida
  → 404, NULL/url_len=0 → 404, length mismatch (shorter/longer)
  rejeitado, page_url out-of-range → NULL, NULL out pointer safe.
- `tests/test_browser_runtime_fetch.c` — **25 asserts host** com
  pipes mock: no-pending → 0, known URL roundtrip (frame lido
  da pipe + decode → seq/nav_id/status/body), unknown URL → 404
  no payload, dois requests sequenciais (slot single-shot
  funciona quando drenado entre eles), broken pipe limpa
  engine_alive.
- `Makefile` —
  - kernel link: `apps/browser_ipc/fetch.o` +
    `apps/browser_chrome/fetch_resolver.o` adicionados ao kernel
    (chrome live aqui, não no engine ring 3).
  - host suite: `test_browser_fetch_resolver.c` +
    `test_browser_runtime_fetch.c` registrados.
- `tests/test_runner.c` — dois novos `_run` registrados.

**Validação:**

- 27 + 25 = 52 asserts standalone passam.
- Suite browser inteira **513/513** com este slice
  (162+40+49+71+31+27+25+61+47).
- Sem regressão em runtime/chrome/e2e existentes.

### Slice 5d (engine usa fetch) — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Escopo entregue:** o engine ring 3 (capybrowser) abandona a
página hardcoded e passa a buscar o conteúdo real via fetch IPC.
A nav agora é genuinamente bidirecional: `NAVIGATE` → engine
emite `EVENT_FETCH_REQUEST` → chrome resolve via `fetch_resolver`
→ chrome envia `FETCH_RESPONSE` → engine parseia o `body` e
emite `EVENT_LOG`. HTTP real (slice 5e) espera `libcapy-net`.

**Arquivos:**

- `userland/bin/capybrowser/main.c` — substituição completa do
  fluxo `run_navigate`:
  - novo `g_fetch_seq` independente do header seq;
  - `emit_fetch_request(seq, nav_id, url, url_len)` para mandar
    `EVENT_FETCH_REQUEST`;
  - `emit_nav_failed(nav_id, reason)` extraído como helper;
  - `wait_for_fetch_response(...)` bloqueia em fd 0 mantendo
    `PING/PONG` vivos durante a espera; trata `CANCEL` (retorna
    -2 para o caller emitir `NAV_CANCELLED`) e `SHUTDOWN` (sai
    com `capy_exit(0)`); ignora respostas com seq/nav_id
    "stale" (de navegações superadas);
  - `emit_parsed_log(doc)` extrai a formatação `parsed N nodes
    title=X` em helper, chamada agora sobre o body recebido por
    fetch em vez do `k_demo_page`;
  - `INPUT_PAYLOAD_BUF` 1 KiB → 4 KiB para acomodar
    `FETCH_RESPONSE` com body + ctype + prefixo;
  - status não-OK do fetch surface como `NAV_FAILED` com
    `reason="fetch_status=NNN"`.
- `tests/test_browser_e2e.c` — fake_engine atualizado para
  espelhar o capybrowser real:
  - struct ganha `pending_nav`, `pending_nav_id`,
    `pending_fetch_seq`, `fetch_seq`;
  - `engine_emit_navigate_sequence` quebrada em
    `engine_begin_navigation` (phase 1: NAV_STARTED + progress
    FETCH + EVENT_FETCH_REQUEST) e `engine_complete_navigation`
    (phase 2: progress PARSE/RENDER + frame + NAV_READY);
  - `engine_pump` despacha `BROWSER_IPC_FETCH_RESPONSE` chamando
    `engine_complete_navigation`;
  - `drain_chrome_events` agora chama
    `chrome_runtime_dispatch_pending_fetch` quando a action
    `FETCH_REQUESTED` aparece;
  - novo helper `session_run_until_idle(s, now, &actions)`
    alterna pump+drain até quiescência; cenários `happy`,
    `restart` e `two_navigations` migrados para usá-lo.
  - cenário happy adiciona checks: `engine.total_received >= 2`,
    `last_kind == FETCH_RESPONSE`, `count == 7` (era 6);
  - `restart` espera 7 eventos pós-restart;
  - `two_navigations` espera ≥ 14 eventos polled.

**Validação:**

- Suite browser inteira **514/514** verde
  (162+40+49+71+31+27+25+61+48).
- E2E ganhou +1 assert (47→48); cobertura agora exercita o
  fetch real em vez de simular eventos.
- `clang -fsyntax-only` cross-target freestanding limpo em
  `userland/bin/capybrowser/main.c`, `chrome.c`, `runtime.c`,
  `fetch_resolver.c`, `fetch.c`.

**Próximo passo (slice 5e — HTTP real):**

- Bloqueado em F4 (`libcapy-net`). Quando estiver pronto, o
  resolver embutido vira fallback; URLs `http://` e `https://`
  vão para um backend que fala HTTP/1.1 + TLS via socket.
- Provavelmente entrega a partir de `feature/m5-w5`.

### Slice 4-final (raster real em ring 3) — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Escopo entregue:** o engine ring 3 deixa de mandar um quadrado
azul como placeholder e passa a renderizar pixels reais a partir
do `capyhtml_document` parseado. A pipeline completa
(NAVIGATE → fetch → parse → layout → raster → EVENT_FRAME) agora
atravessa ring 3 inteiro, sem mais hardcodes nem stubs.

**Arquivos novos:**

- `userland/lib/capyhtml/include/capyhtml/font.h` +
  `src/font.c` — fonte 8x8 ASCII embutida (cópia do
  `font8x8_basic` do kernel, com nota de "manter em sincronia").
  Fornece `capyhtml_font_glyph_row(byte)` e
  `capyhtml_font_ops_default(out)` que materializa um
  `capyhtml_font_ops` apto a alimentar o `capyhtml_layout`.
- `userland/lib/capyhtml/include/capyhtml/raster.h` +
  `src/raster.c` — rasterizer freestanding BGRA8888:
  - `capyhtml_raster_target` descreve o framebuffer do caller;
  - `capyhtml_palette` mapeia cada `capyhtml_color_role` para
    ARGB + cor de fundo;
  - `capyhtml_raster_clear`, `capyhtml_raster_draw` e
    `capyhtml_raster_render` formam a API completa;
  - blit de glifo escala 1..8x via replicação de pixel,
    bold = segunda blit deslocada +1px, underline = strip
    horizontal de 1px, bullet/rule = `fill_rect` clipado;
  - kinds desconhecidas são silenciosamente ignoradas
    (forward-compat com slice 3b).
- `tests/test_capyhtml_raster.c` — **30 asserts** cobrindo
  clear, blit do glifo `'A'` (lit-count 30 exato), fallback
  de `color_role` inválido, bold dobra pixels, underline
  cobre largura do run, bullet preenche quadrado, rule
  preenche faixa, kinds desconhecidas são ignoradas, clip
  fora da viewport não escreve nem crasha, render walk de
  lista mista de cmds, lookup do glifo de espaço (zerado),
  `'A'` (lit) e fallback `'?'` para byte ≥128, e validação
  do `capyhtml_font_ops_default` (8x8, gap > 0,
  measure_width escala com scale e lida com NULL).

**Arquivos alterados:**

- `userland/bin/capybrowser/main.c`:
  - novos defines `ENGINE_FB_W=192, ENGINE_FB_H=128`,
    framebuffer estático `g_frame_payload` em `.bss` (96 KiB
    + 12 B de header); a página é zerada na carga pelo
    `elf_loader.c` (que já fazia `elf_memset(0)` em cada
    page mapeada);
  - `engine_default_palette` casa as cores do tema `capyos`:
    fundo off-white, texto near-black, heading rose-800,
    link pink-600, muted gray, bullet rose-800;
  - `emit_real_frame(nav, doc)` faz `capyhtml_layout` →
    `capyhtml_raster_render` → `send_frame(EVENT_FRAME, ...)`
    em uma única payload de 96 KiB. Se o layout falhar, o
    caller pode usar o fallback stub.
  - `run_navigate` agora distingue `have_doc`: se o parse
    teve sucesso, chama `emit_real_frame`, senão cai para
    `emit_stub_frame` (mesmo quadrado azul de antes,
    preservado como fallback visual de erro);
  - comentário do header refletindo que `.bss` é seguro neste
    binário porque o ELF loader zera as páginas mapeadas.
- `Makefile`:
  - novos objetos `CAPYHTML_FONT_OBJ`, `CAPYHTML_RASTER_OBJ`
    com regras dedicadas;
  - linkados em `CAPYBROWSER_OBJS`;
  - `capyhtml-userland-syntax` agora roda também em
    `font.c` e `raster.c`;
  - `HOST_TEST_SRCS` inclui o novo `test_capyhtml_raster.c`
    + as duas TUs.
- `tests/test_runner.c`: registrado
  `test_capyhtml_raster_run`.

**Validação:**

- 30/30 asserts no novo test_capyhtml_raster passando.
- Suite completa **644 asserts** verde via `make test`
  (162+40+49+71+31+27+25+61+48 browser + 19+36+30 capyhtml +
  outros).
- `clang -fsyntax-only -target x86_64-unknown-linux-gnu` limpo
  em `userland/bin/capybrowser/main.c` com o novo flow.
- `make capyhtml-userland-syntax` verde (4 TUs cross-checked
  em ambiente freestanding sem `-Iinclude`).

**Decisões de design:**

- *Por que .bss?* Stack ring 3 é 64 KiB; o framebuffer (96 KiB)
  não cabe. Heap não existe. `.bss` é a única opção razoável.
  Confirmado que o ELF loader zera as páginas em
  `elf_loader.c:69-73`, então não precisamos inicializar
  manualmente.
- *Por que 192x128?* Alto o bastante para mostrar h1 (24 px)
  + 2 parágrafos + lista; payload IPC de ~96 KiB cabe nos
  1 MiB do limite de wire (`BROWSER_IPC_MAX_PAYLOAD`). Maior
  inflaria a cópia para o compositor sem ganho visual nesta
  fase.
- *Por que duplicar a font?* O syntax-check de capyhtml roda
  com `-nostdinc` e sem `-Iinclude`, deliberadamente
  isolando a userland de qualquer header do kernel. Compartilhar
  a tabela exigiria mover-a para um caminho neutro e ajustar
  o build do kernel — pode ser feito em um cleanup futuro.

**Próximo passo:**

- Slice 5e (HTTP real, F4-blocked) e cleanup do parser
  kernel-side antigo (`src/apps/html_viewer/`) que agora é
  redundante.



**Escopo:**

- Estabelecer protocolo IPC `REQUEST_FETCH(url)` /
  `EVENT_FETCH_BYTES(...)` / `EVENT_FETCH_DONE` entre o engine ring 3 e o
  chrome kernel-side.
- O kernel-chrome continua usando `net/http.c` + `tls.c` (zero migração de
  net/tls neste slice).
- Adiciona host test do codec do novo kind (espelho do test_browser_ipc).

**Estimativa:** 1–2 sessões.

### Slice 6 — desativar `src/apps/html_viewer/` e remover — ✅ ENTREGUE

**Status:** ✅ 2026-05-01 (branch `feature/m5-w4`).

**Escopo entregue (remoção agressiva):**

- Apagado `src/apps/html_viewer/` completo (18 TUs, ~7188 linhas):
  `app_entry_async.c`, `async_runtime.c`, `common.c`,
  `forms_and_response.c`, `html_parser.c`, `html_tree_helpers.c`,
  `navigation_budget.c`, `navigation_state.c`, `public_api.c`,
  `render_primitives.c`, `render_tree.c`, `resource_loading.c`,
  `response_classification.c`, `text_url_helpers.c`, `ui_input.c`,
  `ui_mouse.c`, `ui_runtime.c`, `ui_shell.c` + `internal/`.
- Apagado `src/apps/css_parser/` completo (3 TUs `apply.c`,
  `common.c`, `parse.c` + `internal/`, ~880 linhas) — só existia
  para o html_viewer.
- Apagados headers `include/apps/html_viewer.h` (254 linhas) e
  `include/apps/css_parser.h`.
- Apagados `tests/test_html_viewer.c` + `tests/html_viewer/*.inc`
  (8 fragments, ~1818 linhas total).
- `Makefile`:
  - removidas 21 entradas `$(BUILD)/x86_64/apps/html_viewer/...`
    e `.../css_parser/...` de `CAPYOS64_OBJS`;
  - removidas 21 fontes `src/apps/html_viewer/*.c` +
    `src/apps/css_parser/*.c` de `TEST_SRCS`;
  - removida `tests/test_html_viewer.c` de `TEST_SRCS`.
- `tests/test_runner.c`: extern + chamada de
  `run_html_viewer_tests` removidos.
- `src/gui/desktop/desktop.c`:
  - `#include "apps/html_viewer.h"` removido;
  - `menu_action_browser` agora é no-op com comentário
    apontando F3.3f como wiring real;
  - `html_viewer_tick()` per-frame substituído por comentário
    explicando que a responsabilidade migrou para o chrome
    runtime (`chrome_runtime_poll_event` +
    `chrome_runtime_check_watchdog`).
- `src/shell/commands/extended.c`:
  - `#include "apps/html_viewer.h"` removido;
  - `cmd_open_browser` imprime
    `"browser: kernel-side viewer removed; ring-3 chrome
    wiring in F3.3f"` em vez de chamar `html_viewer_open()`.

**Validação:**

- `make test` (host suite) **100% verde** após remoção;
  capyhtml: 19+36+30 = 85 asserts; browser: 543; total
  global > 600 asserts livres de regressão.
- `clang -fsyntax-only -target x86_64-unknown-linux-gnu` limpo
  em `desktop.c` e `extended.c` pós-edição.
- `grep -rE "html_viewer|css_parser" src/ tests/ include/`
  retorna apenas os comentários explicativos (zero referências
  funcionais).
- Build dirs antigos (`build/x86_64/apps/html_viewer/`,
  `.../css_parser/`) ficam como artefatos órfãos até o próximo
  `make clean` — nenhum impacto funcional.

**O que NÃO foi feito (por design):**

- ~~Spawn ring-3 chrome a partir do menu Browser~~ — **entregue
  em seguida como F3.3f** (2026-05-01, mesma sessão).
- Compartilhar a tabela de fonte 8x8 entre kernel e userland —
  decisão consciente do slice 4-final; pode ser feito em
  cleanup futuro quando o build do kernel tolerar
  `-Iuserland/lib/capyhtml/include`.

---

## 3. Ordem recomendada de PRs

1. **PR-1** (este commit) — plano de fatiamento (este arquivo).
2. **PR-2** — Slice 1 (esqueleto + syntax-check em CI).
3. **PR-3** — Slice 2 (parser puro extraído + host test).
4. **PR-4** — Slice 3 (render lógico).
5. **PR-5** — Slice 4 (capybrowser consome o parser).
6. **PR-6** — Slice 5 (fetch IPC).
7. **PR-7** — Slice 6 (cleanup + fechar F3.3c).

Cada PR é mergeável independentemente sem regressão dos smokes existentes
porque o kernel-side `html_viewer` continua íntegro até o slice 6.

---

## 4. Dependências cruzadas

- **F1 (release α.5):** não bloqueia F3.3c. F3.3c entra na próxima minor
  alpha (α.6).
- **F4 (sockets userland):** independente. Quando F4 entregar
  `libcapy-net`, o slice 5 pode trocar o IPC de fetch por chamadas diretas
  da libcapy-net no engine ring 3 — mas isso é **escopo F3.3c+net**, não
  F3.3c.
- **F3.3e (smoke browser-spawn):** continua válido em todos os slices; o
  smoke de slice 4 estende-o, não substitui.

---

## 5. Critério de "F3.3c entregue"

- [ ] Parser HTML executa no espaço de endereçamento do `capybrowser`
      (PID ≠ kernel PID 0).
- [ ] Crash sintético no parser (`#PF` no engine) é contido pelo kernel
      (smoke `smoke-x64-browser-isolation` verde).
- [ ] Watchdog mata o engine quando o parser entra em loop infinito
      (smoke `smoke-x64-browser-watchdog` verde).
- [ ] `make test` host: ≥ 50 asserts novos cobrindo parser/render
      portados.
- [ ] `src/apps/html_viewer/*.c` removido ou reduzido a < 200 linhas de
      glue.

---

## 6. Notas de execução

- **Macros `UNIT_TEST` vs userland:** o parser usa `#ifndef UNIT_TEST` para
  importar `kernel/task.h`. Isso vira `#ifdef CAPYHTML_HOSTED_KERNEL` na
  nova lib (defaultando off no userland e on no kernel).
- **Buffers estáticos enormes (`HTML_URL_MAX`, `HTML_TEXT_MAX`):** ok no
  ring 3 também — o stack do `capybrowser` é generoso (ver `_start`).
- **Yield cooperativo:** preservar como callback é o que mantém o
  Workstream 3 vivo no userland.
