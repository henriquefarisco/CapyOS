# CapyOS — Política de peer verification em libcapy-tls

## Objetivo

`libcapy-tls` será a fronteira userland para HTTPS autenticado. Antes do backend BearSSL userland existir, a API já deve recusar configurações que tentem desabilitar validação de peer ou hostname.

## Contrato em `0.8.0-alpha.69+20260510`

- `config == NULL` representa configuração segura padrão.
- Quando `struct capy_tls_config` é fornecida, `verify_peer` deve ser exatamente `1`.
- `verify_peer=0`, valores negativos ou qualquer valor diferente de `1` retornam `CAPY_TLS_EINVAL`.
- Configuração de CA inconsistente continua retornando `CAPY_TLS_EINVAL`.
- Configuração estruturalmente segura continua falhando fechado com `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland real.

## Segurança

A política impede que o primeiro backend TLS userland aceite modo inseguro por conveniência. HTTPS deve ser autenticado por padrão, e exceções futuras precisarão de uma API explícita e auditável, não de um campo permissivo silencioso.

## Próximo incremento

Quando o backend BearSSL userland aterrissar, `verify_peer == 1` deve alimentar SNI, cadeia de certificados, SAN/CN e publicação de `peer_verified`/`hostname_validated` em `capy_tls_security_info`.
