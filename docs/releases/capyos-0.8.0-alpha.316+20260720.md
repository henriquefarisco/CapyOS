# CapyOS 0.8.0-alpha.316+20260720

**Data:** 2026-07-20
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.316+20260720`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** hardening do instalador + release de coordenacao

## Resumo executivo

Fecha a evidencia fail-closed do instalador e o gate VMware UEFI/E1000; integra CapyAI 0.2.1 e o indice de modulos reproduzivel.

## Mudancas

- O instalador UEFI publica prompts e identidade do alvo também no canal serial,
  aceita input serial controlado pelo harness e mantém a seleção destrutiva
  explícita por `PathId` + token literal `ERASE`.
- O smoke de instalação passa a exigir tabela e contagem inequívocas de alvos,
  seleciona o disco por capacidade única, comprova que o alvo mudou e que um
  disco guard maior permaneceu byte a byte idêntico por SHA-256.
- Imagens destrutivas são criadas exclusivamente dentro de `build/ci`; falhas
  preservam evidências, arquivos preexistentes nunca são truncados e a recovery
  key é removida dos logs persistidos.
- O fluxo lida com desktop, setup, reboot intermediário de módulos, novo login e
  releitura de marker persistente sem aceitar partial/background install como
  conclusão.
- Novo contrato VMware Workstation cria VM scratch UEFI + E1000, usa dois VMDKs,
  redige segredos e produz manifesto de evidência validado e auditável.
- O CapyAI avança de `0.2.0` para `0.2.1`, mantendo `capy-ai-core` artifact v0 e
  acrescentando split sem leakage e gate massivo com zero underclassification.
- `VERSION.yaml`, first boot e smoke networked pinam o índice agregado da própria
  tag `v0.8.0-alpha.316+20260720`, eliminando drift silencioso de compatibilidade.

## Validacao

- `make test`
- `make layout-audit`
- `make version-audit`
- `make all64 TOOLCHAIN64=host`
- `make iso-uefi TOOLCHAIN64=host`
- `make verify-release-checksums TOOLCHAIN64=host`
- `make smoke-x64-qemu-installer-wizard`
- `make smoke-x64-vmware-installer-wizard TOOLCHAIN64=host` — aprovado em
  VMware Workstation UEFI/E1000 com dois discos elegíveis, alvo alterado, guard
  byte-idêntico, fresh install, first boot, login e marker relido após reboot.
- Evidência pública sanitizada:
  [`evidence/capyos-0.8.0-alpha.316+20260720-installer-wizard.manifest`](evidence/capyos-0.8.0-alpha.316+20260720-installer-wizard.manifest).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.315+20260715` | `0.8.0-alpha.316+20260720` | `capyos-base` v3 / package-apply v1 inalterados |
| **CapyAI** | `0.2.0` | `0.2.1` | `capy-ai-core` artifact v0 inalterado |

Sem mudança de ABI. CapyUI `2.24.1`, CapyBrowser `0.6.7`, CapyAgent `0.0.10`,
CapyCodecs `0.0.12`, CapyLang `0.1.12` e CapyBenchmark `0.0.11` permanecem
inalterados.

_Build: `0.8.0-alpha.316+20260720`_
