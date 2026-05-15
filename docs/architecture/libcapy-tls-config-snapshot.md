# CapyOS — Snapshot de configuração efetiva em libcapy-tls

## Objetivo

`libcapy-tls` precisa transformar a configuração pública em uma configuração interna normalizada antes de criar contexto TLS real. `0.8.0-alpha.71+20260510` introduz esse snapshot sem habilitar handshake.

## Contrato em `0.8.0-alpha.71+20260510`

- `capy_tls_config_resolve()` recebe `struct capy_tls_config` opcional.
- `config == NULL` gera `verify_peer=1`, CA vazia e `CAPY_TLS_TIMEOUT_DEFAULT_MS`.
- `timeout_ms=0` também normaliza para `CAPY_TLS_TIMEOUT_DEFAULT_MS`.
- CA customizada válida é preservada no snapshot.
- Configurações inseguras ou inconsistentes retornam falha e são mapeadas para `CAPY_TLS_EINVAL` por `capy_tls_connect_tcp()`.
- Configuração efetiva válida ainda retorna `CAPY_TLS_EUNSUPPORTED` porque o backend BearSSL userland não existe.

## Segurança e manutenção

O snapshot reduz duplicação e evita que o futuro contexto TLS interprete campos públicos crus em múltiplos pontos. Isso facilita aplicar SNI, timeout, trust anchors e segurança de peer de forma consistente.

## Próximo incremento

Em `0.8.0-alpha.72+20260510`, `capy_tls_context_prepare()` passou a armazenar socket, hostname validado e `capy_tls_effective_config` em `struct capy_tls_context`, ainda sem iniciar handshake ou retornar contexto ao caller.
