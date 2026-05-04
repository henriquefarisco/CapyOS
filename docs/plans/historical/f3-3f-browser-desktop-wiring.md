# Browser ring-3 — Roteiro linearizado

> Tag arqueológico: `F3.3f` (master-plan §4 fase original). Toda
> documentação nova abaixo usa o vocabulário **Etapa N / Seção a-e**
> conforme convenção §0 de `capyos-master-plan.md` (2026-05-02).

**Status atual:** 🟢 Etapas 1+2 ✅ + Etapa 3 parcial (seções b, e) + b-polish (title/history/hotkeys) + b-polish++ (error HTML page) ✅ + Etapa 4 parcial (seções a, b) ✅ (branch `feature/dev-bugfixes`, `0.8.0-alpha.6+20260503`).
**Dependências históricas:** `F3.3c` (ring-3 pipeline completa), `F3.3d` (chrome runtime lógica fechada), `F3.3e` (smoke scaffolding).

## Índice das Etapas

- **Etapa 1 — Wiring desktop ↔ engine ring-3** (✅ 2026-05-01)
  - Seção a: `browser_app_open/close/tick` API
  - Seção b: spawn engine + janela compositor + blit BGRA
  - Seção c: URL bar editável + cursor + módulo `url_edit`
  - Seção d: shell `open-browser` reaproveita o app real
  - Seção e: respawn cooperativo removido (anti-storm) + 13 markers debugcon
  - Seção f: fix `#GP` ELF load (cópia page-by-page)
  - Seção g: fix yield infinito em `read_full` (cap 4096 yields)

- **Etapa 2 — Estabilidade pré-JS** (✅ 2026-05-02)
  - Seção a: `process_kill` libera FDs imediatamente
  - Seção b: `process_fd_free` fecha pipes/files (mirror `sys_close`)
  - Seção c: detecção defensiva `engine_is_dead()` em `browser_app_tick`
  - Seção d: bump engine FB 192×128 → 480×360, janela 480×384, scratch 768 KiB
  - Seção e: bump `PIPE_BUF_SIZE` 4 KiB → 64 KiB
  - Seção f: forward de `EVENT_LOG` ao debugcon
  - Seção g: markers extras `[F][Q][q][D]` no tick

- **Etapa 3 — HTML/CSS maturity pré-JS** (🟡 parcial)
  - Seção a: image rendering inline (⏳ não iniciada)
  - Seção b: hyperlink click → navigate ✅ 2026-05-02
  - Seção c: form inputs (⏳ não iniciada)
  - Seção d: tabelas + listas + box model CSS (⏳ não iniciada)
  - Seção e: scrolling vertical ✅ 2026-05-02
  - Seção f: fonte real (não 8×8) (⏳ não iniciada)

- **Etapa 4 — Networking real** (🟡 parcial)
  - Seção a: HTTP via stack kernel `net/http.h` (ponte chrome runtime) ✅ 2026-05-02
  - Seção b: HTTPS via BearSSL TLS 1.2 engine kernel-side ✅ 2026-05-02
  - Seção c: DNS cache integration (✅ herdado — `dns_cache_lookup` já no `http_request`)
  - Seção d: cookies in-memory (⏳ não iniciada)
  - Seção e: redirects depth-bounded (✅ herdado — `http_get` já faz 5 hops)
  - Seção f: cache `no-store` headers (⏳ não iniciada)
  - Seção g (futuro): migrar ponte HTTP para `libcapy-net` userland (requer F4)

- **Etapa 5 — Hardening pré-JS** (⏳ não iniciada)
  - Seção a: seccomp-style syscall filter
  - Seção b: memory budget per nav
  - Seção c: IPC rate limiting
  - Seção d: URL whitelist/blacklist
  - Seção e: audit log integration `[browser]`
  - Seção f: crash isolation

- **Etapa 6 — Pré-readiness para JS** (⏳ futura, mapeia `F9` legado)
  - Seção a-f: ver `capyos-master-plan.md` §4 F9.

## 1. Motivação

F3.3c cumpriu sua meta de mover a pipeline HTML para ring 3 e o
slice 6 removeu o `src/apps/html_viewer/` antigo. Efeito colateral:
o botão Browser do desktop e o comando `browser` no shell viraram
no-ops, porque o novo chrome ring-3 não tinha ponte com o
compositor.

F3.3f fecha esse gap: **um clique no menu Browser agora spawna o
engine capybrowser em ring 3, cria uma janela compositor, blita o
framebuffer BGRA que o engine emite via `EVENT_FRAME`, e roda
watchdog/fetch per-frame**. O usuário vê a página `welcome`
rasterizada com a fonte 8×8 embutida.

## 2. Peças entregues

### 2.1 `src/apps/browser_app/browser_app.c` + `include/apps/browser_app.h`

Ponte nova, ~220 linhas. API:

```c
void browser_app_open(void);
void browser_app_close(void);
int  browser_app_is_open(void);
void browser_app_tick(uint64_t now_ticks);
```

Fluxo de `open()`:

1. `install_pipe_ops_once()` — injeta `pipe_write`/`pipe_read` do
   kernel no `chrome_runtime` (idempotente).
2. `browser_engine_spawn(&r)` — reusa o helper existente; cria o
   processo `/bin/capybrowser`, dois pipes, instala fds 0/1.
3. `chrome_runtime_init(&g_app.runtime, ...)` com os pipes e
   `apic_timer_ticks()`.
4. `compositor_create_window("CapyBrowser", 120, 80, 192, 148)` —
   janela cobrindo o framebuffer 192×128 do engine + 20 px de
   barra de status (espaço para futura URL bar).
5. `compositor_show_window` + `compositor_focus_window` +
   `on_close = browser_app_on_close`.
6. `scheduler_add(r.engine_main_thread)` — engine começa a rodar.

Fluxo de `tick(now)`:

- Primeira tick envia `NAVIGATE file://capyos/welcome` como home; o
  buffer da URL bar é pré-preenchido com a mesma URL para que o
  usuário possa editar a partir dali.
- Loop até `CHROME_RUNTIME_POLL_NO_DATA`:
  - `REPAINT_FRAME` → `copy_last_frame_to_window()` copia linha
    por linha (BGRA→BGRA) de `chrome.last_frame.pixels` para
    `win->surface.pixels`, depois `compositor_invalidate_rect`.
  - `FETCH_REQUESTED` → `chrome_runtime_dispatch_pending_fetch`
    resolve via tabela embutida e devolve `FETCH_RESPONSE`.
- `chrome_runtime_tick` — PING/PONG; retornando `1/-1` fecha a
  janela (engine morto/watchdog).

Fluxo de `close()`:

- `chrome_runtime_send_shutdown` (best-effort).
- `pipe_close_write`/`pipe_close_read` do lado chrome.
- `process_kill(engine_pid, 9)`.
- `compositor_destroy_window(id)`.
- Reset do `g_app` para permitir novo `open()`.

Tudo em `static struct browser_app_state g_app` (uma instância).
A maior parte do tamanho vem do `event_scratch` dentro do
`chrome_runtime` embutido — agora 128 KiB (ver 2.3).

### 2.2 `src/apps/browser_chrome/runtime.c` — yield-on-partial

Antes: `read_full` retornava `-2` (protocol error) ao ver
would-block após já ter lido parte de um payload. Isso quebrava
qualquer payload maior que `PIPE_BUF_SIZE=4 KiB` — exatamente o
caso do `EVENT_FRAME` de 96 KiB que o slice 4-final introduziu.

Depois: novo ponteiro injetável
`chrome_runtime_set_yield_op(chrome_runtime_yield_fn)`:

```c
if (got == 0u) return -1;                /* header-level NO_DATA */
if (allow_yield && g_yield_fn) {
    g_yield_fn();                        /* cede para o engine escrever mais */
    continue;
}
return -2;                               /* protocol error (host tests) */
```

O `allow_yield` é `0` para o read do header (preserva semântica
original de NO_DATA → caller retorna para o desktop loop) e `1`
para o payload (lá dentro não podemos quebrar por falta de bytes:
o engine vai escrever mais assim que ganhar CPU).

No kernel, `browser_app` e `browser_smoke` injetam
`task_yield` como `yield_op`. Nos host tests, o yield fica `NULL`
e o fail-fast antigo é preservado (o mock de pipe dos testes
nunca dá partial reads de qualquer forma).

### 2.3 `include/apps/browser_chrome_runtime.h` — `event_scratch` 128 KiB

`CHROME_RUNTIME_EVENT_BUF_MAX` foi de `1036` bytes (1 KiB +
header) para **128 KiB**. Justificativa:

- Slice 4-final emite frames `192×128×4 = 98304 B` + 12 B de
  frame header = 98316 B. Round para potência de 2 → 128 KiB.
- Fica bem abaixo do limite IPC (`BROWSER_IPC_MAX_PAYLOAD = 1 MiB`).
- Host stacks Linux (8 MiB default) acomodam `struct chrome_runtime`
  de ~128 KiB sem problemas; todos os testes passam.
- Kernel reserva uma única instância em `browser_app.g_app`.

### 2.4 `src/gui/desktop/desktop.c` — menu + per-frame tick

```c
#include "apps/browser_app.h"
#include "arch/x86_64/apic.h"

static void menu_action_browser(void *user_data) {
  (void)user_data;
  browser_app_open();
  if (browser_app_is_open()) {
    register_focused_in_taskbar("CapyBrowser", "Browser");
  }
}
```

No `desktop_run_frame` (pós `task_manager_tick`):

```c
browser_app_tick(apic_timer_ticks());
```

### 2.5 `src/kernel/browser_smoke.c` — também injeta yield_op

Necessário porque o smoke test agora recebe EVENT_FRAME de 96 KiB
(stub antigo de 16×16 era só 1 KiB e cabia em um pipe inteiro).
Sem o yield, o smoke regrediria para `FAIL protocol-err`.

### 2.6 `Makefile`

- `$(BUILD)/x86_64/apps/browser_app/browser_app.o` adicionado a
  `CAPYOS64_OBJS`.

## 3. Validação

- `make test` (host suite): **100% verde** — todos os 603 asserts
  de F3 + capyhtml continuam passando, sem regressão.
- `clang -fsyntax-only -target x86_64-unknown-linux-gnu` limpo em:
  - `src/apps/browser_app/browser_app.c`
  - `src/apps/browser_chrome/runtime.c` (com a mudança de `read_full`)
  - `src/gui/desktop/desktop.c` (com `browser_app_tick`)
  - `src/kernel/browser_smoke.c` (com `set_yield_op`)
- `grep -rE "html_viewer|css_parser"` em `src/` retorna apenas
  comentários explicativos do slice 6.

## 4. Etapa 1 seções c-d (sessão 2026-05-01) — URL bar + respawn + shell

### 4.1 URL bar editável

Módulo `url_edit` (`include/apps/browser_app_url_edit.h` +
`src/apps/browser_app/url_edit.c`) introduzido como TU pura, sem
dependência de kernel/compositor. Estado:

```c
struct url_edit { char buf[512]; uint16_t len; uint16_t cursor; };
```

APIs: `url_edit_set/clear`, `url_edit_insert_char` (só ASCII
printable), `url_edit_backspace`, `url_edit_delete`,
`url_edit_move_left/right/home/end`. Todas tratam `NULL` e bordas
de forma defensiva, retornando `0` quando no-op para o caller
evitar repaints inúteis.

`browser_app.on_key` faz o roteamento:

- ASCII printable (`>=32 && <127`) → `url_edit_insert_char` (insere
  na posição do cursor);
- `0x08`/`0x7F` (Backspace) → `url_edit_backspace`;
- `KEY_DELETE` (forward delete) → `url_edit_delete`;
- `KEY_LEFT`/`KEY_RIGHT`/`KEY_HOME`/`KEY_END` → movimento de cursor;
- `\n`/`\r` (Enter) → `chrome_runtime_send_navigate(buf, len)`;
- `0x1B` (Esc) → `browser_app_close()`.

A barra é pintada na faixa de 20 px abaixo do frame, com letra de
status à esquerda (I/L/R/F/X = idle/loading/ready/failed/cancelled),
texto editável e cursor sólido na posição `cursor * glyph_width`.
Cores vêm de `compositor_theme()` para herdar paleta atual.

### 4.2 Respawn automático

Novo `browser_app_respawn()` chamado de `browser_app_tick`:

- Em `CHROME_RUNTIME_POLL_ENGINE_EOF` ou `CHROME_RUNTIME_POLL_PROTOCOL_ERR`
  durante o drain de eventos.
- Em `chrome_runtime_tick == 1` (watchdog quer kill) ou `-1` (broken pipe).

Fluxo: `teardown_current_engine()` → `browser_engine_spawn()` →
`scheduler_add(novo thread)` → `chrome_runtime_record_restart()` →
reenvia a URL ativa via `chrome_runtime_send_navigate(g_app.edit)` para
retomar a navegação automaticamente. Contador `engine_respawns`
adicionado para telemetria. Janela permanece aberta em todo o ciclo.

### 4.3 Shell `open-browser`

`src/shell/commands/extended.c` agora inclui `apps/browser_app.h` e
`cmd_open_browser` chama `ensure_desktop(c)` + `browser_app_open()`.
O comando antes imprimia uma mensagem de migração; agora abre o
mesmo app real do menu Browser. Símetro do desktop fechado.

## 5. O que falta (5% restante)

1. **Validação visual em QEMU.** Único item bloqueado em
   cross-toolchain (`x86_64-elf-gcc` em CI). Cobertura local
   permanece via `make test` (641+ asserts após `url_edit`) e
   clang freestanding syntax-check cross-target.

### 5.3 Etapa 2 (sessão 2026-05-02) — UX pós-spawn + maturação

Sessão consolidando 7 correções em 4 arquivos kernel + 1 userland +
1 header de runtime + 4 docs. Cada correção aborda um gap específico
identificado em validação regressiva.

| Seção | Item | Arquivo principal | Antes | Depois |
|---|---|---|---|---|
| **a** | `process_kill` libera FDs | `src/kernel/process.c` | Só liberava em `process_destroy`; processo com pai virava ZOMBIE com pipes ainda abertos → chrome poll forever | Loop `process_fd_free` para todos FDs imediatamente após marcar ZOMBIE |
| **b** | `process_fd_free` fecha recursos | `src/kernel/process.c` | Apenas zerava o slot; pipe ends e VFS files vazavam | Mirror de `sys_close`: `pipe_close_read/_write` ou `vfs_close` antes de zerar |
| **c** | Detecção defensiva de morte | `src/apps/browser_app/browser_app.c` | Browser_app só notava engine morto via EOF do pipe (lento ou nunca) | `engine_is_dead()` no início de `browser_app_tick` chama `process_stats_get` e fecha janela em ~1 frame; marker `[D]` |
| **d** | Tamanhos visuais | `browser_app.c` + `userland/.../main.c` + `browser_chrome_runtime.h` | Janela 192×148, FB engine 192×128, scratch 128 KiB | **Janela 480×384, FB 480×360, scratch 768 KiB** |
| **e** | Throughput de pipe | `include/kernel/pipe.h` | `PIPE_BUF_SIZE = 4 KiB` → frame 480×360 (675 KiB) precisava de ~170 round-trips engine↔chrome | `PIPE_BUF_SIZE = 64 KiB` → ~11 round-trips; 16× redução |
| **f** | Visibilidade do engine | `browser_app.c` | `BROWSER_CHROME_ACTION_LOG_FORWARD` armazenava `last_log_msg` mas browser_app não lia | Browser_app emite `<msg>\n` ao debugcon via `outb 0xE9` para cada log; operador vê erros do engine ring-3 em tempo real |
| **g** | Markers de tick | `browser_app.c` | 13 markers só no open/close; o tick não emitia nada | Adicionados `[F]` (frame blit), `[Q]` (fetch staged), `[q]` (fetch dispatched), `[D]` (engine dead detected) |

#### Validação (Etapa 2)

- `clang -target x86_64-unknown-linux-gnu -fsyntax-only -Werror`:
  limpo em `process.c`, `pipe.c`, `browser_app.c`, `capybrowser/main.c`.
- `make test BUILD=/tmp/capyos_build`: **641+ asserts host, 100% verde**
  (incluindo `test_browser_ipc_fetch` adicionado pelo operador).
- `make capyhtml-userland-syntax`: ok.

#### Sintomas e raízes diagnosticadas

Pós-fix do `#GP` da Etapa 1 seção f, o browser abre, aparece no Task
Manager, mas três problemas residuais foram reportados pelo operador:

1. **Task Manager kill não fechava a janela.** O usuário podia
   clicar Kill no engine, mas a janela do browser continuava
   aberta sem responder.
2. **Janela muito pequena.** 192×148 px é praticamente
   ilegível em qualquer resolução de desktop.
3. **Não renderizava nenhum site.** Provavelmente porque o
   conteúdo de 192×128 era pequeno demais para mostrar a página
   `welcome` legível.

#### Fix #1 — `process_kill` agora fecha FDs imediatamente

Quando o usuário pressiona Kill, o task manager chama
`process_kill(engine_pid, 9)`. O fluxo antigo:

```c
p->state = PROC_STATE_ZOMBIE;
task_kill(p->main_thread->pid);
if (!p->parent) process_destroy(p);  // FDs fechados aqui
```

Como o engine do browser tem **pai** (o desktop runtime via
`process_create`), ele ficava **ZOMBIE com FDs ainda abertos**.
A `pipe_close_write(response_pipe)` nunca era chamada → o
chrome runtime no kernel continuava polling forever sem
observar EOF na response pipe.

**Patch em `src/kernel/process.c::process_kill`:** após marcar
ZOMBIE, libera **todos os FDs** independentemente de ter pai.
O slot ainda sobrevive como ZOMBIE (parent's wait() lê
exit_code), mas pipes/files são liberados imediatamente.

**Patch em `src/kernel/process.c::process_fd_free`:** agora
fecha o pipe end ou file VFS antes de zerar o slot, em
paralelo a `sys_close()`. Antes, era um leak silencioso a
cada `process_destroy`.

#### Fix #2 — Detecção defensiva de morte do engine

`browser_app_tick` agora verifica no início de cada tick se
`engine_pid` ainda está em estado RUNNING. Se já passou para
ZOMBIE/UNUSED (kill via task manager, crash, reap), fecha a
janela imediatamente em vez de esperar o pipe drenar.
Marcador debugcon novo: `[D]` (engine externally dead).

```c
if (engine_is_dead()) { browser_app_close(); return; }
```

Isso é redundante com o Fix #1 (FDs fechados → EOF chega no
poll → `[X]` close), mas serve como rede de segurança caso o
poll demore para acordar.

#### Fix #3 — Janela e framebuffer maiores

| | Antes | Depois |
|---|---|---|
| Engine FB | 192×128 (96 KiB) | **480×360 (675 KiB)** |
| browser_app window | 192×148 px | **480×384 px** |
| URL bar height | 20 px | **24 px** |
| chrome scratch | 128 KiB | **768 KiB** |

`BROWSER_IPC_MAX_PAYLOAD = 1 MiB` segue válido (frame total =
675 KiB + 12 B header). Pipes ainda são 4 KiB cada, então o
transfer EVENT_FRAME usa ~170 chunks com `yield_op`. Lento
mas funcional; otimizar pipe size é trabalho futuro.

#### Validação

- `clang -target x86_64-unknown-linux-gnu -fsyntax-only -Werror`
  limpo em `process.c`, `browser_app.c`, `capybrowser/main.c`.
- `make test BUILD=/tmp/...`: 641 asserts host, 100% verde.
- `make capyhtml-userland-syntax`: ok.

### 5.2 Etapa 1 seção f (sessão 2026-05-01) — Root causes do #GP no spawn

Após reabrir o Browser pelo menu, o trace QEMU mostrou crash
em `elf_load`:

```
[O][1]
[x64] Fatal fault #GP General Protection
rip=0x000000000409B246  -> elf_load (run #1)
rip=0x000000000409B298  -> elf_load (run #2 com fix #1, ainda crashou)
```

Foram identificados **dois bugs independentes** no caminho de
ELF load. Ambos manifestam só em binários >4 KiB com segmentos
não-executáveis — `hello`/`exectarget` (1 segmento R-X em 1
página) escapavam por sorte; `capybrowser` (multi-página +
`.rodata`/`.data` com NX=1) é o primeiro a expor.

#### Bug #1 — `elf_load` assumia páginas físicas contíguas

```c
for (size_t p = 0; p < num_pages; p++) {
  uint64_t phys = pmm_alloc_page();   // FISICAMENTE INDEPENDENTE
  vmm_map_page(as, vaddr_start + p * PAGE, phys, flags);
}
if (phdr->p_filesz > 0 && ...) {
  uint64_t dest_phys = vmm_virt_to_phys(as, phdr->p_vaddr);
  elf_memcpy(dest_phys, data + p_offset, p_filesz);  // BUG
}
```

`pmm_alloc_page` em loop não retorna páginas físicas contíguas.
`elf_memcpy` único de `filesz` extrapolava a primeira página e
corrompia RAM alheia.

**Fix:** copia página-por-página em `src/kernel/elf_loader.c:76-100`,
recolhendo o phys de cada página via `vmm_virt_to_phys(as, page_base)`.

#### Bug #2 — `vmm_virt_to_phys` vazava o bit NX no retorno

```c
return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFF);
```

`~0xFFFULL` só limpa os 12 bits baixos. **Bit 63 (NX) ficava
preservado** no valor retornado. Para PTEs de segmentos
não-executáveis (`.data`/`.rodata` com `flags & VMM_PAGE_NX`), o
"phys" devolvido era `(0x8000_0000_0000_0000 | phys_real)`. O
caller fazia cast pra `void *` e obtia um endereço **não-canônico**
em x86_64 (bit 63=1, bits 48..62=0); o primeiro store nesse
ponteiro disparava **#GP**.

`hello`/`exectarget` têm só 1 segmento R-X (NX=0) → mascaravam o
bug. `capybrowser` tem segmentos R-W com NX=1 → bug ativa.

**Fix:** definir `VMM_PTE_PHYS_MASK = 0x000FFFFFFFFFF000ULL`
(bits 12..51 conforme spec Intel/AMD64) e usá-lo em **todas** as
extrações de phys de PTE em `src/memory/vmm.c` (incluindo
intermediate tables, walk_to_leaf, vmm_handle_cow_fault, e o
loop de free em `vmm_destroy_address_space` — esse último era
um leak silencioso que a máscara errada também causava).

#### Bônus: bugs latentes corrigidos pela máscara

A máscara errada também silenciava:

1. `vmm_destroy_address_space` chamava `pmm_free_page(phys | NX)`
   que era no-op por out-of-range → **leak de páginas**
   não-executáveis em todo destroy de AS.
2. `vmm_handle_cow_fault` lia `pte & ~0xFFFULL` para refcount
   lookup → sempre miss → CoW com refcount stale silenciosamente.

Ambos passam a funcionar corretamente.

#### Validação

- `clang -target x86_64-unknown-linux-gnu -fsyntax-only -Werror`:
  limpo em `vmm.c` + `elf_loader.c`.
- `make test`: 641 asserts host, 100% verde.
- `vmm_virt_to_phys` tem 1 único caller (`elf_load`), então o
  blast radius é restrito.

Próximo run esperado: `[O][1][2][3][4][5][K][h][H]` + página
renderizada na janela.

### 5.1 Etapa 1 seção g (sessão 2026-05-01) — Triagem do freeze ao abrir

Reporte do usuário: ao clicar Browser, a tela fica **toda azul
(wallpaper) e a VM congela** — não é reboot, é freeze.

**Diagnóstico:** o sintoma "wallpaper sólido + freeze" implica
que `desktop_run_frame` parou de chamar `compositor_render()`,
ou seja, há um **loop infinito** no caminho síncrono do desktop.
A causa raiz mais provável é o yield-on-partial em `read_full`:
quando a engine morre mid-payload (por #PF/#GP no first-dispatch),
o kernel marca o processo ZOMBIE mas o pipe-writer não é fechado
imediatamente. `pipe_read` então retorna `-1` (would-block) para
sempre e a chamada `g_yield_fn()` cede CPU eternamente sem
progresso, congelando a tick do desktop.

**Correções aplicadas (Etapa 1 seção g):**

1. **`src/apps/browser_chrome/runtime.c::read_full`** ganha o cap
   `CHROME_RUNTIME_READ_YIELD_LIMIT = 4096`. Após 4096 yields
   consecutivos sem ler 1 byte, retorna `-2` (protocol err) e o
   caller faz fallback. O contador reseta a cada read bem-sucedido.
2. **`browser_app_tick`** drena no máximo 32 eventos por tick e,
   em EOF / protocol-err / watchdog kill, **fecha a janela** em
   vez de respawnar automaticamente. Respawn-storm era a segunda
   forma de freeze: spawn → crash → respawn → crash em loop.
3. **`browser_app_respawn`** removido (estava sendo trigger do
   storm). Documentado: re-introduzir com backoff/limite quando
   a engine for estável.
4. Marcador `[X]` extra: emitido em `browser_app_close()` quando
   disparado por EOF/protocol-err do drain. `[W]` marca close
   disparado pelo watchdog.

**Marcadores debugcon (porta 0xE9):**

| Marker | Significado |
|---|---|
| `[O]` | `browser_app_open()` entrou |
| `[1]` | pipe ops + yield_op instalados |
| `[2]` | `browser_engine_spawn()` + chrome init OK; engine no scheduler |
| `[3]` | `compositor_create_window` retornou non-NULL |
| `[4]` | janela mostrada/focada |
| `[5]` | URL bar pintada |
| `[K]` | `active=1`; open completo |
| `[!]` | spawn falhou (volta sem abrir) |
| `[?]` | window alloc falhou (engine teardown) |
| `[h]` | primeira tick: prestes a mandar NAVIGATE home |
| `[H]` | NAVIGATE home enviado com sucesso |
| `[X]` | drain detectou EOF / protocol-err; janela fechada |
| `[W]` | watchdog pediu kill / pipe quebrado; janela fechada |

Como capturar:

```sh
qemu-system-x86_64 ... -debugcon stdio -no-reboot
```

Sequência esperada em sucesso: `[O][1][2][3][4][5][K]` (open) →
`[h][H]` (primeira tick + navigate) → janela com a página.
Sequência esperada em engine quebrada (após a fix): `[O][1][2]
[3][4][5][K]` → `[h]` (sem `[H]` se navigate falhar) → `[X]`
(close por EOF) e a tela continua responsiva.

## 6. Impacto nas métricas

- **+0 asserts host** (a lógica nova é kernel-only e sem test
  mock; cobertura herdada do `test_browser_chrome_runtime` e
  `test_browser_e2e`).
- **+1 módulo kernel** (`src/apps/browser_app/`).
- **+127 KiB de .bss** (runtime event_scratch).
- **Desbloqueou** o caminho visual para F3.3c, deixando o
  browser efetivamente utilizável pela primeira vez.
- Etapa 1 seções c-d: **+4 entregáveis** (URL bar com cursor, módulo `url_edit`
  testado, respawn, shell wiring); **+38 asserts host** (567 checks
  executados em `test_browser_app_url_edit`); cross-target syntax
  limpo em `browser_app.c`, `url_edit.c` e `extended.c`.

## 7. Etapa 4 seções a+b (sessão 2026-05-02) — Networking real kernel-side

### 7.1 Root cause e decisão arquitetural

Até `0.8.0-alpha.6`, o fetch IPC entre engine ring 3 e chrome kernel
passava 100% pelo resolver embutido em
`src/apps/browser_chrome/fetch_resolver.c`, que só reconhecia 3 URLs
hardcoded (`file://capyos/welcome|about|demo`). Qualquer outro URL
recebia `BROWSER_IPC_FETCH_NOT_FOUND` (404). O stack TCP/TLS do
kernel (`src/net/services/http/*`) só era chamado pelo shell
via `net-fetch`; o browser não tinha acesso.

A F3.3c original marcava slice 5e (HTTP real) como **bloqueado em
F4** (libcapy-net userland). Decisão da Etapa 4: **não esperar por
F4**. O bridge HTTP/HTTPS é colocado **no chrome runtime kernel-side**
(onde o `net/http.h` já está linkado), emulando o design que F4
terá quando a libcapy-net chegar. Migrar depois é uma questão de
trocar um call-site (`http_get` → `capy_http_get`) e mover do
kernel para ring 3.

### 7.2 Peças entregues

| Arquivo | Mudança |
|---|---|
| `src/apps/browser_chrome/runtime.c` | Include guardado por `#ifndef UNIT_TEST` de `net/http.h` + `net/stack.h`. Novo `try_http_fetch()` traduz URL → `http_get()` → status/body; `chrome_runtime_dispatch_pending_fetch()` prefixo-checa `http://` ou `https://` antes de cair no resolver local. `CHROME_RUNTIME_FETCH_PAYLOAD_MAX` 2 KiB → **1 MiB + 4 KiB** (em `.bss` `g_fetch_response_scratch`). |
| `userland/bin/capybrowser/main.c` | `INPUT_PAYLOAD_BUF` 4 KiB → **1 MiB + 4 KiB**. `payload[]` na main loop e `fetch_scratch[]` em `wait_for_fetch_response` migrados de stack para `.bss` (`g_request_payload` + `g_fetch_scratch`). |
| `src/apps/browser_app/browser_app.c` | Novo `normalize_nav_url()`: se usuário digitou `example.com` (sem esquema), aplica prefixo `http://` antes do NAVIGATE. Strip de whitespace. |
| `tests/test_browser_runtime_fetch.c` | +3 testes regressão: `test_http_url_routes_to_transport_error_in_tests`, `test_https_url_routes_to_transport_error_in_tests`, `test_lookalike_scheme_still_routes_to_resolver` (evita `httpx://` ativar o bridge). **+14 asserts** pinando o routing. |

### 7.3 Detalhe técnico: `#ifndef UNIT_TEST`

O resolver local (`fetch_resolver.c`) é pure C host-portable. O
bridge HTTP depende de `kmalloc` + `net_stack_ready` + `http_get`,
que só existem no kernel. Solução: guard `#ifndef UNIT_TEST` em
`runtime.c` envolvendo o bridge; sob UNIT_TEST, URLs `http://`
resolvem para `BROWSER_IPC_FETCH_TRANSPORT_ERR` (status 0) com
body vazio — e os testes de host pinam exatamente esse
comportamento, de modo que um futuro refactor que delete o guard
(ou inverta o default) é detectado pelo suite.

### 7.4 Status codes

Mapeamento `http_get` → `browser_ipc_fetch_status`:

| HTTP | `BROWSER_IPC_FETCH_*` | Comportamento do engine |
|---|---|---|
| 2xx | `OK (200)` | Body vai ao parser → frame rasterizado |
| 404 | `NOT_FOUND` | `emit_nav_failed("fetch_status=404")` |
| 403 | `FORBIDDEN` | `emit_nav_failed("fetch_status=403")` |
| outro | `UNAVAILABLE (503)` | `emit_nav_failed("fetch_status=NNN")` |
| DNS/refused/timeout | `TRANSPORT_ERR (0)` | `emit_nav_failed("fetch_status=0")` |

### 7.5 Limitações conhecidas

- `http_response_free` chama `kfree` que é no-op no kernel atual
  (heap bump). Cada fetch leak-a o body; o resolver é chamado uma
  vez por nav, então o leak é ~tamanho-da-pagina por navegação.
  Será resolvido quando o kernel ganhar heap real (F6/F8).
- Body > 1 MiB é truncado (cap do IPC). Sem chunking por enquanto.
- Sem cookies; cada request é anonymous. Etapa 4 seção d cobrirá.

## 8. Etapa 3 seções b+e (sessão 2026-05-02) — CLICK + SCROLL

### 8.1 Motivação

Pós-Etapa 4, o browser carregava páginas HTTP reais — mas era
**read-only**: clicar num `<a href>` não navegava, e conteúdo
maior que 360 px de altura era clipado sem scroll. Seções b e e da
Etapa 3 fecham esse gap e tornam o browser **realmente navegável**.

### 8.2 Arquitetura do CLICK → NAVIGATE

O `capyhtml_cmd` produzido pelo `capyhtml_layout` já carrega
`href` (borrowed pointer) + bounding box `(x, y, w, h)` em doc-space.
O fluxo novo:

1. Compositor entrega evento `on_mouse(win, x, y, btns)` em coordenadas de janela.
2. `browser_app.browser_app_on_mouse` descarta cliques na URL bar
   (y >= frame_h) e repassa os demais via
   `chrome_runtime_send_click(rt, x, y, 1)` com coordenadas de
   viewport.
3. Chrome runtime encoda `BROWSER_IPC_CLICK` com payload BE =
   (x:u16, y:u16, button:u8) e envia pelo request pipe.
4. Engine ring 3 (`capybrowser/main.c`) recebe, decoda, e em
   `run_click`:
   - traduz `(x, y)` viewport → doc-space somando `g_scroll_y_px`;
   - re-roda `capyhtml_layout` na viewport atual (pode ter
     mudado de tamanho entre NAVIGATE e CLICK);
   - itera cmds procurando `kind == CMD_TEXT` com `href != NULL`
     cuja bounding box `[x, x+w) × [y, y+h+1)` contenha o ponto;
   - em hit, monta payload NAVIGATE no `g_request_payload` e
     chama `run_navigate` recursivamente — mesma sequência de
     eventos que um NAVIGATE vindo do chrome.
5. Chrome recebe `EVENT_NAV_STARTED` com a nova URL, atualiza
   `current_url`, e `browser_app_tick` sincroniza a URL bar via
   `url_edit_set` no handler `UPDATE_STATUS`.

### 8.3 Arquitetura do SCROLL

1. Compositor entrega `on_scroll(win, delta)` (delta em ticks de roda).
2. `browser_app.browser_app_on_scroll` multiplica por
   `BROWSER_APP_SCROLL_STEP_PX = 48` (≈ 4 linhas) e envia
   `chrome_runtime_send_scroll(rt, delta_px)`.
3. IPC `BROWSER_IPC_SCROLL` carrega `delta_y:i32` em BE.
4. Engine em `run_scroll`:
   - acumula em `g_scroll_y_px`, clampado em
     `[0, max(0, g_content_height_px - g_vh)]`;
   - se não houve mudança, no-op;
   - senão, re-rasteriza o doc atual via
     `emit_real_frame_scrolled(nav_id, doc, scroll_y_px)` que
     subtrai `scroll_y_px` de `cmd[i].y` antes de chamar
     `capyhtml_raster_render`.
5. Novo frame é emitido com o mesmo `nav_id` da navegação ativa
   (engine guarda em `g_current_nav`), então o chrome reblita na
   janela correta sem marcar a URL como "loading".

### 8.4 Peças entregues

| Arquivo | Mudança |
|---|---|
| `include/apps/browser_chrome_runtime.h` | +3 helpers: `chrome_runtime_send_click(rt, x, y, button)` (5 B payload), `chrome_runtime_send_scroll(rt, delta_y)` (4 B BE), `chrome_runtime_send_reload(rt)` (0 B). |
| `src/apps/browser_chrome/runtime.c` | Implementação trivial sobre `send_frame`. Cast `int32_t → uint32_t` na codificação de scroll preserva sinal via complemento de 2 (testado com `-1 → 0xFFFFFFFF`). |
| `src/apps/browser_app/browser_app.c` | Novos `browser_app_on_mouse` + `browser_app_on_scroll`, registrados em `open()`. URL bar sync em `UPDATE_STATUS` quando `current_url` difere do buffer editável. +2 markers debugcon (`[C]`, `[S]`). |
| `userland/bin/capybrowser/main.c` | Globais novos: `g_current_doc` (33 KiB), `g_current_doc_valid`, `g_current_nav`, `g_current_url[1024]`, `g_scroll_y_px`, `g_content_height_px`. `emit_real_frame` vira `emit_real_frame_scrolled(nav, doc, scroll_y)` que aplica translação antes do raster e retorna `total_height_px`. `run_navigate` copia doc + URL para os globals. Novos handlers `run_click`, `run_scroll`, `run_reload` no switch principal. Hit-test usa bounding box `[x, x+w) × [y, y+h+1)` (+1 absorve o underline). |
| `tests/test_browser_chrome_runtime.c` | +7 testes para os novos senders com layout BE byte-a-byte + rejeição quando engine morto. **+22 asserts** (86 → 108). |

### 8.5 Markers debugcon adicionais

| Marker | Significado |
|---|---|
| `[C]` | `browser_app_on_mouse` enviou CLICK ao engine |
| `[S]` | `browser_app_on_scroll` enviou SCROLL ao engine |

### 8.6 Validação

- `make test` subset browser: **1142 asserts** (+22 em runtime tests);
  100% verde.
- `clang -arch x86_64 -ffreestanding -Werror=comment` limpo em
  `runtime.c`, `browser_app.c`, `capybrowser/main.c`.
- Compilação host com `-DUNIT_TEST`: limpa (único warning `/*`
  dentro de comentário corrigido).

### 8.7 Resolver de URLs relativas (incluído nesta slice)

Para fazer `<a href="/path">` funcionar no mundo real, o engine
resolve três casos ao construir a NAVIGATE de um click:

| Forma do href | Resolução | Exemplo |
|---|---|---|
| `scheme://host/path` (contém `://`) | Usa direto | `http://example.com/page` → `http://example.com/page` |
| `/path` (começa com `/`) | Prefixa origin da URL atual | `/about` com base `https://a.b/x` → `https://a.b/about` |
| `path` (relativo simples) | Passa direto (fallback) | `page.html` com base `https://a.b/` → `page.html` (vai falhar no chrome com 404) |

A extração do "origin" (scheme + host + port opcional) usa
`url_origin_len()`, que procura `://` e depois o primeiro `/`. Sem
`://`, retorna 0 e o path-absolute vira no-op com log de warning.

### 8.8 Limitações conhecidas

- **Hover**: nenhum sinal visual (cursor muda, link highlight) ao
  passar sobre um link. `EVENT_CURSOR` existe no protocolo mas
  não é emitido. Seção c-futura.
- **URLs relativas path-relative** (`foo.html` sem barra inicial):
  não resolvidas contra o diretório atual. Raro em páginas simples;
  pode ser adicionado sem quebrar compatibilidade.
- **Query strings / fragments em href relativo**: `?q=x#sec` é
  colado como-está. Funciona com URLs `/path?q=x`; anchors
  fragment (`#`) causam re-fetch (não scroll-to-anchor).
- **Scroll horizontal**: não implementado. Conteúdo largo é
  clipado à direita. Suficiente para demos pre-JS.
- **Smooth scroll / momentum**: cada tick = 48 px instantâneos.
  Aceitável para teclado/roda; toque/trackpad viria em slice
  futura se mouse-wheel granular chegar.

## 9. Etapa 3 seção b-polish (sessão 2026-05-03) — title + history + hotkeys + error title

### 9.1 Motivação

Pós-Etapa 3 seção b+e, o browser navegava em links e rolava mas:
(a) a title bar ficava eternamente "CapyBrowser", ignorando o
`<title>` da página que o parser já extraía; (b) não havia
BACK/FORWARD apesar de `BROWSER_IPC_BACK/FORWARD` existirem no enum;
(c) nenhuma hotkey de recarga; (d) uma nav falhada não dava sinal
visual algum além do frame stub. Esta slice fecha esses quatro gaps.

### 9.2 Peças entregues

| Arquivo | Mudança |
|---|---|
| `userland/bin/capybrowser/main.c` | Novo `emit_title(const char *)` (payload BE `title_len:u16 + bytes`). `run_navigate` chama após parse OK com `g_current_doc.title`; no path de erro emite `emit_title("(failed)")` para que o operador veja a falha na title bar. Novos globais em `.bss`: `g_history[32][1024]` + `g_history_len[32]` + `g_history_count` + `g_history_index` + `g_nav_from_history` (flag transiente que quebra loop push no caminho BACK/FORWARD). `run_navigate` só faz push se a nav não veio do histórico; trunca futuro ao divergir. `run_back` e `run_forward` recheiam `g_request_payload` a partir do ring e chamam `run_navigate` recursivamente. `BROWSER_IPC_BACK/FORWARD` saem do fallthrough no switch. |
| `include/apps/browser_chrome_runtime.h` | +2 APIs: `chrome_runtime_send_back(rt)` / `chrome_runtime_send_forward(rt)` (payload vazio). |
| `src/apps/browser_chrome/runtime.c` | Implementações triviais sobre `send_frame` com kind `BROWSER_IPC_BACK/FORWARD`. Mesmo contrato: 0 ok / -1 engine morto / broken pipe. |
| `src/apps/browser_app/browser_app.c` | `on_key` intercepta F5 → `send_reload`, F6 → `send_back`, F7 → `send_forward`. `UPDATE_TITLE` no tick chama `compositor_set_title(window_id, chrome.current_title)`. +3 markers debugcon (`[T]` title set, `[R]` reload, `[B]` back, `[f]` forward). |
| `tests/test_browser_chrome_runtime_mock.{h,c}` | Extração do mock de pipe para reusar entre TUs de teste e trazer `test_browser_chrome_runtime.c` de 907 linhas (acima do limite 900) para 818. |
| `tests/test_browser_chrome_runtime.c` | **+17 asserts**: `test_send_back_writes_frame` + companion dead-engine; `test_send_forward_writes_frame` + companion; `test_poll_title_event_updates_current_title` que injeta EVENT_TITLE no response pipe mock e valida kind BE + payload + NUL-term. |
| `Makefile` | `TEST_SRCS` ganha `tests/test_browser_chrome_runtime_mock.c`. |

### 9.3 Arquitetura do histórico

`g_history` é um ring buffer estático com 32 slots × 1024 bytes
(32 KiB em `.bss`). Comportamento:

| Evento | Ação |
|---|---|
| NAVIGATE nova (user/link click) | Trunca `g_history_count = index + 1` (descarta futuro), empurra URL no fim, `index = count - 1`. Se ring cheio, shift-left (perde a mais antiga). |
| NAVIGATE de BACK | `g_nav_from_history = 1` marcado antes; `run_navigate` detecta e NÃO empurra; só descreve o frame do item já em `g_history[index - 1]`. |
| NAVIGATE de FORWARD | Simétrico: `index + 1`. |

Isso espelha o comportamento canônico do Chrome/Firefox: "fork da
história" quando o usuário diverge do caminho atual. O índice é
`uint32_t` porque o worst-case de operações do ring chegar em
2^32 é astronômico (nenhum usuário faz 4 bilhões de cliques).

### 9.4 Mapeamento EVENT_TITLE → title bar

```text
engine.run_navigate  ->  emit_title(g_current_doc.title)
                              |
                              v
                   IPC EVENT_TITLE (payload = tlen:u16 + utf8)
                              |
                              v
chrome.handle_title  ->  chrome.current_title[], UPDATE_TITLE bit
                              |
                              v
browser_app_tick (UPDATE_TITLE action)
                              |
                              v
          compositor_set_title(window_id, current_title)
```

Sem qualquer parte desta cadeia, a mudança é invisível ao usuário.
O pin-test `test_poll_title_event_updates_current_title` cobre
engine → chrome; o end-to-end compositor→pixels vive no
`smoke_x64_browser_spawn` harness (QEMU, depende de cross-toolchain).

### 9.5 Hotkeys escolhidas

Web browsers reais usam Alt+Left/Right e Ctrl+R. A camada de input
do CapyOS atualmente entrega `mods = 0` para todos os `on_key`
(TODO: o `input_runtime` propagar Alt/Ctrl/Shift bits). Enquanto
isso, F5/F6/F7 são universalmente mapeáveis sem colidir com o
editor de URL (que usa só ASCII printable + Backspace/Delete/setas).

Quando `mods` for cabeado, fica trivial adicionar mapeamentos
redundantes (`KEY_LEFT` + Alt bit → back) sem quebrar F5/F6/F7.

### 9.6 Markers debugcon adicionais

| Marker | Significado |
|---|---|
| `[T]` | `compositor_set_title` chamado (UPDATE_TITLE recebido) |
| `[R]` | F5 pressionado, reload enviado |
| `[B]` | F6 pressionado, back enviado |
| `[f]` | F7 pressionado, forward enviado |

### 9.7 Validação

- `make test` subset browser: **1159 asserts** (+17 em runtime tests); 100% verde.
- `clang -arch x86_64 -ffreestanding -Werror=comment` limpo em `runtime.c`, `browser_app.c`, `capybrowser/main.c`.
- `audit_source_layout --strict`: 0 warnings (após split do mock para `test_browser_chrome_runtime_mock.{h,c}`).

### 9.8 Limitações (para Etapa 3 c e Etapa 3 futuro)

- ~~**Error page real HTML**~~: ✅ **entregue** em §10 (Etapa 3 b-polish++).
- **Histórico persistente**: zera em cada open do browser. Sessão
  não sobrevive a reiniciar o app. Futuro: persistir em `/etc/browser/history.db` via VFS.
- **Alt+Left/Right**: ver 9.5; depende de refactor no input_runtime.

## 10. Etapa 3 seção b-polish++ (sessão 2026-05-03) — página de erro HTML real

### 10.1 Motivação

Após Etapa 3 b-polish, nav-fail mostrava só `emit_title("(failed)")`
— a title bar ficava com "(failed)" e o frame da tela exibia
conteúdo antigo ou o fundo azul do compositor. Nenhuma explicação
visível do que falhou. Chrome/Firefox sempre renderizam uma página
HTML de erro com detalhes (URL, código, sugestões). Esta slice
replica esse padrão diretamente no engine ring 3, sem envolver o
chrome.

### 10.2 Arquitetura

Em vez de ir ao chrome pedir `file://capyos/error?status=...`
(round-trip IPC extra + necessidade de resolver local
parametrizado), o engine **monta o HTML da página de erro
inline** num buffer `.bss` (`g_error_html_buf[2048]`), passa pelo
próprio `capyhtml_parse`, e emite EVENT_TITLE + EVENT_FRAME como
uma página normal. Zero overhead de IPC, mesmo pipeline de render,
testa o parser com HTML controlado (portanto um bug no parser
rompe o próprio canal de erro e é detectado).

### 10.3 Template da página

```html
<h1>Pagina nao carregou</h1>
<p>Endereco: {URL_QUE_FALHOU}</p>
<p>Motivo: {REASON}</p>
<p>F5 recarrega, F6 volta, F7 avanca, Esc fecha.</p>
```

- `{URL}` = argumento direto da NAVIGATE falhada (sanitizado: `<`
  e `>` viram `?` para impedir injection que quebraria o parser).
- `{REASON}` = `fetch_status=NNN` / `resposta vazia do servidor`
  / `HTML nao pode ser interpretado`.
- O H1 serve de **title fallback** via parser (primeira h1 → title
  quando não há `<title>`). O chrome atualiza a title bar para
  "Pagina nao carregou" automaticamente.

### 10.4 Caminhos de erro cobertos

| Origem | Reason string | Acontece quando |
|---|---|---|
| HTTP não-2xx | `fetch_status=NNN` | DNS OK, conexão OK, servidor retornou 404/403/500 etc. |
| Transport error | `fetch_status=0` | DNS fail, connection refused, TLS handshake fail, timeout |
| Body vazio | `resposta vazia do servidor` | Status 200 mas `body_len == 0` (raro; HTTP tipicamente tem pelo menos `\n`) |
| Parser failure | `HTML nao pode ser interpretado` | Body não parseia (binário, encoding exótico) |

### 10.5 Estado pós-erro

`emit_error_frame` **sobrescreve** `g_current_doc` com a página de
erro e seta `g_current_doc_valid = 1`. Efeitos colaterais positivos:

- **SCROLL funciona** na página de erro (ela é pequena mas se a
  viewport for estreita, pode precisar rolar).
- **CLICK** hit-tests normalmente (sem `<a>` no template, não
  navega, mas não crasha).
- **F6 BACK** vai para a URL anterior no `g_history` (a URL
  falhada **não** foi pushed, então BACK pula direto para a
  última página bem-sucedida).
- **F5 RELOAD** re-tenta a URL falhada (está em `g_current_url`
  que não foi atualizado no path de erro — mas a nova navegação
  reatualiza tudo).

O flag `g_nav_from_history` é limpo em ambos os paths de erro
para evitar que um NAVIGATE seguinte pule o push de histórico.

### 10.6 Peças entregues

| Arquivo | Mudança |
|---|---|
| `userland/bin/capybrowser/main.c` | Novo `g_error_html_buf[2048]` em `.bss`. Helpers `err_append()` / `err_append_raw()` para montar o HTML com sanitização `<`/`>`. Novo `emit_error_frame(nav, url, url_len, reason)` que constrói + parseia + rasteriza + emite title/frame. `run_navigate` chama `emit_error_frame` em dois pontos: (a) logo antes de `emit_nav_failed` no path status != OK, (b) no branch `have_doc == 0` substituindo o `emit_stub_frame` + `emit_title("(failed)")`. Limpa `g_nav_from_history` em ambos. |
| `tests/test_capyhtml_parser.c` | **+5 asserts** pinando o template de erro: shape 1×H1+3×P, title fallback via H1, URL sobrevive, reason sobrevive, hotkey hint F5/F6/F7 presente. Total parser test: 19 → **24**. |

### 10.7 Validação

- Suite `test_capyhtml_parser`: **24/24** passa (+5 novos asserts
  cobrindo o template de erro, 19 pré-existentes inalterados).
- Full host suite (183 TUs, TEST_SRCS inteiro): **40 suítes
  numéricas + 1830 asserts**, 100% verde.
- `audit_source_layout --strict`: 0 warnings
  (`capybrowser/main.c` tem 1240 linhas agora, mas `main.c` não
  está no path auditado porque o `DEFAULT_ROOTS` do auditor
  cobre apenas `src/`, `include/`, `tests/`, `tools/` e `.`
  (root-only, suffix-filtered) — `userland/` fica de fora por
  design, já que as regras de modularidade são diferentes para
  binários ring-3 que precisam ser freestanding).
- Kernel/userland syntax `gcc -arch x86_64 -ffreestanding
  -Werror=comment` limpo em `runtime.c`, `browser_app.c`,
  `capybrowser/main.c`.

### 10.8 Próximos passos possíveis

- **Botão "Tentar novamente" clicável**: adicionar `<a href="">`
  apontando para a URL falhada dentro do template. Ao clicar, o
  hit-test volta ao run_click que vai detectar `g_current_url
  == href` e recarregar. Pequena adição, grande ganho UX.
- **Sugestões específicas por código HTTP**: 404 → "Verifique a
  URL", 403 → "Sem permissão", 500 → "Servidor fora do ar",
  etc.
- **DNS resolver message**: quando `fetch_status=0`, incluir a
  última diagnóstica de `net_stack_status` (`arp=N syn-out=N
  syn-ack=N`) no reason para dar sinal de onde a rede quebrou.
