# CapyOS — política de desenvolvimento desacoplado

**Status:** referência arquitetural para projetos desenvolvidos fora do repositório principal.
**Autoridade:** o plano mestre continua sendo a fonte de verdade para a sequência do sistema base.

## Objetivo

Permitir que componentes de alto nível evoluam em projetos apartados sem quebrar a integração futura com o CapyOS. O sistema base deve se comprometer apenas com contratos estáveis, adaptadores pequenos e gates de integração.

## Regra principal

Um componente pode ser desenvolvido fora do CapyOS quando tiver:

- contrato de entrada/saída versionado;
- runner ou biblioteca host-testável;
- backend mock sem dependência de kernel;
- limites explícitos de memória, tempo e permissões;
- golden tests compartilháveis;
- adaptador CapyOS pequeno e substituível.

Se uma parte precisa de MMIO, DMA, interrupções, memória física, boot, storage real ou política de segurança integrada, ela pertence ao sistema base.

## O que fica no sistema base

O repositório CapyOS deve priorizar:

- boot UEFI/GPT x86_64;
- kernel, scheduler, memória, processos e syscalls nativas;
- drivers, device manager, DMA, IRQ, hotplug e fallback;
- storage, filesystem, criptografia integrada, auth e update;
- rede base, TLS userland, sockets e política de certificados;
- compositor, input, janelas, timers e APIs nativas estáveis;
- sandbox, permissões, ABI pública, package install e integração final;
- gates oficiais e evidência de release.

## O que deve ser desacoplado

Projetos apartados recomendados:

- CapyLang core;
- CapyBrowse Text core e HTML-to-text;
- HTML/CSS static layout core;
- formato de pacotes/manifests e resolver;
- modelo de widgets CapyUI retained-mode;
- benchmark harness e jogos de benchmark;
- decoders/codecs puros de mídia;
- fixtures/golden tests e specs.

## Componentes acoplados existentes a conter ou extrair

| Área | Estado atual | Direção recomendada |
|---|---|---|
| `html_viewer`/browser histórico | implementação removida do core ativo; `browser_homepage` permanece como configuração do sistema base | manter browser core fora do sistema até adaptador da etapa correta |
| CapyUI/widgets | fundação GUI já integrada ao desktop com widgets/context menu/inline prompt ativos | preservar compositor/input no sistema; separar modelo de widget, layout e display list como contrato |
| Package/update | update e assinatura já integrados | manter update agent no sistema; desenvolver `.capypkg`/resolver/manifest como lógica pura antes do installer real |
| Decoders de imagem | loaders legados removidos do tree; CapyOS não envia decoders de imagem até existir adaptador `gui/codecs/` apropriado | manter backend de desenho no sistema; tratar codecs novos como bibliotecas puras com limites/fuzz/golden tests |
| CapyLang planejada | ainda etapa futura | desenvolver core fora; integrar só por host ABI versionada e sandbox |
| Benchmarks Snake/Asteroids | planejados para CapyLang | desenvolver jogos contra backend mock; integrar quando CapyLang bindings 2D/input/timer existirem |

## Repositórios externos atuais

O registro de migração e ownership externo fica em
`docs/reference/integration/external-core-repositories.md`. A fronteira de
instalação modular fica em
`docs/reference/integration/modular-installation-architecture.md`. A higiene
total de migração (concluída — fontes/headers legados removidos do tree)
está documentada em
`docs/reference/integration/core-migration-quarantine.md`. O adaptador
in-tree ativo para pacotes Capy remotos é `services/capypkg` com CLI
`pkg-*` em `capysh`.

Estado inicial:

- `CapyBrowser`: browser core novo ainda precisa ser implementado fora do
  sistema; codecs agora pertencem ao `CapyCodecs`.
- `CapyAgent`: modelo/resolver inicial de pacotes, índice de componentes por
  tag release e manifesto de release extraídos/inicializados fora do sistema.
- `CapyCodecs`: snapshots BMP/PNG/JPEG extraídos como codecs portáteis.
- `CapyUI`: widget tree/event routing extraído como modelo portátil.
- `CapyBenchmark`: report/baseline evaluator inicializado como harness portátil.
- `CapyLang`: repositório inicializado; um protótipo in-tree foi identificado
  e fica fora do link padrão até migração por host ABI na etapa correta.

Distribuição modular alpha:

- usar tags de release GitHub, sha256 e ABI mínima como fluxo inicial;
- manter assinatura/certificado como hardening obrigatório antes de uma cadeia
  oficial de updates para usuários finais.

## Contrato mínimo de integração

Cada projeto desacoplado precisa declarar:

1. **Nome e versão de contrato.** Ex.: `capylang-host-abi-v1`.
2. **Formato de artefato.** Ex.: bytecode, JSON manifest, display list, RGBA/PCM.
3. **Chamadas permitidas ao host.** Ex.: tabela de funções, sem syscalls diretas.
4. **Modelo de erro.** Códigos determinísticos, fail-closed quando aplicável.
5. **Limites.** Memória máxima, tempo por frame/request, tamanho de input.
6. **Segurança.** Sandbox, permissões, validação, dados sensíveis e logs.
7. **Testes.** Golden fixtures, fuzz quando pertinente e runner host.
8. **Gate de integração.** Smoke/gate externo recomendado no CapyOS.
9. **Perfil de instalação.** Basic/Recommended/Custom, dependências e fallback.

## Critérios para importar para o CapyOS

Um projeto apartado só entra no repositório principal quando:

- o contrato está documentado em `docs/reference/integration/`;
- existe suite host-side reproduzível fora do CapyOS;
- o adaptador CapyOS é pequeno e isolado;
- o sistema base já possui APIs necessárias;
- não adiciona dependências Linux ao kernel base;
- passa por revisão de segurança/privacidade/desempenho;
- o plano mestre aponta a etapa correta para integração.

## Antiobjetivos

- Não transformar projetos apartados em progresso oficial da sequência ativa.
- Não portar dependências grandes sem ABI/sandbox/gates.
- Não acoplar CapyLang, browser ou package manager diretamente a internals do kernel.
- Não permitir que um app/script chame syscall ou ponteiro de kernel diretamente.
- Não mover a plataforma oficial antes de evidência externa.

## Validação

Esta política é documental. Mudanças futuras que movam código devem recomendar validação externa conforme área tocada, tipicamente `make layout-audit`, `make test`, `make all64` e gates smoke específicos executados fora desta máquina.
