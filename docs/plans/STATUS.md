# CapyOS — Status Centralizado dos Planos

**Data de referência:** 2026-04-30
**Versão atual:** `0.8.0-alpha.4+20260429` (alpha.5 aguarda validação CI dos smokes M5)
**Plataforma oficial:** `VMware + UEFI + E1000`

Este documento é o **resumo executivo** dos planos vivos. Para detalhes técnicos
e evidências, consultar:

- [`active/capyos-robustness-master-plan.md`](active/capyos-robustness-master-plan.md) — fonte primária M0–M8
- [`active/m5-userland-progress.md`](active/m5-userland-progress.md) — marco pós-M4: fork/exec/wait/IPC/shell + isolamento de crash (~95%, branch `feature/m5-development`, aguardando CI dos 6 smokes)
- [`active/post-m5-ux-followups.md`](active/post-m5-ux-followups.md) — itens reportados manualmente (browser congela, task manager incompleto, `clear` ausente). Mapeados em W1–W3 para execução pós-merge de M5
- [`historical/m4-finalization-progress.md`](historical/m4-finalization-progress.md) — detalhamento das fases M4 (0–11), arquivado em 2026-04-30 após 31/31 fases concluídas
- [`active/system-master-plan.md`](active/system-master-plan.md) — visão macro do produto
- [`active/system-execution-plan.md`](active/system-execution-plan.md) — sequência operacional
- [`README.md`](README.md) — índice de classificação dos planos

## Convenções

- ✅ `Implementado` — entregue com evidência (teste/smoke/release note).
- 🟡 `Parcial` — base no código, falta gate ou cobertura para promover.
- 🔴 `Ainda não iniciado` — backlog.
- ⛔ `Bloqueado` — depende de outra fase.
- 🗄 `Substituído` — superado por documento mais recente.

A coluna **Conclusão (%)** é uma estimativa pragmática:

- `0%` = nenhum código/doc relevante.
- `25–50%` = primitiva ou helper pronto, mas sem integração.
- `50–80%` = integração presente, falta cobertura ou gate.
- `80–99%` = funcional, falta apenas validação real / smoke / promoção.
- `100%` = `Implementado` validado.

---

## Sumário por marco (master plan)

| Marco | Tema | Itens | Implementados | % do marco | Pendências |
|---|---|---|---|---|---|
| **M0** | Verdade oficial | 5 | 5 | **100%** | 🟡 M0.1 marcado como `Parcial` apenas porque é doc vivo (manutenção contínua) |
| **M1** | Build reproduzível | 5 | 5 | **100%** | — |
| **M2** | DHCP no boot | 5 | 0 | **75%** | Todos `Parcial` aguardando smoke VMware+E1000 |
| **M3** | Clean code | 5 | 5 | **100%** | — |
| **M4** | Processos/scheduler | 5 | 0 | **70%** | Todos `Parcial` aguardando flip preemptivo (fase 8) |
| **M5** | Performance | 5 | 5 | **100%** | — |
| **M6** | Segurança | 5 | 4 | **90%** | M6.4 aguarda assinatura ponta-a-ponta dos checksums |
| **M7** | CAPYFS/recovery | 5 | 5 | **100%** | — |
| **M8** | Browser/internet | 6 | 4 | **66%** | M8.2 (isolamento) e M8.6 (JS sandbox) não iniciados |
| **TOTAL** | | **46** | **28** | **~85%** | |

---

## Pendências detalhadas

### 🟡 M2 — DHCP automático no boot (75%)

| ID | Item | Status | % | Depende de | Bloqueador |
|---|---|---|---|---|---|
| M2.1 | DHCP padrão em instalação nova | 🟡 Parcial | 90% | — | smoke VMware+E1000 real |
| M2.2 | DHCP no bootstrap de rede | 🟡 Parcial | 90% | — | smoke VMware+E1000 real |
| M2.3 | Retry DHCP em background | 🟡 Parcial | 90% | — | smoke VMware+E1000 real |
| M2.4 | Diagnóstico de lease | 🟡 Parcial | 90% | — | smoke VMware+E1000 real |
| M2.5 | Smoke de DHCP+DNS+fetch | 🟡 Parcial | 50% | M2.1–M2.4 | harness VMware+E1000 obrigatório |

**Dependência externa:** infraestrutura de CI com VMware + adaptador E1000.

### 🟡 M4 — Processos, scheduler e serviços reais (70%)

| ID | Item | Status | % | Depende de | Bloqueador |
|---|---|---|---|---|---|
| M4.1 | Scheduler integrado ao runtime | 🟡 Parcial | 80% | M4 fase 8 | flip preemptivo no kernel vivo |
| M4.2 | Processos com isolamento mínimo | 🟡 Parcial | 90% | M4 fase 7c | CoW + smoke de isolamento real |
| M4.3 | Syscalls básicas completas | 🟡 Parcial | 75% | M4 fase 5+6 | argv/envp packing, fork real |
| M4.4 | `networkd`/`logger`/`update-agent` como jobs | 🟡 Parcial | 60% | M4 fase 8 | dispatch via scheduler real |
| M4.5 | Task manager com tarefas reais | 🟡 Parcial | 80% | M4 fase 8 | kill-by-row + telemetria fase 7 |

**Caminho crítico:** Phase 8 do M4 (flip preemptivo + smoke QEMU) — destrava 5 itens de uma vez.

### 🟡 M6 — Segurança base (90%)

| ID | Item | Status | % | Depende de | Bloqueador |
|---|---|---|---|---|---|
| M6.4 | Build oficial endurecido | 🟡 Parcial | 75% | M2 (smoke VMware+E1000) | Falta assinar checksums dos artefatos de release ponta-a-ponta. SHA-256 de payload de update já entregue. |

### 🔴 M8 — Internet, navegação e browser (66%)

| ID | Item | Status | % | Depende de | Bloqueador |
|---|---|---|---|---|---|
| M8.2 | Isolamento por processo + watchdog | � Desbloqueado por M5 | 0% | M4 + M5 (processos userland reais + kill seguro) | aguarda kickoff (M5 fechado destravou) |
| M8.6 | JavaScript robusto e sandboxed | 🔴 Não iniciado | 0% | M8.2 + budgets | Pós-isolamento |

---

## M5 Userland — Fases (de [`active/m5-userland-progress.md`](active/m5-userland-progress.md))

> Este "M5" é o **marco userland pós-M4**, distinto da coluna "M5
> Performance" do master plan (que está 100%). Fica nesta seção
> separada até promover ao master plan na release `0.8.0-alpha.5`.

| Fase | Descrição | Status | Evidência | Smoke |
|---|---|---|---|---|
| A | SYS_FORK + CoW userland | ✅ DONE | `user_task_arm_for_fork`, `sys_fork`, `capy_fork`; +7 host tests builder | `smoke-x64-fork-cow` (aguarda CI) |
| B | SYS_EXEC + embedded_progs registry | ✅ DONE | `process_exec_replace`, `sys_exec`, `capy_exec`; +16 host tests | `smoke-x64-exec` (aguarda CI) |
| C | wait()/exit() síncronos | ✅ DONE | `sys_exit`→`process_exit`, `sys_wait`, `capy_wait` | `smoke-x64-fork-wait` (aguarda CI) |
| D | SYS_PIPE + IPC mínimo | ✅ DONE | `sys_pipe`, FD pipe-aware r/w/close, fork inheritance; +12 host tests | `smoke-x64-pipe` (aguarda CI) |
| F | Isolamento de crash multi-processo | ✅ DONE | dispatcher kill-path (M4 phase 4+5f); fork+segfault smoke | `smoke-x64-fork-crash` (aguarda CI) |
| E | Shell interativo `capysh` ring 3 | ✅ DONE | stdin_buf (256B SPSC ring), SYS_READ fd 0 blocking, keyboard dual-feed, `userland/bin/capysh/main.c` (banner/prompt/builtins), embedded blob, `CAPYOS_BOOT_RUN_CAPYSH`; +19 host tests | `smoke-x64-capysh` (HMP sendkey injection, aguarda CI) |
| G | Docs + release | 🟡 Parcial (60%) | G.1–G.3 prontos (este STATUS.md, m5-progress, post-m5-ux-followups) | G.4 release notes + tag, G.5 master plan promote — aguardam CI verde |

**Progresso M5 userland:** 41/43 sub-fases (~95%). 84 novos host
asserts (frame builder + embedded_progs + pipe + stdin_buf). Sem
warnings de `make layout-audit`. Branch `feature/m5-development`
pronta para push manual ao GitHub.

**Caminho crítico:** validação CI dos 6 smokes → release notes →
tag `0.8.0-alpha.5` → merge → mover doc para `historical/`.

---

## Backlog pós-M5 — UX/Estabilidade (de [`active/post-m5-ux-followups.md`](active/post-m5-ux-followups.md))

Reportados manualmente durante validação da branch
`feature/m5-development` (2026-04-30). **Não são** parte de M5;
mapeados aqui para execução após merge.

| ID | Workstream | Sintoma | Status | Bloqueia |
|---|---|---|---|---|
| W1 | TTY polish | `mess` (clear) não limpava tela dentro do desktop terminal | ✅ DONE 2026-04-30 — `shell_clear_screen` agora rota via callback context-aware (terminal widget ou fbcon); `clear` adicionado em capysh ring 3 | — |
| W2 | Task manager + service registry | Apps lançados pós-boot não apareciam; services nunca refrescavam | ✅ DONE 2026-04-30 — `task_manager_tick()` chamado por frame de `desktop_run_frame` invalida o widget a cada ~0.5s; botão Kill funcional via `process_kill(pid, 9)` | — |
| W3 | Browser/html_viewer responsiveness | Carregamento congelava desktop; sites pesados travavam o sistema | ✅ DONE 2026-04-30 (core) — yield cooperativo no `html_parse` (cada 1024 iter), timeout duro de 30s via `html_viewer_tick`, drain async sem precisar interação. W3.4 (mover html_viewer para processo userland) deferido p/ M8.2 | M8.2 stretch desbloqueado por M5 |

**Validação:** `make test` 7/7 + op_budget + privilege + buffer_cache_pacing OK; `make layout-audit` sem warnings; `clang -fsyntax-only` limpo nos arquivos kernel-side editados (task_manager.c, desktop.c, async_runtime.c, html_parser.c, navigation_state.c, output_files.c).

---

## M4 Finalization — Fases (de [`m4-finalization-progress.md`](historical/m4-finalization-progress.md))

| Fase | Descrição | Status | Asserts | Depende de |
|---|---|---|---|---|
| 0 | Observability (`task_iter`, `process_iter`, `perf-task`) | ✅ DONE | 42 | — |
| 1 | `service-runner` kernel task | ✅ DONE | 17 | 0 |
| 2 | `context_switch` IRQ-safe seam | ✅ DONE | 30 | 1 |
| 3 | MSR/GDT contract para SYSCALL/SYSRET | ✅ DONE | 36 | 2 |
| 3.5 | `enter_user_mode` + per-CPU area | ✅ DONE | 19 | 3 |
| 4 | Fault isolation (classifier) | ✅ DONE | 42 | 3.5 |
| 5a | capylibc skeleton | ✅ DONE | 60 | 3.5 |
| 5b | Primeiro binário user `hello` | ✅ DONE | 7 | 5a |
| 5c | Embed `hello.elf` + spawn helper | ✅ DONE | 15 | 5b |
| 5d | `kernel_main` wiring (`CAPYOS_BOOT_RUN_HELLO`) | ✅ DONE | reuso | 5c |
| 5e | Smoke QEMU `smoke-x64-hello-user` | ✅ DONE | CI | 5d |
| 5f | Smoke segfault `smoke-x64-hello-segfault` | ✅ DONE | CI | 5e + 4 |
| 6 | `process_destroy` lifecycle | ✅ DONE | 17 | 5c |
| 6.5 | Process tree linkage | ✅ DONE | +23 | 6 |
| 6.6 | Zombie reaping (`process_reap_orphans`) | ✅ DONE | +21 | 6.5 |
| 7a | Recoverable user `#PF` seam | ✅ DONE | 50 | 4 |
| 7b | Real demand-paging body + RSS | ✅ DONE | +33 | 7a |
| **7c** | CoW (clone AS + RO PTE flips, decisão pura, refcount table) | ✅ DONE | +35 | 7b |
| **8a** | Preemptive primitives (quantum init + `scheduler_set_running`) | ✅ DONE | +9 | 7b |
| **8b** | `kernel_main` wiring + `smoke-x64-preemptive` | ✅ DONE | CI | 8a |
| **8c** | APIC IRQ 0 install fix (`irq_install_handler(0, apic_timer_irq_handler)`) + smoke marker | ✅ DONE | CI | 8b |
| **8d** | Global `sti` site + observation soak (`apic_timer_ticks > 0`) + extração `preemptive_boot.c` | ✅ DONE | CI | 8c |
| **8e** | First-task trampoline (`context_switch_into_first`) + two-task kernel demo + `smoke-x64-preemptive-demo` | ✅ DONE | CI | 8d |
| **8f.1** | TSS scaffolding (struct + GDT slot + LTR) p/ ring-3 IRQ safety | ✅ DONE | +17 | 8e |
| **8f.2** | Per-task RSP0 swap (cpu_local + TSS) via `arch_sched_apply_kernel_stack` hook | ✅ DONE | +7 | 8f.1 |
| **8f.3** | Single-task ring-3 preemption smoke (`CAPYOS_HELLO_BUSY` + `smoke-x64-preemptive-user`) | ✅ DONE | CI | 8f.2 |
| **8f.4** | Synthetic IRET frame builder (`x64_user_first_dispatch` + `user_task_arm_for_first_dispatch`) | ✅ DONE | +15 | 8f.3 |
| **8f.5** | Two-task ring-3 spawn helper (`kernel_boot_run_two_busy_users`) + RAX rank passing via crt0 + `smoke-x64-preemptive-user-2task` | ✅ DONE | +5 / CI | 8f.4 |
| **9** | Integração de smokes via `smoke-x64-preemptive-all` agregador | ✅ DONE | CI | 8f.5 |
| **10** | Docs + release: master plan bumped (M4.1–M4.5 → Implementado), m4-finalization-progress.md atualizado, STATUS consolidado | ✅ DONE | docs | 9 |
| **11** | Cleanup: m4-finalization-progress.md marcado como `READY_TO_ARCHIVE`, .PHONY consolidado, layout-audit limpo | ✅ DONE | docs | 10 |

**Progresso M4:** **31/31 fases concluídas → 100%** 🎉

Toda a M4 (M4.1 Scheduler integrado, M4.2 Processos isolados, M4.3 Syscalls
básicas, M4.4 Networkd/logger/update-agent como jobs, M4.5 Task manager
real) está em estado **Implementado**. A tabela do master plan
(`docs/plans/active/capyos-robustness-master-plan.md`) traz a nota
"Update 2026-04-30" consolidando o fechamento.

### Grafo de dependências M4

```
0 ─┬─ 1 ─ 2 ─ 3 ─ 3.5 ─┬─ 4 ──┬─ 7a ─ 7b ─┬─ 7c
                       │      │           │
                       │      └─ 5f       └─ 8a ─ 8b ─ 8c ─ 8d ─ 8e ─ 8f ─ 9 ─ 10 ─ 11
                       │
                       └─ 5a ─ 5b ─ 5c ─┬─ 5d ─ 5e
                                        │
                                        └─ 6 ─ 6.5 ─ 6.6
```

---

## Itens fora dos planos formais (hotfixes 2026-04-30)

| Item | Status | Observação |
|---|---|---|
| Boot UEFI/EBS robusto (Print fora da janela crítica + dbgcon) | ✅ Aplicado | Pendente registro em M0.1 (doc vivo) |
| `com1.c/h` → `serial_com1.c/h` (workaround exFAT macOS) | ✅ Aplicado | Pendente registro em M3.5 |
| Remoção do limite artificial de 1 GiB no framebuffer | ✅ Aplicado | Pendente registro em M0.1 |
| Print de `[UEFI] GOP fb base=...` no loader | ✅ Aplicado | Diagnóstico permanente |

---

## Caminho crítico para fechar `0.8.0-alpha.5+`

A ordem de prioridade para destravar a maior quantidade de marcos:

1. **M5 userland CI** — push da branch (já feito) + branch
   `feature/dev-bugfixes` (atual W1+W2+W3); rodar 6 smokes
   (`fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`,
   `capysh`); destrava release `0.8.0-alpha.5` e M8.2.
2. **M5 G.4/G.5** — release notes + tag + promoção no master plan.
3. **M2 smoke VMware+E1000** — destrava M2.1–M2.5 e M6.4 (parte
   do gate VMware).
4. **M6.4 assinatura ponta-a-ponta** — checksums + smoke
   VMware+E1000.
5. **W3.4 (stretch)** — mover `html_viewer` para processo
   userland separado via fork+exec (M5 desbloqueou). Casa com
   M8.2.
6. **Registrar hotfixes 2026-04-30** em M0.1 e M3.5 (doc only).

**Já concluído nesta rodada:** W1 (clear context-aware),
W2 (task manager auto-refresh + Kill button), W3 core
(parser yield + timeout 30s + per-frame async drain).

## Critérios de aceite da release α (do master plan)

- [x] Build oficial `TOOLCHAIN64=elf` passa.
- [x] Stack protector ativo na trilha de release.
- [x] ISO UEFI e disco provisionado com manifestos e checksums.
- [x] Nova instalação usa `network_mode=dhcp` por padrão.
- [ ] Boot em VMware+E1000 tenta DHCP automaticamente (smoke real).
- [x] `net-status` mostra modo, driver, ready, IP, gateway, DNS, erro DHCP.
- [x] `make test`, `make layout-audit`, `make all64`, `make iso-uefi` passam.
- [ ] Smoke oficial valida boot, login, persistência, DHCP, DNS, `net-fetch` em VMware+E1000.
- [x] Documento vivo de robustez atualizado.

**Pendências de aceite:** 2 de 9 (ambas dependem do harness VMware+E1000).

---

## Manutenção deste documento

Atualize sempre que:

- Um item do master plan mudar de status (`Parcial` → `Implementado`, etc.).
- Uma fase do M4 finalization, M5 userland ou de qualquer plano em
  `active/` concluir.
- Um workstream de `post-m5-ux-followups.md` (W1/W2/W3) avançar.
- Um hotfix relevante for aplicado (registrar em "Itens fora dos planos formais"
  e abrir entrada correspondente no master plan se aplicável).

A regra é simples: **nenhum item promovido para `Implementado` sem evidência
(teste, smoke, log de release ou commit citado)**.
