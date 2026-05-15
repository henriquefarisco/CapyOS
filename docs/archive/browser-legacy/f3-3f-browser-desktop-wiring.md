# Browser ring-3 — Roteiro linearizado

> Tag arqueológico: `F3.3f` (master-plan §4 fase original). Toda
> documentação nova abaixo usa o vocabulário **Etapa N / Seção a-e**
> conforme convenção §0 de `capyos-master-plan.md` (2026-05-02).

**Status atual:** 🟢 Etapa 1 ✅ + Etapa 2 ✅ (branch `feature/dev-bugfixes`, `0.8.0-alpha.6+20260503`).
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

- **Etapa 3 — HTML/CSS maturity pré-JS** (⏳ não iniciada)
  - Seção a: image rendering inline
  - Seção b: hyperlink click → navigate
  - Seção c: form inputs
  - Seção d: tabelas + listas + box model CSS
  - Seção e: scrolling vertical
  - Seção f: fonte real (não 8×8)

- **Etapa 4 — Networking real** (⏳ depende de `F4`)
  - Seção a: HTTP via `libcapy-net`
  - Seção b: HTTPS via TLS engine
  - Seção c: DNS cache integration
  - Seção d: cookies in-memory
  - Seção e: redirects depth-bounded
  - Seção f: cache `no-store` headers

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
