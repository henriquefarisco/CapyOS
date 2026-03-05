# Plano historico: bootloader automatico via ISO

Status: descontinuado como trilha principal em 2026-03-05.

Este documento descrevia um plano antigo para criar uma particao de boot focada
em `BIOS/MBR` e recuperacao automatica via ISO. Esse fluxo nao faz mais parte
do caminho suportado do projeto.

## Situacao atual

- trilha oficial: `UEFI/GPT/x86_64`
- provisionamento oficial: `tools/scripts/provision_gpt.py`
- auditoria oficial: `make inspect-disk IMG=<imagem>`
- smoke oficial: `make smoke-x64-cli`

## Motivo da descontinuacao

- a migracao para 64 bits consolidou o boot em `BOOTX64.EFI`
- o legado `BIOS/x86_32` foi removido do pipeline de build e release
- manter um segundo bootloader historico aumentaria a superficie de regressao

## Uso deste arquivo

Considere este documento apenas como referencia historica. Para o fluxo atual,
use:

- `README.md`
- `docs/architecture.md`
- `docs/HYPERV_SETUP.md`
- `docs/cli_test_plan.md`
