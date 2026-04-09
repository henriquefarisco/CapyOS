# CapyOS 0.8.0-alpha.0

Data: 2026-03-05
Canal: alpha
Commit base validado: 43e97c5

## Destaques

- nome oficial consolidado como `CapyOS` na documentacao e no manifesto de
  versao
- trilha unica formalizada como `UEFI/GPT/x86_64`
- boot validado pelo disco provisionado, com login e persistencia apos reboot
- `make inspect-disk` incorporado ao fluxo oficial de auditoria
- documentacao reorganizada por dominio, com indice central e pasta de arquivo

## Mudancas tecnicas relevantes

- runtime x64 passa a priorizar o handle logico da particao `DATA`
- fallback RAW fica restrito a erro real de probe
- caminho EFI BlockIO continua como base da persistencia validada
- scripts de provisionamento e smoke foram segmentados para reduzir acoplamento

## Validacao recomendada

```bash
make test
make all64
make smoke-x64-cli
make inspect-disk IMG=build/disk-gpt.img
```

## Observacoes

- o boot ainda usa identificadores tecnicos em caixa alta como `CAPYOS64.BIN`
  e `CAPYOS.LOG` dentro do fluxo UEFI/FAT
- o instalador via ISO continua sem smoke ponta a ponta dedicado
- o boot x64 ainda depende de `EFI ConIn` em parte dos cenarios
