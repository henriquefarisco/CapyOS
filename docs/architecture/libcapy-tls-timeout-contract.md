# CapyOS — Política de timeout em libcapy-tls

## Objetivo

`libcapy-tls` precisa limitar timeouts antes do backend BearSSL userland real para evitar hangs longos, falhas espúrias por timeout baixo demais e divergência entre callers futuros.

## Contrato em `0.8.0-alpha.70+20260510`

- `CAPY_TLS_TIMEOUT_DEFAULT_MS` define o default público planejado.
- `CAPY_TLS_TIMEOUT_MIN_MS` define o menor timeout explícito aceito.
- `CAPY_TLS_TIMEOUT_MAX_MS` define o maior timeout explícito aceito.
- `timeout_ms=0` representa default seguro.
- `0 < timeout_ms < CAPY_TLS_TIMEOUT_MIN_MS` retorna `CAPY_TLS_EINVAL`.
- `timeout_ms > CAPY_TLS_TIMEOUT_MAX_MS` retorna `CAPY_TLS_EINVAL`.
- Timeouts válidos continuam falhando fechado com `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland.

## Segurança e performance

A janela impede que callers congelem fluxos de UI/update por tempo abusivo e também impede timeouts quase imediatos que seriam indistinguíveis de falhas de rede reais.

## Próximo incremento

Em `0.8.0-alpha.71+20260510`, o timeout validado passou a ser normalizado por `capy_tls_config_resolve()` antes do contexto real existir. Quando `capy_tls_connect_tcp()` materializar contexto real, esse timeout efetivo deve ser persistido no contexto e aplicado ao transporte TCP/TLS userland.
