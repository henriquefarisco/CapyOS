# CapyOS 0.8.0-alpha.258+20260523

**Data:** 2026-05-23
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.258+20260523`
**Plataforma oficial:** VMware + UEFI + E1000
**Escopo:** correcao da esteira de deploy modular e pin CapyUI 2.13.1

## Resumo

Esta release corrige a falha do deploy alpha.257 na esteira. O runner do
CapyOS clonava `CapyUI` a partir de `main`, mas esse branch ainda nao tinha os
sources display-list publicados em `2.13.1`; por isso o build remoto parava em
`No rule to make target 'build/x86_64/capyui-desktop/taskbar_display_list.o'`.

O sister `CapyUI` foi alinhado em `main` para `2.13.1`, e o CapyOS agora
declara a pinagem correta nos metadados e docs autoritativos.

## Mudancas

- `VERSION.yaml` e `include/core/version.h` avancam para
  `0.8.0-alpha.258+20260523`.
- Matriz cross-repo atualizada para `CapyUI` `2.13.1`.
- Release notes e status docs passam a registrar a correcao da esteira.
- A nova tag evita reaproveitar a tag `alpha.257`, que ja teve runs quebrados.

## Validacao

Validacoes esperadas para esta release:

- `make version-audit`
- `make layout-audit`
- `make test`
- `make boot-perf-baseline-selftest`
- `make clean all64 iso-uefi verify-release-checksums TOOLCHAIN64=host`

O deploy deve publicar `main`, `develop` e a tag
`v0.8.0-alpha.258+20260523`, depois acompanhar CI, CodeQL e Release Artifacts
ate conclusao verde.
