# CapyOS - Plano-Mestre de Evolucao do Sistema

Data de referencia: 2026-04-08.
Escopo: validar o estado estrutural do CapyOS no repositorio atual e definir
um caminho tecnico completo ate um sistema solido, com boa base de
performance, seguranca, interface grafica com mouse, softwares basicos,
atualizacoes automaticas e linguagem propria.

Este documento e deliberadamente mais amplo que:
- `docs/plans/system-delivery-roadmap.md`
- `docs/plans/system-roadmap.md`
- `docs/plans/platform-hardening-plan.md`

Esses documentos continuam uteis para backlog tatico. O objetivo aqui e
organizar a arquitetura-alvo do produto inteiro e a sequencia correta de
implantacao.

Complemento operacional:
- `docs/plans/system-execution-plan.md`
  - traduz este plano-mestre em uma sequencia de execucao concreta, com
    progresso estimado, gates de release e marcos imediatos

## 1. Base de validacao

Este plano foi alinhado com o estado real do repositorio, principalmente a
partir de:
- `README.md`
- `docs/architecture/system-overview.md`
- `docs/plans/system-delivery-roadmap.md`
- `docs/plans/system-roadmap.md`
- `docs/plans/platform-hardening-plan.md`
- `src/arch/x86_64/kernel_main.c`
- `src/arch/x86_64/kernel_shell_runtime.c`
- `src/core/system_init.c`
- `include/boot/handoff.h`
- `Makefile`

## 2. Diagnostico executivo

O CapyOS ja deixou de ser apenas bring-up de boot. O projeto ja tem uma trilha
real:
- `UEFI/GPT/x86_64` como caminho oficial
- loader UEFI funcional
- kernel x64 com framebuffer, shell e runtime de storage
- volume `DATA` cifrado
- `CAPYFS` persistente
- setup inicial, login e CLI
- testes de host e smokes automatizados

Mas o sistema ainda nao e estruturalmente solido como SO geral. Hoje ele e um
kernel/runtime integrado com shell, filesystem proprio e parte do fluxo de
plataforma. O que falta para virar um sistema operacional maduro nao e uma
colecao aleatoria de features; faltam blocos fundacionais.

Resumo pragmatico:
- o projeto esta pronto para ser endurecido e transformado em plataforma
- nao esta pronto ainda para pular direto para desktop completo, browser,
  apps avancados ou linguagem propria
- a ordem de implantacao importa muito: se GUI, apps e linguagem vierem antes
  de scheduler, ABI, servicos, sockets, observabilidade e atualizacao
  transacional, o sistema vai crescer de forma fragil

## 3. Estado atual validado

## 3.1 O que existe hoje

### Boot e plataforma
- `BOOTX64.EFI` + handoff x64 funcional.
- `UEFI/GPT/x86_64` e a unica trilha suportada de verdade.
- o boot por disco provisionado, instalacao por ISO e reboot com persistencia
  ja fazem parte do fluxo documentado.

### Storage e filesystem
- `CAPYFS` operacional com VFS proprio.
- volume persistente cifrado no `DATA`.
- estrutura base do sistema gerada no primeiro boot.
- buffer cache e sincronizacao explicita.

### Seguranca atual
- AES-XTS + PBKDF2-SHA256 para o volume.
- banco de usuarios proprio com salt e hash por usuario.
- sessao autenticada e configuracao persistente.

### Runtime e shell
- CapyCLI modular.
- setup inicial, login, sessao, logout e persistencia basica.
- configuracao global e parte da configuracao por usuario.

### Testes e tooling
- `make test`
- `make all64`
- `make iso-uefi`
- smokes de CLI e ISO
- scripts de provisionamento e auditoria

## 3.2 O que nao existe ainda como fundacao

### Execucao e isolamento
- nao existe modelo de processos robusto
- nao existe userspace separado do kernel com fronteira clara
- nao existe ABI de syscall madura
- nao existe scheduler preemptivo/base multitarefa de verdade
- nao existe modelo de IPC padronizado

### Memoria
- nao existe memoria virtual completa orientada a processos
- nao existe espaco de enderecamento por processo
- nao existe paginacao com isolamento, copy-on-write e politicas maduras
- nao existe allocator por dominio com observabilidade real

### Rede e servicos
- nao existe stack de sockets completa para userland
- nao existe TLS/PKI pronta para apps e updates
- nao existe gerenciador de servicos/daemons
- nao existe firewall maduro nem auditoria de rede

### UI/desktop
- nao existe stack grafica completa
- nao existe mouse suportado de ponta a ponta como produto
- nao existe compositor, gerenciador de janelas ou toolkit
- nao existe formato de app grafico ou runtime de apps

### Distribuicao e atualizacao
- nao existe package manager
- nao existe repositorio oficial de artefatos instalaveis
- nao existe mecanismo de atualizacao transacional
- nao existe rollback automatico de update
- nao existe cadeia de assinatura de update

### Engenharia de produto
- nao existe CI/release pipeline endurecido ate artefato assinavel
- nao existe estrategia de crash dump e diagnostico persistente madura
- nao existe estrategia de compatibilidade/binario/app

## 4. Lacunas estruturantes prioritarias

| Dominio | Situacao atual | O que falta | Risco de ignorar |
|---|---|---|---|
| Boot trust chain | loader + handoff estaveis | Secure Boot, medicao, assinatura, rollback de boot | update quebrar boot e dificultar recuperacao |
| Kernel core | boot/login/CLI funcionais | scheduler, syscalls, isolamento, faults robustos | features altas construidas sobre runtime fragil |
| Memoria | allocator e runtime inicial | VM por processo, pager, protecao de paginas | qualquer userspace real vira risco de corrupcao |
| FS e storage | CAPYFS ja funciona | journal, fsck, recovery, quotas, integridade | perda de dados e corrupcao silenciosa |
| Seguranca | cifra de volume e auth basica | integridade autenticada, auditoria, sandbox, politicas | sistema vulneravel quando ganhar rede/apps |
| Rede | stack parcial e drivers iniciais | sockets, TLS, DHCP completo, DNS robusto, firewall | impossivel sustentar updates, browser e servicos |
| Servicos | shell e bootstrap | service manager, timers, jobs, workers | sistema sem background controlado |
| GUI | framebuffer e splash | mouse, compositor, janelas, toolkit, font stack | desktop improvisado e instavel |
| Apps | shell modular | formato de app, ABI, runtime, package manager | software basico vira amontoado de binarios acoplados |
| Updates | build/ISO manuais | repo, manifestos, assinatura, staged rollout, rollback | cada release vira reinstalacao |
| Observabilidade | logs e smokes parciais | logs persistentes, traces, metrics, crash dump | regressao dificil de localizar |
| Dev platform | C/ASM e scripts | SDK, CI/CD, docs de API, linguagem propria | ecossistema nao escala |

## 5. Arquitetura-alvo do produto

## 5.1 Visao-alvo

O CapyOS solido deve convergir para a arquitetura abaixo:

1. Firmware/boot
- UEFI
- loader assinado
- kernel assinado
- manifestos de boot versionados
- fallback/rollback de versao anterior

2. Kernel
- gerenciamento de memoria virtual
- scheduler preemptivo
- interrupcoes, timers e workers
- VFS + CAPYFS endurecido
- stack de rede com sockets
- GPU/framebuffer base
- input keyboard/mouse/USB
- IPC e syscalls

3. Userspace base
- `init`/service manager
- runtime de sessoes
- shell
- bibliotecas base
- package/update daemon
- configuracao persistente

4. Desktop
- compositor/window server
- terminal grafico
- file manager
- editor de texto
- painel de configuracoes
- central de software/update
- utilitarios de rede, armazenamento e seguranca

5. Plataforma de apps
- ABI estavel
- formato de pacote
- permissao/sandbox
- toolkit grafico
- APIs de FS, rede, UI, notificacoes, clipboard, configuracao

6. Plataforma de desenvolvimento
- SDK
- documentacao de syscalls/APIs
- runtime de linguagem propria
- toolchain oficial

## 5.2 Principios arquiteturais obrigatorios

1. Nao continuar expandindo feature de produto sem fronteira clara entre
   kernel e userland.
2. Toda nova capacidade de sistema deve nascer observavel, testavel e com
   criterio de rollback.
3. Update automatico deve ser tratado como parte da arquitetura, nao como
   script tardio.
4. GUI nao deve nascer acoplada ao boot path.
5. Linguagem propria nao deve nascer antes de existir runtime de processos,
   package manager e APIs minimamente estaveis.

## 5.3 Canais de entrega e governanca de branches

Para evitar que o sistema de atualizacao e a governanca do repositorio sigam
em direcoes diferentes, a politica de branch deve ser parte da arquitetura:

- `main`
  - canal estavel
  - corresponde ao channel `stable`
  - so recebe mudancas com build, testes e smokes fechados
- `develop`
  - canal de integracao
  - corresponde ao channel `develop`
  - recebe consolidacao de features antes de promocao

Consequencia tecnica:
- o `update-agent` precisa tratar `main` e `develop` como trilhas distintas
- manifestos, staging e rollback devem preservar o branch/channel de origem
- a promocao `develop -> main` deve ser uma decisao de release, nao um efeito
  colateral de desenvolvimento diario

## 6. Metas transversais

## 6.1 Performance

Metas:
- reduzir boot ate login em VM oficial
- reduzir flushes sincronas desnecessarias
- introduzir medicao real de latencia de FS, auth e rede
- escalar de single-task bring-up para multitarefa sem regressao de
  responsividade

Medidas estruturais:
- profiling de boot, FS e rede
- scheduler + workers
- read-ahead/write-back
- tuning de NVMe/AHCI
- cache de metadados e pagina
- filas assincronas de I/O

## 6.2 Seguranca

Metas:
- proteger integridade do boot
- proteger integridade do filesystem
- endurecer auth e sessao
- criar trilha segura para updates
- habilitar isolamento de apps e servicos

Medidas estruturais:
- assinatura de artefatos
- metadata autenticada no CAPYFS
- auditoria de auth
- sandbox e politicas
- stack TLS/certificados
- atualizacao assinada com rollback

## 6.3 Confiabilidade

Metas:
- nenhum update deve exigir reinstalacao manual
- power loss nao deve corromper o sistema sem capacidade de recuperacao
- logs de falha devem sobreviver ao reboot
- primeiro boot, login e update devem ter comportamento previsivel

Medidas estruturais:
- journal/WAL
- fsck/replay
- panic/crash dump
- logs persistentes
- health checks de boot e update

## 7. Roadmap macro recomendado

## Fase 0 - Fechamento do release atual

Objetivo:
- encerrar bugs de boot, setup, login, input e preferencia visual
- reduzir ruido visual e instabilidades do fluxo oficial

Entregas:
- boot/splash/setup previsiveis
- logs persistentes e silenciosos por padrao
- layout/tema/splash respeitados
- caminho VMware suportado como foco de validacao

Saida esperada:
- `0.8.x` utilizavel como base de plataforma

## Fase 1 - Hardening de plataforma

Objetivo:
- fechar o runtime x64 como fundacao confiavel

Entregas:
- `ExitBootServices` consolidado no caminho principal
- faults/excecoes/diagnostico de kernel mais robustos
- input nativo minimo confiavel
- storage runtime sem ambiguidades
- observabilidade de boot/driver/storage

Dependencias:
- fase 0

Criterio de aceite:
- 100 boots consecutivos em matriz de VM sem travamento silencioso
- smoke ISO + smoke HDD estaveis
- falhas devem gerar diagnostico preservavel

## Fase 2 - Memoria, tarefas e servicos base

Objetivo:
- sair do runtime quase monolitico e criar base de execucao real

Entregas:
- task struct
- scheduler cooperativo primeiro, depois preemptivo
- filas de trabalho e timers
- sincronizacao minima
- service manager basico (`init`, `logger`, `update-agent`, `networkd`)

Dependencias:
- fase 1

Criterio de aceite:
- servicos de background rodam sem quebrar shell/login
- tarefas lentas deixam de bloquear a sessao inteira

## Fase 3 - Memoria virtual e fronteira kernel/userland

Objetivo:
- criar a primeira arquitetura sustentavel para apps e ferramentas

Entregas:
- espaco de enderecamento por processo
- paginacao com permissao e isolamento
- syscalls base
- loader ELF para userspace
- `libc` minima do sistema
- IPC inicial (pipes, fila de mensagens ou shared memory controlada)

Dependencias:
- fase 2

Criterio de aceite:
- shell e primeiros utilitarios podem migrar para userland
- crash de app nao derruba o kernel

## Fase 4 - Filesystem, recovery e seguranca de dados

Objetivo:
- tornar armazenamento confiavel e auditavel

Entregas:
- versionamento de superblock
- journal/WAL
- replay apos shutdown sujo
- `fsck.CAPYFS`
- integridade autenticada de metadata
- quotas e politicas basicas

Dependencias:
- fases 1 e 3

Criterio de aceite:
- power loss simulado nao causa perda estrutural irreparavel
- corrupcoes conhecidas podem ser detectadas e, quando possivel, reparadas

## Fase 5 - Rede completa e trilha segura de atualizacao

Objetivo:
- transformar rede em servico de sistema e habilitar distribuicao do produto

Entregas:
- stack socket para userland
- DHCP, DNS, TCP e UDP robustos
- TLS minimo do sistema
- repositorio oficial de updates
- metadata assinada
- `update-agent` e `updaterctl`

Dependencias:
- fases 2, 3 e 4

Criterio de aceite:
- sistema consegue consultar repositorio, baixar update, validar assinatura e
  aplicar staging sem reinstalacao manual

## Fase 6 - Desktop foundation

Objetivo:
- sair de framebuffer/splash e construir interface de sistema real

Entregas:
- input de mouse (`PS/2`, `USB HID`, depois integracoes especificas de VM)
- compositor software inicial sobre framebuffer
- protocolo de janela/superficie
- gerenciamento de foco
- fontes, cursores e eventos
- toolkit minimo do sistema

Dependencias:
- fases 2, 3 e 5

Criterio de aceite:
- terminal grafico, painel e dialogos funcionam com teclado e mouse

## Fase 7 - Softwares basicos de sistema

Objetivo:
- oferecer ambiente desktop minimo usavel

Pacote inicial recomendado:
- terminal grafico
- file manager
- editor de texto
- visualizador de logs/diagnostico
- painel de configuracoes
- central de rede
- central de armazenamento
- visualizador de imagens
- utilitario de update
- software center simples

Dependencias:
- fase 6

Criterio de aceite:
- um usuario comum consegue operar o sistema sem depender da CLI para tarefas
  basicas

## Fase 8 - Sistema de pacotes e atualizacao automatica via GitHub

Objetivo:
- entregar software e atualizacoes de forma controlada

Arquitetura recomendada:

### Etapa inicial
- origem: GitHub Releases do repositorio oficial
- artefatos:
  - manifesto de canal
  - manifesto de release
  - pacotes/artefatos assinados
  - checksums
- verificacao:
  - assinatura Ed25519 offline
  - hash por artefato

### Componentes
- `update-agent`
  - roda em background
  - checa canal estavel/beta/dev
  - baixa metadata
  - valida assinatura
  - baixa artefatos para staging
- `pkgd`
  - instala/remove pacotes
  - controla dependencias
  - atualiza banco local
- `software-center`
  - UI para updates e apps
- `boot slot manager`
  - controla slot `A/B` de kernel/boot
  - faz rollback automatico se o boot novo nao confirmar saude

### Modelo tecnico recomendado
- curto prazo:
  - GitHub Releases como backend de distribuicao
  - update de sistema por imagem/pacotes assinados
- medio prazo:
  - espelho proprio de repositorio
  - metadata incremental
  - delta updates

### Requisitos para isso funcionar de forma solida
- assinatura de release
- chave offline de publicacao
- staging area
- confirmacao de boot saudavel
- rollback
- compatibilidade entre versoes de package DB

Dependencias:
- fases 3, 4 e 5

Criterio de aceite:
- update automatico baixa, valida, instala, reinicia e confirma saude
- em falha, rollback automatico para slot anterior

## Fase 9 - Plataforma de apps e SDK

Objetivo:
- permitir desenvolvimento sustentavel de software para o sistema

Entregas:
- ABI estavel de userspace
- `libsys`/`libui`/`libnet`
- formato de app/pacote
- manifestos de permissao
- sandbox por capacidade
- SDK documentado
- templates e toolchain oficial

Dependencias:
- fases 3, 6 e 8

Criterio de aceite:
- apps externos conseguem usar FS, rede e UI sem acoplamento direto ao kernel

## Fase 10 - Linguagem propria do sistema

Objetivo:
- introduzir uma linguagem propria apenas quando a plataforma ja puder
  sustenta-la

Nome de trabalho sugerido:
- `CapyLang`

Estrategia recomendada:

### Etapa 1 - linguagem de automacao
- parser simples
- VM/bytecode ou interpretador
- foco em scripts de sistema, automacao e tools
- bindings para shell, FS e configuracao

### Etapa 2 - linguagem de apps utilitarios
- modulos
- FFI controlada
- runtime estavel
- package manager integrado

### Etapa 3 - linguagem de primeira classe do sistema
- toolkit grafico
- stdlib de rede, arquivos, async
- compilador oficial
- `LSP`, formatter e test runner

Dependencias:
- fases 3, 5, 6, 8 e 9

Criterio de aceite:
- linguagem resolve problema real de automacao e apps, nao apenas branding

Observacao importante:
- criar linguagem propria antes de existir ABI, package manager, update system
  e toolkit grafico so desloca complexidade para o lugar errado

## Fase 11 - Seguranca de produto e confianca de plataforma

Objetivo:
- endurecer o sistema para uso mais serio e para distribuicao automatica

Entregas:
- Secure Boot
- measured boot
- auditoria persistente
- lockout e politicas de senha
- ACL e papeis
- firewall
- antivirus/antimalware somente depois de servicos, FS e update maduros
- sandbox de app
- testes de seguranca e fuzzing

Dependencias:
- fases 4, 5, 8 e 9

## Fase 12 - Ecossistema e software pesado

Objetivo:
- abrir caminho para compatibilidade e software mais complexo

Possiveis trilhas futuras:
- navegador real
- runtime Python 3
- compatibilidade Linux tipo subsistema
- IA local
- engine de dados

Observacao tecnica:
- browser, Python completo, compatibilidade Linux e IA local nao sao "features
  a adicionar"; sao programas/plataformas que exigem processo, memoria,
  sandbox, rede, TLS, scheduler, package manager e update maduros.

## 8. Plano especifico para GUI com mouse

## 8.1 Ordem correta

1. Input confiavel
- teclado
- mouse PS/2
- mouse USB HID
- eventos de ponteiro

2. Compositor inicial
- surfaces
- dirty rects
- dupla de buffers
- z-order
- foco

3. Toolkit
- widgets base
- texto
- botoes
- lista
- dialogos
- menus

4. Sessao grafica
- login grafico
- launcher
- terminal
- painel de configuracao

## 8.2 O que evitar

- acoplar GUI ao boot path
- desenhar apps diretamente do kernel sem estrategia de userspace
- criar toolkit antes de definir modelo de eventos e superficies
- tentar fazer browser antes de ter compositor e sandbox

## 9. Plano especifico para softwares basicos

Prioridade recomendada:

### Basicos obrigatorios
- terminal
- editor de texto
- file manager
- configuracoes
- updater
- visualizador de logs

### Basicos de desktop
- image viewer
- calculator
- network manager
- disk manager
- package browser

### Softwares complexos para depois
- browser completo
- office suite
- multimedia stack completa
- IDE propria

## 10. Itens faltantes estruturantes

Os itens abaixo sao os maiores faltantes do sistema hoje:

1. Scheduler real.
2. Modelo de processos e userland.
3. Syscall ABI estavel.
4. Memoria virtual por processo.
5. IPC e service manager.
6. Sockets/TLS e stack de rede para apps.
7. Journal/fsck/integridade do CAPYFS.
8. Logs persistentes e crash dump.
9. Package manager e updater transacional.
10. Mouse + compositor + toolkit.
11. Assinatura de boot e update.
12. SDK e linguagem propria.

Se esses itens forem ignorados, qualquer ganho visual ou de marketing vira
divida tecnica rapidamente.

## 11. Matriz de prioridade real

### Prioridade maxima
- boot confiavel
- storage confiavel
- auth/sessao
- logs e recovery
- scheduler/base de execucao

### Prioridade alta
- syscalls/userland
- sockets/TLS
- service manager
- updates assinados
- package manager

### Prioridade media
- GUI
- mouse
- apps basicos
- toolkit

### Prioridade tardia
- linguagem propria
- browser completo
- compatibilidade Linux
- IA local

## 12. Criticos de performance

1. Fazer profiling antes de otimizar.
2. Remover flush sincrono excessivo no primeiro boot e no runtime.
3. Introduzir workers para I/O, crypto e updates.
4. Criar indicadores minimos:
   - tempo boot -> login
   - tempo login -> prompt
   - latencia de leitura/escrita pequena
   - throughput sequencial
   - tempo de aplicacao de update

## 13. Criticos de seguranca

1. Integridade do boot antes de auto update.
2. Integridade de metadata do CAPYFS antes de confiar em recovery.
3. TLS/certificados antes de client de update.
4. Sandboxing antes de ecossistema de apps.
5. Auditoria persistente antes de features administrativas mais sensiveis.

## 14. Criterios de maturidade por marco

Um sistema CapyOS pode ser considerado estruturalmente solido quando:

### Plataforma
- boot oficial e previsivel
- update nao exige reinstalacao
- rollback existe
- falhas criticas deixam rastro persistente

### Kernel
- multitarefa real
- isolamento de processos
- syscalls definidas
- panic/fault observaveis

### Storage e seguranca
- replay/journal
- fsck
- integridade autenticada
- politicas de auth

### Rede
- sockets/TLS
- updates e repositorio seguros
- firewall minimo

### Desktop
- sessao grafica com mouse
- apps basicos usaveis
- package/update UI

### Ecossistema
- SDK
- formato de app
- linguagem propria ou runtime oficial

## 15. Sequencia recomendada de releases

Sugestao objetiva:

1. `0.8.x`
- estabilizacao do fluxo atual

2. `0.9.x`
- hardening de plataforma
- recovery/logs

3. `0.10.x`
- scheduler + services

4. `0.11.x`
- syscalls + userland inicial

5. `0.12.x`
- journal/fsck/integridade

6. `0.13.x`
- sockets/TLS/update foundation

7. `0.14.x`
- compositor/mouse/desktop foundation

8. `0.15.x`
- apps basicos + software center

9. `0.16.x`
- package/update automatico completo

10. `0.17.x+`
- SDK + CapyLang + ecossistema

## 16. Plano de acao imediato recomendado

Para manter o projeto na trilha correta, a proxima sequencia deveria ser:

1. Fechar bugs do boot, setup, login e UI atual.
2. Consolidar logging persistente e diagnostico de falha.
3. Encerrar o hardening da plataforma x64.
4. Projetar scheduler, tasks e services antes de qualquer GUI.
5. Projetar syscall ABI e userland base antes de package manager e apps.
6. Projetar update assinado e rollback antes de automacao de update.
7. Comecar GUI somente apos tasks, services e input/mouse minimo.
8. Entrar em linguagem propria so depois de existir SDK e runtime de apps.

## 17. Referencias cruzadas

- `docs/architecture/system-overview.md`
- `docs/plans/system-delivery-roadmap.md`
- `docs/plans/system-roadmap.md`
- `docs/plans/platform-hardening-plan.md`
- `docs/testing/boot-and-cli-validation.md`
