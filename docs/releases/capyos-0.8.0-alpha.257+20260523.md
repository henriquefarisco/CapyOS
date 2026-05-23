# CapyOS 0.8.0-alpha.257+20260523

**Data:** 2026-05-23
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.257+20260523`
**Plataforma oficial:** VMware + UEFI + E1000
**Escopo:** deploy modular corrente com integracao CapyUI/CapyOS e endurecimento do caminho de instalacao remota

## Resumo

Esta release consolida o trabalho recente no fluxo de deploy modular.
O caminho de instalacao remota segue resiliente, os adaptadores de
display-list continuam alinhados com o sister `CapyUI` e os instaladores
por plataforma foram mantidos no contrato oficial do release.

## Mudancas

- `VERSION.yaml` avanca para `0.8.0-alpha.257+20260523`.
- `README.md` e os docs de status passam a apontar para a versao corrente.
- `docs/releases/README.md` publica o novo release note e move a entrada
  anterior para historico.
- A matriz de compatibilidade continua apontando o `CapyUI` validado para o
  fluxo de instalacao modular.
- Os instaladores por plataforma permanecem no pacote do release corrente.

## Validacao

Esta release reutiliza os gates locais do fluxo CapyOS normal:

- `make version-audit`
- `make layout-audit`
- `make test`
- `make clean all64 iso-uefi verify-release-checksums TOOLCHAIN64=host`
- `make release-check`

As smokes ISO e a publicacao no GitHub Release devem ser executadas no
passo de deploy.
