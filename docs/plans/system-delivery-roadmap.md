# CapyOS - Roadmap Tecnico de Entregas

Data de referencia: 2026-03-15.

Este documento consolida o plano tecnico atual do CapyOS em uma sequencia
executavel de entregas. O objetivo nao e listar apenas ideias; o foco aqui e
deixar claro:

- onde o projeto esta agora
- o que precisa acontecer antes de cada frente maior
- o que bloqueia o que
- qual e a ordem de implementacao mais segura
- como validar cada etapa ponta a ponta

## 1. Estado atual do projeto

Base consolidada:

- trilha oficial `UEFI/GPT/x86_64`
- instalador por ISO funcional
- boot instalado validado em `Hyper-V Gen2`
- storage nativo em `AHCI` e `NVMe`
- `ExitBootServices` ja faz parte do caminho suportado
- input `Hyper-V` estabilizado com promocao tardia do `VMBus`

Problema atual de produto:

- ainda existem bugs criticos de fluxo de boot, teclado e preferencias visuais
- ha inconsistencias entre o assistente de configuracao, o runtime x64 e a CLI
- varias frentes futuras dependem de fundacoes que ainda nao existem:
  scheduler, servicos, sockets/TLS, localizacao, ABI de apps e GUI

Fase atual:

- `Fase A - Estabilizacao do release`

Objetivo da Fase A:

- fechar todos os bugs que quebram boot, input, configuracao inicial e UX
  basica do sistema instalado
- entregar comportamento previsivel para ISO, login, shell e preferencias
- deixar a base pronta para evolucao de rede, servicos e GUI

## 2. Principios de priorizacao

1. Corrigir primeiro tudo que pode quebrar boot, instalacao ou login.
2. Antes de iniciar frentes grandes, fechar as dependencias estruturais delas.
3. Evitar construir feature de alto nivel sobre caminhos ainda inconsistentes.
4. Toda entrega precisa ter criterio de aceite e teste de regressao.
5. Preferir ampliar automacao de smoke para fluxos oficiais de release.

## 3. Mapa de dependencias

| Frente | Depende de | Bloqueia |
|---|---|---|
| Cancelamento seguro da ISO | loader UEFI consistente | release ISO publico |
| Teclado BR com dead keys | pipeline de input/layout estavel | setup, login, shell, i18n |
| Tema/splash persistentes | `config.ini` honrado pelo kernel x64 | UX, preferencias por usuario |
| Prompt/home/paths | sessao + shell coerentes | UX diaria |
| Idiomas por usuario | configuracao por usuario + catalogo de strings | release multilanguage |
| Rede completa | drivers, stack, sockets, servicos | navegador, apps online, firewall |
| Scheduler/multithread | timer/IRQ nativos + modelo de tarefas | servicos, GUI, rede avancada |
| Servicos em background | scheduler + IPC/job control | gerenciador de tarefas, AV, ML |
| GUI + mouse | input completo, compositor, scheduler, apps ABI | navegador, apps graficos |
| Apps basicos | runtime de apps + FS/UI/IPC | experiencia desktop |
| Python 3 | ABI, processos, memoria, FS, runtime de pacotes | automacao, apps, ML |
| ML no sistema | servicos, scheduler, rede, seguranca, storage | aprendizado local, assistentes |
| Firewall/AV | rede completa + servicos + auditoria | hardening de release |
| Integracao Linux tipo WSL | scheduler, processos, mount/IPC, seguranca | compatibilidade avancada |
| Cargas pesadas / dados / blockchain | scheduler, multithread, rede, observabilidade | nichos de IA e computacao intensiva |

## 4. Backlog organizado por entregas

## Entrega A1 - Correcao critica de boot e instalacao

Escopo:

- corrigir cancelamento do boot pela ISO
- impedir que o kernel da propria ISO suba quando o usuario cancelar o
  instalador
- diferenciar cancelamento do usuario de falha real do instalador
- adicionar regressao automatizada para esse fluxo

Arquivos-alvo esperados:

- `src/boot/uefi_loader.c`
- `tools/scripts/smoke_x64_flow.py`
- `tools/scripts/smoke_x64_iso_cancel.py` ou equivalente

Criterio de aceite:

- com ISO inserida e disco bootavel presente, ao cancelar o instalador o
  firmware deve seguir para o proximo boot disponivel
- o sistema nao pode cair no kernel da ISO
- falha real do instalador nao pode mascarar erro bootando um caminho invalido

Status:

- concluida nesta branch e coberta por smoke dedicado

## Entrega A2 - Input confiavel com layout BR

Escopo:

- revisar dead keys no caminho `PS/2`, `EFI` e `Hyper-V`
- corrigir composicao de acentos para `br-abnt2`
- garantir que tecla de acento aguarde o proximo input antes de emitir
- preservar fallback quando combinacao nao existir
- expandir testes de layout e testes de traducao runtime

Arquivos-alvo esperados:

- `src/arch/x86_64/input_runtime.c`
- `src/drivers/input/keyboard/layouts/br_abnt2.c`
- `tests/test_keyboard_layouts.c`
- possivelmente novo teste dedicado de composicao

Criterio de aceite:

- `' + a -> a acentuado`
- `~ + a/o -> a/o nasal`
- `^ + a/e/o -> vogais com circunflexo`
- acento seguido de espaco ou caractere sem composicao deve emitir o acento e o
  caractere correto
- login, setup e shell precisam respeitar o layout ativo

Status:

- implementacao base e rendering x64 concluidos nesta branch
- validacao manual dos acentos basicos concluida
- scancodes estendidos de `/` e `?` corrigidos no runtime x64 e no caminho
  legado

## Entrega A3 - Preferencias visuais reais no runtime

Escopo:

- fazer o kernel x64 respeitar `theme`, `keyboard` e `splash` de
  `/system/config.ini`
- ligar tema escolhido no assistente ao runtime real
- desligar a animacao de splash quando `splash=disabled`
- mostrar logs de boot quando splash estiver desabilitado
- criar comando de CLI para alterar tema e persistir a configuracao
- criar comando de CLI para habilitar/desabilitar splash sem reinstalar

Arquivos-alvo esperados:

- `src/arch/x86_64/kernel_main.c`
- `src/core/system_init.c`
- `src/shell/commands/system_control.c`
- possivelmente `src/core/kcon.c` ou UI x64

Criterio de aceite:

- tema escolhido no setup precisa aparecer no boot e na CLI
- comando de CLI deve alterar o tema sem exigir reformatacao
- comando de CLI deve alternar `splash=enabled/disabled` sem exigir reformatacao
- splash so aparece quando habilitado
- caminho sem splash precisa privilegiar log legivel de inicializacao

Status:

- persistencia de tema e comando `config-theme` validados por smoke oficial
- refresh visual em runtime x64 entrou nesta branch
- `config-splash` entrou com persistencia automatizada e cobertura de smoke
- validacao manual final de tema e `config-splash` concluida

## Entrega A4 - UX basica da shell

Escopo:

- garantir prompt com usuario autenticado real
- entrar na `home` do usuario apos login
- ajustar prompt para mostrar caminho resumido, por exemplo `~/pasta3`
- garantir `mypath` e prompt coerentes entre si

Dependencias:

- nenhuma estrutural alem de sessao/shell, mas deve vir depois de A1-A3 para
  nao mascarar bugs maiores de input/setup

Criterio de aceite:

- login cai em `/home/<usuario>`
- prompt nao cresce indefinidamente
- caminhos absolutos continuam disponiveis via comando proprio

Status:

- formatter unificado validado nesta branch
- smoke oficial cobre usuario real, `HOME`, `PWD` e compactacao de caminho

## Entrega A5 - Configuracao por usuario e base de localizacao

Escopo:

- separar configuracao global de configuracao por usuario
- introduzir idioma por usuario
- manter comandos estaveis, traduzindo apenas textos do sistema
- suportar pelo menos `pt-BR`, `en`, `es`

Dependencias:

- A4
- mecanismo de leitura/gravação de preferencias por usuario

Entregas tecnicas desta fase:

- esquema de configuracao por usuario
- catalogo de strings
- API de traducao de mensagens
- migracao gradual das strings hardcoded mais visiveis

Arquivos-alvo esperados:

- `include/core/user_prefs.h`
- `src/core/user_prefs.c`
- `include/core/localization.h`
- `src/core/localization.c`
- `src/core/session.c`
- `src/shell/commands/system_control.c`
- `src/shell/commands/help.c`
- `src/core/login_runtime.c`

Criterio de aceite:

- cada usuario pode persistir seu proprio idioma sem afetar `/system/config.ini`
- sistema aceita pelo menos `pt-BR`, `en` e `es`
- idioma persistido reaparece apos logout e reboot
- shell e mensagens mais visiveis da sessao autenticada trocam para o idioma do
  usuario
- smoke oficial cobre a persistencia do idioma entre os boots

Status:

- perfil por usuario em `~/.config/capyos/user.ini` implementado nesta branch
- comando `config-language` implementado e validado por smoke oficial
- catalogo inicial cobre login autenticado, ajuda, sessao e comandos de
  configuracao centrais
- instalador ISO agora pergunta idioma primeiro e provisiona `language=en/es/pt-BR`
  para setup e sistema
- `add-user` passou a criar `home` e preferencias sob contexto de sistema,
  semeando idioma padrao do sistema para o novo usuario
- smoke oficial agora cobre heranca de idioma para usuario secundario e
  persistencia apos reboot
- comandos de arquivos, busca, rede e gestao de usuarios agora respeitam o
  idioma ativo na sessao autenticada
- `system_control`, `system_info`, ajuda e sessao foram alinhados para manter
  os textos principais em `pt-BR`, `en` e `es`
- smoke oficial de disco provisionado e ISO valida agora os textos em ingles
  para comandos de arquivos, rede e usuarios
- labels tecnicos deliberadamente estaveis (`USER=`, `ipv4=`, `uid=` etc.)
  permanecem sem traducao para preservar legibilidade e compatibilidade de
  scripts
- criterio de aceite desta entrega atendido nesta branch

## Entrega A6 - Controle de energia real

Escopo:

- corrigir `shutdown-reboot` para reiniciar o sistema de verdade
- corrigir `shutdown-off` para desligar a VM/host de verdade
- remover caminhos x64 que apenas imprimem a mensagem e entram em `halt`
- ligar o runtime x64 ao driver ACPI real ja existente
- adicionar regressao automatizada para reboot/poweroff

Dependencias:

- A1-A5
- runtime x64 com ACPI linkado no kernel

Criterio de aceite:

- `shutdown-reboot` encerra a instancia atual e o boot seguinte sobe normalmente
- `shutdown-off` encerra a instancia atual sem exigir kill externo do hypervisor
- `reboot` e `halt` no login pre-shell continuam funcionando porque delegam aos
  aliases da CLI
- smoke oficial ou smoke dedicado falha se o comando apenas imprimir a mensagem
  sem efetivar a transicao de energia

Status:

- bug reportado apos validacao manual do release
- hotfix priorizado antes da Fase B
- driver ACPI real linkado ao kernel x64 nesta branch de hotfix
- runtime x64 passou a entregar `RSDP` e `EFI_SYSTEM_TABLE` do handoff para o
  driver de energia
- reboot/poweroff agora tentam `UEFI ResetSystem` como caminho primario, com
  `ACPI`/fallbacks legados como contingencia
- `shutdown-reboot` passou a delegar para `acpi_reboot()` com auto-init
- `shutdown-off` passou a delegar para `acpi_shutdown()` real
- smoke oficial agora falha se reboot/poweroff apenas imprimirem a mensagem sem
  efetivar a transicao de energia
- validado por smoke e pelo ambiente real

## Entrega B1 - Rede de verdade e API de comunicacao

Escopo:

- endurecer driver de rede atual e probes
- consolidar configuracao IPv4, DHCP e DNS operacional
- projetar camada de sockets
- preparar base para TLS e chamadas externas

Dependencias:

- plataforma de input/storage/boot estabilizada
- observabilidade minima para debug de rede

Bloqueia:

- navegador
- apps online
- firewall real
- integracoes externas

Status:

- em andamento na branch `codex/network-b1-stage1`
- primeira fatia priorizada: separar `NIC detectada` de `backend operacional`
- probe agora deve preferir drivers realmente implementados, preservando
  observabilidade quando apenas hardware conhecido sem backend existir
- `net-status` e o bootstrap precisam mostrar `runtime=ready|driver-missing|init-failed`
  antes da entrada de DHCP e sockets
- persistencia de IPv4 estatico em `/system/config.ini` concluida nesta branch;
  `net-set` deixou de ser estado volatil e o boot reaplica `ipv4/mask/gateway/dns`
- `network_mode=static|dhcp` e cliente DHCP minimo concluidos nesta branch,
  com fallback estatico preservado no `config.ini`
- cliente DNS minimo via UDP/53 concluido nesta branch, com comando
  `net-resolve <hostname>` e cobertura de smoke em modo `dhcp`
- validacao manual em `Hyper-V Gen2` mostrou o proximo bloqueio real da B1:
  a VM expoe NIC sintetica `Hyper-V NetVSC`, enquanto o runtime atual ainda so
  sobe backends PCI (`e1000`/`tulip`)
- o probe agora passa a reportar `hyperv-netvsc` como backend detectado sem
  runtime, em vez de `unknown/none`, para tornar o diagnostico explicito dentro
  do proprio sistema
- a descoberta generica de offers do `VMBus` ja foi isolada em um modulo
  proprio `NetVSC`, mas segue totalmente fora do caminho de boot e da CLI no
  `Hyper-V Gen2`: os testes reais mostraram reboot ate com consulta explicita de
  `offer`, entao `net-refresh` ficou desativado para esse backend
- a abertura do canal `VMBus`, `NetVSP` e `RNDIS` continua pendente para uma
  fase separada, sem ainda promover a NIC a backend operacional
- a fundacao de codec `RNDIS` ja entrou com builders/parsers e testes de host,
  para destravar o backend dedicado sem voltar a tocar no `Hyper-V` real antes
  da hora
- a state machine offline do `NetVSC` (`initialize -> query mtu -> query mac ->
  set packet filter`) ja existe com testes de host; falta agora acoplar isso ao
  backend dedicado sobre `VMBus`, sem religar nenhuma chamada experimental na
  CLI
- a camada offline de envelope `NetVSP` agora tambem existe para empacotar
  requests `RNDIS` do controle dedicado, ainda totalmente fora do runtime do
  `Hyper-V`
- a orquestracao offline de sessao `NetVSP -> RNDIS` tambem ja existe com
  testes de host, para o backend futuro acoplar handshake e controle sem nascer
  dentro do shell nem do boot
- o backend dedicado `NetVSC` tambem ja existe como state machine isolada por
  callbacks (`offer -> channel -> NetVSP -> RNDIS`), ainda sem chamar `VMBus`
  no runtime real
- o transporte generico de canal `VMBus` ja foi extraido do driver de teclado e
  o adapter real `NetVSC -> VMBus` ja compila, mas continua fora da stack e da
  CLI enquanto o caminho de runtime nao estiver pronto para validacao controlada
- o controlador passivo de runtime `NetVSC` agora ja vive dentro da stack com
  observabilidade (`controller=disabled phase=...`), mas segue desativado por
  politica para nao reabrir regressao no `Hyper-V`
- o `net-refresh` agora so tenta avancar o controlador `NetVSC` quando a offer
  sintetica ja estiver em cache; o passo continua pequeno e controlado para a
  primeira validacao manual no `Hyper-V`
- a validacao manual mais recente no `Hyper-V Gen2` fechou o diagnostico de
  dependencia: o runtime de rede segue com `bus=disconnected cache=miss` e a
  tentativa de conectar `VMBus` a partir da CLI reinicia a VM; portanto a ordem
  correta mudou
- para `Hyper-V Gen2`, o proximo prerequisito real passou a ser um runtime
  nativo de plataforma nesse backend, em especial storage `Hyper-V SCSI/StorVSC`
  e um gerenciador seguro de conexao `VMBus` fora da CLI
- a fundacao offline de `StorVSC` passa a ser o proximo corte concreto:
  envelope `StorVSP`, sessao de inicializacao (`begin -> version ->
  properties -> end -> enumerate`) e testes de host, tudo ainda fora do
  runtime real
- o backend offline `StorVSC` (`offer -> channel -> control -> ready`) passa a
  ser a sequencia imediata seguinte, para deixar toda a logica de controle
  validada antes de conectar isso ao `VMBus` real
- em seguida, o runtime passivo `StorVSC` precisa existir no mesmo molde do
  `NetVSC`, para que a plataforma possa carregar esse controlador sem ainda
  ativa-lo no `Hyper-V` real
- o adapter `StorVSC -> VMBus` agora ja compila em modulo proprio, com GUID
  publico do storage sintetico `Hyper-V` e o mesmo contrato de transporte
  generico usado pelo trilho de rede
- o runtime x64 de storage agora carrega esse controlador `StorVSC` de forma
  passiva e expõe telemetria de boot (`bus/cache/controller/phase`), sem ainda
  tentar abrir canal ou negociar `VMBus` no `Hyper-V` real
- o kernel agora ja possui um hook interno de `arming` para o `StorVSC`, preso
  aos mesmos pontos seguros em que o runtime sai de firmware e promove input
  nativo; esse hook so habilita o controlador quando `VMBus` e `offer` ja
  estiverem prontos, sem ainda abrir canal ou avancar controle
- o corte seguinte tambem ja entrou: depois do `arming`, o kernel pode avancar
  o `StorVSC` apenas ate `phase=channel`, consumindo a `offer` cacheada e
  preparando o canal offline, mas ainda sem emitir `OPENCHANNEL`
- o passo seguro seguinte mapeado passa a ser `VMBus prepare`, mas a tentativa
  automatica no boot foi recuada: no `Hyper-V Gen2` real ela ainda destabiliza
  o primeiro boot apos formatacao, entao esse preparo fica adiado ate existir
  um ponto de ativacao ainda mais restrito
- enquanto esse corte mais restrito nao chega, a observabilidade do sistema
  deve mostrar de forma explicita o motivo do bloqueio (`platform-hybrid`,
  `vmbus-unprepared`, `offer-miss`, etc.) para `NetVSC` e `StorVSC`
- enquanto esse prerequisito nao existir, o sistema deve expor explicitamente o
  bloqueio como `platform=hybrid bootsvc=active storage-fw=on/off`, em vez de
  sugerir que o problema esta apenas no `NetVSC`
- proximos cortes naturais da B1: camada de sockets/requests, DNS cache e
  base para chamadas externas/TLS; no trilho `Hyper-V`, isso depende antes do
  backend `NetVSC/RNDIS` sobre `VMBus`, que por sua vez depende do trilho
  `StorVSC/VMBus runtime`

## Entrega B2 - Timer/IRQ, scheduler e multithread

Escopo:

- completar timer nativo pos-`ExitBootServices`
- estruturar task model, run queue e worker threads
- preparar base para servicos, rede, UI e jobs

Dependencias:

- plataforma x64 estavel
- pontos de entrada bem definidos no kernel

Bloqueia:

- servicos em background
- gerenciador de tarefas
- GUI responsiva
- ML em background
- cargas de trabalho pesadas

## Entrega B3 - Servicos, jobs e observabilidade

Escopo:

- modelo de servicos em background
- gerenciador de tarefas
- filas de trabalho
- melhor rastreamento de falhas
- auditoria de eventos criticos

Dependencias:

- scheduler funcional
- timer/IRQ nativos

Bloqueia:

- antivirus
- firewall aplicavel
- ML residente
- rede avancada

## Entrega C1 - Stack de seguranca e confiabilidade

Escopo:

- hardening de `users.db`
- comparacao constante de hash
- integridade autenticada de metadata
- journal/WAL do CAPYFS
- recuperacao pos-falha
- politicas de auditoria

Dependencias:

- base atual de storage e auth

Bloqueia:

- release publico mais agressivo
- AV/firewall com confianca maior
- Python/apps externos com menor risco

## Entrega C2 - GUI, mouse e modelo de apps

Escopo:

- camada grafica inicial
- cursor/mouse
- compositor simples
- eventos de janela
- API de apps graficos

Dependencias:

- scheduler
- servicos
- input completo incluindo mouse/HID
- estrategia de desenho/compositor

Bloqueia:

- navegador
- calculadora/editor/snake em GUI

## Entrega C3 - Apps basicos

Escopo:

- calculadora
- editor de texto
- snake
- utilitarios de sistema

Dependencias:

- CLI mais madura e/ou GUI minima
- modelo de apps definido

## Entrega D1 - Runtime extensivel

Escopo:

- compatibilidade com Python 3
- estudo e desenho da linguagem propria do sistema
- ABI e runtime de execucao

Dependencias:

- processos/tarefas
- memoria mais madura
- IO/FS/IPC previsiveis

Observacao:

- a linguagem propria nao deve iniciar como parser isolado; primeiro precisa de
  objetivo claro: automacao, apps do sistema, extensoes ou scripting seguro

## Entrega D2 - Inteligencia e automacao avancada

Escopo:

- arquitetura de ML local
- servicos de inferencia leves
- politica de dados e permissao
- possivel integracao com modelos de mercado

Dependencias:

- servicos, scheduler, rede, armazenamento, seguranca

Risco:

- sem isolamento e politica de recursos, ML pode degradar todo o sistema

## Entrega D3 - Integracoes de plataforma

Escopo:

- firewall controlavel
- antivirus basico de servicos/permissoes
- compatibilidade com Linux tipo WSL
- estudo de cargas altas, IA aplicada e possivel linha de blockchain

Dependencias:

- rede real
- servicos
- scheduler
- seguranca/auditoria
- isolamento de processos

## 5. Itens importantes que precisam entrar no roadmap

Estes pontos nao estavam todos explicitos na lista original, mas sao necessarios
para o projeto nao travar mais adiante:

- formato de pacote/distribuicao de apps
- API estavel de sistema para CLI e apps
- sockets/TLS e DNS como fundacao para qualquer cliente de rede
- crash log, dump e rastreio de falhas
- matriz de testes por release: ISO, install, upgrade, login, storage, rede
- fluxo de update e recovery
- estrategia de compatibilidade de hardware real
- politica de permissao para apps de terceiros
- perfil de build/release assinado

## 6. Ordem recomendada de execucao

1. A1 - cancelamento seguro da ISO
2. A2 - teclado BR e composicao de acentos
3. A3 - tema/splash e comando de CLI
4. A4 - UX da shell
5. A5 - idiomas por usuario
6. B1 - rede e API de comunicacao
7. B2 - scheduler/multithread
8. B3 - servicos/gerenciador de tarefas
9. C1 - seguranca e recuperacao
10. C2 - GUI e mouse
11. C3 - apps basicos
12. D1 - Python 3 e linguagem propria
13. D2 - ML
14. D3 - integracoes avancadas

## 7. Definicao de pronto da Fase A

A Fase A so pode ser considerada encerrada quando:

- a ISO nao quebra o boot ao cancelar instalacao
- o layout `br-abnt2` funciona de forma previsivel no setup, login e shell
- tema, splash e layout configurados persistem e sao honrados no runtime
- a shell entra na `home` do usuario e exibe prompt usavel
- existir pelo menos uma base inicial de idiomas por usuario planejada e
  especificada, mesmo que a implementacao completa fique para a entrega seguinte
- os smokes oficiais cobrirem instalacao, cancelamento da ISO, login e
  persistencia

## 8. Execucao desta branch

Objetivo desta branch:

- registrar este roadmap completo
- fechar A5 e preparar a entrada da fase B

Status desta branch:

- roadmap consolidado: concluido
- A1: concluida e validada
- A2: concluida e validada manualmente
- A3: concluida e validada manualmente
- A4: concluida e validada por smoke
- A5: concluida e validada por smoke
- A6: implementada e validada por smoke; aguardando validacao manual
- B1: em andamento; probe/runtime, IPv4 persistente, DHCP minimo e DNS resolve
  concluidos nesta branch
