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
  - guia de provisionamento e boot no Hyper-V Gen2
- `testing/boot-and-cli-validation.md`
  - roteiro de validacao do boot, login, CLI e persistencia
- `reference/cli-reference.md`
  - referencia dos comandos do CapyCLI
- `plans/mvp-implementation-plan.md`
  - plano operacional do ciclo atual
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

## Leitura recomendada

1. `../README.md`
2. `architecture/system-overview.md`
3. `setup/hyper-v.md`
4. `testing/boot-and-cli-validation.md`
5. `releases/README.md`
