# CapyOS 0.8.0-alpha.317+20260720

**Data:** 2026-07-20
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.317+20260720`
**Plataforma oficial:** VMware + UEFI + E1000
**Tipo:** fundacao A/B fail-closed + handoff interno v9

> **Errata (2026-07-27):** esta nota descreve a fatia de fundacao. O trabalho
> in-tree avancou antes da publicacao: o handoff chegou a v10 com token de
> tentativa, o flush duravel nativo existe e o loader consome o slot-control.
> Consulte [`capyos-0.8.0-alpha.318+20260727.md`](capyos-0.8.0-alpha.318+20260727.md)
> para o estado vigente do lifecycle A/B.

## Resumo executivo

Entrega a fundacao transacional de boot slots A/B sem habilitar apply. O boot
handoff avanca para v9 append-only, GPT e identidade multi-disco ficam
fail-closed e o updater continua retornando `-60` ate existir flush nativo,
consumo UEFI do controle e rollback comprovado apos reboot.

## Mudancas

- Boot-control v0 redundante com duas copias, CRC, geracao monotônica,
  confirmed/pending/tries e resultado de commit indeterminado.
- Layout A/B fixo dentro do BOOT existente, sem alterar GPT ou DATA.
- Store de slot inativo com payload-first, flush, SHA-256 integral de readback,
  padding validado e header-last; aceita apenas `EMPTY`/`FAILED` e recusa restage
  de slot `VALID` antes de qualquer I/O.
- Provider raw valida headers/arrays primary+backup GPT, CRCs, igualdade byte a
  byte, ranges, GUIDs, atributos e binding exato de ESP/BOOT/DATA antes de mapear
  LBAs relativos; um vetor com CRC32 colidente cobre arrays divergentes.
- Handoff v9 preserva o prefixo v8 de 440 bytes e acrescenta identidade GPT; o
  tamanho v9 de 560 bytes, offsets e flags canônicas fechadas são validados.
- UEFI exige que raw/ESP/DATA compartilhem o mesmo prefixo físico de Device Path,
  impedindo troca por clone de GUID/LBA; I/O destrutivo respeita `Media->IoAlign`
  e toda falha de `FlushBlocks` aborta antes do marcador de sucesso/reboot.
- O manifest BOOT é validado sem type-punning; UEFI e runtime nativo exigem
  seleção única e revalidam o fingerprint antes do mount.
- Discos antigos com GUIDs duplicados entram por modo legado validado somente
  para mount; nunca recebem capability de update.
- Instalador passa a gerar GUIDs nao-zero e distintos, e recusa discos acima do
  ABI atual de setores 32-bit.
- Runtime retem raw+DATA estrito depois do mount, mas zera integralmente qualquer
  provider de saída porque flush duravel nativo ainda nao existe; autorização de
  staging vinculada à geração durável do manager também permanece pendente.

## Postura fail-closed

`update-prepare`, `update-stage`, `update-arm on`, `update-apply`,
`update-confirm-health` e `update-rollback-check` continuam indisponiveis com
`UPDATE_AGENT_ERR_UNSUPPORTED` (`-60`). Esta release nao fecha o criterio de
update da Etapa 8.

## Validacao

- Testes focados de slot store, GPT/provider, policy v9 e installer policy:
  aprovados.
- `make release-check TOOLCHAIN64=elf` — exit `0`; inclui `make test`,
  `layout-audit`, `version-audit`, selftests, kernel, loader UEFI, ISO e checksums.
- `make verify-release-checksums TOOLCHAIN64=elf` — exit `0` nos cinco artefatos;
  ISO final `98ef62154093fe4200d6930d3e1664cca72e528e150660e8d853de6ac177e053`.
- `make smoke-x64-qemu-installer-wizard TOOLCHAIN64=elf` — exit `0` com target
  novo, guard intacto, first boot, login e marker relido.
- `make smoke-x64-vmware-installer-wizard TOOLCHAIN64=elf` — exit `0` na
  plataforma oficial com dois discos, target alterado, guard byte-identico,
  login e persistencia apos reboot.
- Evidencia sanitizada da mesma ISO ELF:
  [`evidence/capyos-0.8.0-alpha.317+20260720-installer-wizard.manifest`](evidence/capyos-0.8.0-alpha.317+20260720-installer-wizard.manifest).
- `make modules-index` — exit `0`, nove entradas incluindo CapyAI.
- `make smoke-x64-iso-modules-net` — bloqueado por pré-condição externa: a URL da
  tag `alpha.317` retorna 404 porque os assets ainda não foram publicados; rerun
  obrigatório pós-publicação.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.316+20260720` | `0.8.0-alpha.317+20260720` | handoff interno v8 -> v9; `capyos-base` v3 inalterado |

CapyUI `2.24.1`, CapyAI `0.2.1`, CapyBrowser `0.6.7`, CapyAgent `0.0.10`,
CapyCodecs `0.0.12`, CapyLang `0.1.12` e CapyBenchmark `0.0.11` permanecem
inalterados.

_Build: `0.8.0-alpha.317+20260720`_
