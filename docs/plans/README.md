# Indice de planos CapyOS

> **Atualizado em 2026-05-15** — Etapas 3-15 reorganizadas por ROI ao usuário desktop comum e expandidas para 14 etapas (3-16) sem violar a regra sequencial estrita. Sequência antiga preservada em [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).
>
> **Histórico anterior (2026-05-10):** reorganização do plano ativo para uma sequência bloqueante. Entregas concluídas até `0.8.0-alpha.93+20260510` foram movidas para documentação histórica.
>
> A pasta `active/` continua com **um único plano vivo**.
>
> **Visão executiva:** [`STATUS.md`](STATUS.md) mostra a próxima etapa permitida e as etapas bloqueadas.

## Estrutura de pastas

```
docs/plans/
├── README.md                # este índice
├── STATUS.md                # status centralizado da sequência ativa
├── active/                  # plano vigente que orienta implementação
├── historical/              # planos legados e entregas consolidadas
└── experimental/            # trilhas de laboratório fora da release oficial
```

## Estados aceitos

- `Ativo`: fonte válida para decisão técnica atual.
- `Histórico`: entrega consolidada ou plano arquivado, sem orientar implementação
  nova sozinho.
- `Experimental`: investigação sem promessa de release.
- `Substituído`: mantido por rastreabilidade.

## Plano ativo

| Documento | Papel atual |
|---|---|
| [`capyos-master-plan.md`](active/capyos-master-plan.md) | Fonte de verdade única. Define a sequência bloqueante Etapa 1 → Etapa 16 reorganizada por ROI ao usuário desktop comum em 2026-05-15: CapyUI polish (1), sessão gráfica (2), drivers+USB HID+storage (3), CapyDisplay 2D+scheduler (4), TLS real (5), apps maduros (6), browser usável (7), release+instalador (8), package manager+SDK (9), áudio+multimídia (10), WiFi+power (11), JS engine (12), CapyLX unificado (13), Wayland (14), Mesa+CapyLang (15) e hardening 1.0 (16). |

## Entregas históricas consolidadas

| Documento | Papel | Consolidado em |
|---|---|---|
| [`implementation-delivered-through-alpha93.md`](historical/implementation-delivered-through-alpha93.md) | Implementação finalizada até `0.8.0-alpha.93+20260510`; substitui checklists concluídos no plano ativo. | 2026-05-10 |
| [`capyos-master-plan-legacy-through-alpha93.md`](historical/capyos-master-plan-legacy-through-alpha93.md) | Snapshot do master plan antes da reorganização sequencial. | 2026-05-10 |
| [`capyos-status-legacy-through-alpha93.md`](historical/capyos-status-legacy-through-alpha93.md) | Snapshot do STATUS antes da remoção de itens concluídos do plano ativo. | 2026-05-10 |
| [`capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md) | Snapshot da sequência Etapas 3-15 do master plan antes da reordenação por ROI ao usuário desktop comum. | 2026-05-15 |
| [`f3-browser-delivered.md`](historical/f3-browser-delivered.md) | F3 browser ring-3 histórico. | 2026-05-03 |
| [`ux-w7-ish-delivered.md`](historical/ux-w7-ish-delivered.md) | UX W7-ish histórica entregue. | 2026-05-03 |
| [`f3-3c-html-viewer-userland-slicing.md`](historical/f3-3c-html-viewer-userland-slicing.md) | Fatiamento da migração parser/render/raster/fetch para ring 3. | 2026-05-03 |
| [`f3-3f-browser-desktop-wiring.md`](historical/f3-3f-browser-desktop-wiring.md) | Wiring desktop↔engine ring-3 histórico. | 2026-05-03 |

## Planos históricos anteriores

| Documento | Observação |
|---|---|
| [`capyos-robustness-master-plan.md`](historical/capyos-robustness-master-plan.md) | Plano de robustez consolidado no master legado. |
| [`m5-userland-progress.md`](historical/m5-userland-progress.md) | Progresso M5 userland consolidado. |
| [`post-m5-ux-followups.md`](historical/post-m5-ux-followups.md) | Follow-ups W1-W3 consolidados. |
| [`system-master-plan.md`](historical/system-master-plan.md) | Plano sistêmico antigo. |
| [`system-roadmap.md`](historical/system-roadmap.md) | Roadmap técnico antigo. |
| [`system-execution-plan.md`](historical/system-execution-plan.md) | Sequência de execução antiga. |
| [`capyos-master-improvement-plan.md`](historical/capyos-master-improvement-plan.md) | Plano de melhoria antigo. |
| [`browser-status-roadmap.md`](historical/browser-status-roadmap.md) | Roadmap antigo do browser. |
| [`source-organization-roadmap.md`](historical/source-organization-roadmap.md) | Regras de organização de código consolidadas. |
| [`m4-finalization-progress.md`](historical/m4-finalization-progress.md) | M4.1-M4.5 entregues. |
| [`platform-hardening-plan.md`](historical/platform-hardening-plan.md) | Hardening de plataforma anterior. |
| [`mvp-implementation-plan.md`](historical/mvp-implementation-plan.md) | Plano operacional MVP encerrado. |
| [`architecture-restructure-and-improvements.md`](historical/architecture-restructure-and-improvements.md) | Substituído por docs de arquitetura/layout. |
| [`improvement-audit.md`](historical/improvement-audit.md) | Auditoria de melhoria antiga. |
| [`refactor-plan.md`](historical/refactor-plan.md) | Plano de refactor antigo. |
| [`system-delivery-roadmap.md`](historical/system-delivery-roadmap.md) | Roadmap de entrega antigo. |

## Planos experimentais

| Documento | Trilha | Observação |
|---|---|---|
| [`hyperv-network-reset-plan.md`](experimental/hyperv-network-reset-plan.md) | Investigação Hyper-V/rede | Hyper-V está fora da trilha oficial de release. |
| [`network-hyperv-refactor-and-update-plan.md`](experimental/network-hyperv-refactor-and-update-plan.md) | Refatoração de rede Hyper-V | Hyper-V está fora da trilha oficial de release. |

## Regras de manutenção

1. `active/` deve manter um único plano vivo.
2. Nenhuma etapa posterior inicia antes da anterior fechar 100%.
3. Nenhuma promoção para `Implementado`/✅ sem evidência.
4. Mover documento entre pastas exige atualizar `STATUS.md` e este índice.
5. Plataforma oficial: `VMware + UEFI + E1000`.
