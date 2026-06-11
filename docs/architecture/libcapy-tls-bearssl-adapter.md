# CapyOS — Adaptador BearSSL metadata-only em libcapy-tls

> **Status (alpha.264, Etapa 5 concluída):** este documento descreve o
> adaptador **metadata-only** (alpha.93), que segue valendo como o
> caminho de **opt-out** / fail-closed (`CAPYOS_TLS_USERLAND_HANDSHAKE=0`). A Etapa 5 plugou **in-tree**
> o engine BearSSL userland real (`br_ssl_client` + `br_sslio`) em
> `capy_tls_backend_connect`, atrás de `CAPYOS_TLS_USERLAND_HANDSHAKE`
> (**promovida a default em alpha.264**; opt-out com `=0`). Estado vivo por slice em
> [`etapa-5-tls-userland-readiness.md`](etapa-5-tls-userland-readiness.md).

## Objetivo

`libcapy-tls` precisa declarar a fronteira do futuro adaptador BearSSL userland antes de inicializar qualquer engine real. `0.8.0-alpha.93+20260510` adiciona um contrato metadata-only para o adaptador, gated pelo backend plan e pelo estado BearSSL reservado, mantendo fail-closed e handshake desabilitado.

## Contrato em `0.8.0-alpha.93+20260510`

- `CAPY_TLS_BEARSSL_ADAPTER_SCHEMA_VERSION` declara schema `1`.
- `CAPY_TLS_BEARSSL_ADAPTER_ENGINE_BEARSSL_USERLAND` declara a engine reservada `0x1`.
- `CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FLAGS` declara metadata-only, backend-plan-gated, reserved-state-gated, engine-init-disabled e handshake-disabled.
- `CAPY_TLS_DEFAULT_BEARSSL_ADAPTER_FINGERPRINT` declara `0xE73E3E65`.
- `capy_tls_default_bearssl_adapter_contract()` retorna o contrato interno do adaptador.
- `capy_tls_default_bearssl_adapter_consistent()` valida schema, engine, flags, backend plan, estado reservado, manifesto v3, bundle metadata-only, fingerprint, `metadata_only=1`, `adapter_initialized=0` e `handshake_allowed=0`.
- O backend stub propaga `bearssl_adapter_ready`, `bearssl_adapter_initialized`, schema, engine, flags e fingerprint quando usa o trust store padrao.
- O caminho de CA custom permanece sem adaptador BearSSL porque ainda nao ha parse/normalizacao userland do certificado custom.
- `capy_tls_backend_connect()` continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente pronto.

## Segurança e compatibilidade

O adaptador metadata-only cria uma etapa auditavel antes da inicializacao real de BearSSL. A ABI publica permanece inalterada, certificados continuam ausentes, buffers reais continuam ausentes e o handshake permanece desabilitado.

## Próximo incremento

Avancar para a validacao preflight de handshake userland ainda fail-closed, sem executar handshake real ate que trust anchors, buffers, engine e verificacao criptografica estejam completos.
