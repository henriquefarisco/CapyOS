# CapyOS — Status executivo

**Data:** 2026-05-03 · **Versão:** `0.8.0-alpha.6+20260503` · **Plataforma:** VMware + UEFI + E1000

> **Fonte de verdade:** [`active/capyos-master-plan.md`](active/capyos-master-plan.md).
> Este documento é o resumo navegável.
>
> **Entregas históricas** ficam em [`historical/`](historical/):
>
> - **M0–M8 + M4 final + M5 userland + W1–W3** → `historical/m4-finalization-progress.md`,
>   `historical/m5-userland-progress.md`, `historical/post-m5-ux-followups.md`.
> - **F3 browser ring-3 completo** (F3.3a..h, Etapas 1/2/3 b+e + polish + polish++,
>   Etapa 4 a+b) → [`historical/f3-browser-delivered.md`](historical/f3-browser-delivered.md).
> - **UX W7-ish completo** (start menu rounded + hover, context menu
>   module, inline_prompt module, desktop_icons module com grid +
>   click/right-click + open `.txt` + Rename via vfs_rename, file
>   manager Open/Rename/Delete/New, Save/Open no editor) ✅ 2026-05-03 →
>   [`historical/ux-w7-ish-delivered.md`](historical/ux-w7-ish-delivered.md).
> - **F4 homepage** (`browser_homepage` em system_settings, default
>   `https://wikipedia.org`, fallback automatico para
>   `file://capyos/wikipedia` quando rede falha; modulo dedicado
>   `src/apps/browser_app/homepage.c` + pagina embarcada com
>   conteudo Wikipedia-style sobre capivaras) ✅ 2026-05-03.
> - **F4 toolbar + Settings tab** (botoes Back/Forward/Reload/Home/Go
>   no URL bar do browser, Enter validado, modulo dedicado
>   `src/apps/browser_app/toolbar.c` + `nav.c`, novo tab Browser no
>   Settings app exibindo homepage configurada e hints) ✅ 2026-05-03.
> - **F4 settings-actions** (mudancas direto na UI: tema com 5
>   opcoes clicaveis em Display, layout em Keyboard, idioma em
>   Language, "Add user..." em Users com inline_prompt encadeado
>   (username -> password) gravando em `/etc/users.db`, "Edit
>   homepage" em Browser; aplicacao em runtime via update do
>   `g_shell_settings` + persistencia em `/system/config.ini`) ✅ 2026-05-03.
> - **F4 i18n GUI** (modulo `lang/app_language.c` + macro `APP_T(pt,en,es)`;
>   tabs e labels do Settings, taskbar/start menu, file_manager
>   toolbar+context+status, text_editor Save/Open, task_manager
>   tabs/Refresh/Restart/Kill, desktop_icons context menu, inline_prompt
>   hint -- todos traduzidos via `localization_select` no idioma da
>   sessao ativa) ✅ 2026-05-03.
> - **F4 minimize/maximize + cursors** (3 botoes no title bar:
>   Close [X], Maximize/Restore [□]/[][], Minimize [_]; novos campos
>   `minimized`/`maximized`/`saved_frame`/`loading`/`on_cursor_hint`
>   na `gui_window`; APIs `compositor_minimize_window`/
>   `compositor_toggle_maximize_window`/`compositor_set_cursor`; 6
>   bitmap cursores 8x12 (ARROW/TEXT/RESIZE_H/RESIZE_V/RESIZE_DIAG/
>   LOADING) com hit-test automatico em borda/canto; I-beam na URL
>   bar do browser e body do editor; ampulheta enquanto pagina
>   carrega) ✅ 2026-05-03.
>
> **Convenção de nomenclatura:** toda descrição nova usa `Etapa N` +
> `Seção a/b/c…`. `F1..F10` e `M0..M8` são tags arqueológicas.

---

## Progresso global

`[████░░░░░░] 35%` *(F1 95% + F3 99% + F4 20% de 10 fases)*

| Fase | Tema | Progresso | Status | Depende de |
|---|---|---|---|---|
| **F1** | Release `0.8.0-alpha.6` (M5 + W1/W2/W3 + F3 browser) | `[█████████░] 95%` | 🟡 bump local validado; snapshot publicado em `main`/`develop`; falta tag | — |
| **F2** | DHCP smoke VMware+E1000 + assinatura Ed25519 | `[█░░░░░░░░░] 10%` | 🔴 código existe; aguarda harness VMware | F1 |
| **F3** | Browser em processo userland + watchdog (M8.2 + W3.4) | `[█████████▉] 99%` | 🟡 Etapas 1+2 + 3 a/b/c/d/e (+ polish/polish++) + 4 a/b ✅; Etapa 3 d refinement (colspan + auto-fit) + 3 c refinement (textarea) + Etapa 5 rate limiter ✅ 2026-05-03; falta Etapa 3 a fetch/decode real + f + Etapa 5 (resto) + smoke visual QEMU | F1 |
| **F4** | Sockets userland + TLS (`libcapy-net` + `libcapy-tls`) | `[██░░░░░░░░] 20%` | 🟡 Etapa 4 a+b ✅ via bridge kernel-side (F3.3g); c/d (userland libs) pendentes | F1 (paralelo com F3) |
| **F5** | Update real via GitHub Releases (fetch + Ed25519) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F4, F2 |
| **F6** | Sessão gráfica completa (mouse, login GUI, dispatcher) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F1 (paralelo com F3/F4) |
| **F7** | Apps básicos (file_manager, text_editor, settings, image_viewer, calculator, log_viewer) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F3, F6 |
| **F8** | Package manager + SDK + ABI estável | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F4, F5, F7 |
| **F9** | JS engine sandboxed (M8.6) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F3 |
| **F10** | CapyLang (linguagem própria) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F8 |

**Legenda:** ✅ implementado · 🟡 em andamento · 🔴 não iniciado · ⛔ bloqueado

---

## O que falta fazer

Lista condensada dos próximos incrementos, agrupados por fase. Detalhe
técnico (entregáveis, critérios de aceite, validação) vive no master plan.

### F1 — Release `0.8.0-alpha.6` (5% restante)

- [x] Push do snapshot da release ao GitHub em `main` e `develop`.
- [x] CI principal verde em `main` e `develop` para o snapshot publicado.
- [ ] CI executa 6 smokes M5 (`fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh`) após o push.
- [ ] CI executa `make release-check` com toolchain `x86_64-elf-*`.
- [ ] Tag `0.8.0-alpha.6+20260503` após CI verde.

### F2 — DHCP smoke VMware + assinatura (90% restante)

- [ ] Harness `tools/scripts/smoke_x64_vmware.py` usando `govc`/`vmrun` (depende de infra externa).
- [ ] Chave Ed25519 offline + `sign_release.py` + `verify_release_signature.py`.
- [ ] Documentação `docs/security/release-signing.md` + procedimento de rotação.
- [ ] `make smoke-x64-vmware-dhcp` roda em CI.

### F3 — Browser ring-3 (3% restante)

Cada seção é independente; escolher por impacto visual:

- [x] **Etapa 3 seção a** — Imagens inline ✅ *MVP placeholder entregue 2026-05-03* (parser emite IMG node com src+alt; render emite `CMD_IMAGE` com 100×80 clampado; raster desenha placeholder cinza + borda + marcador de canto + alt text centralizado; `+33 asserts` host). Falta: fetch+decode real (IPC IMAGE_REQUEST/RESPONSE + wiring com `png_loader.c`/`jpeg_loader.c` do chrome).
- [x] **Etapa 3 seção c** — Form inputs ✅ *MVP + polish + textarea + select entregue 2026-05-03* (parser reconhece `<form action>`/`<input type/name/value/placeholder>`/`<textarea name>`/`<select name>`+`<option value>`; render emite `CMD_INPUT` com subtipo+node_idx; raster desenha caixa estilizada por subtipo (text/submit/password com `*`/textarea com altura 72 px e texto top-aligned/select com triangulo "▼"); polish: borda 2 px HEADING + caret, percent-encoding RFC 3986; engine: `g_focused_input_idx` + `hit_test_doc` + `run_key` + `run_submit`; `+58` MVP+polish + `+13` textarea + `+13` select. Falta: POST + validação client-side + cursor blink.
- [x] **Etapa 3 seção d** — Tabelas ✅ *MVP + colspan + auto-fit entregue 2026-05-03* (parser reconhece `<table>`/`<tr>`/`<td>`/`<th>` com `colspan`; `<th>` seta bold=1 para HEADING color; render lay-out grid: soma colspans da primeira TR, calcula `cell_w = avail_w / cols` com clamp em `TABLE_MIN_CELL_W` (cell overflow tolerated, raster clipa); cell emite com `w = colspan * cell_w` e avança col_index por colspan; novo `CMD_CELL`; raster desenha 1 px borda LINK + bg MUTED para TH; defesas: nested/zero-cols/excess-cells/colspan-clamp; `+41 asserts` MVP + `+22 asserts` colspan/auto-fit (7 parser + 15 render). Falta: box-model CSS inline `style=`, rowspan, scroll horizontal nativo.
- [ ] **Etapa 3 seção f** — Fonte real (TTF ou bitmap 16×16 substituindo o 8×8 atual).
- [~] **Etapa 5** — Hardening pré-JS: 🟡 IPC rate limiting + URL whitelist + bytes observability + audit log subsystem ✅ 2026-05-03 (`CHROME_RUNTIME_INCOMING_RATE_MAX = 64/tick`, status `POLL_RATE_LIMITED`; opt-in `chrome_runtime_set_url_policy(fn)`; `total_event_bytes_received` u64; novo modulo dedicado `src/apps/browser_chrome/audit_log.{c,h}` com ring buffer 32 entries + 6 categorias (NAV/RATE_DROP/POLICY_DENY/ENGINE_EOF/PROTOCOL/FETCH) e API `capyc_audit_init/record/count/visible/at`, hooks em send_navigate/poll/dispatch_pending_fetch; `+45 asserts`); falta: syscall filter de engine, memory budget enforcement, debugcon callback do audit log.
- [ ] **Validação visual QEMU**: `make smoke-x64-browser-spawn` passa
      em CI com cross-toolchain.

### F4 — Sockets userland + TLS (80% restante)

- [ ] **Seção c** — `SYS_SOCKET/BIND/CONNECT/SEND/RECV` syscalls +
      `FD_TYPE_SOCKET` + `userland/lib/capylibc-net/`.
- [ ] **Seção d** — `userland/lib/capylibc-tls/` (BearSSL recompilado como
      userland blob) + trust store userland.
- [ ] Migração do `capybrowser` do bridge kernel-side (F3.3g) para libcapy-net.
- [ ] Smoke `smoke-x64-tls-handshake` (cliente TLS em ring 3 contra httpd local).

### F5 — Update real via GitHub (100% restante)

- [ ] `update_agent_fetch_remote_manifest()` via libcapy-net + Ed25519 verifier.
- [ ] Comandos shell `update-fetch`, `update-apply`.
- [ ] Policy: canal `develop` → branch `develop` releases; `stable` → tags `v*`.
- [ ] Smoke `smoke-x64-update-fetch` com servidor HTTP local em QEMU.

### F6 — Sessão gráfica completa (100% restante)

- [ ] Loginwindow GUI (tela de senha no compositor).
- [ ] `desktop_run_frame` via APIC timer em vez de spin.
- [ ] Mouse PS/2 + USB HID fim-a-fim, dispatcher central de eventos.
- [ ] Taskbar funcional com botão Iniciar + relógio + notificações.
- [ ] Terminal gráfico consumindo shell real.
- [ ] CTRL+ALT+F1 fallback TTY.
- [ ] Smokes `smoke-x64-gui-session` + `smoke-x64-mouse-events`.

### F7 — Apps básicos (100% restante)

Cada app é um processo ring-3 isolado (lição de F3):

- [ ] `task_manager` polish (gráfico CPU/RSS).
- [ ] `text_editor` (VFS, syntax highlight básico).
- [ ] `file_manager` (CAPYFS browse/copy/move/delete).
- [ ] `settings` (network mode, theme, language, user mgmt).
- [ ] `image_viewer` standalone (PNG/JPEG/zoom).
- [ ] `calculator` (4 ops, decimal, hex).
- [ ] `log_viewer` (`/var/log/*` com filtro por nível).
- [ ] `userland/lib/libcapy-ui/` — toolkit (button, list, textbox, dialog).
- [ ] Ícones em `assets/icons/`.

### F8 — Package manager + SDK (100% restante)

- [ ] Formato `.capypkg` (tar + manifest + signature).
- [ ] `pkgd` daemon ring-3 (install/remove/list/verify).
- [ ] Repositório `capyos-packages` GitHub-hosted.
- [ ] `software-center` app GUI.
- [ ] `docs/api/abi-stable.md` documentando ABI congelada.
- [ ] `libcapy-fs`, `libcapy-ui`, `libcapy-net`, `libcapy-tls` como SOs userland.
- [ ] `capyos-sdk` repo separado com samples.

### F9 — JavaScript engine sandboxed (100% restante)

- [ ] Decisão: integrar QuickJS ou escrever CapyJS subset.
- [ ] Bridge DOM ↔ JS no `capybrowser`.
- [ ] Budget de execução (já existe infra `op_budget`).
- [ ] Smokes `js-basic`, `js-dom-mutation`, `js-timeout-killed`.

### F10 — CapyLang (100% restante)

- [ ] **Etapa 1** — parser + VM bytecode + bindings shell/FS/config (`.capyscript` interpretado).
- [ ] **Etapa 2** — módulos, FFI, integração com `libcapy-ui`.
- [ ] **Etapa 3** — stdlib, async, LSP, formatter.

---

## Critérios de aceite da release α

| | Critério | Estado |
|---|---|---|
| ✅ | Build oficial `TOOLCHAIN64=elf` passa | done |
| ✅ | Stack protector ativo na trilha de release | done |
| ✅ | ISO UEFI e disco provisionado com manifestos e checksums | done |
| ✅ | Nova instalação usa `network_mode=dhcp` por padrão | done |
| ⏳ | Boot em VMware+E1000 tenta DHCP automaticamente (smoke real) | depende de F2 |
| ✅ | `net-status` mostra modo, driver, ready, IP, gateway, DNS, erro DHCP | done |
| ✅ | `make test`, `make layout-audit`, `make all64`, `make iso-uefi` passam | done |
| ⏳ | Smoke oficial valida boot, login, persistência, DHCP, DNS, `net-fetch` em VMware+E1000 | depende de F2 |
| ✅ | Documento vivo de robustez atualizado | done |

**7 de 9 fechados;** os 2 abertos dependem do harness VMware+E1000 (F2).

---

## Validação regressiva atual (sem `make`)

Harness reproducível via `gcc` direto (cross-platform host):

| Camada | Resultado |
|---|---|
| `audit_source_layout --strict` | ✅ 0 warnings |
| `audit_version_manifest` | ✅ `0.8.0-alpha.6+20260503` alinhado |
| `check_boot_perf_baseline --self-test` | ✅ passa |
| Syntax check x86_64 freestanding `-Werror=comment` | ✅ `runtime.c`, `browser_app.c`, `capybrowser/main.c` |
| Full host suite (`TEST_SRCS`, 186 TUs) | ✅ 41 suítes numéricas, **2058 asserts numéricos** + ~35 grupos sem contagem |

Comandos canônicos:

```bash
python3 tools/scripts/audit_source_layout.py --strict
python3 tools/scripts/audit_version_manifest.py
python3 tools/scripts/check_boot_perf_baseline.py --self-test
make test                         # suite host oficial
make layout-audit                 # inclui no release-check
make capyhtml-userland-syntax     # cross-target freestanding
```

---

## Manutenção deste documento

Atualize sempre que:

- Uma fase F1–F10 muda de progresso em ≥ 10% ou muda de status.
- Uma sub-fase ativa fecha (promover para ✅ e mover detalhe para `historical/`).
- Uma versão nova for tagueada (atualizar **Versão** no topo).
- Um hotfix relevante for aplicado (registrar no master plan Apêndice B).

**Regra única:** nada promovido para ✅ sem evidência (teste host,
smoke, release note ou commit citado). Nenhum plano novo em `active/`;
consolidar no `capyos-master-plan.md` ou arquivar em `historical/`.
