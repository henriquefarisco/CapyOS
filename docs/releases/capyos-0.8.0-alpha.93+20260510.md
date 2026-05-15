# CapyOS 0.8.0-alpha.93+20260510

## Resumo executivo

Este patch avanca F4 em `libcapy-tls` adicionando um adaptador BearSSL userland metadata-only. O contrato e gated pelo backend plan e pelo estado BearSSL reservado, mas mantem `adapter_initialized=0`, `handshake_allowed=0` e o backend publico ainda retorna `CAPY_TLS_EUNSUPPORTED`.

## Principais entregas

- Adiciona `userland/lib/capylibc-tls/capy_tls_bearssl_adapter.c`.
- Adiciona `struct capy_tls_bearssl_adapter_contract` ao contrato interno de `libcapy-tls`.
- Adiciona flags internas para metadata-only, backend-plan-gated, reserved-state-gated, engine-init-disabled e handshake-disabled.
- Propaga `bearssl_adapter_ready`, schema, engine, flags, fingerprint e `adapter_initialized=0` no backend stub para o caminho de trust store padrao.
- Integra o novo objeto em `CAPYLIBC_TLS_OBJS` e na suite host declarativa.

## Seguranca e compatibilidade

- O contrato publico de `libcapy-tls` permanece inalterado e fail-closed.
- O backend continua retornando `CAPY_TLS_EUNSUPPORTED` para entradas estruturalmente validas.
- Nenhuma engine BearSSL userland e inicializada.
- Nenhum contexto BearSSL, buffer de I/O, certificado ou chave e alocado, copiado ou parseado.
- O adaptador exige backend plan `0x4F809D54`, estado reservado `0x7D1732D0`, manifesto v3 e bundle metadata-only antes de qualquer futuro avanço.

## Validacao estatica

- Revisao estatica confirmou que o novo modulo nao chama BearSSL, VMware, OpenSSL, `make`, `git` ou subprocessos.
- Revisao estatica confirmou que `adapter_initialized`, `engine_initialized`, `handshake_allowed` e `handshake_started` permanecem `0`.
- Revisao estatica confirmou que scripts temporarios `tmp_alpha93*.py` foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
