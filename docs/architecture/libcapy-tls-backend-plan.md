# CapyOS — Plano interno do backend BearSSL userland em libcapy-tls

## Objetivo

`libcapy-tls` precisa preparar a fronteira do futuro backend BearSSL userland sem permitir handshake prematuro. `0.8.0-alpha.91+20260510` adiciona um plano interno fail-closed que declara engine, schema, flags, trust metadata exigido e fingerprint antes de qualquer estado BearSSL real existir.

## Contrato em `0.8.0-alpha.91+20260510`

- `CAPY_TLS_BACKEND_PLAN_SCHEMA_VERSION` declara schema `1`.
- `CAPY_TLS_BACKEND_PLAN_ENGINE_BEARSSL_USERLAND` declara a engine reservada `0x1`.
- `CAPY_TLS_DEFAULT_BACKEND_PLAN_FLAGS` declara fail-closed-only, handshake-disabled, trust-metadata-gated e bearssl-state-absent.
- `CAPY_TLS_DEFAULT_BACKEND_PLAN_FINGERPRINT` declara `0x4F809D54`.
- `capy_tls_default_backend_plan()` retorna o plano interno.
- `capy_tls_default_backend_plan_consistent()` valida schema, engine, flags, manifesto v3, bundle metadata-only, fingerprint e `handshake_allowed=0`.
- O backend stub propaga `backend_plan_ready`, `handshake_allowed`, schema, engine, flags e fingerprint quando usa o trust store padrao.
- O caminho de CA custom permanece sem plano BearSSL porque ainda nao ha parse/normalizacao userland do certificado custom.
- `capy_tls_backend_connect()` continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente pronto.

## Segurança e compatibilidade

O plano cria uma fronteira auditavel para o futuro BearSSL userland sem mudar ABI publica, sem inicializar BearSSL e sem remover o fail-closed. A etapa reduz o risco de habilitar handshake antes de o trust metadata, o bundle e o estado criptografico real estarem consistentes.

## Próximo incremento

Em `0.8.0-alpha.92+20260510`, o backend plan passou a gatear o estado BearSSL reservado metadata-only. Em `0.8.0-alpha.93+20260510`, o adaptador BearSSL userland tambem passou a ser gated e metadata-only.
