# CapyOS — Status executivo

**Data:** 2026-05-01 · **Versão:** `0.8.0-alpha.5+20260501` · **Plataforma:** VMware + UEFI + E1000

> **Fonte de verdade:** [`active/capyos-master-plan.md`](active/capyos-master-plan.md). Este documento é só o resumo navegável: cada fase F*x* aparece com **progresso, status e dependências** em ordem de desenvolvimento. Detalhes técnicos, entregáveis e critérios de aceite ficam no master plan.

---

## Plano linear F1 → F10

**Progresso global:** `[██░░░░░░░░] 20%` *(F1 80% + F3 70% de 10 fases; F2/F4–F10 não iniciadas)*

| Fase | Tema | Progresso | Status | Depende de |
|---|---|---|---|---|
| **F1** | Release `0.8.0-alpha.5` (consolidação M5 + W1/W2/W3) | `[████████░░] 80%` | 🟡 bump local feito; aguarda CI dos 6 smokes + tag | — |
| **F2** | DHCP smoke VMware+E1000 + assinatura Ed25519 | `[█░░░░░░░░░] 10%` | 🔴 código já existe; aguarda harness VMware externo | F1 |
| **F3** | Browser em processo userland + watchdog (M8.2 + W3.4) | `[███████░░░] 70%` | 🟡 stack lógica fechada + smoke kernel-side scaffolding pronto; falta migração do parser (F3.3c) e cross-toolchain para rodar o smoke | F1 |
| **F4** | Sockets userland + TLS (`libcapy-net` + `libcapy-tls`) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F1 (paralelo com F3) |
| **F5** | Update real via GitHub Releases (fetch + Ed25519) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F4, F2 |
| **F6** | Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F1 (paralelo com F3/F4) |
| **F7** | Apps básicos (file_manager, text_editor, settings, image_viewer, calculator, log_viewer) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F3, F6 |
| **F8** | Package manager + SDK + ABI estável | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F4, F5, F7 |
| **F9** | JS engine sandboxed (M8.6) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F3 |
| **F10** | CapyLang (linguagem própria) | `[░░░░░░░░░░]  0%` | 🔴 não iniciado | F8 |

**Legenda de status:** ✅ implementado · 🟡 em andamento · 🔴 não iniciado · ⛔ bloqueado

---

## Detalhe das fases ativas

### F1 — Release `0.8.0-alpha.5` (80%)

| Entregável | Progresso | Status |
|---|---|---|
| E1.4–E1.6, E1.8 — bump versão + release note + screenshots placeholder + master plan | `[██████████] 100%` | ✅ 2026-05-01 |
| E1.1 — push de `feature/dev-bugfixes` para o GitHub | `[░░░░░░░░░░]   0%` | 🔴 decisão do usuário |
| E1.2 — CI dos 6 smokes M5 (`fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh`) | `[░░░░░░░░░░]   0%` | 🔴 depende de E1.1 |
| E1.3 — `make release-check` em CI | `[░░░░░░░░░░]   0%` | 🔴 depende de E1.1 |
| E1.7 — tag `0.8.0-alpha.5+20260501` | `[░░░░░░░░░░]   0%` | 🔴 depende de E1.2 + E1.3 |

### F3 — Browser isolado + watchdog (70%)

| Sub-fase | O que faz | Progresso | Status |
|---|---|---|---|
| F3.3a | Protocolo IPC binário (header + codec + 162 asserts) | `[██████████] 100%` | ✅ 2026-05-01 |
| F3.3b | Stub `capybrowser` ring 3 + `/bin/capybrowser` em embedded_progs (+4 asserts) | `[██████████] 100%` | ✅ 2026-05-01 |
| F3.3d | Chrome scaffolding: watchdog (49) + dispatcher (66) + runtime (61) + spawn helper + e2e (47) | `[██████████] 100%` | ✅ camada lógica fechada |
| F3.3e | Smoke `smoke-x64-browser-spawn`: kernel boot wiring + harness QEMU + 9 markers debugcon | `[███████░░░]  70%` | 🟡 código + harness prontos; aguarda cross-toolchain (`x86_64-elf-*`) em CI para executar |
| F3.3c | Migração do `html_viewer` para userland (parser HTML/CSS, image decoders, fetch HTTP) | `[░░░░░░░░░░]   0%` | 🔴 ~2 sessões; desbloqueia smoke `browser-isolation`/`browser-watchdog` reais |

**Métricas de F3 até hoje:** +389 asserts host (162 IPC + 4 embedded_progs + 49 watchdog + 66 chrome + 61 runtime + 47 e2e); `make test` 100% verde; `make layout-audit` sem warnings; `clang -fsyntax-only -target x86_64-unknown-linux-gnu` limpo em `browser_smoke.c` e `browser_engine_spawn.c`.

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

## Anexo A — Histórico (entregue antes de 2026-05-01)

Estes blocos do plano antigo já estão em estado **Implementado** e não voltam como tarefa. Detalhe técnico em [`active/capyos-master-plan.md`](active/capyos-master-plan.md) §2.1 e em [`historical/m4-finalization-progress.md`](historical/m4-finalization-progress.md).

| Bloco | Tema | Onde |
|---|---|---|
| M0 / M1 / M3 | Governança, build reproduzível, layout estrito | master plan §2.1 |
| M4 (31 fases 0…11) | Processos + scheduler preemptivo + CoW + TSS + ring-3 preemption | `historical/m4-finalization-progress.md` |
| M5-perf | Métricas de boot, baseline, op_budget | master plan §2.1 |
| M5-userland (fases A–F) | fork/exec/wait/pipe + capysh ring 3 + isolamento de crash | `historical/m5-userland-progress.md` |
| M6.1–M6.3, M6.5 | Política de senha, auditoria, privilégios, journal v2 HMAC | master plan §2.1 |
| M7 | WAL + replay + fsck + recovery cause + update transacional | master plan §2.1 |
| M8.1, M8.3–M8.5 | Browser estado formal + budgets + cache + `about:network`/`about:memory` | master plan §2.1 |
| W1, W2, W3 core | TTY clear context-aware + task manager auto-refresh + browser parser yield | `historical/post-m5-ux-followups.md` |

> Pendências antigas que ainda existem (M2, M6.4, M8.2, M8.6) foram mapeadas para fases F1–F10 — não são listadas separadamente aqui.

---

## Anexo B — Hotfixes não cobertos por fase formal

| Item | Status | Quando |
|---|---|---|
| Boot UEFI/EBS robusto (Print fora da janela crítica + dbgcon) | ✅ aplicado | hotfix 2026-04-30 |
| `com1.c/h` → `serial_com1.c/h` (workaround exFAT macOS) | ✅ aplicado | hotfix 2026-04-30 |
| Remoção do limite artificial de 1 GiB no framebuffer | ✅ aplicado | hotfix 2026-04-30 |
| Print `[UEFI] GOP fb base=...` no loader | ✅ aplicado | hotfix 2026-04-30 |

---

## Manutenção deste documento

Atualize sempre que:

- Uma fase F1–F10 muda de progresso em ≥ 10% ou muda de status.
- Uma sub-fase ativa fecha.
- Uma versão nova for tagueada (atualizar campo **Versão** no topo).
- Um hotfix relevante for aplicado (Anexo B).

**Regra única:** nada promovido para ✅ sem evidência (teste host, smoke, release note ou commit citado).
