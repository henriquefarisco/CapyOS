# CapyOS 0.8.0-alpha.92+20260510

## Resumo executivo

Este patch avanca F4 em `libcapy-tls` reservando o estado BearSSL userland como metadata-only. O novo contrato declara schema, engine, flags, fingerprint, plano de backend e trust metadata exigidos, mas mantem engine, contexto e buffers ausentes, com `engine_initialized=0` e `handshake_allowed=0`.

## Principais entregas

- Adiciona `userland/lib/capylibc-tls/capy_tls_bearssl_state.c`.
- Adiciona `struct capy_tls_bearssl_reserved_state` ao contrato interno de `libcapy-tls`.
- Adiciona flags internas para metadata-only, engine ausente, contexto ausente, buffers ausentes e handshake desabilitado.
- Propaga `bearssl_state_ready`, schema, engine, flags, fingerprint e tamanhos reservados no backend stub para o caminho de trust store padrao.
- Integra o novo objeto em `CAPYLIBC_TLS_OBJS` e na suite host declarativa.

## Seguranca e compatibilidade

- O contrato publico de `libcapy-tls` permanece inalterado e fail-closed.
- O backend continua retornando `CAPY_TLS_EUNSUPPORTED` para entradas estruturalmente validas.
- Nenhuma engine BearSSL userland e inicializada.
- Nenhum contexto BearSSL, buffer de I/O, certificado ou chave e alocado/copiado/parseado.
- O estado reservado exige o backend plan `0x4F809D54`, manifesto v3 e bundle metadata-only antes de qualquer futuro avanço.

## Validacao estatica

- Revisao estatica confirmou que o novo modulo nao chama BearSSL, VMware, OpenSSL, `make`, `git` ou subprocessos.
- Revisao estatica confirmou que `engine_initialized`, `handshake_allowed` e `handshake_started` permanecem `0`.
- Revisao estatica confirmou que scripts temporarios `tmp_alpha92*.py` foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
