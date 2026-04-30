# CapyOS — Status Centralizado dos Planos

**Data de referência:** 2026-04-30
**Versão atual:** `0.8.0-alpha.4+20260429`
**Plataforma oficial:** `VMware + UEFI + E1000`

Este documento é o **resumo executivo** dos planos vivos. Para detalhes técnicos
e evidências, consultar:

- [`active/capyos-robustness-master-plan.md`](active/capyos-robustness-master-plan.md) — fonte primária M0–M8
- [`active/m4-finalization-progress.md`](active/m4-finalization-progress.md) — detalhamento das fases M4 (0–11)
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
| M8.2 | Isolamento por processo + watchdog | 🔴 Não iniciado | 0% | M4 (processos reais + kill seguro) | M4 Phase 8 |
| M8.6 | JavaScript robusto e sandboxed | 🔴 Não iniciado | 0% | M8.2 + budgets | Pós-isolamento |

---

## M4 Finalization — Fases (de [`m4-finalization-progress.md`](active/m4-finalization-progress.md))

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
| **7c** | CoW (clone AS + RO PTE flips) | 🔴 Não iniciado | — | 7b + `vmm_clone_address_space` |
| **8** | Flip preemptivo + smoke QEMU automático | 🔴 Não iniciado | — | 7b + APIC timer ativo |
| **9** | Integração de smokes (host + QEMU) | 🔴 Não iniciado | — | 8 |
| **10** | Docs + release | 🔴 Não iniciado | — | 9 |
| **11** | Cleanup | 🔴 Não iniciado | — | 10 |

**Progresso M4:** 17/22 fases concluídas → **~77%** das fases planejadas.

### Grafo de dependências M4

```
0 ─┬─ 1 ─ 2 ─ 3 ─ 3.5 ─┬─ 4 ──┬─ 7a ─ 7b ─ 7c
                       │      │
                       │      └─ 5f
                       │
                       └─ 5a ─ 5b ─ 5c ─┬─ 5d ─ 5e
                                        │
                                        └─ 6 ─ 6.5 ─ 6.6
                                                       │
                                                       ▼
                                               7b ─ 8 ─ 9 ─ 10 ─ 11
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

1. **M4 Phase 8** (flip preemptivo + smoke QEMU) — destrava M4.1–M4.5 e M8.2.
2. **M4 Phase 9** — integra smokes existentes ao `release-check`.
3. **M2 smoke VMware+E1000** — destrava M2.1–M2.5 e M6.4 (parte do gate VMware).
4. **M6.4 assinatura ponta-a-ponta** — checksums + smoke VMware+E1000.
5. **M4 Phase 10/11** — promoções no master plan + cleanup.
6. **Registrar hotfixes 2026-04-30** em M0.1 e M3.5 (doc only).

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
- Uma fase do M4 finalization concluir.
- Um hotfix relevante for aplicado (registrar em "Itens fora dos planos formais"
  e abrir entrada correspondente no master plan se aplicável).

A regra é simples: **nenhum item promovido para `Implementado` sem evidência
(teste, smoke, log de release ou commit citado)**.
