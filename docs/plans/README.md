# Indice De Planos CapyOS

Este indice classifica os planos em vigor para evitar fontes de verdade
paralelas. Quando um plano novo substituir um antigo, atualize esta tabela e o
`docs/plans/capyos-robustness-master-plan.md`.

Estados aceitos neste indice:
- `Ativo`: fonte valida para decisao tecnica atual.
- `Historico`: contexto antigo, nao deve guiar implementacao nova sozinho.
- `Experimental`: investigacao ou trilha de laboratorio sem promessa de release.
- `Substituido`: mantido apenas por rastreabilidade; use o documento indicado.

| Documento | Estado | Papel atual | Fonte preferida / observacao |
|---|---|---|---|
| `capyos-robustness-master-plan.md` | Ativo | Plano vivo principal de robustez, status e evidencias | Fonte primaria para M0-M8 |
| `system-master-plan.md` | Ativo | Visao macro de evolucao do sistema | Usar junto do plano de robustez |
| `system-roadmap.md` | Ativo | Roadmap tecnico por dominio | Alinhar status no plano de robustez |
| `system-execution-plan.md` | Ativo | Sequencia operacional do ciclo atual | Validar contra gates atuais antes de executar |
| `capyos-master-improvement-plan.md` | Ativo | Consolidacao tecnica recente da trilha x64 | Complementa o plano de robustez |
| `platform-hardening-plan.md` | Ativo | Hardening de plataforma x64 | Fonte de detalhes para M1 e M6 |
| `browser-status-roadmap.md` | Ativo | Roadmap especifico do browser | Fonte de detalhes para M8 |
| `source-organization-roadmap.md` | Ativo | Reducao de monolitos e organizacao de codigo | Fonte de detalhes para M3 |
| `mvp-implementation-plan.md` | Historico | Plano operacional anterior do MVP | Conferir com o plano de robustez antes de retomar tarefas |
| `architecture-restructure-and-improvements.md` | Historico | Ideias de reorganizacao arquitetural | Preferir `source-organization-roadmap.md` e `architecture/source-layout.md` |
| `improvement-audit.md` | Historico | Auditoria de melhorias ja levantadas | Migrar itens relevantes para o plano de robustez |
| `refactor-plan.md` | Historico | Plano de refatoracao da migracao x64 | Preferir `source-organization-roadmap.md` |
| `system-delivery-roadmap.md` | Historico | Roadmap de entrega anterior | Preferir `system-execution-plan.md` e o plano de robustez |
| `hyperv-network-reset-plan.md` | Experimental | Investigacao Hyper-V/rede | Hyper-V esta fora da trilha oficial de release |
| `network-hyperv-refactor-and-update-plan.md` | Experimental | Refatoracao de rede Hyper-V | Hyper-V esta fora da trilha oficial de release |

Regra de manutencao:
- planos `Ativo` podem orientar implementacao;
- planos `Historico` e `Experimental` precisam ser reconciliados com a trilha
  oficial `VMware + UEFI + E1000` antes de virar tarefa;
- planos `Substituido` nao devem receber novas tarefas.
