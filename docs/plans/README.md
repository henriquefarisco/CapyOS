# Indice De Planos CapyOS

Este indice classifica os planos em vigor para evitar fontes de verdade
paralelas. Quando um plano novo substituir um antigo, atualize esta tabela e o
[`active/capyos-robustness-master-plan.md`](active/capyos-robustness-master-plan.md).

> **Visao executiva:** [`STATUS.md`](STATUS.md) consolida todas as pendencias
> dos planos vigentes, percentual de conclusao por marco/fase e o grafo de
> dependencias entre fases.

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

| Documento | Papel atual | Fonte preferida / observacao |
|---|---|---|
| [`capyos-robustness-master-plan.md`](active/capyos-robustness-master-plan.md) | Plano vivo principal de robustez, status e evidencias | Fonte primaria para M0-M8 |
| [`m5-userland-progress.md`](active/m5-userland-progress.md) | M5 userland pos-M4 (fork/exec/wait/pipe + capysh + isolamento) | ~95%, branch `feature/m5-development` aguardando CI dos 6 smokes |
| [`post-m5-ux-followups.md`](active/post-m5-ux-followups.md) | Itens de UX/estabilidade reportados manualmente: TTY (`clear`), task manager, browser responsiveness | Workstreams W1-W3, executar pos-merge de M5 |
| [`system-master-plan.md`](active/system-master-plan.md) | Visao macro de evolucao do sistema | Usar junto do plano de robustez |
| [`system-roadmap.md`](active/system-roadmap.md) | Roadmap tecnico por dominio | Alinhar status no plano de robustez |
| [`system-execution-plan.md`](active/system-execution-plan.md) | Sequencia operacional do ciclo atual | Validar contra gates atuais antes de executar |
| [`capyos-master-improvement-plan.md`](active/capyos-master-improvement-plan.md) | Consolidacao tecnica recente da trilha x64 | Complementa o plano de robustez |
| [`browser-status-roadmap.md`](active/browser-status-roadmap.md) | Roadmap especifico do browser | Fonte de detalhes para M8 |
| [`source-organization-roadmap.md`](active/source-organization-roadmap.md) | Reducao de monolitos e organizacao de codigo | Fonte de detalhes para M3 |

## Planos historicos (`historical/`)

| Documento | Conclusao | Substituido por / observacao |
|---|---|---|
| [`m4-finalization-progress.md`](historical/m4-finalization-progress.md) | M4.1-M4.5 entregues 2026-04-30 (31/31 sub-fases) | Notas tecnicas de execucao do M4 (CoW, scheduler preemptivo, TSS/RSP0, ring-3 preemption); referido pelo "Update 2026-04-30" do plano de robustez |
| [`platform-hardening-plan.md`](historical/platform-hardening-plan.md) | Marcos A/B/C/D concluidos na trilha Hyper-V/QEMU | Trilha oficial agora e VMware+UEFI+E1000; detalhes de M1/M6 rastreados no plano de robustez |
| [`mvp-implementation-plan.md`](historical/mvp-implementation-plan.md) | Plano operacional do MVP encerrado | Conferir com o plano de robustez antes de retomar tarefas |
| [`architecture-restructure-and-improvements.md`](historical/architecture-restructure-and-improvements.md) | Ideias de reorganizacao arquitetural | Preferir `active/source-organization-roadmap.md` e `architecture/source-layout.md` |
| [`improvement-audit.md`](historical/improvement-audit.md) | Auditoria de melhorias ja consumidas pelos planos vivos | Migrar itens remanescentes para o plano de robustez |
| [`refactor-plan.md`](historical/refactor-plan.md) | Plano de refatoracao da migracao x64 concluida | Preferir `active/source-organization-roadmap.md` |
| [`system-delivery-roadmap.md`](historical/system-delivery-roadmap.md) | Roadmap de entrega anterior | Preferir `active/system-execution-plan.md` e o plano de robustez |

## Planos experimentais (`experimental/`)

| Documento | Trilha | Observacao |
|---|---|---|
| [`hyperv-network-reset-plan.md`](experimental/hyperv-network-reset-plan.md) | Investigacao Hyper-V/rede | Hyper-V esta fora da trilha oficial de release |
| [`network-hyperv-refactor-and-update-plan.md`](experimental/network-hyperv-refactor-and-update-plan.md) | Refatoracao de rede Hyper-V | Hyper-V esta fora da trilha oficial de release |

## Regras de manutencao

- planos em `active/` podem orientar implementacao;
- planos em `historical/` e `experimental/` precisam ser reconciliados com a
  trilha oficial `VMware + UEFI + E1000` antes de virar tarefa;
- planos `Substituido` nao devem receber novas tarefas;
- ao mover um plano entre pastas, atualizar tambem [`STATUS.md`](STATUS.md) e o
  campo de evidencia/proximo passo do item correspondente em
  [`active/capyos-robustness-master-plan.md`](active/capyos-robustness-master-plan.md).
