# CapyOS — Estado BearSSL reservado em libcapy-tls

## Objetivo

`libcapy-tls` precisa separar a reserva de estado do futuro BearSSL userland da inicializacao real da engine. `0.8.0-alpha.92+20260510` adiciona um contrato metadata-only para declarar que o estado BearSSL ainda esta ausente, mantendo fail-closed ate haver alocacao, inicializacao e validacao criptografica completas.

## Contrato em `0.8.0-alpha.92+20260510`

- `CAPY_TLS_BEARSSL_STATE_SCHEMA_VERSION` declara schema `1`.
- `CAPY_TLS_BEARSSL_STATE_ENGINE_BEARSSL_USERLAND` declara a engine reservada `0x1`.
- `CAPY_TLS_DEFAULT_BEARSSL_STATE_FLAGS` declara metadata-only, engine-absent, context-bytes-absent, io-bytes-absent e handshake-disabled.
- `CAPY_TLS_DEFAULT_BEARSSL_CONTEXT_BYTES` e `CAPY_TLS_DEFAULT_BEARSSL_IO_BUFFER_BYTES` permanecem `0`.
- `CAPY_TLS_DEFAULT_BEARSSL_STATE_FINGERPRINT` declara `0x7D1732D0`.
- `capy_tls_default_bearssl_reserved_state()` retorna o estado reservado interno.
- `capy_tls_default_bearssl_reserved_state_consistent()` valida schema, engine, flags, backend plan, manifesto v3, bundle metadata-only, bytes reservados zero, fingerprint, `metadata_only=1`, `engine_initialized=0` e `handshake_allowed=0`.
- O backend stub propaga `bearssl_state_ready`, `bearssl_engine_initialized`, schema, engine, flags, fingerprint e bytes reservados quando usa o trust store padrao.
- O caminho de CA custom permanece sem estado BearSSL reservado porque ainda nao ha parse/normalizacao userland do certificado custom.
- `capy_tls_backend_connect()` continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente pronto.

## Segurança e compatibilidade

O estado reservado cria uma etapa auditavel antes da alocacao real de contexto BearSSL. A ABI publica permanece inalterada, certificados continuam ausentes, buffers reais continuam ausentes e o handshake permanece desabilitado.

## Próximo incremento

Em `0.8.0-alpha.93+20260510`, o adaptador BearSSL userland foi declarado como metadata-only e continua fail-closed. O proximo incremento deve preparar preflight de handshake sem executar handshake real.
