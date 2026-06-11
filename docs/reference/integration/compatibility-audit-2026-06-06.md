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

## Addendum — CapyOS alpha.264 (2026-06-07)

A Etapa 5 (TLS userland real) **fechou** em `alpha.264` apos o gate externo
(build flag-on + `make smoke-x64-vmware-tls-handshake` + `release-check`): a
flag `CAPYOS_TLS_USERLAND_HANDSHAKE` foi **promovida a default** (opt-out com
`=0`; afeta apenas o build ring-3 `USERLAND_CFLAGS`, nao o `HOST_CFLAGS`).
Hardening de seguranca no mesmo intervalo: overflows de integer no ELF loader
userland + boot, tetos de custo KDF no volume header, robustez adversarial
DNS/DHCP/ICMP/ARP e bound do `names_equal` do CAPYFS. A **Etapa 6** (apps
basicos + CapyBrowse Text) abriu; primeiros slices CapyOS-side entregues:
`capy_net_strerror` (6.2) e `capy_net_diagnose_stage`/`capy_net_stage_name`
(6.3, base do diagnostico de rede DNS/TCP/TLS/HTTP), ambos host-testados.

**Estado cross-repo inalterado por este bump:** nenhum contrato/ABI/manifesto/
quota/politica de assinatura sister mudou. CapyUI `2.22.0`, CapyCodecs `0.0.7`,
CapyLang `0.1.8`, CapyAgent `0.0.7`, CapyBrowser `0.5.0` e CapyBenchmark `0.0.7`
permanecem nos pins do lote anterior. Este addendum mantem esta auditoria como
referencia atual da matriz para `alpha.264`.
