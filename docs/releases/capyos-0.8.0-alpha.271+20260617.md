# CapyOS 0.8.0-alpha.271+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.271+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Instalacao OFFLINE de modulos (bundle local) ponta-a-ponta (Etapa 6 in-tree)

## Resumo executivo

Corrige a instalacao de modulos pelo caminho **offline** (bundle local
embutido na ISO), que ate aqui falhava de ponta a ponta. Antes, o full
mode buscava o indice de modulos numa URL com hostname (github/repo) que
o resolver **cache-only** nao resolve, ficando em retry sem instalar
nada; e o caminho offline (`make iso-uefi-local-modules`), embora
existisse, nunca tinha sido validado e tinha dois bugs. **A Etapa 6 NAO
esta fechada** e o canal **remoto assinado** segue dependente dos
follow-ups (resolver DNS ativo + repo publicado + verificador Ed25519 da
CapyAgent).

## Root cause + fixes

- **Fix 1 -- runtime (`src/services/capypkg/capypkg_install.c`):** o
  install alocava `CAPYPKG_PAYLOAD_MAX` (8 MiB) por pacote via `kalloc()`.
  O heap do kernel e de 16 MiB, entao 8 MiB contiguos -- metade do heap --
  falham assim que o heap do first-boot esta parcialmente usado/fragmentado,
  abortando **todo** install com `CAPYPKG_ERR_QUOTA`. (O index, com buffer
  de 16 KiB na pilha, passava; os testes host nunca pegaram porque usam um
  buffer estatico de 8 MiB, nao `kalloc`.) Agora aloca o `size_bytes`
  declarado do pacote (real, ~1.2 MiB), limitado ao teto de 8 MiB. O hint
  e so um hint: o fetcher ainda respeita o cap e `capypkg_verify_payload`
  rechecа o SHA-256, entao um hint errado so encolhe o buffer e gera um
  verify-fail limpo, nunca injeta bytes nao verificados.
- **Fix 2 -- build (`Makefile`):** `iso-uefi-local-modules` passa a
  regenerar o bundle embutido (`local-modules-bundle`) **antes** do
  `all64`. Um build limpo quebrava porque `capypkg_local_bundle.c` inclui
  o `capypkg_local_bundle_data.h` gerado, que o `make clean` removia e nada
  regenerava antes do compile; e como `make` recompila por timestamp e nao
  por flag, objetos stale de um build default deixavam o bundle inativo +
  a URL do github, fazendo o fetch do indice ir para a rede e falhar
  (`dns rc=-6`).

## Gate de dev (novo) + docs

- **`make smoke-x64-iso-local-modules`:** clean + ISO de laboratorio
  (`CAPYOS_LOCAL_MODULES=1`) + smoke de install FULL no first-boot que
  **falha** se o modulo de desktop nao instalar. Requer os 6 repos irmaos
  empacotados; QEMU = feedback de dev, VMware = aceite oficial.
- Runbook `docs/operations/manual-module-deploy-runbook.md` secao 5.6:
  documenta o fluxo offline (sem rede/DNS/signer).

## Validacao (host, QEMU+OVMF)

- `make smoke-x64-iso-local-modules` (clean): o first-boot FULL instala os
  **7 modulos embutidos** (`org.capyos.ui.widget-core`,
  `org.capyos.ui.desktop-session`, e demais) com `Completed: 7  Failed: 0`,
  a sessao desktop ativa (boot2 "Welcome"), e a persistencia passa --
  tudo sem DNS, sem rede e sem signer.
- `make version-audit` -- verde.

## Escopo pendente

- Caminho **remoto assinado** (producao): resolver DNS ativo (hoje
  cache-only) + repo publicado alcancavel + verificador Ed25519 da
  CapyAgent (P0). Sem eles, full mode com a URL remota continua em retry.
- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.270+20260617` | `alpha.271+20260617` | Install OFFLINE de modulos (bundle local) ponta-a-ponta: payload alloc por size_bytes + ordem de build do bundle + gate |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos irmaos
permanecem nas versoes de `alpha.266` (sem bump).

_Build: `0.8.0-alpha.271+20260617`_
