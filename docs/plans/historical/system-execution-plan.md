# CapyOS - Plano de Execucao do Sistema

Data de referencia: 2026-04-09.
Escopo: continuar o plano-mestre com uma trilha de execucao concreta a partir
do estado atual do repositorio, das branches `main` e `develop` e do que ja foi
endurecido na fundacao de servicos, recovery e updates.

Este documento nao substitui:
- `docs/plans/active/system-master-plan.md`
- `docs/plans/active/system-roadmap.md`

Ele existe para responder uma pergunta mais operacional:
"qual e a ordem correta de execucao daqui para frente, o que ja esta feito e o
que falta para chegar a um sistema solido?"

## 1. Estado atual consolidado

## 1.1 O que ja esta entregue

### Plataforma base
- trilha suportada consolidada em `UEFI/GPT/x86_64`
- fluxo de `make all64`, `make iso-uefi`, `make test` e smokes funcionando
- instalacao por ISO e primeiro boot persistente
- runtime de storage com volume `DATA` cifrado
- `CAPYFS` e VFS operacionais
- setup inicial, login, shell e sessao persistente

### Fundacao de servicos
- `service manager` com:
  - estados estruturados
  - polling cooperativo
  - intervalo por ticks
  - `start`, `stop`, `restart`
  - falhas, restart e backoff
  - dependencias entre servicos
  - targets `core`, `network`, `maintenance`, `full`
- persistencia do target padrao em configuracao
- politica de boot que degrada o target quando storage/rede nao estao prontos

### Recovery
- boot pode entrar em `maintenance`
- sessao de recovery direta, sem login normal
- `recovery-status`, `recovery-storage`, `recovery-network`,
  `recovery-verify`, `recovery-resume`
- fallback controlado para runtime em RAM quando o volume persistente falha
- `recovery-storage-repair` para recompor a base minima quando o volume monta
- verificacao estrutural do `CAPYFS`

### Updates
- canais persistentes:
  - `stable -> main`
  - `develop -> develop`
- estado local de update persistido em disco
- staging local de update
- comandos de operador:
  - `update-status`
  - `update-check`
  - `update-stage`
  - `update-arm`
  - `update-clear`
  - `update-history`
  - `update-channel`
  - `update-import-manifest`

### Observabilidade atual
- `klog` persistente
- relatorio de recovery persistente
- smokes de CLI e ISO atualizados para o fluxo atual

## 1.2 O que ainda nao esta fechado

### Kernel e runtime
- scheduler realmente preemptivo e maduro
- fronteira estavel entre kernel e userland
- ABI de syscall
- processos e espaco de enderecamento por processo
- IPC e workers isolados

### Storage e confiabilidade
- journal/WAL do `CAPYFS`
- fsck/replay/reparo real do filesystem
- integridade autenticada de metadata e blocos
- rollback de update com semantica transacional

### Rede
- stack de sockets completa
- DHCP/DNS maduros
- TLS/PKI
- firewall e politicas de rede
- `VMXNET3` ainda nao validado

### GUI
- desktop existe no codigo, mas ainda nao esta ligado ao boot/login
- mouse ainda nao esta encadeado de ponta a ponta
- dispatcher de eventos grafico ainda nao fecha o ciclo
- terminal grafico nao esta ligado a shell real
- falta sessao desktop e launcher de apps

### Produto e ecossistema
- package manager
- repositorio oficial de pacotes
- updates por payload real, nao apenas manifesto/catalogo local
- SDK
- runtime de apps
- linguagem propria

## 2. Medicao honesta de progresso

Estimativa pragmatica do projeto inteiro:
- progresso rumo a um sistema solido de uso geral: `18%`
- falta total para esse objetivo: `82%`

Estimativa por trilha:

| Dominio | Estado atual | Progresso estimado |
|---|---|---:|
| Boot, instalacao e recovery | funcional e estruturado | 70% |
| Supervisor de servicos | fundacao pronta | 60% |
| Storage e persistencia | funcional, sem recovery forte | 35% |
| Rede | parcial e sem userland maduro | 25% |
| Observabilidade | basica | 30% |
| Updates | local, sem pipeline real | 25% |
| Seguranca de plataforma | parcial | 20% |
| Userland/ABI/processos | embrionario | 10% |
| GUI/Desktop | scaffold | 8% |
| Apps/SDK | inicial | 2% |
| Linguagem propria | nao iniciada de forma sustentavel | 0% |

## 3. Ordem correta de execucao

Este e o encadeamento recomendado para nao construir o produto sobre uma base
fragil.

## 3.1 Etapa A - Runtime, scheduler e jobs reais

Objetivo:
- sair do modelo "shell + loops cooperativos" para um runtime com tarefas,
  workers e servicos mais autonomos

Entregas obrigatorias:
- scheduler preemptivo confiavel
- timers/workers com ownership claro
- separacao entre shell interativa e jobs de background
- medicao de starvation, latencia e travamento

Definition of done:
- jobs periodicos nao dependem do loop de input
- `networkd`, `logger` e `update-agent` rodam por cadencia do runtime
- o sistema suporta servicos continuos sem degradar login/shell

## 3.2 Etapa B - Fronteira kernel/userland

Objetivo:
- criar o primeiro contorno sustentavel entre nucleo e espaco de usuario

Entregas obrigatorias:
- syscalls basicas
- espaco de enderecamento por processo
- modelo de processo/task
- carregamento de binarios userland
- isolamento minimo de memoria

Definition of done:
- shell e pelo menos um utilitario deixam de ser apenas runtime interno do
  kernel
- falha de app/processo nao deve comprometer a sessao inteira

## 3.3 Etapa C - Storage endurecido e update transacional

Objetivo:
- fazer storage e update deixarem de ser "persistentes" e passarem a ser
  "recuperaveis"

Entregas obrigatorias:
- journal ou WAL do `CAPYFS`
- verificacao e replay
- reparo controlado de metadata
- staging de payload de update
- apply atomico
- rollback seguro

Definition of done:
- reboot no meio de update nao deve destruir boot nem volume
- recovery deve distinguir "replay", "rollback" e "reparo manual"

## 3.4 Etapa D - Rede segura para produto

Objetivo:
- sair da conectividade basica e chegar a uma base de produto

Entregas obrigatorias:
- sockets para userland
- DHCP/DNS robustos
- TLS/PKI
- fetch real de manifesto e payload do GitHub
- verificacao criptografica de origem e integridade

Definition of done:
- `update-agent` consegue buscar manifestos reais da branch configurada
- downloads e verificacoes nao dependem de injecao manual de arquivo

## 3.5 Etapa E - Sessao grafica minima

Objetivo:
- transformar o scaffold grafico existente em sessao desktop funcional

Entregas obrigatorias:
- ligar `desktop_init()` e `desktop_run_frame()` ao boot/login
- encadear mouse ao runtime
- criar dispatcher real de eventos da GUI
- conectar terminal grafico a shell real
- taskbar, janelas, foco e abertura basica de apps

Definition of done:
- o usuario consegue entrar numa sessao grafica
- mover cursor, clicar, focar janela e abrir terminal grafico

## 3.6 Etapa F - Plataforma de apps, pacotes e SDK

Objetivo:
- sair do sistema fechado e abrir a plataforma para software real

Entregas obrigatorias:
- package manager
- formato de pacote
- repositorio oficial
- instalacao, atualizacao e remocao de apps
- bibliotecas base
- SDK e docs de API

Definition of done:
- apps basicos do sistema podem ser atualizados sem reinstalar a ISO

## 3.7 Etapa G - Linguagem propria

Objetivo:
- criar a linguagem como plataforma, nao como experimento isolado

Precondicoes:
- processos, ABI e runtime estaveis
- package manager funcional
- APIs do sistema minimamente consolidadas
- SDK e cadeia de distribuicao prontas

Definition of done:
- a linguagem roda como ambiente de software do sistema e nao como modulo
  acoplado ao kernel

## 4. Gaps especificos da GUI

Esta secao existe porque a GUI parece "quase pronta" olhando superficialmente
o repositorio, mas ainda nao esta operacional como produto.

Bloqueios atuais:
- `desktop_init()` e `desktop_run_frame()` ainda nao fazem parte do fluxo vivo
  de boot/login
- `mouse_ps2_init()` e o handler de IRQ do mouse nao estao integrados ao
  runtime principal
- a fila de eventos da GUI nao tem dispatcher central fechando input, widgets
  e janelas
- o terminal grafico ainda nao consome a shell real
- a stack grafica ainda depende basicamente do framebuffer herdado do GOP

Sequencia minima correta da GUI:
1. ligar a sessao desktop ao boot/login
2. ligar mouse e teclado ao loop grafico
3. fechar o dispatcher de eventos
4. conectar terminal grafico a shell
5. adicionar launcher e sessao de apps

## 5. Gaps especificos de update

O stack atual de update ja serve como fundacao, mas ainda nao e update de
produto.

Ja existe:
- canal `stable/develop`
- branch mapeada em configuracao
- catalogo e staging locais
- historico e estado persistente

Ainda falta:
- fetch real do GitHub
- manifesto assinado
- download de payload
- apply atomico
- rollback automatico
- politica de rollout e bloqueio por compatibilidade

## 6. Gates de release

## 6.1 Gate para `develop`

Para uma entrega entrar em `develop`, ela deve fechar:
- `make test`
- `make all64`
- smokes aplicaveis
- documentacao minima da feature
- comportamento coerente com o canal `develop`

## 6.2 Gate para `main`

Para promover de `develop` para `main`, deve fechar:
- build limpa da trilha oficial
- smoke de ISO e boot
- validacao manual em `VMware + E1000`
- ausencia de regressao em setup, login, shell e storage
- documentacao e release notes atualizadas

## 7. Proxima frente recomendada

A proxima frente correta nao e GUI nem linguagem propria. E esta:

1. scheduler/workers e runtime autonomo de servicos
2. fronteira kernel/userland
3. recovery real de storage e update transacional
4. rede segura e fetch real de updates
5. sessao grafica minima

Se a ordem for invertida, o projeto cresce visualmente, mas fica tecnicamente
instavel.

## 8. Resultado esperado ao fim desta trilha

Ao fim das etapas A ate E, o CapyOS deve ter:
- boot e recovery previsiveis
- servicos reais em background
- userland com fronteira minima
- storage recuperavel
- update real por repositorio
- sessao grafica com mouse e terminal funcional

So depois disso o projeto entra em terreno bom para:
- central de software
- apps maiores
- sandbox
- SDK externo
- linguagem propria
