# CapyOS — Master Plan único e linear

**Data de referência:** 2026-05-01
**Versão atual:** `0.8.0-alpha.4+20260429` (alpha.5 aguarda CI dos smokes M5)
**Plataforma oficial:** `VMware + UEFI + E1000`
**Branch ativa:** `feature/dev-bugfixes`
**Substitui:** `capyos-robustness-master-plan.md`, `system-master-plan.md`, `system-roadmap.md`, `system-execution-plan.md`, `capyos-master-improvement-plan.md`, `browser-status-roadmap.md`, `source-organization-roadmap.md`, `m5-userland-progress.md`, `post-m5-ux-followups.md` (todos arquivados em `historical/` em 2026-05-01).

---

## 1. Propósito deste documento

Este é o **único plano vivo** do CapyOS. Ele consolida:

1. O que já foi entregue (M0–M8 do plano antigo de robustez, M4 finalization, M5 userland, W1–W3 UX).
2. O que falta — em forma de **fases lineares numeradas (F1…F10)**, cada uma com escopo finito, dependências explícitas, critério de aceite verificável e plano de execução.
3. A visão de longo prazo (pós-F10) sem prometer datas.

Regras de manutenção:

- **Toda mudança relevante atualiza este documento e o `STATUS.md`.**
- Fases só promovem para `Implementado` com **evidência** (host test + smoke + commit).
- Nenhuma feature de produto começa sem fechar a fase anterior, exceto quando explicitamente marcada como `paralelo possível`.
- Nenhum plano novo pode ser criado em `active/` sem reconciliar com este. `historical/` recebe planos finalizados ou substituídos.

---

## 2. Estado atual consolidado

### 2.1 Entregue (não volta a este plano como tarefa)

| Bloco | Status | Evidência principal |
|---|---|---|
| **M0** Governança / matriz de suporte / gates de release | ✅ 100% | `make release-check`; `docs/reference/driver-support-matrix.md` |
| **M1** Build `TOOLCHAIN64=elf` + stack-protector + checksums + version-audit | ✅ 100% | `make all64 TOOLCHAIN64=elf`; `make iso-uefi`; `make version-audit` |
| **M3** Layout estrito (≤ 900 linhas, headers `internal/`, audit cruzado) | ✅ 100% | `make layout-audit --strict` |
| **M4** Processos + scheduler preemptivo + CoW + TSS/RSP0 + ring-3 preemption (31/31 fases) | ✅ 100% | `historical/m4-finalization-progress.md`; suítes `test_vmm_anon_regions`, `test_vmm_cow`, `test_pmm_refcount`, `test_user_task_init`, `test_context_switch`; smokes `smoke-x64-preemptive-all` |
| **M5-perf** Métricas de boot, baseline, `perf-*` commands, op_budget genérico | ✅ 100% | `docs/performance/boot-baseline.json`; `make boot-perf-baseline-selftest` |
| **M5-userland** fork/exec/wait/pipe + capysh ring 3 + isolamento de crash | ✅ 95% (CI pendente) | A.1–F.4 entregues; smokes `fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh` (aguardam CI) |
| **M6.1** Política de senha + lockout | ✅ 100% | `tests/test_auth_policy.c` |
| **M6.2** Auditoria persistente (`[auth]`/`[net]`/`[update]`/`[recovery]`/`[priv]`/`[browser]`/`[capyfs-journal]`) | ✅ 100% | `tests/test_audit_events.c` |
| **M6.3** API centralizada de privilégios | ✅ 100% | `src/auth/privilege.c`; `tests/test_privilege.c` |
| **M6.5** CAPYFS journal v2 autenticado (HMAC-SHA256) | ✅ 100% | `tests/test_journal.c` |
| **M7** WAL + replay + fsck + recovery cause + update transacional | ✅ 100% | `tests/test_journal`, `test_capyfs_check`, `test_capyfs_journal_cause`, `test_update_transact` |
| **M8.1** Browser com estado formal + strict mode + isolation hooks | ✅ 100% | `tests/html_viewer/navigation_cases.inc` |
| **M8.3** Budgets parse/render/external + `nav_op_budget` cooperativo | ✅ 100% | `tests/html_viewer/resource_cases.inc` |
| **M8.4** DNS cache TTL + HTTP cache com budget + `no-store` | ✅ 100% | `tests/test_dns_cache`; `tests/html_viewer/resource_cases.inc` |
| **M8.5** `about:network` / `about:memory` | ✅ 100% | `tests/html_viewer/resource_cases.inc` |
| **W1** TTY polish (`clear` context-aware via callback) | ✅ 100% (2026-04-30) | `src/shell/core/shell_main/output_files.c`; `userland/bin/capysh/main.c` |
| **W2** Task manager auto-refresh + Kill button | ✅ 100% (2026-04-30) | `src/apps/task_manager.c`; `src/gui/desktop/desktop.c` |
| **W3 core** Browser parser yield + timeout 30s + per-frame async drain | ✅ 100% (2026-04-30) | `src/apps/html_viewer/html_parser.c`; `src/apps/html_viewer/async_runtime.c` |

### 2.2 Pendente (entra como fase neste plano)

| Origem | Item | Vira fase |
|---|---|---|
| M2.1–M2.5 | DHCP no smoke oficial VMware+E1000 | **F2** |
| M5.G.4 / G.5 | Release notes + tag `0.8.0-alpha.5` + master-plan promote | **F1** |
| M6.4 | Assinatura ponta-a-ponta dos checksums + smoke VMware | **F2** (acopla) |
| M8.2 + W3.4 | Browser em processo userland isolado + watchdog | **F3** |
| Sistema | Stack sockets userland + TLS p/ apps | **F4** |
| Sistema | Update real via GitHub Releases (fetch + Ed25519) | **F5** |
| Sistema | Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher) | **F6** |
| Sistema | Apps básicos (file_manager, text_editor, settings, image_viewer) | **F7** |
| Sistema | Package manager + SDK + ABI estável | **F8** |
| M8.6 | JS engine sandboxed | **F9** |
| Sistema | CapyLang (linguagem própria) | **F10** |

### 2.3 Métricas globais

- **Progresso rumo a "SO sólido de uso geral":** ~30% (era 18% no plano antigo; M4+M5+M7+W1–W3 fecharam grandes blocos).
- **Cobertura de testes host:** 7/7 suites principais + op_budget + privilege + buffer_cache_pacing + 110+ asserts M4 + 84 asserts M5.
- **Smokes QEMU em CI:** 4 preemptivos + 6 M5 (aguardando push) + boot + ISO + CLI + capysh.
- **Lacunas estruturais críticas:** rede sockets userland, TLS de apps, package manager, GUI fim-a-fim, JS engine.

---

## 3. Princípios arquiteturais (inalteráveis)

1. **Não expandir feature de produto sem fronteira clara entre kernel e userland.** M5 entregou a fronteira — toda nova feature deve respeitá-la.
2. **Toda capacidade nova nasce observável, testável e com critério de rollback.**
3. **Update automático faz parte da arquitetura, não é script tardio.**
4. **GUI não acopla ao boot path.** Sessão gráfica é serviço, não inicialização.
5. **Linguagem própria só após ABI, package manager, toolkit gráfico e SDK estáveis.**
6. **Plataforma oficial é `VMware + UEFI + E1000`.** Hyper-V e QEMU são laboratório.
7. **Branches:** `main` = canal `stable`; `develop` = canal `develop`. Update agent trata como trilhas distintas.
8. **Layout estrito:** ≤ 900 linhas por arquivo C/teste, headers `internal/` para fronteiras privadas, `make layout-audit --strict` em CI.

---

## 4. Plano linear de entregas (F1 → F10)

Cada fase tem o mesmo formato:

```
### Fx — Nome
**Criticidade:** alta / média / baixa
**Depende de:** lista
**Paralelo possível com:** lista
**Risco:** texto
**Esforço estimado:** sessões/sprints
**Branch sugerida:** nome

#### Objetivo
#### Entregáveis (E1, E2, …)
#### Critérios de aceite (checkbox)
#### Como será feito (passos técnicos)
#### Validação (commands, suites, smokes)
#### Saída esperada / DoD
```

---

### F1 — Release `0.8.0-alpha.5` (consolidação M5 + W1/W2/W3)

**Status:** 🟡 Em andamento (2026-05-01) — bump local concluído; falta CI + tag.
**Criticidade:** alta — destrava M8.2 e o ciclo seguinte.
**Depende de:** branch `feature/continue-development` (M5+W1+W2+W3 já merge no HEAD).
**Paralelo possível com:** F2 (smoke VMware) — diferentes superfícies.
**Risco:** baixo — código já entregue, falta apenas validação CI + bookkeeping de release.
**Esforço:** 1 sessão.
**Branch sugerida:** `release/0.8.0-alpha.5`.

#### Progresso 2026-05-01

- ✅ E1.4 — `VERSION.yaml`, `include/core/version.h` e `README.md` bumped para `0.8.0-alpha.5+20260501`.
- ✅ E1.5 — Release note `docs/releases/capyos-0.8.0-alpha.5+20260501.md` escrita.
- ✅ E1.6 — Diretório `docs/screenshots/0.8.0-alpha.5/` criado com `README.md` placeholder (PNGs aguardam captura pós-CI verde).
- ✅ E1.8 — Master plan atualizado (este bloco).
- ✅ Validações locais passaram: `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`.
- ⏳ E1.1 — Push do branch para o GitHub (pendente — usuário decide quando).
- ⏳ E1.2 — CI roda os 6 smokes M5 (pendente — depende de E1.1 + cross-toolchain `x86_64-elf-*`).
- ⏳ E1.3 — `make release-check` em CI (host local não tem cross-compiler).
- ⏳ E1.7 — Tag `0.8.0-alpha.5+20260501` (pendente — após E1.2 + E1.3 verdes).

#### Objetivo

Fechar o release alpha.5 consolidando:

- M5 userland (fork/exec/wait/pipe + capysh + isolamento de crash) — já 95%.
- W1+W2+W3 UX (clear, task manager, browser responsiveness) — já 100%.
- Promover M4.1–M4.5 e marcar M8.2 como "pronto para iniciar" no master plan.

#### Entregáveis

- **E1.1** — Push de `feature/dev-bugfixes` ao GitHub, abertura de PR contra `develop`.
- **E1.2** — CI executa **6 smokes M5**: `fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh`. Todos verdes.
- **E1.3** — CI executa **release-check**: `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`, `make all64 TOOLCHAIN64=elf`, `make iso-uefi TOOLCHAIN64=elf`, `make release-checksums`, `make verify-release-checksums`.
- **E1.4** — Bump em `VERSION.yaml`, `include/core/version.h`, `README.md` para `0.8.0-alpha.5+<YYYYMMDD>`.
- **E1.5** — Release notes em `docs/releases/capyos-0.8.0-alpha.5+<date>.md` cobrindo: SYS_FORK/EXEC/WAIT/PIPE, capysh ring 3, isolamento de crash, W1/W2/W3, parser yield + timeout do browser.
- **E1.6** — Screenshots `docs/screenshots/0.8.0-alpha.5/` (capysh prompt, task manager Kill button, browser carregando com cancel).
- **E1.7** — Tag `0.8.0-alpha.5+<date>`.
- **E1.8** — Atualização deste master plan: F1 → ✅, M8.2 entra em F3.

#### Critérios de aceite

- [ ] PR `feature/dev-bugfixes` → `develop` mergeado.
- [ ] Os 6 smokes M5 passam em CI.
- [ ] `make release-check` passa em CI.
- [ ] `make version-audit` valida `VERSION.yaml` ↔ headers ↔ README ↔ screenshots ↔ release note.
- [ ] Release note publicado em `docs/releases/`.
- [ ] Tag git criada e pushed.
- [ ] STATUS.md atualizado com `0.8.0-alpha.5+<date>`.

#### Como será feito

1. `git push origin feature/dev-bugfixes`.
2. Abrir PR descrevendo W1+W2+W3.
3. Aguardar CI verde (host tests + 6 smokes M5 + release-check).
4. Mergear em `develop`.
5. Em `develop`, rodar `tools/scripts/audit_version_manifest.py` localmente; bumpar versões.
6. Escrever release note seguindo o template de `docs/releases/capyos-0.8.0-alpha.4+20260429.md`.
7. Capturar screenshots no QEMU (`make all64 && qemu-system-x86_64 ... -display gtk`).
8. Tag + push.
9. Mover `m5-userland-progress.md` e `post-m5-ux-followups.md` para `historical/` (já feito em 2026-05-01 como parte deste plano consolidado).

#### Validação

```bash
make release-check
make smoke-x64-fork-cow
make smoke-x64-exec
make smoke-x64-fork-wait
make smoke-x64-pipe
make smoke-x64-fork-crash
make smoke-x64-capysh
make version-audit
```

#### Saída esperada

`0.8.0-alpha.5` publicado. M4.1–M4.5 oficialmente "Implementado". M8.2 (browser isolado) entra como F3.

---

### F2 — DHCP no smoke oficial VMware+E1000 + assinatura de checksums

**Criticidade:** alta — fecha M2 (75% → 100%) e M6.4.
**Depende de:** F1 (release base limpo); harness VMware acessível.
**Paralelo possível com:** F3 (browser isolado) — superfícies independentes.
**Risco:** médio — depende de infraestrutura VMware externa que pode não estar disponível em CI público.
**Esforço:** 2–3 sessões.
**Branch sugerida:** `feature/m2-vmware-smoke`.

#### Objetivo

Validar que uma instalação nova em VMware+E1000:

- Boota via UEFI sem intervenção.
- Obtém DHCP automaticamente.
- Resolve DNS e faz fetch HTTP/HTTPS no `net-fetch`.
- Diagnostica falha de lease em `net-status`.

E publicar release com **checksums assinados** (Ed25519) — não só hash.

#### Entregáveis

- **E2.1** — Harness `tools/scripts/smoke_x64_vmware.py` que orquestra `vmrun`/`govc` para subir VM, capturar serial console, validar markers de boot.
- **E2.2** — Variante do `smoke_x64_cli.py` parametrizável para o harness VMware (driver=`e1000`, network=`bridged`, BIOS=`uefi`).
- **E2.3** — `make smoke-x64-vmware-dhcp` — alvo agregador.
- **E2.4** — Documentação `docs/testing/vmware-e1000-smoke.md` cobrindo setup, expectativas, troubleshooting.
- **E2.5** — Chave Ed25519 offline gerada + `tools/scripts/sign_release.py` que assina `build/release-artifacts.sha256` e emite `.sig`.
- **E2.6** — `tools/scripts/verify_release_signature.py` consumido por `verify-release-checksums`.
- **E2.7** — Documento `docs/security/release-signing.md` com procedimento de chave + rotação.

#### Critérios de aceite

- [ ] Em VMware+E1000, boot novo obtém DHCP em ≤ 10s sem comando manual.
- [ ] `net-status` mostra IP, gateway, DNS, sem `dhcp_last_error`.
- [ ] `net-fetch http://example.org` retorna 200 OK.
- [ ] `net-fetch https://example.org` valida TLS e retorna 200 OK (somente após F4 entregar TLS userland; aceite parcial: HTTP funciona, HTTPS reservado para F4).
- [ ] `make smoke-x64-vmware-dhcp` passa em CI.
- [ ] `release-check` produz `release-artifacts.sha256` + `.sig` válidos.
- [ ] `verify-release-signature.py` rejeita assinatura mutilada (teste negativo).
- [ ] M2.1–M2.5 promovidos para ✅ no master plan.
- [ ] M6.4 promovido para ✅.

#### Como será feito

1. Provisionar template VMware (kernel + ISO atual).
2. Escrever harness Python usando `govc` (free, headless).
3. Capturar log serial via `Serial.fileType=file`.
4. Adicionar markers explícitos no kernel: `[dhcp] lease ack ip=...` e `[net] ready` para o harness verificar.
5. Gerar chave Ed25519 com `openssl genpkey` ou `ssh-keygen -t ed25519`. Documentar onde fica armazenada (offline).
6. Adaptar `release-checksums` para emitir `.sig` no fim.
7. CI roda `verify-release-signature` antes de aceitar a tag.

#### Validação

```bash
make smoke-x64-vmware-dhcp
make release-check                   # agora inclui assinatura
python3 tools/scripts/verify_release_signature.py build/release-artifacts.sha256 build/release-artifacts.sha256.sig
```

#### Saída esperada

M2 fecha; M6.4 fecha. Todos os 9 critérios de aceite da release α passam.

---

### F3 — Browser em processo userland isolado + watchdog (M8.2 + W3.4)

**Status:** 🟡 Em andamento — F3.3a concluída (2026-05-01).
**Criticidade:** alta — fecha o sintoma "navegador derruba o sistema" definitivamente.
**Depende de:** F1 (M5 mergeado), porque usa `fork+exec+pipe` reais.
**Paralelo possível com:** F2 (testes vs. infra), F4 (sockets userland — pode evoluir junto).
**Risco:** alto — primeiro app real em ring 3 com IPC bidirecional bidirecional ao desktop.
**Esforço:** 4–6 sessões.
**Branch sugerida:** `feature/f3-browser-isolation`.

#### Progresso

- ✅ **F3.3a — Protocolo IPC** (2026-05-01):
  - `docs/architecture/browser-ipc.md` — especificação completa (header BE, 11 kinds de request, 10 kinds de event, fluxos canônicos, watchdog, restrições de segurança, versionamento).
  - `include/apps/browser_ipc.h` — header binário compartilhado kernel/userland (magic `0xCB1B`, 21 kinds, struct header de 12 bytes, API de codec/validação).
  - `src/apps/browser_ipc/codec.c` — encode/decode big-endian + validação de kind/payload_len + classificação request/event.
  - `tests/test_browser_ipc.c` — 162 asserts (round-trip de 21 kinds, layout BE byte-a-byte, rejeição de magic/kind/payload/buffer/NULL, classificação, min_payload).
- ✅ **F3.3b — Stub `capybrowser` ring 3** (2026-05-01):
  - `userland/bin/capybrowser/main.c` — engine ring 3 que faz IPC ponta-a-ponta sobre fd 0/1: NAVIGATE → NAV_STARTED + 3× NAV_PROGRESS (fetch/parse/render) + EVENT_FRAME (16×16 BGRA solido) + NAV_READY; PING → PONG; CANCEL → NAV_CANCELLED; SHUTDOWN → exit; outros kinds drenam payload silenciosamente.
  - `Makefile` — regras `CAPYBROWSER_ELF/OBJS/BLOB_OBJ` espelhando o pattern hello/exectarget/capysh; `src/apps/browser_ipc/codec.c` recompilado como objeto userland (USERLAND_CFLAGS) e linkado no engine.
  - `src/kernel/embedded_progs.c` — `/bin/capybrowser` registrado; setter de teste `embedded_progs_test_set_capybrowser`.
  - `include/kernel/embedded_progs.h` — documentação atualizada com `/bin/capybrowser` e `/bin/capysh`.
  - `tests/test_embedded_progs.c` — +4 asserts cobrindo lookup do capybrowser e distinção dos demais blobs.
  - `make test` ainda 100% verde (166 asserts no test_browser_ipc + test_embedded_progs); `make layout-audit` sem warnings; `clang -fsyntax-only` limpo no main.c e codec.c.
  - Build do ELF do binário pendente de cross-toolchain (`x86_64-elf-gcc/ld`) — validável em CI via `make capybrowser-elf`.
- ⏳ **F3.3c** — migração do `html_viewer` para userland (parser HTML/CSS, image decoders, fetch HTTP).
- ✅ **F3.3d — Chrome scaffolding + watchdog (camada lógica)** (2026-05-01):
  - ✅ `include/apps/browser_watchdog.h` + `src/apps/browser_chrome/watchdog.c` — máquina de estado pura (PING/PONG/timeout/restart) totalmente desacoplada de syscalls; tempo injetado pelo caller.
  - ✅ `tests/test_browser_watchdog.c` — 49 asserts cobrindo init, intervalo de PING, PONG matching/wrong-nonce/sem-ping, tick antes/depois de timeout, kill após `MAX_MISSED_PONGS`, restart limpo, alloc_nonce monotônico com wrap, PONG quebrando streak, KILL bloqueando PONG tardio, args NULL.
  - ✅ Política configurável via constantes (`PING_INTERVAL=5s`, `PONG_TIMEOUT=1s`, `MAX_MISSED_PONGS=2`).
  - ✅ `include/apps/browser_chrome.h` + `src/apps/browser_chrome/chrome.c` — estado do chrome (status/nav_id/url/title/erro/last_frame/watchdog) + `browser_chrome_dispatch_event()` que consome eventos IPC já decodificados e devolve bitmask de ações (`REPAINT_FRAME`/`UPDATE_TITLE`/`UPDATE_STATUS`/`LOG_FORWARD`/`PROTOCOL_ERR`); helpers de geração de payload `NAVIGATE` e bookkeeping de seq monotônico com wrap.
  - ✅ `tests/test_browser_chrome.c` — 66 asserts cobrindo init, NAVIGATE-sent, dispatch de TITLE/NAV_STARTED/PROGRESS/READY/FAILED/CANCELLED/FRAME/PONG/LOG, validação de stride/total/stale-nav-id, rejeição de kind de request como evento, payload curto, args NULL, bookkeeping de telemetria.
  - ✅ Stale frame detection: dispatcher descarta NAV_PROGRESS/READY/FAILED/CANCELLED/FRAME com `nav_id != current_nav_id` (sem ação retornada).
  - ✅ PONG roteado automaticamente para `c->watchdog`.
  - ✅ `include/apps/browser_chrome_runtime.h` + `src/apps/browser_chrome/runtime.c` — orquestração entre dispatcher e pipes do kernel: send_navigate/cancel/shutdown/ping com encode automático e bookkeeping no chrome+watchdog; poll_event que lê header+payload, valida, despacha e devolve mascara de ações + status (NO_DATA / EVENT_HANDLED / ENGINE_EOF / PROTOCOL_ERR); tick que avança watchdog, dispara PING quando devido e sinaliza kill ao caller; record_restart que troca pipes/pid e limpa frame antigo. Pipe ops injetáveis via `chrome_runtime_set_pipe_ops()` para teste host (em produção apontam para `pipe_write`/`pipe_read` do kernel).
  - ✅ `tests/test_browser_chrome_runtime.c` — 61 asserts cobrindo init com pid 0/válido, send_navigate escrevendo header BE no pipe, send com engine morto/broken-pipe, poll com NO_DATA/EOF/EVENT_HANDLED/PROTOCOL_ERR, PONG roteado para watchdog, tick disparando PING no intervalo, tick retornando kill após `MAX_MISSED_PONGS`, tick retornando -1 em broken pipe, record_restart limpando frame, payload grande demais rejeitado, sem pipe ops fail-safe, FRAME 2x2 com pixels validados, drenagem sequencial de múltiplos eventos.
  - ✅ `include/kernel/browser_engine_spawn.h` + `src/kernel/browser_engine_spawn.c` — helper kernel-side `browser_engine_spawn(out)` que (1) resolve `/bin/capybrowser` em `embedded_progs`, (2) valida ELF, (3) cria 2 pipes, (4) cria processo via `process_create`, (5) carrega ELF via `elf_load_into_process`, (6) instala fds 0 (read end do request_pipe) e 1 (write end do response_pipe) na tabela do engine, (7) devolve `(request_pipe_id, response_pipe_id, engine_pid, engine_proc, engine_main_thread)` ao caller (que decide quando fazer `scheduler_add`). Cleanup completo em qualquer caminho de falha (5 códigos de erro). Linkado em `CAPYOS64_OBJS`; compilação validada via `clang -fsyntax-only -Iinclude` (limpo).
  - ✅ `tests/test_browser_e2e.c` — **47 asserts** de integração end-to-end exercitando o stack inteiro (codec + dispatcher + watchdog + runtime) com pipes mockados e um "fake engine" que espelha exatamente o `userland/bin/capybrowser/main.c`. Cenários: navegação feliz (NAVIGATE → 6 eventos → READY → SHUTDOWN → EOF), watchdog PING/PONG, watchdog timeout até kill request, cancel durante navegação, crash do engine após NAV_STARTED, restart com `record_restart` limpando frame antigo, lixo no pipe gerando PROTOCOL_ERR, duas navegações back-to-back, PONG não solicitado tolerado.
  - ⏳ Widget compositor real (URL bar, blit de `last_frame.pixels` no framebuffer) — entregue em F3.3c+widget (depende de F3.3c trazer pixels reais via parser HTML).
  - ⏳ Restart UI no compositor (mensagem "Browser reiniciado") — entregue em F3.3c+widget.
- 🟡 **F3.3e — Smoke kernel-side `smoke-x64-browser-spawn`** (2026-05-01, scaffolding completo):
  - ✅ `include/kernel/browser_smoke.h` + `src/kernel/browser_smoke.c` — `kernel_boot_run_browser_smoke()` (noreturn) que spawna o engine via `browser_engine_spawn`, conecta `pipe_write`/`pipe_read` à runtime, arma o engine main_thread via `user_task_arm_for_first_dispatch`, cria a kernel task `browser-poller` e adiciona ambas ao scheduler. O poller envia NAVIGATE → drena eventos → após READY envia PING → após PONG envia SHUTDOWN → detecta EOF → `[browser-smoke] OK`. Boot CPU bloqueia em `hlt`; APIC tick (preemptive) intercala poller (ring 0) e engine (ring 3).
  - ✅ `src/arch/x86_64/kernel_main.c` — gating `#ifdef CAPYOS_BOOT_RUN_BROWSER_SMOKE` chama o helper (1 linha) após o preemptive scheduler armado.
  - ✅ `Makefile` — target `smoke-x64-browser-spawn` que faz `clean` + `all64` com `-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_BROWSER_SMOKE` + `iso-uefi` + `manifest64` + dispara o harness.
  - ✅ `tools/scripts/smoke_x64_browser_spawn.py` — harness QEMU/UEFI espelhado em `smoke_x64_hello_user.py`. Valida 9 markers determinísticos no debugcon (`spawn pid=`, `navigate-sent`, `event NAV_STARTED`, `event FRAME`, `event NAV_READY`, `event PONG`, `shutdown-sent`, `engine-eof`, `OK`) com timeout 45s; rejeita em `panic` ou `[browser-smoke] FAIL`.
  - ✅ Validação local: `clang -fsyntax-only -target x86_64-unknown-linux-gnu` limpo em `browser_smoke.c`; `make test` 100% verde; `make layout-audit` sem warnings; `python3 -m py_compile` no harness sem erros.
  - ⏳ Execução real depende de cross-toolchain `x86_64-elf-*` (não disponível no host macOS atual). CI com toolchain executa o alvo fim-a-fim; sucesso fecha F3.3e.
  - ⏳ Smokes adicionais `smoke-x64-browser-isolation` (kill manual + restart) e `smoke-x64-browser-watchdog` (engine congelado → kill por timeout) — entregues em F3.3c.

A migração do parser HTML/CSS para userland (F3.3c) continua sendo o próximo passo bloqueante. Quando entregue, o capybrowser passa a renderizar pixels reais a partir do parse, o widget compositor pode blittar `last_frame.pixels` em uma janela real, e os smokes específicos de isolamento/watchdog podem ser exercitados com cenários representativos.

#### Objetivo

Mover `html_viewer` para um processo userland separado. Compositor mantém apenas o "frame" da janela (chrome). Renderização, parse, fetch ocorrem em ring 3. IPC via pipes M5. Watchdog mata e reinicia o processo se exceder budget de tempo/memória.

#### Entregáveis

- **E3.1** — `userland/bin/capybrowser/main.c` — binário ring 3 que recebe URL via stdin (pipe), emite frames de surface RGBA via stdout (pipe). Inicialmente: stub que renderiza retângulo colorido como "loading".
- **E3.2** — Protocolo IPC `docs/architecture/browser-ipc.md`: comandos (`navigate`, `cancel`, `back`, `forward`, `scroll`), eventos (`title`, `frame`, `cursor`, `error`).
- **E3.3** — `src/apps/browser_chrome/` — janela do compositor que embute o processo browser, lê o pipe de surface e blita no widget. URL bar, botões nav, ícone de status.
- **E3.4** — Migração incremental do `html_viewer` atual para o binário userland. Parser HTML/CSS, image decoders, resource budgets, op_budget — tudo recompila como blob userland.
- **E3.5** — Watchdog: `src/apps/browser_chrome/watchdog.c` mede heartbeat (`navigation_id` reportado via pipe). Sem heartbeat por 10s → `process_kill(pid, SIGKILL=9)` + restart automático com mensagem "Browser reiniciado por travamento".
- **E3.6** — Crash recovery: se browser sair com status != 0, chrome mostra erro + botão "Recarregar".
- **E3.7** — Smoke `smoke-x64-browser-isolation`: lança chrome, mata o processo browser via `process_kill`, valida que chrome detecta e reinicia.
- **E3.8** — Smoke `smoke-x64-browser-watchdog`: força loop infinito no parser (env var `CAPYBROWSER_FREEZE`), valida que watchdog mata em ≤ 10s.

#### Critérios de aceite

- [ ] Crash do browser não derruba desktop nem outros apps.
- [ ] Watchdog reinicia browser travado em ≤ 10s.
- [ ] Compositor permanece responsivo durante carregamento de página de 10MB.
- [ ] `task_manager` lista o processo `capybrowser` separadamente do desktop e permite `Kill`.
- [ ] M8.2 promovido para ✅.
- [ ] Browser mantém todas as features atuais (HTTP, redirects, CSS, imagens, cookies, history, bookmarks, find-in-page).

#### Como será feito

1. **Fase 3a** (1 sessão): definir protocolo IPC. Estruturas em `include/apps/browser_ipc.h` compartilhado entre kernel/userland.
2. **Fase 3b** (1 sessão): stub ring 3 que recebe URL e emite frame estático.
3. **Fase 3c** (2 sessões): migração do `html_viewer` para userland. Maior cuidado: image decoders (PNG/JPEG via tinf/...) precisam compilar como blob userland (sem incluir kernel headers).
4. **Fase 3d** (1 sessão): watchdog + restart + crash UI.
5. **Fase 3e** (1 sessão): smokes + estabilização.

#### Validação

```bash
make smoke-x64-browser-isolation
make smoke-x64-browser-watchdog
make test                            # cobertura de host suite continua verde
make layout-audit                    # nenhum monolito reintroduzido
```

#### Saída esperada

M8.2 fecha. W3.4 (stretch) entregue. Browser se torna o **primeiro app real em ring 3**, abrindo o caminho para os apps de F7.

---

### F4 — Stack de sockets userland + TLS (libcapy-net + libcapy-tls)

**Criticidade:** alta — destrava update real, browser HTTPS, qualquer IPC de rede futuro.
**Depende de:** F1 (M5 fork/exec) + parcialmente F3 (motivação real). Pode iniciar antes de F3 fechar.
**Paralelo possível com:** F3.
**Risco:** alto — toca rede, criptografia e ABI userland.
**Esforço:** 5–8 sessões.
**Branch sugerida:** `feature/f4-userland-net`.

#### Objetivo

Hoje, rede é **chamada direta de kernel** (`html_viewer_issue_request` chama `http_request_send` dentro do kernel). Para apps reais isolados, precisamos de **socket syscalls + libcapy-net em userland**.

#### Entregáveis

- **E4.1** — `SYS_SOCKET`, `SYS_BIND`, `SYS_CONNECT`, `SYS_SEND`, `SYS_RECV`, `SYS_CLOSE` (close já existe).
- **E4.2** — File descriptor type `FD_TYPE_SOCKET` análogo a `FD_TYPE_PIPE`. Aproveita stack de rede atual no kernel via wrappers.
- **E4.3** — `userland/lib/capylibc-net/` — wrappers `capy_socket()`, `capy_connect()`, etc.
- **E4.4** — `userland/lib/capylibc-tls/` — TLS 1.2/1.3 via BearSSL (já no `third_party/`). Compila como userland blob.
- **E4.5** — Migração do `capybrowser` (F3) para usar libcapy-net + libcapy-tls em vez de syscalls atuais bypass.
- **E4.6** — Host tests + smoke `smoke-x64-tls-handshake` (cliente TLS conecta a servidor local QEMU `httpd`).
- **E4.7** — TLS trust store userland: re-empacota `src/security/tls_trust_anchors.c` como bundle userland.

#### Critérios de aceite

- [ ] Cliente userland faz TLS handshake com servidor de teste e troca dados.
- [ ] `capybrowser` (F3) navega `https://example.org` via libcapy-tls, sem chamar kernel TLS.
- [ ] Falha de cert é reportada com erro estável.
- [ ] `net-fetch https://...` no shell continua funcionando (legacy: ainda usa kernel; refatoração futura).

#### Como será feito

1. **F4a** (2 sessões): syscalls + FD type. Reutiliza buffer pipe pattern.
2. **F4b** (2 sessões): libcapy-net stubs + smokes básicos (TCP plaintext).
3. **F4c** (3 sessões): libcapy-tls (BearSSL recompilado como userland).
4. **F4d** (1 sessão): migração capybrowser.

#### Validação

```bash
make smoke-x64-tls-handshake
make smoke-x64-browser-https
```

#### Saída esperada

Apps userland podem fazer rede sem bypass. Caminho para update real (F5) fica aberto.

---

### F5 — Update real via GitHub Releases (fetch + Ed25519)

**Criticidade:** média — produto utilizável existe sem isso, mas distribuição automática depende.
**Depende de:** F4 (TLS userland) + F2 (assinatura de release já gerada).
**Paralelo possível com:** F6 (GUI).
**Risco:** médio — toca staged update + boot slot já entregues; risco controlado.
**Esforço:** 3–4 sessões.
**Branch sugerida:** `feature/f5-update-fetch`.

#### Objetivo

`update-agent` deixa de depender de `update-import-manifest` manual e passa a:

1. Buscar manifest assinado de `https://github.com/<repo>/releases/latest`.
2. Validar Ed25519.
3. Baixar payload (kernel `.bin` + manifest novo).
4. Validar SHA-256 (já existe via M6.4).
5. Stage + arm + reboot + confirm health (já existe via M7.5).

#### Entregáveis

- **E5.1** — `update_agent_fetch_remote_manifest()` — usa libcapy-net + libcapy-tls.
- **E5.2** — Ed25519 verifier embarcado no kernel (BearSSL já fornece).
- **E5.3** — Comandos shell: `update-fetch`, `update-apply`.
- **E5.4** — Política: canal `develop` busca `develop` branch releases; canal `stable` busca tags `v*.*.*`.
- **E5.5** — Smoke `smoke-x64-update-fetch` (servidor HTTP local serve manifest fake assinado).
- **E5.6** — Documentação `docs/operations/update-from-github.md`.

#### Critérios de aceite

- [ ] `update-fetch` baixa manifest do GitHub, valida assinatura.
- [ ] `update-apply` aplica payload via `update_agent_apply_boot_slot_verified`, confirma health, faz rollback se confirmar falhar.
- [ ] Manifest mutilado é rejeitado.
- [ ] Manifest com versão menor que a atual é rejeitado (downgrade protection).
- [ ] Suite passa.

#### Como será feito

1. **F5a** Implementar fetch + verify.
2. **F5b** Adicionar comandos shell + audit log `[audit] [update] fetched ...`.
3. **F5c** Smoke ponta-a-ponta com servidor HTTP local em QEMU user-mode network.

#### Validação

```bash
make smoke-x64-update-fetch
```

#### Saída esperada

Sistema atualizável sem reinstalação manual.

---

### F6 — Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher)

**Criticidade:** alta — sem isso, GUI fica "scaffold visível mas inerte" como hoje.
**Depende de:** F1 (release base limpo). Pode iniciar imediatamente após F1.
**Paralelo possível com:** F4, F5.
**Risco:** médio — código existe; bottleneck é integração e timing.
**Esforço:** 4–6 sessões.
**Branch sugerida:** `feature/f6-gui-session`.

#### Objetivo

Hoje:

- `desktop_init/run_frame` existe mas autostart é frágil (extended.c chama mas pode bloquear o login runtime).
- Mouse PS/2 inicializa mas não há dispatcher unificado de eventos.
- Terminal gráfico não consome shell real fim-a-fim — W1 mitigou parte (clear callback), falta o resto.
- Login é por TTY framebuffer; não há login GUI.

Depois desta fase:

- Boot → splash → login GUI → desktop com mouse/teclado/janelas/foco/abertura de apps.

#### Entregáveis

- **E6.1** — Loginwindow GUI: tela de senha em compositor, com lista de usuários, falha visível.
- **E6.2** — `desktop_run_frame` integrado ao loop principal do kernel (não mais via spin de `cmd_desktop_start`); usa timer APIC, não `for(volatile)`.
- **E6.3** — Mouse: drivers PS/2 + USB HID (xHCI já existe parcial). Eventos chegam ao compositor via `input_runtime`.
- **E6.4** — Dispatcher central de eventos: keyboard + mouse + window focus + IPC. Substitui handlers ad-hoc.
- **E6.5** — Taskbar funcional com botão de "Iniciar" (lista de apps), relógio (já existe), notificações simples.
- **E6.6** — Terminal gráfico consome shell real (já parcial via W1, fechar fim-a-fim).
- **E6.7** — Fallback CLI: tecla F1 no login GUI volta para shell TTY (recovery/dev).
- **E6.8** — Smoke `smoke-x64-gui-session` (sendkey via QEMU, valida boot → login → click em ícone → app abre).

#### Critérios de aceite

- [ ] Boot novo entra em login GUI sem comando manual.
- [ ] Login com credencial válida abre desktop.
- [ ] Mouse move, click e drag funcionam em VMware+E1000+SVGA II.
- [ ] Janela tem foco, pode ser arrastada, fechada.
- [ ] Taskbar mostra apps abertos; click alterna foco.
- [ ] Terminal gráfico aceita comandos e renderiza saída.
- [ ] CTRL+ALT+F1 muda para TTY de recovery.

#### Como será feito

1. **F6a** Migrar autostart desktop para timer-driven loop.
2. **F6b** Mouse PS/2 → input_runtime → compositor.
3. **F6c** Dispatcher unificado.
4. **F6d** Loginwindow GUI (escreve em `auth/user.c` via syscall).
5. **F6e** Taskbar polimento + smoke.

#### Validação

```bash
make smoke-x64-gui-session
make smoke-x64-mouse-events           # novo: clica em coordenada conhecida e valida hit-test
```

#### Saída esperada

Sessão gráfica utilizável fim-a-fim. Base para F7.

---

### F7 — Apps básicos do desktop

**Criticidade:** média — torna o sistema utilizável sem CLI.
**Depende de:** F3 (browser isolado serve de pattern), F6 (GUI completa).
**Paralelo possível com:** F8 (package manager).
**Risco:** baixo — apps são compositores de syscalls + libs já existentes.
**Esforço:** 1 sessão por app + 1 sessão por suite final = ~7 sessões.
**Branch sugerida:** `feature/f7-apps-basicos`.

#### Objetivo

Pacote inicial de apps do desktop:

| App | Estado atual | Meta F7 |
|---|---|---|
| `task_manager` | ✅ Funcional (W2) | Polimento: gráfico de CPU/RSS por processo |
| `text_editor` | scaffold | Editar/salvar arquivos VFS, syntax highlight básico |
| `file_manager` | scaffold | Browse, copy, move, delete em CAPYFS |
| `settings` | scaffold | Network mode, theme, language, user mgmt |
| `image_viewer` | parcial (no html_viewer) | App standalone: PNG/JPEG, zoom, próximo/anterior |
| `calculator` | inexistente | Simples: 4 ops, decimal, hex |
| `log_viewer` | inexistente | Browse `/var/log/*`, filtro por nível |

Todos como **processos ring 3 isolados** (lição de F3).

#### Entregáveis

- **E7.1** — `userland/bin/text_editor/`, `userland/bin/file_manager/`, etc.
- **E7.2** — `src/apps/<app>_chrome/` widgets do compositor (frames + IPC, igual capybrowser).
- **E7.3** — `userland/lib/libcapy-ui/` — toolkit minimalista (button, list, textbox, dialog).
- **E7.4** — Smokes por app.
- **E7.5** — Pacote inicial de ícones em `assets/icons/`.

#### Critérios de aceite

- [ ] Cada app abre, executa função primária, fecha sem crash.
- [ ] Falha de um app não derruba desktop.
- [ ] Todos passam smoke.

#### Como será feito

Por app (1 sessão):

1. Definir IPC (input/output via pipe).
2. Implementar binário userland.
3. Implementar chrome no compositor.
4. Smoke.

#### Validação

```bash
make smoke-x64-text-editor
make smoke-x64-file-manager
make smoke-x64-settings
make smoke-x64-image-viewer
make smoke-x64-calculator
make smoke-x64-log-viewer
```

#### Saída esperada

Usuário comum opera sem CLI.

---

### F8 — Package manager + SDK + ABI estável

**Criticidade:** média — destrava ecossistema, mas não é cliente-final.
**Depende de:** F4 (rede userland), F5 (update fetch reaproveita boa parte), F7 (apps reais como cliente).
**Paralelo possível com:** F9 (JS) e F10 (linguagem).
**Risco:** médio — define ABI que ficará estável a longo prazo.
**Esforço:** 6–10 sessões.
**Branch sugerida:** `feature/f8-pkgd`.

#### Objetivo

Distribuir apps separadamente do kernel. Hoje, todo binário userland é embarcado no kernel via `embedded_progs`. Isso não escala.

#### Entregáveis

- **E8.1** — Formato `.capypkg` (tar+manifest+signature).
- **E8.2** — `pkgd` (daemon ring 3): `install`, `remove`, `list`, `verify`.
- **E8.3** — Repositório oficial GitHub-hosted (`capyos-packages` repo).
- **E8.4** — `software-center` (app GUI para descoberta/install).
- **E8.5** — ABI documentada: `docs/api/abi-stable.md` listando syscall numbers, struct layouts congelados.
- **E8.6** — `libcapy-fs`, `libcapy-ui`, `libcapy-net`, `libcapy-tls` como SOs userland (links dinâmicos? ou estáticos? **Decisão de F8**: começar estáticos, links dinâmicos depois).
- **E8.7** — SDK header pack + samples + `capyos-sdk` repo separado.
- **E8.8** — Smoke `smoke-x64-pkg-install` (instala pacote local, executa, remove).

#### Critérios de aceite

- [ ] `pkg install hello` baixa, valida assinatura, instala em `/usr/bin/hello`.
- [ ] `pkg list` lista instalados com versões.
- [ ] `pkg remove hello` remove e atualiza DB.
- [ ] DB de pacotes sobrevive reboot.
- [ ] Update de pacote sem reinstalar OS.

#### Como será feito

Sequência longa; ver branch dedicada com sub-fases F8a–F8h documentadas no PR.

#### Validação

```bash
make smoke-x64-pkg-install
make smoke-x64-pkg-update
```

#### Saída esperada

Plataforma aberta para software de terceiros.

---

### F9 — JavaScript engine sandboxed (M8.6)

**Criticidade:** baixa-média — essencial para web moderna, opcional para SO.
**Depende de:** F3 (browser isolado), F4 (rede), F8 (formato de app pode reaproveitar bytecode).
**Paralelo possível com:** F10.
**Risco:** alto — JS engine é projeto enorme em si.
**Esforço:** 10+ sessões. **Decisão arquitetural**: usar QuickJS (C, 200KB, MIT) ou escrever subset (CapyJS). Decidir no início da fase.
**Branch sugerida:** `feature/f9-js-engine`.

#### Objetivo

Renderizar páginas que dependem de JS minimamente (sem React/Angular completos). Sandboxed dentro do processo `capybrowser`.

#### Entregáveis

- **E9.1** — Decisão: integrar QuickJS ou escrever CapyJS subset (ECMAScript 5 mínimo).
- **E9.2** — Bridge DOM ↔ JS no `capybrowser`.
- **E9.3** — Budget de execução (op_budget já tem): mata script de loop infinito.
- **E9.4** — Sem `eval` arbitrário, sem acesso a syscalls (só DOM bridge).
- **E9.5** — Smokes `js-basic`, `js-dom-mutation`, `js-timeout-killed`.

#### Critérios de aceite

- [ ] Página com `document.title = "Hello"` atualiza título.
- [ ] Loop infinito é interrompido em ≤ 5s pelo budget.
- [ ] M8.6 promovido para ✅.

#### Validação

```bash
make smoke-x64-js-basic
make smoke-x64-js-timeout
```

---

### F10 — CapyLang (linguagem própria)

**Criticidade:** baixa — branding/ecossistema.
**Depende de:** F8 (precisa de package manager para distribuir runtime).
**Paralelo possível com:** F9.
**Risco:** alto — projeto longo.
**Esforço:** 20+ sessões em três etapas.
**Branch sugerida:** `feature/f10-capylang`.

#### Objetivo

Linguagem nativa do CapyOS para automação primeiro, apps depois.

#### Entregáveis (em ondas)

- **Etapa 1 — automação:** parser, VM bytecode, bindings shell/FS/config. Roda como `.capyscript` interpretado.
- **Etapa 2 — apps utilitários:** módulos, FFI controlada, integração com libcapy-ui.
- **Etapa 3 — primeira-classe:** stdlib, async, LSP, formatter.

#### Critérios de aceite (Etapa 1)

- [ ] Script `.capyscript` executa em ring 3 via `capylang` interpreter.
- [ ] Bindings: `fs.read`, `fs.write`, `shell.exec`, `config.get/set`.
- [ ] Smoke `smoke-x64-capylang-hello`.

---

## 5. Roadmap macro pós-F10 (visão sem datas)

| Fase futura | Tema | Pré-requisitos |
|---|---|---|
| F11 | Secure Boot + measured boot | F2 (assinatura), F8 (chain) |
| F12 | SMP / multicore | scheduler atual está single-core |
| F13 | USB completo (mass storage, áudio, câmera) | xHCI base existe |
| F14 | GPU acelerada (drivers reais para Intel/AMD/NVIDIA) | F12 desejável |
| F15 | Compatibilidade Linux subset (syscall translation layer) | F4 + F8 |
| F16 | Office suite, multimedia stack, IDE própria | F8 + F10 + ecossistema |
| F17 | IA local (LLM small) | F12 + F14 |

---

## 6. Validação — comandos canônicos

| Cenário | Comando |
|---|---|
| Validação mínima após qualquer mudança | `make test && make layout-audit && make all64` |
| Pré-commit completo | `make release-check` |
| Pré-release | `make release-check && make smoke-x64-vmware-dhcp` (após F2) |
| Smokes M5 | `make smoke-x64-fork-cow smoke-x64-exec smoke-x64-fork-wait smoke-x64-pipe smoke-x64-fork-crash smoke-x64-capysh` |
| Smokes M4 preemptivo | `make smoke-x64-preemptive-all` |
| Smokes browser (após F3) | `make smoke-x64-browser-isolation smoke-x64-browser-watchdog` |
| Smokes GUI (após F6) | `make smoke-x64-gui-session smoke-x64-mouse-events` |

---

## 7. Layout / clean code (regras vivas)

Mantidas do M3 fechado. Não voltam a ser fase porque já estão em CI:

- ≤ 900 linhas por arquivo C/runtime/teste (exceções documentadas: `tls_trust_anchors.c` por dados estáticos).
- Headers privados em `internal/<modulo>_internal.h`.
- `make layout-audit --strict` em CI; bloqueia merge.
- Includes cruzados de `internal/` entre módulos distintos são erro.
- Funções utilitárias canônicas: `kstring.h` (`kstrlen`, `kstreq`, `kmemzero`, etc.). Duplicar é violação.

---

## 8. Critérios globais de "SO sólido"

CapyOS é considerado **estruturalmente sólido** quando:

### Plataforma
- [ ] Boot oficial previsível (M1 ✅, F2 fecha smoke).
- [ ] Update sem reinstalação (F5).
- [ ] Rollback automático (M7 ✅).
- [ ] Logs persistentes (M6.2 ✅).

### Kernel
- [x] Multitarefa real (M4 ✅).
- [x] Isolamento de processos (M4 + M5 ✅).
- [x] Syscalls definidas (M5 + ABI documentar em F8).
- [x] Panic/fault observáveis (M4 fase 4 ✅).

### Storage
- [x] Replay/journal (M7 ✅).
- [x] Fsck (M7 ✅).
- [x] Integridade autenticada (M6.5 ✅).
- [x] Política de auth (M6.1 ✅).

### Rede
- [ ] Sockets userland (F4).
- [ ] TLS (F4).
- [ ] Updates seguros (F5).
- [ ] Firewall mínimo (futuro F11+).

### Desktop
- [ ] Sessão gráfica com mouse (F6).
- [ ] Apps básicos (F7).
- [ ] UI de update (F5 + F8).

### Ecossistema
- [ ] SDK (F8).
- [ ] Formato de app (F8).
- [ ] Linguagem própria (F10).

**Score atual:** 11/22 ✅ (50% dos critérios estruturais).
**Score após F1–F8:** 21/22 ✅ (95%).

---

## 9. Sequência de releases recomendada

| Versão | Conteúdo | Fase fechada |
|---|---|---|
| `0.8.0-alpha.5` | M5 userland + W1/W2/W3 | **F1** |
| `0.8.0-alpha.6` | DHCP smoke VMware + assinatura release | **F2** |
| `0.8.0-beta.1` | Browser isolado + watchdog | **F3** |
| `0.8.0-beta.2` | Sockets userland + TLS | **F4** |
| `0.9.0-alpha.1` | Update via GitHub | **F5** |
| `0.9.0-beta.1` | GUI sessão completa | **F6** |
| `0.9.0` | Apps básicos | **F7** |
| `0.10.0` | Package manager + SDK | **F8** |
| `0.11.0+` | JS engine | **F9** |
| `0.12.0+` | CapyLang Etapa 1 | **F10 (Etapa 1)** |
| `1.0.0` | CapyLang completa + Secure Boot + SMP | F10 + F11 + F12 |

---

## 10. Manutenção deste documento

**Atualize sempre que:**

- Uma fase F1–F10 muda de status.
- Um item promovido para ✅ ganha evidência (teste/smoke/release note).
- Uma decisão arquitetural de impacto (ABI, formato de pacote, etc.) é tomada.
- Um hotfix significativo é aplicado (registrar em "Itens fora do plano formal" + abrir entrada na fase relevante).

**Não crie outros planos em `active/`.** Se for inevitável (ex.: branch experimental longo), o plano novo:

1. Refere este como pai;
2. Vive em `experimental/` se for laboratório;
3. Vai para `historical/` ao fim, com o conteúdo migrado para uma fase deste plano.

A regra é simples: **um único plano linear, evidência para cada promoção, nada paralelo sem justificativa documentada.**

---

## Apêndice A — Mapeamento dos planos antigos

Os documentos abaixo foram **arquivados em `historical/`** em 2026-05-01 e estão consolidados aqui:

| Documento antigo | Conteúdo migrado para |
|---|---|
| `capyos-robustness-master-plan.md` | §2.1 (M0–M8 entregue) + §4 (F1–F10) |
| `m5-userland-progress.md` | §2.1 (M5-userland) + F1 |
| `post-m5-ux-followups.md` | §2.1 (W1–W3) |
| `system-master-plan.md` | §3 (princípios) + §4 (F4–F10) + §5 (roadmap pós) |
| `system-roadmap.md` | §4 (distribuído por fase) + §7 (clean code) |
| `system-execution-plan.md` | §4 (etapas A–G distribuídas em F1–F10) |
| `capyos-master-improvement-plan.md` | §2.1 (fases CONCLUIDO) + §4 (F6 GUI fix integration) |
| `browser-status-roadmap.md` | §2.1 (M8.1/8.3/8.4/8.5 + W3) + F3 (M8.2) + F9 (M8.6) |
| `source-organization-roadmap.md` | §7 (regras vivas) |
| `historical/m4-finalization-progress.md` | §2.1 (M4 100%) — permanece como referência técnica |

Este apêndice serve apenas para rastreabilidade. Não consultá-los para decisões — usar sempre este master plan.

---

## Apêndice B — Itens fora dos planos formais (hotfixes registrados)

| Item | Status | Onde foi consolidado |
|---|---|---|
| Boot UEFI/EBS robusto (Print fora da janela crítica + dbgcon markers) | ✅ Aplicado | M0/F2 (referência futura em release notes) |
| `com1.c/h` → `serial_com1.c/h` (workaround exFAT macOS) | ✅ Aplicado | §7 (clean code — exceção documentada de naming) |
| Remoção do limite artificial de 1 GiB no framebuffer | ✅ Aplicado | M0 (já consolidado em master plan antigo) |
| Print de `[UEFI] GOP fb base=...` no loader | ✅ Aplicado | Diagnóstico permanente (M0) |
| W1 callback de clear context-aware | ✅ 2026-04-30 | §2.1 |
| W2 task_manager_tick + Kill button | ✅ 2026-04-30 | §2.1 |
| W3 parser yield + timeout 30s | ✅ 2026-04-30 | §2.1 |
