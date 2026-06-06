# Compatibility audit 2026-06-06

**CapyOS core:** `0.8.0-alpha.263+20260606`
**Escopo:** release de Etapa 5 TLS userland prerequisites + CapyBrowser `0.5.0`.

## Versoes coordenadas

| Repositorio | Versao anterior | Versao atual | Contrato |
|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.262+20260602` | `0.8.0-alpha.263+20260606` | `capyos-base` v3 + `capyos-package-apply` v1 |
| `CapyBrowser` | `0.3.0` | `0.5.0` | `capy-browser-core` v1 planejada; superficies host-testaveis expandidas |

Sem bump nos demais repositorios: CapyUI `2.22.0`, CapyCodecs `0.0.7`,
CapyLang `0.1.8`, CapyAgent `0.0.7` e CapyBenchmark `0.0.7`.

## CapyBrowser 0.5.0

`CapyBrowser` passa a consolidar CSS parse/cascade, layout estatico,
display-list, download, sessao privada e forms basicos no runner host. O
runtime CapyOS-side continua gated por Etapas 6-7; a matriz registra a versao
maxima testada, mas nao promove o modulo para producao.

## CapyOS alpha.263

O core avanca a Etapa 5 com entropia userland, tempo real, trust anchors,
engine TLS BearSSL, handshake-drive e validacao X.509 host-side. O backend
BearSSL de producao esta plugado atras de `CAPYOS_TLS_USERLAND_HANDSHAKE`
e permanece default OFF ate smoke externo.

## Validacao

- CapyBrowser: `make validate`, `make package STAGE=core`,
  `make package STAGE=text`.
- CapyOS: `make test`, `make layout-audit`, `make version-audit`,
  `make all64 PROFILE=full`, `make iso-uefi`, `make manifest64`,
  `make release-check`.
