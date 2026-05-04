# F3 — Browser ring-3 isolado: entregas realizadas

> **Arquivo historico** (2026-05-03). Consolida tudo que foi entregue
> na fase **F3 (Browser isolado + watchdog, tag original M8.2)** ate
> `0.8.0-alpha.6+20260503`. O que **resta** fazer esta em
> [`active/capyos-master-plan.md`](../active/capyos-master-plan.md) §F3
> ("Restante").
>
> Tags arqueologicas preservadas apenas para rastreabilidade:
> `F3.3a` .. `F3.3h`. Toda documentacao nova usa a convencao **Etapa
> N / Secao a-e** (ver §0 do master plan).
>
> Predecessores (tambem em `historical/`):
> - [`f3-3c-html-viewer-userland-slicing.md`](f3-3c-html-viewer-userland-slicing.md)
>   — fatiamento Slice 1..6 da migracao do parser/render/raster/fetch
>   para ring 3.
> - [`f3-3f-browser-desktop-wiring.md`](f3-3f-browser-desktop-wiring.md)
>   — roteiro linearizado do wiring desktop↔engine, com 10 secoes
>   detalhadas (Etapas 1, 2, 3 b/e, 3 b-polish, 3 b-polish++, 4 a/b).

---

## 1. Visao geral

A fase F3 entrega o primeiro app real em ring 3 do CapyOS: um browser
isolado em um processo userland spawnado pelo chrome kernel-side via
`browser_engine_spawn`, comunicando-se exclusivamente por pipes IPC
com protocolo binario big-endian, rasterizando pixels reais via
`libcapyhtml` (parser/render/raster freestanding), buscando conteudo
HTTP/HTTPS real pelo stack kernel (`net/http.h` + BearSSL TLS 1.2) e
renderizando paginas de erro HTML nativas em falhas de navegacao.

- **Status:** 🟢 96% entregue. Restam poucos itens visiveis
  (imagens inline, formularios, CSS de tabelas/box-model, fonte
  variavel), todos independentes entre si; ver master plan §F3
  "Restante".
- **Bloqueio unico para 100%:** validacao visual em QEMU depende de
  cross-toolchain `x86_64-elf-*` em CI, que nao roda no host macOS
  de desenvolvimento.

---

## 2. Subfases entregues (F3.3a .. F3.3h)

| Subfase | Tema | Marco |
|---|---|---|
| **F3.3a** | Protocolo IPC binario (header BE + codec + kinds REQUEST/EVENT) | 2026-05-01 ✅ |
| **F3.3b** | Stub `capybrowser` ring-3 + `/bin/capybrowser` em `embedded_progs` | 2026-05-01 ✅ |
| **F3.3c** | Migracao `html_viewer` → userland `libcapyhtml` (parser + render + raster + fetch IPC) | 2026-05-01 ✅ |
| **F3.3d** | Chrome scaffolding: watchdog + dispatcher + runtime + spawn helper + e2e | 2026-05-01 ✅ |
| **F3.3e** | Smoke `smoke-x64-browser-spawn`: kernel boot wiring + harness QEMU (🟡 70% — aguarda cross-toolchain CI) | 2026-05-01 scaffolding |
| **F3.3f** | Desktop wiring: menu Browser → spawn ring-3 chrome + janela compositor blita `chrome.last_frame` | 2026-05-01 ✅ |
| **F3.3g** | Etapa 4 a+b: HTTP/HTTPS real no fetch bridge kernel-side | 2026-05-02 ✅ |
| **F3.3h** | Etapa 3 b+e: CLICK → NAVIGATE + SCROLL + RELOAD | 2026-05-02 ✅ |
| **F3.3h.2** | Etapa 3 b-polish: EVENT_TITLE + BACK/FORWARD history + hotkeys F5/F6/F7 | 2026-05-03 ✅ |
| **F3.3h.3** | Etapa 3 b-polish++: pagina de erro HTML real (substitui stub azul) | 2026-05-03 ✅ |
| **F3.3i** | Etapa 3 seção a (placeholder MVP): parser `<img>`, render CMD_IMAGE, raster placeholder cinza + borda + alt text | 2026-05-03 ✅ |
| **F3.3j** | Etapa 3 seção c (forms MVP): parser `<form>`/`<input>`, render CMD_INPUT, raster caixa estilizada por subtipo, engine focus+KEY+submit, runtime `chrome_runtime_send_key`, browser_app `focus_target` URLBAR↔PAGE | 2026-05-03 ✅ |
| **F3.3j.2** | Etapa 3 seção c polish: indicador visual de foco (borda 2 px HEADING + caret) via `CAPYHTML_INPUT_FLAG_FOCUSED`, percent-encoding RFC 3986 (`%XX` uppercase para chars não-unreserved), click-fora-de-input limpa foco e re-emite frame | 2026-05-03 ✅ |
| **F3.3k** | Etapa 3 seção d (tables MVP): parser `<table>`/`<tr>`/`<td>`/`<th>`, render layout grid (cols=count(first TR's cells), cell_w=avail/cols), novo `CMD_CELL` em vez de N rules+text, raster `draw_cell_cmd` com bg MUTED para TH e borda 1 px LINK | 2026-05-03 ✅ |
| **F3.3k.2** | Etapa 3 seção d refinement: `colspan` em `node->reserved[0]` (clamp 1..255), `rl_count_table_first_row_cols` soma colspans, `rl_emit_cell` emite cell com `w = colspan * cell_w` e avança col_index por colspan, defesa de clamp-to-remaining; auto-fit de tabela larga em viewport estreito (clampa cell_w ao MIN ao invés de descartar) | 2026-05-03 ✅ |
| **F3.3j.3** | Etapa 3 seção c refinement: `<textarea name="X">body</textarea>` parser captura body como text + push como TAG_INPUT subtype TEXTAREA (4); render INPUT_TEXTAREA_W=280 / H=72; raster posiciona texto no topo (padding 6 px) ao invés de centro vertical | 2026-05-03 ✅ |
| **F3.5a** | Etapa 5 hardening: rate limiter de eventos incoming no `chrome_runtime` — `CHROME_RUNTIME_INCOMING_RATE_MAX = 64/tick`, novo status `CHROME_RUNTIME_POLL_RATE_LIMITED`, contadores `incoming_in_window` + `total_incoming_drops`, browser_app trata como break do loop. Defesa contra engine spammando frames | 2026-05-03 ✅ |
| **F3.5b** | Etapa 5 hardening URL whitelist (opt-in): `chrome_runtime_set_url_policy(fn)`. Quando instalado, callback é consultado em `chrome_runtime_send_navigate`; deny incrementa `total_url_blocked` e retorna -1 sem tocar o pipe. Útil para kiosk-mode/parental control/testes | 2026-05-03 ✅ |
| **F3.5c** | Etapa 5 hardening observability: contador `total_event_bytes_received` (u64) acumula header+payload de cada evento admitido. Útil para auditoria + detecção de spam (sem enforcement; só telemetria) | 2026-05-03 ✅ |
| **F3.3j.4** | Etapa 3 seção c refinement: `<select name="X">` + `<option value="V">L</option>` MVP. Parser empurra TAG_INPUT (subtype SELECT=5) e TAG_OPTION nodes (name=value, text=label). Primeiro option vira default. Render reusa text dimensions; raster desenha marcador "▼" 5 px na direita em HEADING color. `+13 asserts` | 2026-05-03 ✅ |
| **F3.5d** | Etapa 5 hardening: subsistema dedicado `audit_log` (`include/apps/browser_chrome_audit.h` + `src/apps/browser_chrome/audit_log.c`). Ring buffer 32 entradas, 6 categorias (NAV, RATE_DROP, POLICY_DENY, ENGINE_EOF, PROTOCOL, FETCH), API limpa (init/record/count/visible/at), wraparound O(1) sem alocação dinâmica. Hooks em send_navigate (NAV/POLICY_DENY), poll_event (RATE_DROP/ENGINE_EOF/PROTOCOL), dispatch_pending_fetch (FETCH). Nova suite `tests/test_browser_chrome_audit.c` (4 testes diretos do ring) + 3 testes de integração em `test_browser_chrome_runtime_rate.c` | 2026-05-03 ✅ |

---

## 3. Cobertura de testes host acumulada

Suite host (via `make test` OU compilacao direta por gcc sem make,
ver o harness temporario usado nas sessoes 2026-05-02/03):

| Suite | Asserts | Cobertura |
|---|---:|---|
| `test_browser_ipc` | **162** | Round-trip de 21 kinds, validacao magic/kind/payload |
| `test_browser_ipc_fetch` | **40** | Encode/decode FETCH_REQUEST + FETCH_RESPONSE |
| `test_browser_watchdog` | **49** | PING/PONG state, timeout, kill-after-N-missed, restart |
| `test_browser_chrome` | **71** | Dispatcher de 11 kinds de evento, stale nav_id, PONG routing |
| `test_browser_chrome_fetch` | **31** | Stage/drain/overlap do `pending_fetch_*`, roundtrip response payload |
| `test_browser_fetch_resolver` | **27** | 3 paginas built-in + 404 fallback + case-sensitivity |
| `test_browser_runtime_fetch` | **39** | Dispatch pending fetch; HTTP URL routing vs `file://` resolver |
| `test_browser_chrome_runtime` | **168** | send_navigate/cancel/shutdown/ping/resize/click/scroll/reload/back/forward/**key** + poll EOF/event/title/frame + tick + Etapa 5 rate limiter (init zero, burst drops, tick reset) + URL policy (init zero, deny-all, deny-prefix allow) + bytes observability + audit log integration (NAV/POLICY_DENY/RATE_DROP recording) |
| `test_browser_chrome_audit` | **27** | Audit log ring buffer dedicado: init zera, record em ordem mantém seq + count, ring envolve sobrescrevendo mais antigo após RING_SIZE entries, NULL safety em todos os endpoints |
| `test_browser_e2e` | **48** | Integration end-to-end: navegacao feliz, PING/PONG, cancel, crash-recovery, restart, protocol-err, duas navs back-to-back |
| `test_capyhtml_parser` | **64** | Shape HTML + entidades + skip script/style + error-page template + `<img>` + `<form>`/`<input>` + `<table>`/`<tr>`/`<td>`/`<th>` + `colspan` + `<textarea>` (subtype, body, name, empty) + `<select>`/`<option>` (subtype, name, default, count, value+label, fallback) |
| `test_capyhtml_render` | **120** | Layout H1..H3/P/A/LI, viewport clamp, bold/underline + CMD_IMAGE + CMD_INPUT (subtype, node_idx packing, form action inheritance, value/label) + CMD_CELL (2x2 grid layout, row/col positions, TH=HEADING/TD=TEXT, cells abut, table+P stacking, zero-cols drops, excess-cells clipped) + colspan (first-row sets count, clamp-to-remaining, abut) + auto-fit narrow viewport + textarea (h>=64, subtype TEXTAREA) + select (1 cmd, default text, action) |
| `test_capyhtml_raster` | **64** | BGRA clear/blit/fill_rect, glyph, underline, bullet, rule + CMD_IMAGE placeholder + CMD_INPUT (text/submit/password masking, zero-dims, focus border 2px HEADING, caret, no-caret-on-submit, subtype-mask isolates focus bit, textarea text top-aligned, select dropdown marker isolated) + CMD_CELL (TH bg+border, TD transparent, text in color_role, zero-dims) |
| `test_browser_app_url_edit` | **567** (checks) / **38** (asserts agrupados) | URL bar editor puro: insert/delete/cursor/clamp |

**Total F3: ~1389 asserts numericos** (valores variam ±5 em
versoes proximas por granularidade de contagem). Incluindo testes
de base nao-F3 (M4/M5 etc.), a suite host completa passa **41
grupos / 2058 asserts numericos + ~35 grupos bloco/crypt/drivers
OK sem contagem numerica**.

---

## 4. Detalhe tecnico por area

### 4.1 Protocolo IPC (F3.3a)

- `include/apps/browser_ipc.h` — magic `0xCB1B`, header 12 B BE
  (magic u16, kind u16, seq u32, payload_len u32), 21 kinds
  (11 request + 10 event), `BROWSER_IPC_MAX_PAYLOAD = 1 MiB`.
- `src/apps/browser_ipc/codec.c` — encode/decode + validacao +
  predicates `kind_is_request/event/known`.
- `src/apps/browser_ipc/fetch.c` — encode/decode de
  FETCH_REQUEST (seq, nav_id, method, url_len, url) e
  FETCH_RESPONSE (seq, nav_id, status, ctype_len, body_len,
  ctype, body).

### 4.2 Engine ring-3 (F3.3b, F3.3c, F3.3g, F3.3h)

`userland/bin/capybrowser/main.c` (**1240 linhas**, nao auditado
pelo `layout-audit` porque `userland/` esta fora de `DEFAULT_ROOTS`):

- **IPC loop** com `g_request_payload[1 MiB+4 KiB]` em `.bss`
  (nao cabe na stack ring-3 de 64 KiB).
- **Parser** via `capyhtml_parse` (freestanding, sem libc).
- **Layout + raster** via `capyhtml_layout` → `capyhtml_raster_render`
  em framebuffer BGRA de ate 1024x768 (`g_frame_payload[ENGINE_FRAME_TOTAL]`).
- **Estado persistente** (Etapa 3):
  - `g_current_doc` (33 KiB) + `g_current_doc_valid` + `g_current_nav`
  - `g_current_url[1024]` + `g_current_url_len`
  - `g_scroll_y_px` + `g_content_height_px`
- **Historico** (Etapa 3 b-polish): `g_history[32][1024]` + `g_history_len[]`
  + `g_history_count` + `g_history_index` + flag transiente
  `g_nav_from_history`. Trunca "futuro" em divergencia;
  shift-left quando ring cheio.
- **Error page** (Etapa 3 b-polish++): `g_error_html_buf[2048]`
  com template `<h1>Pagina nao carregou</h1><p>Endereco:
  {URL}</p><p>Motivo: {REASON}</p><p>F5/F6/F7</p>`, sanitiza
  `<`/`>` → `?`. Usado em 4 caminhos: HTTP non-2xx, transport err,
  body vazio, parse fail.
- **Handlers** no switch principal:
  `NAVIGATE`/`PING`/`CANCEL`/`SHUTDOWN`/`RESIZE`/`CLICK`/`SCROLL`/`RELOAD`/`BACK`/`FORWARD`.
- **Hit-test CLICK** (Etapa 3 b): re-layout do doc atual, bounding
  box `[x,x+w) × [y,y+h+1)` (pad absorve underline), href
  resolution (absolute/path-absolute/raw).
- **emit_title** (Etapa 3 b-polish): chamado apos parse bem-sucedido
  e na pagina de erro para que o chrome atualize a title bar via
  `compositor_set_title`.

### 4.3 Chrome runtime kernel-side (F3.3d, F3.3f, F3.3g, F3.3h)

- `src/apps/browser_chrome/chrome.c` (406 linhas) — dispatcher puro
  de eventos IPC, mantem `struct browser_chrome` com status/url/title
  /last_frame/watchdog/pending_fetch. Sem syscalls.
- `src/apps/browser_chrome/runtime.c` (**647 linhas**) — orquestra pipes
  com funcoes injetaveis (`chrome_runtime_set_pipe_ops` +
  `set_yield_op`), poll/tick/dispatch_pending_fetch, 9 senders:
  `send_navigate/cancel/shutdown/ping/resize/click/scroll/reload/back/forward`.
  **HTTP bridge** sob `#ifndef UNIT_TEST` (`try_http_fetch`) chamando
  `http_get` → `BROWSER_IPC_FETCH_*` status. Buffer encode em `.bss`:
  `g_fetch_response_scratch[1 MiB + 4 KiB]`.
- `src/apps/browser_chrome/watchdog.c` — PING nonce + timeout
  `MAX_MISSED_PONGS = 2`.
- `src/apps/browser_chrome/fetch_resolver.c` — 3 paginas
  embutidas (`file://capyos/welcome|about|demo`) + 404 fallback,
  host-portable (usado por testes sem kernel).
- `src/kernel/browser_engine_spawn.c` — spawn /bin/capybrowser +
  cria 2 pipes + instala fds 0/1.

### 4.4 Browser app (F3.3f, F3.3h, F3.3h.2, F3.3h.3)

`src/apps/browser_app/browser_app.c` (**816 linhas**) + `url_edit.c`:

- Window compositor 480×384 com URL bar 24 px no rodape.
- Callbacks: `on_close`, `on_key`, `on_resize`, `on_mouse`, `on_scroll`.
- URL normalization (Etapa 4): auto-prefixa `http://` para dominios
  digitados sem esquema.
- URL bar syncing (Etapa 3 b): atualiza via `url_edit_set`
  quando `chrome.current_url` muda.
- Hotkeys (Etapa 3 b-polish): F5 reload, F6 back, F7 forward.
- Title propagation (Etapa 3 b-polish): chama
  `compositor_set_title(window_id, current_title)` em UPDATE_TITLE.
- Markers debugcon porta 0xE9: 20+ symbols (`[O]`, `[1]`..`[5]`,
  `[K]`, `[h]`, `[H]`, `[F]`, `[Q]`, `[q]`, `[C]`, `[S]`, `[D]`,
  `[X]`, `[W]`, `[T]`, `[R]`, `[B]`, `[f]`) para diagnose sem GDB.

### 4.5 Rede kernel-side (F3.3g, Etapa 4 a+b)

HTTP/HTTPS via stack existente — **nenhum codigo novo de rede**,
apenas ponte IPC:

- `http_get` (redirect_download.c) → segue ate 5 redirects, usa
  `http_request` (request_response.c) → TLS 1.2 via BearSSL quando
  `scheme == https`.
- Mapeamento `http_get rc` → `BROWSER_IPC_FETCH_*`:
  - 2xx → `OK (200)`
  - 404 → `NOT_FOUND`, 403 → `FORBIDDEN`, outros → `UNAVAILABLE (503)`
  - DNS/refused/timeout/TLS fail → `TRANSPORT_ERR (0)`

### 4.6 Biblioteca `libcapyhtml` (F3.3c slices 1..6)

`userland/lib/capyhtml/` — parser + render + raster + font 8x8,
tudo freestanding sem libc/stdlib. Caps: 64 nodes × ~520 B ≈ 33 KiB
por doc. Tags suportadas: h1/h2/h3, p, a (com href), div, span, ul,
li, br, hr, + skip de `<script>/<style>/<head>/<svg>/<iframe>/<object>/<embed>/<template>`.
Entidades: `&amp; &lt; &gt; &quot; &apos; &nbsp;`.

`make capyhtml-userland-syntax` compila parser.c + render.c +
font.c + raster.c com `clang -target x86_64-unknown-linux-gnu
-ffreestanding -nostdinc` sem erros. Garante "userland nunca
re-importa header de kernel".

---

## 5. Comandos de validacao canonicos

```bash
# Auditorias Python (nao dependem de cross-toolchain)
python3 tools/scripts/audit_source_layout.py --strict
python3 tools/scripts/audit_version_manifest.py
python3 tools/scripts/check_boot_perf_baseline.py --self-test

# Suite host (compilado por gcc, sem make — cross-platform)
make test                              # canonico
# Equivalente sem make (host macOS/Linux):
#   gcc -std=c99 -Wall -Wextra -Iinclude -Iuserland/include \
#       -Iuserland/lib/capyhtml/include -Itools/host/include \
#       -Ithird_party/tinf -DUNIT_TEST -g -O0 \
#       $(TEST_SRCS) -o /tmp/tests && /tmp/tests

# Syntax check cross-target (sem toolchain completo)
make capyhtml-userland-syntax

# Smoke QEMU (depende de cross-toolchain x86_64-elf-*)
make smoke-x64-browser-spawn
```

---

## 6. Criterios de aceite ja fechados

- [x] `test_browser_*` 100% verde (todos os 13 grupos listados em §3).
- [x] `make layout-audit --strict` 0 warnings (mock de pipe
      extraido para shared `.h`/`.c` em 2026-05-03 para manter
      `test_browser_chrome_runtime.c` sob 900 linhas).
- [x] `clang -target x86_64-unknown-linux-gnu -Werror=comment`
      limpo em `runtime.c`, `chrome.c`, `browser_app.c`,
      `capybrowser/main.c`, `watchdog.c`, `fetch_resolver.c`,
      codec.c, fetch.c.
- [x] IPC wire-format: 12 B header + ate 1 MiB payload, big-endian,
      magic `0xCB1B`.
- [x] Isolamento ring-3: engine spawnado via `process_create` com
      AS separado; crash do engine nao derruba o chrome; watchdog
      mata e re-arma nao automatico (politica "user re-abre pelo
      menu" para evitar respawn-storm).
- [x] Watchdog PING/PONG + timeout funcional.
- [x] Navegacao HTTP/HTTPS real (nao mais 404 em tudo).
- [x] CLICK em link abre URL do href; URL bar sincroniza.
- [x] SCROLL vertical; content_height clampado contra viewport.
- [x] BACK/FORWARD com historico; RELOAD reenvia URL atual.
- [x] EVENT_TITLE atualiza title bar da janela.
- [x] Pagina de erro HTML renderizada em falhas (4 caminhos).
- [x] URL normalization: `example.com` → `http://example.com`.

---

## 7. Dividas tecnicas documentadas (nao bloqueadoras)

- `http_response_free` chama `kfree` que e no-op no heap atual
  (bump allocator). Body leaks ~tamanho-da-pagina por fetch.
  Sera resolvido quando o kernel ganhar heap real (F6/F8).
- Body > 1 MiB e truncado (limite IPC). Chunking fica para futuro.
- Sem cookies, cache HTTP, ou pagina persistente — todos
  candidatos para Etapa 4 seções d, f e/ou Etapa 5.
- Histórico de navegacao zera a cada open do app (in-memory).
  Persistencia em `/etc/browser/history.db` fica para Etapa 5/6.
- Hit-test ignora botoes != LMB. Middle-click (abrir em nova
  aba) + right-click (menu contexto) pendentes.
- Hover: engine nao emite `EVENT_CURSOR` por mouse-move; chrome
  nao propaga mouse-move do compositor. Requer refactor de
  input_runtime.
- Modifiers de teclado (Alt+Left/Right, Ctrl+R) nao propagados pelo
  compositor (mods sempre 0 em on_key). F5/F6/F7 funcionam como
  alternativa.
- URLs path-relative (`foo.html` sem `/` inicial) nao sao
  resolvidas contra o diretorio atual. path-absolute (`/about`) e
  absolute (com `://`) funcionam.
- Scroll horizontal: conteudo largo clipa na direita.

---

## 8. Referencias de commit + arquivos-chave

Arquivos fonte criados/modificados na trilha F3 (paths absolutos
relativos ao repo root):

```
src/apps/browser_ipc/codec.c
src/apps/browser_ipc/fetch.c
src/apps/browser_chrome/watchdog.c
src/apps/browser_chrome/chrome.c
src/apps/browser_chrome/runtime.c
src/apps/browser_chrome/fetch_resolver.c
src/apps/browser_app/browser_app.c
src/apps/browser_app/url_edit.c
src/kernel/browser_engine_spawn.c
src/kernel/browser_smoke.c
userland/bin/capybrowser/main.c
userland/lib/capyhtml/include/capyhtml/{parser,render,raster,font,types}.h
userland/lib/capyhtml/src/{parser,render,raster,font}.c
include/apps/{browser_ipc,browser_chrome,browser_chrome_runtime,browser_chrome_fetch_resolver,browser_app,browser_app_url_edit,browser_dimensions,browser_watchdog}.h
include/kernel/{browser_engine_spawn,browser_smoke}.h

tests/test_browser_{ipc,ipc_fetch,watchdog,chrome,chrome_fetch,fetch_resolver,runtime_fetch,chrome_runtime,chrome_runtime_mock,e2e,app_url_edit}.c
tests/test_capyhtml_{parser,render,raster}.c
tests/test_browser_chrome_runtime_mock.{h,c}

tools/scripts/smoke_x64_browser_spawn.py
```

Para auditoria git (quando o branch for pushed):

```
git log --oneline --follow src/apps/browser_chrome/runtime.c
git log --oneline --follow userland/bin/capybrowser/main.c
```
