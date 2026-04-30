# Plano de Hardening de Plataforma

Status inicial: 2026-03-12
Escopo: concluir a etapa de hardening da plataforma x64 antes de avancar para
scheduler, userspace mais rico ou novos subsistemas pesados.

## Objetivo da etapa

Transformar o caminho atual `UEFI/GPT/x86_64` de um runtime funcional de
bring-up em uma base de kernel previsivel, com fronteira clara entre firmware e
runtime proprio.

Definicao de concluido:
- boot por HDD provisionado sem depender de `EFI ConIn` no runtime
- handoff UEFI com contrato explicito de capacidades
- `ExitBootServices` fechado no fluxo principal
- base x64 com interrupcoes, timer e input nativos minimos estaveis
- caminho de storage identificado como firmware-borrowed ou native sem ambiguidade
- smoke de HDD e smoke de instalacao por ISO cobrindo login e persistencia

## Problemas atuais

1. O boot permanece em modo hibrido para manter input de firmware.
2. O kernel x64 ainda toma decisoes a partir de ponteiros UEFI implicitos.
3. A base x64 ainda usa stubs para GDT/IDT/PIT e parte do TTY.
4. Input nativo ainda esta incompleto em Hyper-V e USB HID.
5. O runtime de storage depende de EFI BlockIO emprestado pelo loader.
6. O estado da plataforma nao esta visivel o suficiente durante boot e smoke.

## Linhas de trabalho

### 1. Contrato de handoff
- explicitar no handoff quais servicos de firmware permanecem ativos
- separar "ponteiro presente" de "servico valido em runtime"
- registrar modo da plataforma no boot log e no kernel

### 2. Fronteira firmware -> kernel
- introduzir caminho principal com `ExitBootServices`
- manter caminho hibrido apenas como fallback controlado durante a migracao
- eliminar dependencias do kernel em `EFI_SYSTEM_TABLE` fora de modulos
  explicitamente temporarios

### 3. Base x64 real
- substituir stubs de GDT/IDT por implementacoes x64 reais
- instalar excecoes basicas para evitar resets silenciosos
- trocar `pit_ticks` fake por fonte de tempo consistente
- preparar fila minima de eventos/IRQ para input e timer

### 4. Input nativo
- fechar PS/2 polled/IRQ no x64 com caminho confiavel
- endurecer teclado Hyper-V VMBus
- concluir enumeracao XHCI minima e teclado HID
- remover dependencia estrutural de `EFI ConIn`

### 5. Storage de runtime
- tornar explicito quando o kernel esta usando EFI BlockIO emprestado
- preparar caminho nativo para storage persistente sem firmware
- reduzir o acoplamento entre montagem do volume e detalhes do loader

### 6. Observabilidade e falha segura
- exibir modo de plataforma no boot
- registrar razao de fallback para input/storage
- melhorar erros de boot para distinguir problema de firmware, input e disco

### 7. Validacao
- smoke HDD: boot, login, CLI, persistencia
- smoke ISO: instalar, reboot por HDD, login, persistencia
- validacao local do artefato ISO e validacao final em Hyper-V Gen2
- checklist minimo: `make test`, `make all64`, `make smoke-x64-cli`,
  `make smoke-x64-iso`, `make inspect-disk`

## Sequencia recomendada

### Marco A - Contrato explicito e telemetria
- adicionar flags de runtime ao handoff
- fazer kernel consumir flags em vez de inferencias por ponteiro
- imprimir modo da plataforma no boot

Status: iniciado neste branch.

### Marco B - Fundacao x64 minima
- implementar GDT/IDT x64
- instalar tratadores minimos de fault
- substituir timer fake por base real

Status: em andamento neste branch (timebase real entregue; descritores, faults
e caminho de timer/IRQ minimo x64 em preparacao com ativacao condicionada ao
runtime nativo).

### Marco C - Input sem firmware
- promover PS/2/VMBus/HID como fontes primarias
- deixar `EFI ConIn` apenas como fallback temporario e opcional
- remover a necessidade de manter Boot Services por causa do teclado

Status: concluido nesta branch para a trilha `Hyper-V Gen2`, com politica
explicita de backends, probe PS/2
tambem em boot hibrido, separacao entre backend detectado e backend funcional
e aposentadoria controlada do fallback EFI apos confirmacao de input nativo.
Nesta fatia, o teclado VMBus ganhou abertura minima de canal, negociacao de
protocolo 1.0 e polling por ring buffer. Em `Hyper-V Gen2`, a subida
automatica do `VMBus keyboard` foi recolocada em modo conservador durante o
boot hibrido: o kernel adia esse backend enquanto `Boot Services` ainda estao
ativos e mantem `EFI ConIn` como caminho principal ate o runtime nativo dessa
trilha ficar estavel. Na branch de continuidade deste trabalho, o runtime de
input passou a carregar estado explicito de `Hyper-V deferred` e um hook de
promocao controlada para o `VMBus keyboard` em ambiente ja nativo. Esse hook
ja foi ligado no pos-`ExitBootServices` e no loop de input quando o runtime ja
esta fora do firmware, preparando o proximo corte sem reabrir a regressao do
boot hibrido. O driver `VMBus` tambem recebeu endurecimento de reentrada para
inicializacao tardia e a promocao controlada passou a usar retentativa
limitada com backoff curto no runtime nativo, evitando que uma primeira falha
precoce apos `ExitBootServices` congele permanentemente o backend em
`deferred-failed`. Nesta continuacao, o runtime tambem passou a detectar
degradacao do canal `VMBus` durante o polling de teclado, recuar para o
fallback nativo remanescente e reagendar a promocao controlada sem exigir
reboot. A preferencia entre `PS/2` e `VMBus` tambem ficou mais precisa: a
primeira promocao continua conservadora quando `PS/2` ainda esta presente, mas
depois que o `VMBus` confirma input real no runtime ele passa a ser lembrado
como backend preferido nas promocoes seguintes. Nesta rodada, o runtime tambem
ganhou contadores de eventos/promocoes/degradacoes do `VMBus` e estacionamento
automatico do `PS/2` quando o backend sintetico ja acumulou janela minima de
estabilidade, com reativacao automatica do fallback se o canal degradar. A
validacao manual final em `Hyper-V Gen2` fechou essa frente com boot, format,
login e teclado funcionando corretamente.

### Marco D - Saida do modo hibrido
- fechar `ExitBootServices` no fluxo principal
- revisar entry/handoff para ambiente puro de kernel
- garantir que o boot continua funcional sem ponteiros UEFI ativos

Status: em preparacao neste branch com runtime de storage x64 extraido do
`kernel_main`, preservando o backend atual `efi-blockio` como passo de
transicao para o backend nativo. O runtime ja detecta candidato nativo
`NVMe` no smoke QEMU e agora resolve a faixa `DATA` por `gpt-data`, mantendo
`handoff-raw` apenas como fallback de seguranca quando o GPT nativo nao fecha.
No caminho NVMe/QEMU, o runtime promove o backend `nvme` antes de qualquer uso
de `EFI BlockIO`, justamente para evitar a sobreposicao insegura entre driver
nativo e firmware no mesmo controlador. O harness de smoke tambem captura
`debugcon` para diagnostico do bring-up cedo de NVMe. O handoff ja carrega
`image handle`, `map_key`, `memmap_size` e `memmap_capacity` para o corte do
`ExitBootServices` do lado do kernel. Nesta rodada, o kernel executou
`ExitBootServices` nos dois boots do smoke NVMe/QEMU, apos confirmar input e
storage nativos, e o fluxo completo de boot/login/persistencia passou com
`Runtime mode: native`, `Boot services: inativos` e backend
`nvme data=gpt-data firmware=off`. O rearmamento imediato do timer nativo apos
`ExitBootServices` mostrou-se prematuro, entao o branch manteve apenas GDT/IDT
nativos nessa transicao e deixou a ativacao do `PIT/IRQ0` adiada ate o pos-EBS
ficar completamente estavel. Nesta mesma frente entrou um backend `AHCI`
minimo para `SATA/QEMU`, reutilizando o mesmo parser GPT nativo e promovendo
`DATA` por `gpt-data` antes de qualquer uso de `EFI BlockIO`. O primeiro bring-up
de `AHCI` caiu em `#UD` porque o driver carregava `struct block_device_ops`
com ponteiros de funcao inicializados em tempo de link (`.data.rel.local`),
mas o loader atual nao aplica relocoes de dados. A correcao foi migrar esse
ops table para inicializacao em runtime, no mesmo padrao reloc-safe ja usado
no `NVMe`. Com isso, os smokes completos de `SATA/QEMU` e `NVMe/QEMU`
passaram em imagens de 1 GiB, ambos com `Runtime mode: native`,
`Firmware input/block I/O: inativos`, `ExitBootServices` concluido nos dois
boots e persistencia validada.

### Marco E - Fechamento de etapa
- smoke ISO ponta a ponta
- validacao final em Hyper-V Gen2
- limpeza do codigo temporario usado apenas na migracao

Status: concluido para esta implantacao. O smoke ponta a ponta da ISO oficial
foi entregue com automacao de instalacao, reboot pelo disco, login e
persistencia, e a trilha `Hyper-V Gen2` foi validada no fechamento deste
branch. No fluxo local, o instalador entra por `CAPYOS.INI`, conclui a
gravacao do disco, reinicia e os boots subsequentes sobem em
`Runtime mode: native` com `ExitBootServices` concluido e backend
`ahci data=gpt-data firmware=off`. A limpeza adicional de codigo temporario e
o aprofundamento do input nativo em `Hyper-V` seguem como proxima etapa, nao
como bloqueio deste merge.

## Entregaveis tecnicos

- handoff versionado e documentado
- bootstrap x64 com limites claros entre loader e kernel
- base minima de excecoes/timer/input
- docs atualizadas com o caminho oficial da etapa
- suite de validacao cobrindo HDD e ISO

## Riscos da etapa

- regressao de teclado em Hyper-V/OVMF
- perda do caminho de debug se o corte do modo hibrido for abrupto
- boot aparentemente "travado" por fault sem excecao instalada
- quebra do volume persistente se storage for migrado sem matriz de smoke

## Estrategia de execucao

- mudar primeiro o contrato e a observabilidade
- depois trocar um subsistema por vez
- manter fallback temporario enquanto houver cobertura insuficiente
- remover fallback apenas quando o smoke da plataforma estiver estavel
