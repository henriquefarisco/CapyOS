# Documentacao do CapyOS

Indice principal da documentacao ativa do projeto.

Escopo atual:
- nome oficial do sistema: `CapyOS`
- trilha suportada: `UEFI/GPT/x86_64`
- caminho validado: provisionamento por script + boot por HDD + login + CLI

## Estrutura

- `architecture/system-overview.md`
  - visao geral da arquitetura atual, boot, storage, drivers e lacunas
- `setup/hyper-v.md`
  - guia historico de investigacao; Hyper-V nao e trilha suportada de release
- `testing/boot-and-cli-validation.md`
  - roteiro de validacao do boot, login, CLI e persistencia
- `reference/cli-reference.md`
  - referencia dos comandos do CapyCLI
- `plans/mvp-implementation-plan.md`
  - plano operacional do ciclo atual
- `plans/platform-hardening-plan.md`
  - detalha o fechamento da etapa de hardening da plataforma x64
- `plans/system-master-plan.md`
  - plano-mestre de evolucao do sistema inteiro, cobrindo fundacoes faltantes,
    seguranca, performance, GUI, atualizacoes, apps e linguagem propria
- `plans/capyos-master-improvement-plan.md`
  - consolidacao tecnica mais recente da trilha x64, com foco em desktop,
    limpeza do legado 32-bit, drivers e verificacao para `develop`
- `plans/system-execution-plan.md`
  - sequencia de execucao recomendada a partir do estado atual do projeto,
    incluindo progresso, gates de release e proximos marcos
- `plans/refactor-plan.md`
  - fechamento da migracao para a trilha unica x64
- `plans/system-roadmap.md`
  - roadmap tecnico por dominio
- `releases/README.md`
  - indice das release notes
- `archive/`
  - material historico, checkpoints antigos e planos descontinuados

## Convencoes

- documentos ativos ficam em pastas por dominio
- nomes de arquivo seguem `lowercase-kebab-case`
- material historico deve ficar em `archive/`
- release notes antigas podem citar identificadores tecnicos antigos ou fluxos
  legados; nesses casos, trate o arquivo como contexto historico, nao como
  instrucoes atuais

## Politica de branches

- `main`
  - branch estavel
  - referencia para release e canal `stable`
- `develop`
  - branch de integracao
  - referencia para o canal `develop`
- branches de feature
  - servem para implementacao e consolidacao tematica antes do merge

Fluxo esperado:
1. feature branch
2. merge em `develop`
3. validacao
4. promocao para `main`

## Leitura recomendada

1. `../README.md`
2. `architecture/system-overview.md`
3. `plans/system-master-plan.md`
4. `plans/system-execution-plan.md`
5. `testing/boot-and-cli-validation.md`
6. `releases/README.md`
