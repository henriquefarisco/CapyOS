# Contratos de integração para projetos desacoplados

Esta pasta contém contratos para componentes que podem ser desenvolvidos fora do repositório principal do CapyOS e integrados depois por adaptadores pequenos.

## Regra

O plano mestre continua definindo quando uma integração pode entrar no sistema base. Estes documentos não liberam implementação fora de ordem; apenas reduzem risco de incompatibilidade futura.

## Docs autoritativos cross-repo (leia primeiro)

- [`compatibility-matrix.md`](compatibility-matrix.md) — matriz de versões pinadas e ABIs por repositório
- [`capypkg-publisher-manifest-format.md`](capypkg-publisher-manifest-format.md) — formato canônico do manifest line-oriented que o adapter consome
- [`compatibility-audit-2026-05-19.md`](compatibility-audit-2026-05-19.md) — auditoria estática cross-repo (2026-05-19)
- [`../../operations/manual-module-deploy-runbook.md`](../../operations/manual-module-deploy-runbook.md) — runbook de deploy manual de módulos remotos
- [`../../architecture/capypkg-adapter.md`](../../architecture/capypkg-adapter.md) — design e rationale do adapter in-tree

## Repositórios externos

O registro de owners externos, snapshots migrados e pendências locais fica em:

- [`external-core-repositories.md`](external-core-repositories.md)
- [`tag-release-component-index.md`](tag-release-component-index.md)
- [`modular-installation-architecture.md`](modular-installation-architecture.md)
- [`core-migration-quarantine.md`](core-migration-quarantine.md)

## Contratos

| Documento | Projeto apartado | Integração planejada |
|---|---|---|
| [`core-migration-quarantine.md`](core-migration-quarantine.md) | higiene de migração concluída | sources/headers legados removidos do core; adaptador `services/capypkg` ativo |
| [`modular-installation-architecture.md`](modular-installation-architecture.md) | todos os projetos apartados instaláveis | Etapas 8-9 para installer/package; contratos podem evoluir antes |
| [`capylang-integration-contract.md`](capylang-integration-contract.md) | CapyLang core, VM, bytecode e stdlib mínima | Etapa 15 |
| [`browser-core-integration-contract.md`](browser-core-integration-contract.md) | CapyBrowse Text core, HTML-to-text, HTML/CSS estático | Etapas 6-7 |
| [`package-format-integration-contract.md`](package-format-integration-contract.md) | `.capypkg`, manifest, resolver e plano de rollback | Etapa 9 |
| [`capyui-widget-integration-contract.md`](capyui-widget-integration-contract.md) | Widget model, layout, display list e event routing abstrato | Etapas 4 e 6 |
| [`media-codec-integration-contract.md`](media-codec-integration-contract.md) | Decoders/codecs puros de imagem, áudio e vídeo | Etapas 6-7 e 10 |
| [`benchmark-harness-integration-contract.md`](benchmark-harness-integration-contract.md) | Harness, replay, métricas e baseline de benchmark | Etapas 15-16 |

## Critério de aceite para qualquer projeto apartado

Antes de entrar no CapyOS, o projeto precisa ter:

- contrato versionado;
- runner host ou biblioteca testável fora do CapyOS;
- golden tests;
- limites de memória/tempo/input;
- modelo de erro;
- política de segurança;
- adaptador CapyOS pequeno;
- gate externo/manual definido no plano;
- versão pinada em [`compatibility-matrix.md`](compatibility-matrix.md);
- (quando for entrar como pacote remoto) descritor compatível com [`capypkg-publisher-manifest-format.md`](capypkg-publisher-manifest-format.md).
