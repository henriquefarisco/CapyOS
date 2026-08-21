# CapyOS 0.8.0-alpha.321+20260821

**Data:** 2026-08-21
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.321+20260821`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** correcao critica da entrada do instalador + regressao sem UART + bootstrap de ambiente

## Resumo executivo

`alpha.321+20260821` corrige o instalador UEFI em maquinas virtuais sem uma
porta serial COM1 configurada. No VMware, leituras de uma porta de I/O legado
nao mapeada retornavam `0xFF`; o loader interpretava esse valor como entrada,
selecionava COM1 como fonte e preenchia o prompt com caracteres `ÿ`, impedindo
o uso do teclado da VM.

O loader agora detecta COM1 por loopback antes de usa-la, rejeita estados
`0x00`/`0xFF`, erros de recepcao e bytes que nao representam teclas, e somente
fixa a fonte serial depois de receber uma tecla valida. Quando firmware ConIn e
UART compartilham a mesma fila, uma janela limitada de 20 ms permite que EFI
ConIn consuma a tecla antes do fallback direto, evitando perda ou mistura de
entrada.

A release tambem adiciona uma regressao QEMU/OVMF que inicializa a ISO oficial
sem UART, comprova a ausencia de `isa-serial`, observa o prompt ocioso, injeta
somente `0` + Enter por teclado e exige cancelamento sem alterar o disco.

## Mudancas

- **Entrada UEFI:** nova politica pura valida presenca da UART, estado da linha
  e bytes de teclado antes de selecionar COM1.
- **VMs sem serial:** uma porta ausente, inclusive o retorno `0xFF` observado no
  VMware, nao pode mais se transformar em uma sequencia automatica de `ÿ`.
- **EFI ConIn:** o teclado da firmware ganha uma janela curta e limitada para
  consumir a entrada antes do fallback serial direto.
- **Fonte de entrada:** COM1 somente fica fixada depois de uma tecla valida;
  ruido, erros RX e controles nao suportados sao descartados.
- **Observabilidade:** markers em debugcon registram prompt, fonte e
  cancelamento sem depender da propria UART investigada.
- **Regressao QEMU:** o alvo `smoke-x64-qemu-installer-no-uart`, incorporado ao
  smoke da ISO, verifica topologia sem serial, prompt estavel, entrada via ConIn
  e disco byte-identico apos cancelamento.
- **Testes host:** a suite `installer_input_policy` cobre UART ausente, estados
  plausiveis, erros RX, ASCII imprimivel, Enter e Backspace.
- **Bootstrap Windows/WSL:** os instaladores de dependencias passam a validar
  WSL, usuario normal, Python, VMware Workstation, QEMU/OVMF, gnu-efi, xorriso,
  OpenSSL e a toolchain cruzada `x86_64-elf`, propagando falhas de forma
  explicita.

## Validacao

Todos os gates abaixo foram executados sobre o mesmo ISO final versionado,
`build/CapyOS-Installer-UEFI.iso`, SHA-256
`f8348236303d81da091f8be48ef1d55b0cfbc8f219522e8f94eb241dd97e0b7e`.

| Evidencia | Resultado |
|---|---|
| `make release-check TOOLCHAIN64=elf` | passa; testes, auditoria de layout, versao, toolchain, build UEFI e checksums validados |
| QEMU/OVMF sem UART | passa; `isa-serial` ausente, prompt estavel, `0` + Enter via ConIn e disco byte-identico apos cancelamento |
| VMware sem UART | passa; zero dispositivos seriais, prompt limpo e estavel, cancelamento exato por teclado e dois discos byte-identicos |
| VMware oficial multi-disco | passa; selecao e revalidacao explicitas, guard intacto, instalacao nova, primeiro boot, login e persistencia apos reboot |
| ambiente Windows/WSL | passa; Ubuntu WSL2, Python, VMware Workstation, QEMU/OVMF e toolchain cruzada validados |

Hyper-V nao foi executado nesta release; a evidencia oficial permanece VMware,
com QEMU/OVMF como regressao reproduzivel de desenvolvimento.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.320+20260730` | `0.8.0-alpha.321+20260821` | corrige entrada do instalador em VMs sem COM1; contratos externos inalterados |

Sem mudanca de ABI externa. `capyos-base` v3, `capyos-package-apply` v1 e o
handoff interno v10 permanecem inalterados. CapyAI `0.2.1`, CapyUI `2.24.2`,
CapyBrowser `0.6.7`, CapyCodecs `0.0.12`, CapyAgent `0.0.10`, CapyLang
`0.1.12` e CapyBenchmark `0.0.11` permanecem pinados.

_Build: `0.8.0-alpha.321+20260821`_
