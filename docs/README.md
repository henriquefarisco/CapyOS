# Documentacao do CapyOS

Indice principal da documentacao ativa do projeto.

Escopo atual:

- nome oficial do sistema: `CapyOS`
- trilha suportada: `UEFI/GPT/x86_64`
- caminho validado: provisionamento por script + boot por HDD + login + CLI

## Estrutura

- `architecture/system-overview.md`
  - visao geral da arquitetura atual, boot, storage, drivers e lacunas
- `project-overview.md`
  - visao tecnica compacta do projeto, compatibilidade, funcionalidades,
    validacao e roadmap macro
- `architecture/capyui-shell-polish-v1.md`
  - polish visual, launcher, wallpaper, decoracao e tray avancado do CapyUI
    Shell v1 fechado
- `architecture/graphical-session-operational.md`
  - Etapa 2 da sessão gráfica operacional, frame pacing e próximos slices
- `setup/hyper-v.md`
  - guia historico de investigacao; Hyper-V e compatibilidade oficial planejada,
    mas ainda fora da plataforma atual ate gates dedicados
- `testing/boot-and-cli-validation.md`
  - roteiro de validacao do boot, login, CLI e persistencia
- `testing/pr-and-release-checklist.md`
  - checklist humano de PR/release que complementa os gates automaticos
    (`make test`, `make layout-audit`, `make release-check` e
    `make smoke-marker-policy-selftest` quando aplicavel)
- `security/release-signing.md`
  - procedimento operacional de assinatura Ed25519 de checksums de release,
    chave offline, publicacao e rotacao
- `legal/third-party-ui-assets.md`
  - origem e licencas dos icones e cursores de terceiros usados como base
    visual do CapyUI
- `reference/cli-reference.md`
  - referencia dos comandos do CapyCLI
- `operations/update-from-github.md`
  - procedimento operacional do fetch remoto de manifestos assinados pelo
    `update-agent`
- `operations/release-ci-smoke-readiness.md`
  - gate publico de prontidao do smoke VMware oficial antes de ligar a VM real
- `operations/release-ci-smoke-evidence.md`
  - manifesto publico das evidencias pos-smoke VMware oficial
- `operations/release-ci-smoke-acceptance.md`
  - gate publico de aceitacao das evidencias pos-smoke VMware oficial
- `operations/release-ci-smoke-promotion.md`
  - gate publico de promocao pos-smoke VMware oficial
- `plans/mvp-implementation-plan.md`
  - plano operacional do ciclo atual
- `plans/README.md`
  - indice de governanca dos planos ativos, historicos e experimentais
- `plans/platform-hardening-plan.md`
  - detalha o fechamento da etapa de hardening da plataforma x64
- `plans/browser-status-roadmap.md`
  - fonte de verdade do programa do navegador, com status atual, roadmap e
    criterios de validacao por fase
- `screenshots/README.md`
  - politica de snapshots visuais do sistema por versao alpha/beta/stable
- `plans/source-organization-roadmap.md`
  - roadmap incremental para reduzir monolitos, separar modulos/linguagens e
    estabilizar a arquitetura de pastas
- `architecture/source-layout.md`
  - regras oficiais de organizacao de codigo e uso de `make layout-audit`
- `architecture/decoupled-development-contracts.md`
  - politica para desenvolver CapyLang, browser core, package format, widgets,
    codecs e benchmarks fora do sistema base sem quebrar a integracao futura
- `architecture/capypkg-adapter.md`
  - design, fronteiras de seguranca e API do adaptador `services/capypkg`
    para receber pacotes Capy remotos verificados via `capysh`
- `reference/integration/README.md`
  - indice dos contratos de integracao de projetos desacoplados
- `reference/integration/external-core-repositories.md`
  - registro de ownership externo e snapshots migrados para repos core
- `reference/integration/tag-release-component-index.md`
  - contrato alpha para instalação/update modular por tag release, sha256 e ABI
- `reference/integration/compatibility-matrix.md`
  - matriz autoritativa de versões pinadas e ABIs cross-repo
- `reference/integration/capypkg-publisher-manifest-format.md`
  - formato canônico do manifest line-oriented consumido pelo adapter in-tree
- `reference/integration/compatibility-audit-2026-05-19.md`
  - auditoria estática cross-repo de 2026-05-19
- `operations/manual-module-deploy-runbook.md`
  - runbook de deploy manual de módulos remotos durante a instalação do core
- `architecture/libcapy-net-http-hardening.md`
  - contrato de seguranca do request-target HTTP em `libcapy-net`
- `architecture/libcapy-tls-userland-contract.md`
  - contrato da API TLS userland fail-closed de F4
- `architecture/libcapy-net-https-fail-closed.md`
  - contrato do adaptador HTTPS fail-closed em `libcapy-net`
- `architecture/libcapy-tls-hostname-contract.md`
  - contrato de hostname da fronteira TLS userland
- `architecture/libcapy-tls-peer-verification-contract.md`
  - contrato de peer verification obrigatório em `libcapy-tls`
- `architecture/libcapy-tls-timeout-contract.md`
  - contrato da janela de timeout em `libcapy-tls`
- `architecture/libcapy-tls-config-snapshot.md`
  - snapshot interno de configuração efetiva em `libcapy-tls`
- `architecture/libcapy-tls-context-prep.md`
  - contexto interno preparado em `libcapy-tls`
- `architecture/libcapy-tls-context-lifecycle.md`
  - ciclo de vida interno do contexto em `libcapy-tls`
- `architecture/libcapy-tls-context-slot.md`
  - slot interno gerenciado de contexto em `libcapy-tls`
- `architecture/libcapy-tls-connect-slot.md`
  - conexão TLS fail-closed usando slot gerenciado
- `architecture/libcapy-tls-backend-stub.md`
  - backend stub fail-closed em `libcapy-tls`
- `architecture/libcapy-tls-backend-state.md`
  - estado interno de backend em `libcapy-tls`
- `architecture/libcapy-tls-backend-plan.md`
  - plano interno fail-closed do backend BearSSL userland em `libcapy-tls`
- `architecture/libcapy-tls-bearssl-state.md`
  - estado BearSSL userland reservado metadata-only em `libcapy-tls`
- `architecture/libcapy-tls-bearssl-adapter.md`
  - adaptador BearSSL userland metadata-only em `libcapy-tls`
- `architecture/libcapy-tls-trust-metadata.md`
  - metadados de trust anchors em `libcapy-tls`
- `architecture/libcapy-tls-trust-source.md`
  - fonte e bundle userland metadata-only de trust anchors em `libcapy-tls`
- `architecture/kernel-tls-hostname-contract.md`
  - contrato de hostname do TLS kernel-side legado
- `architecture/tls-hostname-shared-policy.md`
  - política compartilhada de hostname TLS entre kernel e userland
- `architecture/etapa-3-driver-foundation-plan.md`
  - plano de slice da Etapa 3 (XHCI + USB HID + storage); preparação técnica subordinada ao master plan
- `architecture/monolith-refactor-roadmap.md`
  - roadmap ativo de divisão dos arquivos C de runtime e teste acima de 900 LOC; rastreia splits feitos e pendentes
- `plans/active/capyos-master-plan.md`
  - plano-mestre ativo e sequencial do CapyOS, com execução bloqueante da
    Etapa 1 ate a Etapa 16 (reorganizado por ROI ao usuário desktop comum em 2026-05-15)
- `plans/STATUS.md`
  - visão executiva da próxima etapa permitida e das etapas bloqueadas
- `plans/historical/implementation-delivered-through-alpha93.md`
  - implementação finalizada até `0.8.0-alpha.93+20260510`
- `plans/README.md`
  - índice dos planos ativos, históricos e experimentais
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
3. `plans/active/capyos-master-plan.md`
4. `plans/STATUS.md`
5. `plans/historical/implementation-delivered-through-alpha93.md`
6. `testing/boot-and-cli-validation.md`
7. `releases/README.md`
