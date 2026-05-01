# Indice De Planos CapyOS

> **Atualizado em 2026-05-01** — todos os planos antigos foram consolidados em
> [`active/capyos-master-plan.md`](active/capyos-master-plan.md). A pasta
> `active/` agora contem **um unico plano linear** (F1-F10 + roadmap macro).

> **Visao executiva:** [`STATUS.md`](STATUS.md) consolida pendencias e
> percentual por fase.

## Estrutura de pastas

```
docs/plans/
├── README.md                # este indice
├── STATUS.md                # status centralizado (% + dependencias)
├── active/                  # planos vigentes que orientam implementacao
├── historical/              # planos legados ou substituidos
└── experimental/            # trilhas de laboratorio fora da release oficial
```

Estados aceitos neste indice:

- `Ativo`: fonte valida para decisao tecnica atual (em `active/`).
- `Historico`: contexto antigo, nao deve guiar implementacao nova sozinho (em `historical/`).
- `Experimental`: investigacao ou trilha de laboratorio sem promessa de release (em `experimental/`).
- `Substituido`: mantido apenas por rastreabilidade; use o documento indicado.

## Planos ativos (`active/`)

| Documento | Papel atual |
|---|---|
| [`capyos-master-plan.md`](active/capyos-master-plan.md) | **Fonte de verdade unica.** Estado consolidado (M0-M8 + M4 final + M5 userland + W1-W3) e plano linear F1-F10. |

## Planos historicos (`historical/`)

Documentos abaixo foram **consolidados em `capyos-master-plan.md`** em
2026-05-01 e ficam aqui apenas para rastreabilidade. Nao orientam decisao
tecnica nova.

### Consolidados em 2026-05-01

| Documento | Conteudo migrado para |
|---|---|
| [`capyos-robustness-master-plan.md`](historical/capyos-robustness-master-plan.md) | §2.1 (M0-M8 entregue) + §4 (F1-F10) |
| [`m5-userland-progress.md`](historical/m5-userland-progress.md) | §2.1 (M5-userland) + F1 |
| [`post-m5-ux-followups.md`](historical/post-m5-ux-followups.md) | §2.1 (W1-W3) |
| [`system-master-plan.md`](historical/system-master-plan.md) | §3 (principios) + §4 (F4-F10) + §5 (roadmap pos) |
| [`system-roadmap.md`](historical/system-roadmap.md) | §4 + §7 (clean code) |
| [`system-execution-plan.md`](historical/system-execution-plan.md) | §4 (etapas A-G distribuidas) |
| [`capyos-master-improvement-plan.md`](historical/capyos-master-improvement-plan.md) | §2.1 (fases CONCLUIDO) + §4 (F6 GUI) |
| [`browser-status-roadmap.md`](historical/browser-status-roadmap.md) | §2.1 (M8 entregue) + F3 (M8.2) + F9 (M8.6) |
| [`source-organization-roadmap.md`](historical/source-organization-roadmap.md) | §7 (regras vivas) |

### Anteriores

| Documento | Observacao |
|---|---|
| [`m4-finalization-progress.md`](historical/m4-finalization-progress.md) | M4.1-M4.5 entregues 2026-04-30 (31/31 sub-fases). Referencia tecnica de execucao do M4 (CoW, scheduler, TSS/RSP0, ring-3 preemption). |
| [`platform-hardening-plan.md`](historical/platform-hardening-plan.md) | Marcos A/B/C/D concluidos. Trilha oficial agora e VMware+UEFI+E1000. |
| [`mvp-implementation-plan.md`](historical/mvp-implementation-plan.md) | Plano operacional do MVP encerrado. |
| [`architecture-restructure-and-improvements.md`](historical/architecture-restructure-and-improvements.md) | Substituido por `architecture/source-layout.md` + §7 do master plan. |
| [`improvement-audit.md`](historical/improvement-audit.md) | Itens consumidos pelos planos vivos. |
| [`refactor-plan.md`](historical/refactor-plan.md) | Substituido por `architecture/source-layout.md` + §7. |
| [`system-delivery-roadmap.md`](historical/system-delivery-roadmap.md) | Substituido pelo plano linear. |

## Planos experimentais (`experimental/`)

| Documento | Trilha | Observacao |
|---|---|---|
| [`hyperv-network-reset-plan.md`](experimental/hyperv-network-reset-plan.md) | Investigacao Hyper-V/rede | Hyper-V esta fora da trilha oficial de release |
| [`network-hyperv-refactor-and-update-plan.md`](experimental/network-hyperv-refactor-and-update-plan.md) | Refatoracao de rede Hyper-V | Hyper-V esta fora da trilha oficial de release |

## Regras de manutencao

1. **Um unico plano em `active/`**. Se for inevitavel criar outro, ele deve
   referenciar o master como pai e ir para `historical/` ou virar uma fase
   no master ao fim.
2. Nenhuma promocao para `Implementado`/✅ sem evidencia (teste, smoke,
   release note ou commit citado).
3. Mover doc entre pastas exige atualizar tambem [`STATUS.md`](STATUS.md) e
   este indice.
4. Plataforma oficial: `VMware + UEFI + E1000`. Hyper-V/QEMU sao laboratorio.
5. Planos `historical/`/`experimental/` nao orientam implementacao nova
   sozinhos.
