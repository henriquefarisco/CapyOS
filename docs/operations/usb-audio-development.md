# USB Audio — Etapa 10, desenvolvimento

Estado em 2026-09-30: **descoberta, fila e despacho testados; reprodução USB ainda não entregue**.
Branch: `feature/etapa-10-audio-multimedia`.

`usb_audio_descriptors.c` seleciona configuração UAC1, alternate não nulo,
terminal USB e endpoint isócrono OUT para PCM estéreo S16/48 kHz full-speed.
Suporta descritores de frequência fixa, lista discreta e faixa contínua; os dois
últimos exigem controle de frequência. Endpoints assíncronos com feedback e
formatos não implementados são recusados. O resultado só é publicado depois de
validar todo o buffer, limitado a 4096 bytes, sem alocação nem I/O.

`make test-usb-audio` integra `make test`. Evidência local em
`build/etapa10-completion/usb-audio-descriptors2.log` e
`usb-audio-sanitize.log`: testes positivos, todos os truncamentos da fixture,
formatos incompatíveis, faixas inválidas, cauda malformada e 10000 mutações
determinísticas passaram; ASan/UBSan também passou.

O núcleo isócrono agora integra os inventários do kernel e dos testes host.
O despachante encaminha cada conclusão ao consumidor da fila, sem sobrescrever
um único latch, e usa um gate não bloqueante para evitar consumo concorrente.
Testes cobrem contenção/retry, lote de três pacotes, conclusão duplicada,
wrap/capacidade da fila, limites DMA e rejeição de contextos sobrepostos.
`usb-events-focused.log`, `usb-events-sanitize.log` e `usb-iso-sanitize.log`
registram aprovação. `make test-usb-audio-cross-objects TOOLCHAIN64=elf` também
passou (`usb-events-cross.log`), executando os objetos freestanding reais no
harness host; isso não substitui DMA em VM.

O teardown de slot conserva memória e DCBAA quando Disable Slot falha; somente
uma confirmação bem-sucedida permite liberar recursos. O teste de falha seguido
de retry exercita essa regra. A primeira suíte ampla (`usb-core-full-tests.log`)
falhou por falta do novo objeto no link; o inventário foi corrigido e a nova
execução em `usb-events-full-tests.log` passou. O checkpoint seguinte de
[DMA/MMIO e runtime](usb-mmio-validation-20260930.md) registra buffer EP0
persistente, correção de page fault em BAR alto, dois boots USB HID e regressão
do player em QEMU/VMware, além da suíte ampla posterior aprovada.

Para o futuro backend, o contrato real é fragmento de **4096 bytes (~21 ms)**,
não 16 KiB. O horizonte de cópia antecipada precisa ficar abaixo do intervalo de
renovação do fragmento, com teste de wrap e de refill, antes de aceitar PCM USB.

Pendências para este driver contar como entregue:

- integrar seleção/configuração de alternate e frequência ao USB core;
- ligar a fila isócrona ao hardware, configurar DMA e validar cancelamento/lifetime;
- integrar backend ao `audio_output`, incluindo posição, stop, erro e desconexão;
- executar áudio capturado em QEMU `usb-audio` e regressões HID/áudio existentes;
- fechar os gates finais da Etapa 10, sem confundir parser com driver funcional.

Atualização 2026-10-01: seleção UAC1 integrada à enumeração, atualização MPS
de EP0 por Evaluate Context e coexistência com teclado USB passaram no QEMU.
Ver [checkpoint de preparação/USB](audio-prepare-validation-20261001.md).
A configuração do alternate e reprodução isócrona eram pendentes nesse checkpoint.
O [checkpoint de saída USB](usb-output-validation-20261001.md) posterior comprova
backend UAC1, captura PCM, mistura/volume, coexistência HID e contenção de unplug.
As pendências de transporte acima foram superadas para o subconjunto documentado;
UAC2, feedback assíncrono e recuperação após falha continuam fora desse suporte.

Referências primárias utilizadas:

- [USB Audio Device Class 1.0](https://www.usb.org/sites/default/files/audio10.pdf), seções 4.3, 4.5, 4.6.
- [USB Audio Data Formats 1.0](https://www.usb.org/sites/default/files/frmts10.pdf), seção 2.2.5.
- [Modelo USB Audio do QEMU 10.2.1](https://github.com/qemu/qemu/blob/v10.2.1/hw/usb/dev-audio.c).
- [Especificação xHCI](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf), seções 6.2.3, 4.11.2.5 e 6.4.3.8–6.4.3.9 consultadas para contexto, agendamento e devolução de ownership.
