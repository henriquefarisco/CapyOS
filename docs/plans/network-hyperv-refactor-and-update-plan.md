# Plano Tecnico - Rede, Hyper-V e Sistema de Atualizacao

## Objetivo

Fechar a trilha de rede do CapyOS com foco em:

- funcionamento real no `Hyper-V Gen2`
- estrutura de codigo modular e validavel
- reducao de arquivos gigantes e altamente acoplados
- estrategia de validacao local, `QEMU` e `Hyper-V`
- preparacao do futuro sistema de atualizacao por rede com canais `main` e `develop`

Este plano assume a base atual da branch `codex/network-architecture-stage1`.

## Matriz de compatibilidade atual

- `QEMU + e1000`
  - status: funcional
  - validado: `IPv4`, `DHCP`, `DNS`, `ICMP/hey`, reboot e persistencia
- `VMware + e1000`
  - status: funcional
  - validado manualmente: `net-mode dhcp` e `hey <hostname>` com internet real
- `Hyper-V Gen2 + NetVSC/StorVSC`
  - status: bloqueado por plataforma
  - estado atual observado em campo: `platform-hybrid`, `bootsvc=active`, com casos em `storage-fw=on` e outros ja em `storage-fw=off`
  - deadlock dominante mais recente: `ebs=wait-input` + `input-gate=wait-boot-services`
  - faltando: destravar um `bridge` nativo pre-`EBS`, depois separar `SynIC` de `VMBus connect`, depois promocao real e segura de `StorVSC`, depois `ExitBootServices`, e so entao `NetVSC`

## Meta de compatibilidade desta frente

- preservar os cenarios `PCI`/`e1000` ja estaveis em `QEMU` e `VMware`
- tornar o diagnostico de rede coerente entre `PCI` e `Hyper-V`
- fechar o backend sintetico do `Hyper-V` sem regredir os backends `PCI`

## Progresso desta branch

Ja concluido nesta trilha:

- `src/shell/commands/network.c` reduzido para registro de comandos, com extracao para:
  - `src/shell/commands/network_common.c`
  - `src/shell/commands/network_diag.c`
  - `src/shell/commands/network_config.c`
  - `src/shell/commands/network_query.c`
- scripts de smoke divididos por responsabilidade:
  - `tools/scripts/smoke_x64_helpers.py`
  - `tools/scripts/smoke_x64_auth.py`
  - `tools/scripts/smoke_x64_boot.py`
  - `tools/scripts/smoke_x64_common.py`
  - `tools/scripts/smoke_x64_flow.py` agora como facade de compatibilidade
- `smoke_x64_cli.py`, `smoke_x64_iso_install.py` e `smoke_x64_iso_cancel.py` reduzidos por extracao do runtime compartilhado de `QEMU/OVMF/disco/log`
- primeira extracao segura de `src/net/stack.c`:
  - sincronizacao e refresh do runtime `Hyper-V` movidos para `src/net/hyperv_runtime.c`
  - contrato publico novo em `include/net/hyperv_runtime.h`
  - `stack.c` agora fica menos acoplado ao detalhe de `NetVSC/VMBus`
- segunda extracao segura de `src/net/stack.c`:
  - `DHCP` e `DNS` movidos para `src/net/stack_services.c`
  - contrato interno novo em `src/net/stack_services.h`
  - `stack.c` deixou de concentrar parsing e envio dos servicos de bootstrap de rede
- terceira extracao segura de `src/net/stack.c`:
  - `ARP` movido para `src/net/stack_arp.c`
  - `ICMP` movido para `src/net/stack_icmp.c`
  - `stack.c` ficou focado em orquestracao de frame/IP/poll/status
- quarta extracao segura de `src/net/stack.c`:
  - backend de NIC movido para `src/net/stack_driver.c`
  - selftest de protocolos movido para `src/net/stack_selftest.c`
  - `stack.c` caiu para a faixa de ~740 linhas
- `tools/scripts/inspect_disk.py` quebrado por dominio:
  - `tools/scripts/inspect_disk_common.py`
  - `tools/scripts/inspect_disk_fat32.py`
  - `tools/scripts/inspect_disk_boot.py`
  - `inspect_disk.py` agora fica focado no CLI e na orquestracao do fluxo
- `tools/scripts/provision_bootmedia.py` quebrado por dominio:
  - `tools/scripts/provision_boot_config.py`
  - `tools/scripts/provision_fat32.py`
  - `provision_bootmedia.py` fica como facade de compatibilidade para `provision_gpt.py`
- primeira extracao segura de `src/drivers/hyperv/vmbus_keyboard.c`:
  - cache de offers movido para `src/drivers/hyperv/vmbus_offers.c`
  - contrato interno privado em `src/drivers/hyperv/vmbus_offers.h`
  - `vmbus_keyboard.c` preserva apenas o adapter publico para `vmbus_query_cached_offer`
- segunda extracao segura de `src/drivers/hyperv/vmbus_keyboard.c`:
  - transporte de ring e pacote inband movidos para `src/drivers/hyperv/vmbus_ring.c`
  - contrato interno novo em `src/drivers/hyperv/vmbus_ring.h`
  - `vmbus_keyboard.c` preserva handshake, offers, canal e teclado sintetico
- terceira extracao segura de `src/drivers/hyperv/vmbus_keyboard.c`:
  - runtime generico de canal movido para `src/drivers/hyperv/vmbus_channel_runtime.c`
  - contrato interno novo em `src/drivers/hyperv/vmbus_channel_runtime.h`
  - `vmbus_keyboard.c` deixou de carregar a logica generica de `GPADL/OPENCHANNEL`
- quarta extracao segura de `src/drivers/hyperv/vmbus_keyboard.c`:
  - protocolo sintetico de teclado movido para `src/drivers/hyperv/vmbus_keyboard_protocol.c`
  - contrato interno novo em `src/drivers/hyperv/vmbus_keyboard_internal.h`
  - parsing do protocolo deixou de ficar misturado com bring-up de barramento
- quinta extracao segura de `src/drivers/hyperv/vmbus_keyboard.c`:
  - estado generico de `Hyper-V/VMBus` movido para `src/drivers/hyperv/vmbus_core.c`
  - contrato interno novo em `src/drivers/hyperv/vmbus_core.h`
  - `vmbus_keyboard.c` passou a depender do runtime generico de canal, em vez de manter sua propria copia de `SynIC/hypercall/offers`
- `tools/scripts/provision_gpt.py` quebrado por responsabilidade:
  - `tools/scripts/provision_gpt_cli.py`
  - `tools/scripts/provision_gpt_workflow.py`
  - `provision_gpt.py` fica como facade de compatibilidade
- `src/arch/x86_64/storage_runtime.c` quebrado por responsabilidade de `Hyper-V`:
  - runtime passivo e diagnostico de `StorVSC` movidos para `src/arch/x86_64/storage_runtime_hyperv.c`
  - contrato interno novo em `src/arch/x86_64/storage_runtime_hyperv.h`
  - `storage_runtime.c` voltou a focar mais em `EFI/AHCI/NVMe/GPT`
- `src/arch/x86_64/storage_runtime.c` quebrado por responsabilidade de GPT nativo:
  - parser GPT e ajuste de faixa efetiva movidos para `src/arch/x86_64/storage_runtime_gpt.c`
  - contrato interno novo em `src/arch/x86_64/storage_runtime_gpt.h`
  - `storage_runtime.c` voltou a focar mais em politica de backend e handoff
- `src/arch/x86_64/storage_runtime.c` quebrado por responsabilidade de descoberta nativa:
  - descoberta e promocao de backend nativo movidas para `src/arch/x86_64/storage_runtime_native.c`
  - contrato interno novo em `src/arch/x86_64/storage_runtime_native.h`
  - `storage_runtime.c` ficou focado em handoff, fallback EFI e politica de runtime
- `src/core/network_bootstrap.c` quebrado por responsabilidade:
  - aplicacao de configuracao movida para `src/core/network_bootstrap_config.c`
  - diagnostico e impressao de estado movidos para `src/core/network_bootstrap_diag.c`
  - `network_bootstrap.c` virou orquestrador pequeno
- `src/arch/x86_64/kernel_main.c` quebrado por telemetria e runtime de plataforma:
  - helpers de handoff, status de plataforma, input, storage e timebase movidos para `src/arch/x86_64/kernel_platform_runtime.c`
  - `cmd_info` e o trace de `EFI BlockIO` tambem passaram a usar esse modulo
  - contrato publico novo em `include/arch/x86_64/kernel_platform_runtime.h`
  - `kernel_main.c` deixou de concentrar esse bloco de diagnostico/runtime
- `src/arch/x86_64/kernel_main.c` quebrado por responsabilidade de volume cifrado:
  - helpers de normalizacao, hash, persistencia de chave ativa e mount/init do volume cifrado movidos para `src/arch/x86_64/kernel_volume_runtime.c`
  - contrato publico novo em `include/arch/x86_64/kernel_volume_runtime.h`
  - `kernel_main.c` preserva apenas wrappers pequenos para manter o fluxo e evitar mexer nas callbacks sensiveis do login
- `src/arch/x86_64/kernel_main.c` quebrado por despacho de shell:
  - roteamento de comandos e aliases movido para `src/arch/x86_64/kernel_shell_dispatch.c`
  - contrato publico novo em `include/arch/x86_64/kernel_shell_dispatch.h`
  - `kernel_main.c` preserva apenas wrappers locais para continuar seguro no caminho de login
- `src/arch/x86_64/kernel_main.c` quebrado por bootstrap de shell:
  - bootstrap de filesystem, fallback em RAM, carga de settings e preparacao do shell movidos para `src/arch/x86_64/kernel_shell_runtime.c`
  - contrato publico novo em `include/arch/x86_64/kernel_shell_runtime.h`
  - `kernel_main.c` preserva wrappers pequenas para seguir compativel com o `login_runtime`
- diagnostico `Hyper-V` consolidado:
  - labels e regras de bloqueio/plataforma compartilhadas movidas para `src/net/hyperv_platform_diag.c`
  - contrato publico novo em `include/net/hyperv_platform_diag.h`
  - `src/core/network_bootstrap_diag.c` e `src/shell/commands/network_common.c` passaram a usar a mesma fonte de verdade
- politica de refresh do runtime `Hyper-V` extraida:
  - regras de decisao `snapshot -> refresh offer -> enable controller -> step` movidas para `src/net/hyperv_runtime_policy.c`
  - contrato publico novo em `include/net/hyperv_runtime_policy.h`
  - `src/net/hyperv_runtime.c` passou a concentrar apenas snapshot, sync de estado e execucao da acao planejada
  - teste de host novo em `tests/test_hyperv_runtime_policy.c`
- observabilidade do proximo passo do `Hyper-V` ampliada:
  - `struct net_stack_status` passou a expor a acao planejada do controlador sintetico
  - `net-status` agora mostra `next=cache-offer|enable|step|noop`
- diagnostico de boot e CLI alinhados:
  - `stage/bus/cache/phase/next/block` do `Hyper-V` agora saem da mesma camada compartilhada em `src/net/hyperv_platform_diag.c`
  - `network_bootstrap_diag.c` deixou de carregar labels e checks ad hoc do `NetVSC/StorVSC`
- gate explicito de ativacao do runtime `Hyper-V`:
  - novo modulo `src/net/hyperv_runtime_gate.c` com contrato em `include/net/hyperv_runtime_gate.h`
  - `stack.c` passou a respeitar o gate antes de qualquer refresh do `NetVSC`
  - `net-status` e o boot agora mostram `gate=wait-platform|wait-bus|wait-runtime|open`
  - teste de host novo em `tests/test_hyperv_runtime_gate.c`
- entrypoint unico de promocao `Hyper-V` no x64:
  - novo modulo `src/arch/x86_64/hyperv_runtime_coordinator.c` com contrato em `include/arch/x86_64/hyperv_runtime_coordinator.h`
  - `kernel_main.c` deixou de chamar `StorVSC` e promocao de input `Hyper-V` em varios pontos diferentes
  - o coordenador agora atualiza o estado global de plataforma sempre que input/storage avancam em runtime
- plano explicito de gate/acao para `StorVSC`:
  - novo modulo `src/arch/x86_64/storage_runtime_hyperv_plan.c` com contrato em `include/arch/x86_64/storage_runtime_hyperv_plan.h`
  - `storage_runtime_hyperv.c` passou a usar `gate/next` como fonte unica para `block`, preparo de barramento e arm/disparo de probe
  - bootstrap e `net-status` agora mostram `storvsc gate=... next=...` alem de `block=...`
- instrumentacao de runtime para validacao `Hyper-V`:
  - `net_hyperv_runtime.c` agora contabiliza `attempts`, `changes`, `last_action` e `last_result`
  - `storage_runtime_hyperv.c` agora contabiliza `attempts`, `changes`, `last_action` e `last_result`
  - novo comando `net-dump-runtime` expoe esse dump consolidado sem depender de ativacao arriscada via CLI
- gate manual de runtime nativo:
  - `runtime-native show|step` agora e o entrypoint controlado do deadlock `EBS/input`
  - o fluxo foi endurecido para ser incremental: um passo pode apenas preparar a base do `VMBus`, e o passo seguinte tenta `ExitBootServices`
  - o CLI diferencia `falhou` de `nenhum passo seguro`, evitando falso positivo de progresso
- desbloqueio interno do `StorVSC`:
  - o plano de storage agora permite `step-runtime` depois de `probe`, em vez de parar para sempre em `channel`
  - se o `VMBus` ja estiver conectado por outro caminho seguro, o `StorVSC` nao fica mais preso apenas por `storage-fw=on`
  - o estado global da plataforma agora diferencia `storage-native` de `storage-synth`
  - a tentativa automatica de `prepare-bus` em runtime hibrido foi revertida depois de reboot no `Hyper-V`
  - o proximo corte precisa usar um ponto ainda mais restrito ou uma validacao fora do login para nao reabrir regressao
- sexta extracao segura de `src/drivers/hyperv/vmbus_core.c`:
  - transporte de `SynIC/hypercall/post/wait/signal` movido para `src/drivers/hyperv/vmbus_transport.c`
  - contrato interno novo em `src/drivers/hyperv/vmbus_transport.h`
  - `vmbus_core.c` ficou focado em negociacao de versao, offers e estado do barramento
- quinta extracao segura de `src/net/stack.c`:
  - utilitarios de memoria, byte-order, checksum, delay e formatacao IPv4 movidos para `src/net/stack_utils.c`
  - contrato interno novo em `src/net/stack_utils.h`
  - `stack.c` ficou mais focado em runtime, protocolo e fluxo da pilha
- sexta extracao segura de `src/net/stack.c`:
  - envio e recepcao IPv4/L4 movidos para `src/net/stack_ipv4.c`
  - contrato interno novo em `src/net/stack_ipv4.h`
  - `stack.c` passou a focar mais em estado, runtime, ARP, DHCP, DNS, ping e polling
- `tools/scripts/provision_gpt_core.py` virou facade de compatibilidade:
  - helpers de disco e scrub movidos para `tools/scripts/provision_gpt_disk.py`
  - helpers de layout GPT movidos para `tools/scripts/provision_gpt_layout.py`

Proxima fila segura de refatoracao:

1. `src/net/stack.c` e `src/net/hyperv_runtime.c` (alinhar runtime `Hyper-V` e isolar o gating de ativacao da rede)
2. `src/core/network_bootstrap_diag.c` e `src/shell/commands/network_*.c` (alinhar update futuro, logs e telemetria final)
3. `src/drivers/hyperv/vmbus_core.c` (separar `Hyper-V core` de `VMBus core`, apenas se o ganho compensar)
4. `src/arch/x86_64/kernel_volume_runtime.c` (split interno entre utilitarios de fs/hash e fluxo de mount cifrado, se continuar crescendo)

## Auditoria do estado atual

### Arquivos mais carregados

- `src/drivers/hyperv/vmbus_core.c` com cerca de `392` linhas
  - agora concentra negociacao de versao, offers e estado do barramento
  - risco principal caiu; a parte mais reutilizavel de transporte saiu do arquivo
- `src/drivers/hyperv/vmbus_transport.c` com cerca de `299` linhas
  - concentra `SynIC`, hypercall, post/wait de mensagens e sinalizacao de eventos
  - virou a base comum para teclado, `StorVSC` e `NetVSC` sem depender do driver do teclado
- `src/drivers/hyperv/vmbus_keyboard.c` com cerca de `234` linhas
  - ficou restrito ao teclado sintetico e ao acoplamento final com o runtime generico
  - risco principal caiu bastante; o arquivo ja nao carrega mais o estado inteiro do barramento
- `src/drivers/hyperv/vmbus_ring.c` com cerca de `230` linhas
  - concentra apenas ring buffer e pacote inband do `VMBus`
  - reduz o acoplamento do teclado com `NetVSC/StorVSC`
- `src/drivers/hyperv/vmbus_channel_runtime.c` com cerca de `273` linhas
  - concentra `GPADL`, `OPENCHANNEL` e o runtime generico de canal
  - ja permite reutilizacao clara por teclado, `NetVSC` e `StorVSC`
- `src/arch/x86_64/storage_runtime.c` com cerca de `380` linhas
  - agora foca em handoff, `EFI BlockIO`, fallback RAW e politica de promocao
  - risco principal caiu; o miolo de descoberta nativa ja saiu do arquivo
- `src/arch/x86_64/storage_runtime_native.c` com cerca de `328` linhas
  - concentra descoberta `AHCI/NVMe`, reconciliacao com handoff e promocao de backend nativo
  - deixa mais claro o caminho para um futuro `storage_runtime_boot/native`
- `src/arch/x86_64/kernel_main.c` com cerca de `1572` linhas
  - ainda concentra bring-up do kernel, login, prompt, shell e fluxo principal
  - risco principal: o arquivo caiu bastante, mas ainda e o principal ponto de integracao do runtime x64
- `src/arch/x86_64/kernel_shell_dispatch.c` com cerca de `48` linhas
  - concentra parse e despacho de comandos/aliases do shell sem misturar isso ao fluxo de login
  - deixa mais claro o futuro split entre bootstrap de shell e runtime de autenticacao
- `src/arch/x86_64/kernel_shell_runtime.c` com cerca de `269` linhas
  - concentra bootstrap de filesystem, fallback RAM e preparacao de settings/contexto do shell
  - reduz bastante o risco de regressao lateral no `kernel_main` e prepara o proximo split seguro do login
- `src/arch/x86_64/kernel_platform_runtime.c` com cerca de `396` linhas
  - concentra contrato de handoff e telemetria/runtime de plataforma do x64
  - reduz drift entre `kernel_main`, `storage_runtime` e `input_runtime`
- `src/arch/x86_64/kernel_volume_runtime.c` com cerca de `782` linhas
  - concentra bootstrap do volume cifrado, persistencia da chave ativa e utilitarios de filesystem local ao runtime
  - reduz o risco de regressao lateral no `kernel_main`, mesmo ainda merecendo um split futuro entre hash/fs/crypto se continuar crescendo
- `src/core/network_bootstrap_diag.c` com cerca de `126` linhas
  - concentra o diagnostico de rede exibido no boot
  - deixa `network_bootstrap.c` pequeno e menos sujeito a regressao lateral
- `src/net/hyperv_platform_diag.c` com cerca de `113` linhas
  - concentra labels e regras de bloqueio `Hyper-V` compartilhadas entre shell e bootstrap
  - reduz drift no ponto mais sensivel de observabilidade da rede no `Hyper-V`
- `src/arch/x86_64/storage_runtime_gpt.c` com cerca de `200` linhas
  - concentra apenas parser GPT nativo e validacao de faixa de dados
- `src/net/stack.c` com cerca de `500` linhas
  - mistura descoberta de driver, runtime da NIC, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, ping e integracao `Hyper-V`
  - risco principal: mudar uma funcao de DHCP, ARP ou runtime pode quebrar fluxo lateral sem isolamento claro
- `src/net/stack_utils.c` com cerca de `87` linhas
  - concentra utilitarios de memoria, endian, checksum, delay e formatacao IPv4
  - prepara futuras extracoes maiores do `stack.c` sem duplicar helpers
- `src/net/stack_ipv4.c` com cerca de `249` linhas
  - concentra envio e recebimento IPv4, inclusive `UDP/TCP/ICMP` no nivel de frame/IP
  - reduz o acoplamento do fluxo L3/L4 com o estado global da stack

### Pontos estruturais ja bons

- `NetVSC`, `RNDIS`, `NetVSP`, `StorVSC`, `StorVSP` e suas state machines offline ja estao separados em modulos pequenos
- `network.c`, `provision_gpt.py`, `provision_bootmedia.py`, `inspect_disk.py` e os smokes principais ja sairam do formato monolitico
- `network_bootstrap.c` ja ficou pequeno e o diagnostico/configuracao sairam para modulos dedicados
- o diagnostico `Hyper-V` agora sai da mesma fonte de verdade no shell e no boot
- `vmbus_keyboard.c` ja ficou pequeno e reaproveita o runtime generico de canal
- ha testes unitarios de host cobrindo boa parte dessas state machines offline
- `net-status` e o bootstrap ja expoem o bloqueio atual da plataforma

### Bloqueio real atual no Hyper-V

O bloqueio atual nao e DNS, DHCP nem a CLI. O bloqueio e de plataforma:

- runtime ainda `hybrid`
- `Boot Services` ainda ativos
- storage ainda preso a firmware (`storage-fw=on`)
- `VMBus` nao pode ser promovido agressivamente no boot nem pela CLI sem risco de reboot

Conclusao: a rede `Hyper-V` so fecha de forma segura quando `StorVSC` e `NetVSC` ganharem um ponto de ativacao mais restrito do que o boot atual e do que `net-refresh`.

## Direcao de arquitetura alvo

### Stack de rede

Estrutura alvo:

- `src/net/core/`
  - estado global da stack
  - configuracao IPv4
  - estatisticas
  - selecao de backend de NIC
- `src/net/l2/`
  - Ethernet
  - ARP
- `src/net/l3/`
  - IPv4
  - ICMP
- `src/net/l4/`
  - UDP
  - TCP basico
- `src/net/services/`
  - DHCP client
  - DNS client
- `src/net/runtime/`
  - integracao com drivers concretos
  - `NetVSC` runtime
  - politicas de refresh/ativacao

### CLI de rede

Estrutura alvo:

- `src/shell/commands/network.c`
  - apenas registro dos comandos
- `src/shell/commands/network_diag.c`
  - `net-status`, `net-ip`, `net-dns`, `net-gw`, `net-refresh`
- `src/shell/commands/network_config.c`
  - `net-set`, `net-mode`
- `src/shell/commands/network_query.c`
  - `net-resolve`, `hey`
- `src/shell/commands/network_common.c`
  - textos, helpers, parse, labels e funcoes compartilhadas

### Hyper-V / VMBus

Sequencia funcional alvo para `Hyper-V Gen2`:

1. `runtime gate`
   - confirmar estado real de `Boot Services`, input nativo e backend de storage ativo
   - bloquear qualquer promocao de rede se o storage ainda depender de firmware
2. `VMBus transport`
   - separar claramente:
     - `hypercall prepared`
     - `SynIC/message pages ready`
     - `INITIATE_CONTACT completed`
     - `offers cached`
3. `StorVSC`
   - preparar barramento sem efeitos colaterais no boot
   - negociar `offer/channel/control`
   - marcar `synthetic_storage_ready` so quando houver backend realmente utilizavel
   - fazer switchover real do storage ativo antes de liberar `ExitBootServices`
4. `ExitBootServices`
   - sair do modo hibrido apenas quando input e storage tiverem caminho seguro
5. `NetVSC`
   - abrir `offer/channel/control`
   - concluir `NetVSP/RNDIS`
   - ligar `TX/RX` ao stack comum
6. `servicos`
   - validar `DHCP`
   - validar `DNS`
   - validar `hey`/ICMP
   - validar reboot e persistencia

Regra de seguranca desta trilha:

- em runtime hibrido, comandos manuais podem no maximo preparar estado passivo
- `connect`, `REQUESTOFFERS`, `OPENCHANNEL` e handshakes de controle devem ficar fora do hibrido ate existir uma janela segura validada em campo

Estrutura alvo:

- `src/drivers/hyperv/hyperv_core.c`
  - deteccao `Hyper-V`, MSRs, SynIC, hypercall base
- `src/drivers/hyperv/vmbus_core.c`
  - `vmbus_init`, estado do barramento, versao, conexao base
- `src/drivers/hyperv/vmbus_channel_runtime.c`
  - open/close channel, GPADL, rings, leitura de pacotes
- `src/drivers/hyperv/vmbus_offers.c`
  - cache de offers e consulta por GUID
- `src/drivers/hyperv/vmbus_keyboard.c`
  - somente teclado sintetico

### Storage runtime

Estrutura alvo:

- `src/arch/x86_64/storage_runtime_boot.c`
  - handoff, EFI, GPT, promocao de backend no boot
- `src/arch/x86_64/storage_runtime_native.c`
  - `AHCI` e `NVMe`
- `src/arch/x86_64/storage_runtime_hyperv.c`
  - politica e diagnostico de `StorVSC`

## Entregas por ordem correta

### Entrega N1 - Observabilidade e politica de bloqueio

Objetivo:

- tornar o motivo do bloqueio explicito para `NetVSC` e `StorVSC`
- impedir tentativas perigosas fora do ponto de ativacao correto

Aceite:

- `net-status` mostra `block=...`
- bootstrap mostra `block=...`
- nenhuma nova tentativa ativa de `VMBus` ocorre no boot nem na CLI

### Entrega N2 - Refatoracao do CLI de rede

Objetivo:

- quebrar `src/shell/commands/network.c` em modulos menores
- preservar todos os comandos e a saida atual

Aceite:

- comportamento identico nos smokes existentes
- arquivo `network.c` reduzido a registro de comandos e glue minimo
- novas funcoes compartilhadas com cobertura local

### Entrega N3 - Refatoracao interna da stack

Objetivo:

- reduzir `src/net/stack.c` por camadas e servicos
- isolar `DHCP`, `DNS`, `ARP`, `ICMP` e runtime de NIC

Aceite:

- `stack.c` deixa de concentrar servicos e protocolos
- testes existentes continuam passando
- novas funcoes ficam em arquivos menores e com escopo claro
- progresso atual:
  - runtime `Hyper-V` isolado em `src/net/hyperv_runtime.c`
  - `DHCP/DNS` isolados em `src/net/stack_services.c`
  - `ARP` isolado em `src/net/stack_arp.c`
  - `ICMP` isolado em `src/net/stack_icmp.c`
  - backend de NIC isolado em `src/net/stack_driver.c`
  - selftest isolado em `src/net/stack_selftest.c`
  - proximo corte seguro: decidir se o restante de `stack.c` ainda merece nova quebra ou se o maior retorno ja passou para `VMBus` e `storage_runtime`

### Entrega N4 - Separacao do VMBus generico do teclado

Objetivo:

- tirar `VMBus` generico de `vmbus_keyboard.c`
- deixar teclado, rede e storage dependentes de um transporte comum e pequeno

Aceite:

- `vmbus_keyboard.c` fica focado em teclado
- `NetVSC` e `StorVSC` passam a depender de `vmbus_core/channel/offers`
- nenhuma regressao de teclado no `Hyper-V`
- progresso atual:
  - cache de offers isolado em `src/drivers/hyperv/vmbus_offers.c`
  - transporte de ring isolado em `src/drivers/hyperv/vmbus_ring.c`
  - runtime generico de canal isolado em `src/drivers/hyperv/vmbus_channel_runtime.c`
  - protocolo sintetico do teclado isolado em `src/drivers/hyperv/vmbus_keyboard_protocol.c`
  - estado generico do barramento isolado em `src/drivers/hyperv/vmbus_core.c`
  - `vmbus_keyboard.c` caiu para a faixa de ~234 linhas e passou a focar so no dispositivo

### Entrega N5 - Runtime seguro de StorVSC

Objetivo:

- criar um ponto de ativacao controlado, fora da CLI e fora do boot inicial
- avancar `StorVSC` por budget pequeno e com diagnostico

Dependencias:

- N1 concluida
- N4 concluida ou com camada de transporte equivalente pronta

Aceite:

- sem reboot ao armar ou consultar `StorVSC`
- logs claros de fase e bloqueio
- `StorVSC` avancando em ambiente controlado quando os pre-requisitos existirem

### Entrega N6 - Runtime seguro de NetVSC

Objetivo:

- usar o mesmo ponto controlado para `NetVSC`
- negociar `NetVSP/RNDIS` sem CLI agressiva

Dependencias:

- N5

Aceite:

- `net-status` sai de `driver-missing`
- `net-resolve` e `DHCP` funcionam no `Hyper-V`
- reboot e boot inicial continuam estaveis

## Validacao correta da rede no Hyper-V

### Validacao manual obrigatoria

Toda rodada sensivel em `Hyper-V` deve validar:

1. boot pela ISO
2. formatacao/instalacao
3. primeiro boot do sistema instalado
4. login
5. `net-status`
6. `net-refresh` apenas quando explicitamente liberado para aquela rodada
7. `net-mode dhcp`
8. `net-resolve example.com`
9. `net-dump-runtime` antes e depois do passo sensivel
10. reboot
11. repetir `net-status`, `net-dump-runtime` e `net-resolve`

Marcador esperado da rodada atual:

- depois de login estavel, sem usar `net-refresh`, o `StorVSC` deve deixar de ficar puramente em `wait-platform`
- o esperado e ver `storvsc gate=prepare-bus` ou `storvsc next=prepare-bus`, seguido de incremento em `attempts`
- o sistema nao deve reiniciar nem no primeiro boot instalado nem ao permanecer ocioso no prompt

### Matriz de aceite

- sem reboot espontaneo
- sem loop de boot
- `net-status` coerente com o estado real do controlador
- mensagens de bloqueio explicitas
- DHCP e DNS funcionando quando o runtime estiver pronto

## Checklist restante para fechar a entrega Hyper-V

### Estado atual

- `QEMU/e1000`: `IPv4 + DHCP + DNS + ICMP/hey` ja funcionam
- `Hyper-V`: diagnostico, gating, politica de refresh e coordenacao de ativacao ja estao estruturados
- bloqueio real restante: plataforma ainda `hybrid`, storage ainda em firmware, e `NetVSC/StorVSC` ainda sem promocao real segura no runtime
  - observacao mais recente de campo: ha cenarios `Hyper-V` em que `storage-fw` ja aparece `off`, entao o deadlock remanescente passa a ser `Boot Services` ativos sem input nativo
  - a partir desta rodada, `net-status`, `net-dump-runtime` e o bootstrap tambem expõem o gate explicito de `ExitBootServices` (`ebs=wait-input|wait-contract|wait-storage-*|ready|failed`) para reduzir validacao “no escuro”
  - o diagnostico agora tambem expõe `input-gate=wait-boot-services|ready|retry|failed`, para deixar explicito quando o teclado `Hyper-V` ainda depende da saida de `Boot Services`
  - `runtime-native show|step` agora existe como entrypoint controlado para observar e acionar um unico passo seguro do coordenador, sem recolocar automacao agressiva no boot
  - `prepare-bridge`, `prepare-synic` e agora tambem `prepare-input` deixaram de fazer parte da trilha recomendada em runtime hibrido; a validacao em Hyper-V real mostrou reboot mesmo em passos minimos
  - a trilha de validacao de campo daqui para frente passa a ser `runtime-native show -> observacao do estado -> net-status/net-dump-runtime`, e nao mais `net-refresh`

### O que falta implementar

1. Promocao segura do `StorVSC` em runtime nativo
   - antes disso, fechar o deadlock de `ExitBootServices` com input nativo
   - isolar a politica de `ExitBootServices` em um gate unico e observavel
   - usar esse gate como pre-requisito explicito da promocao `StorVSC/NetVSC`
   - usar o coordenador x64 como unico ponto de entrada
   - usar `gate/next` do `StorVSC` para liberar cada corte de forma observavel
   - sair de `disabled` para `prepare-bus` e depois `probe` sem reboot
   - manter `BOOT` e login estaveis no `Hyper-V`
   - considerar que parte dos cenarios reais ja nao esta mais bloqueada por `storage-fw`, mas por `Boot Services` + `input-native=no`

  2. Validacao de offer/canal do `StorVSC`
     - consumir a offer cacheada
     - preparar o canal offline e expor progresso em `phase=probe|channel`
     - ainda sem tocar no caminho de I/O de dados real
     - validar que o arm pos-login nao reabre reboot espontaneo no primeiro boot instalado

3. Promocao segura do `NetVSC`
   - liberar o gate de rede apenas depois de `StorVSC` nao depender mais do firmware
   - sair de `runtime=driver-missing` para runtime configurado
   - manter `net-refresh` sem reboot e com diagnostico coerente

4. Handshake real `NetVSP/RNDIS` no `Hyper-V`
   - completar `offer -> channel -> control -> ready`
   - validar `MAC/MTU/filter`
   - expor erro final claro quando falhar

5. Integracao de `TX/RX` do `NetVSC` na stack
   - ligar envio e recebimento reais ao backend sintetico
   - validar `ARP`, `DHCP` e `ICMP` sobre o backend novo
   - so depois promover `DNS` e `hey <hostname>`

6. Validacao ponta a ponta no `Hyper-V`
   - `net-status`
   - `net-mode dhcp`
   - `hey gateway`
   - `net-resolve example.com`
   - reboot
   - repetir diagnostico e confirmacao de lease/resolucao

### Criterio objetivo para considerar a entrega concluida

- sem reboot espontaneo no `Hyper-V` durante boot, login, `net-status` ou `net-refresh`
- `net-status` no `Hyper-V` deixando de mostrar `runtime=driver-missing`
- `gate=open` e `block=none` para o `NetVSC`
- `net-mode dhcp` obtendo lease real
- `hey gateway` respondendo com latencia
- `net-resolve example.com` respondendo corretamente apos DHCP
- reboot preservando estado funcional da rede

### Distancia estimada ate ping/internet solidos

- pilha e diagnostico: praticamente prontos
- backend `Hyper-V` real: ainda faltam os blocos mais sensiveis
- estimativa atual:
  - cerca de `1` grande trilha para `StorVSC`
  - mais `1` grande trilha para `NetVSC`
  - depois `1` rodada de integracao e validacao ponta a ponta

### Instrumentacao desejada

Adicionar nas proximas rodadas:

- snapshot de runtime em `/system/logs/net-runtime.log`
- `net-dump-runtime`
- serial mirror opcional das fases `VMBus/StorVSC/NetVSC`
- numeracao de tentativas e ultimo ponto de falha

## Estrategia de refatoracao segura

Para evitar que uma atualizacao de funcao quebre outra:

- toda extracao deve preservar a API publica existente primeiro
- cada modulo novo precisa de teste local ou smoke cobrindo o comportamento legado
- mover em cortes pequenos: primeiro helpers, depois handlers, depois estado e por fim transporte
- nao misturar refactor estrutural com mudanca de protocolo sensivel na mesma entrega
- toda mudanca `Hyper-V` sensivel deve ficar atras de um ponto unico de ativacao

## Sistema de atualizacao futuro

### Objetivo

Permitir que o sistema:

- consulte atualizacoes pela rede
- opere com dois canais: `main` e `develop`
- baixe artefatos corretos do repositório
- aplique a atualizacao com logs e rollback seguros

### Modelo de canais

- `main`
  - canal estavel
  - atualizacoes apenas de releases promovidos
- `develop`
  - canal de preview/desenvolvimento
  - mais frequente e com risco maior

Configuracao persistente:

- `update.channel=main|develop`
- `update.source=<repo-origem>`
- `update.last_version`
- `update.last_result`

### Comandos planejados

- `update-channel show`
- `update-channel main`
- `update-channel develop`
- `update-check`
- `update-apply`
- `update-log`
- `update-rollback`

### Fluxo tecnico alvo

1. consultar manifesto remoto do canal
2. comparar versao local com versao remota
3. baixar manifesto + kernel + artefatos de userspace
4. validar checksum e assinatura
5. gravar staging em area segura
6. atualizar `BOOT` e arquivos persistentes controlados
7. registrar logs de atualizacao
8. reboot controlado
9. confirmar sucesso no proximo boot
10. reter rollback se a confirmacao nao ocorrer

### Artefatos recomendados

- manifesto do canal
- `kernel`
- `init/config payload`
- changelog curto
- checksums
- assinatura

### Dependencias antes de comecar

- rede funcional no `Hyper-V`
- cliente HTTP/TLS minimo ou downloader equivalente
- camada de armazenamento de staging confiavel
- politica de versao consistente entre `main` e `develop`

## O que comecar agora

Ordem imediata recomendada:

1. fechar `N1` por completo
2. executar `N2` e quebrar o CLI de rede
3. seguir para `N3` e reduzir `stack.c`
4. so depois voltar a `VMBus`, `StorVSC` e `NetVSC` sensiveis

## Resultado esperado ao fim desta frente

- pilha de rede mais legivel e testavel
- `Hyper-V` com diagnostico claro e caminho de ativacao controlado
- bootstrap menos acoplado
- base pronta para updater real com canais `main` e `develop`

## Atualizacao 2026-03-25

- `prepare-storage` foi endurecido para parar no passo seguro disponivel durante runtime hibrido; ele nao conecta nem negocia `VMBus` enquanto `Boot Services` seguem ativos.
- O gate de `ExitBootServices` voltou a exigir que o backend de storage ativo esteja realmente fora do firmware.
- A proxima meta funcional deixou de ser "forcar StorVSC no hibrido" e passou a ser "sair do hibrido com seguranca, depois promover StorVSC/NetVSC".
