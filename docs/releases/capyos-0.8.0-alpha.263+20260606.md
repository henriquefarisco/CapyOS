# CapyOS 0.8.0-alpha.263+20260606

**Data:** 2026-06-06
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.263+20260606`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 5 TLS userland real, prerequisites host-side e compliance CapyBrowser 0.5.0

## Resumo executivo

Esta release avanca a Etapa 5 com a base userland de TLS real: entropia
userland (`SYS_GETRANDOM`), wall-clock (`SYS_CLOCK_REALTIME`), trust anchors
BearSSL, engine ClientHello, handshake-drive host-side, validacao X.509 e o
backend BearSSL plugado atras de `CAPYOS_TLS_USERLAND_HANDSHAKE` (default OFF).
O caminho default continua fail-closed, enquanto o build gated permite validar
o handshake real antes da promocao para default.

## Mudancas entregues

- `SYS_GETRANDOM` exposto na capylibc com limite de 256 bytes por chamada,
  backed pela CSPRNG do kernel.
- `SYS_CLOCK_REALTIME` + helper `capy_tls_unix_to_x509_time`, fechando o gap
  de tempo real para validacao de certificados.
- Trust anchors BearSSL reais validados host-side (146 anchors).
- ClientHello TLS real com SNI testado host-side.
- Handshake-drive BearSSL testado com transporte stub e falhas fail-closed.
- Validacao X.509 testada para cadeia valida, hostname errado, expirado e
  issuer nao confiavel.
- Backend de producao com handshake BearSSL real sob
  `CAPYOS_TLS_USERLAND_HANDSHAKE`; default build permanece sem regressao.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.262+20260602` | `alpha.263+20260606` | Etapa 5 TLS userland prerequisites + backend gated |
| **CapyBrowser** | `0.3.0` | `0.5.0` | CSS cascade, layout/display-list, download, sessao privada e forms host-testados |

Demais repositorios apartados permanecem nos pins da matriz anterior:
CapyUI `2.22.0`, CapyCodecs `0.0.7`, CapyLang `0.1.8`, CapyAgent `0.0.7`
e CapyBenchmark `0.0.7`.

## Evidencias / validacao

Validacao local esperada antes da tag:

- CapyBrowser: `make validate`, `make package STAGE=core`,
  `make package STAGE=text`.
- CapyOS: `make test`, `make layout-audit`, `make version-audit`,
  `make all64 PROFILE=full`, `make iso-uefi`, `make manifest64`,
  `make release-check`.

## Proximos passos

1. Validar o build gated com `CAPYOS_TLS_USERLAND_HANDSHAKE=1` em VMware.
2. Promover o handshake TLS userland para default quando o smoke externo
   estiver verde.
