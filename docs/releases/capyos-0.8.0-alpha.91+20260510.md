# CapyOS 0.8.0-alpha.91+20260510

## Resumo executivo

Este patch avanca F4 em `libcapy-tls` adicionando um plano interno do backend BearSSL userland reservado. O contrato declara engine, schema, flags fail-closed, manifestos de trust metadata exigidos e fingerprint, mas mantem `handshake_allowed=0` e nao instancia BearSSL real.

## Principais entregas

- Adiciona `userland/lib/capylibc-tls/capy_tls_backend_plan.c`.
- Adiciona `struct capy_tls_backend_plan` ao contrato interno de `libcapy-tls`.
- Adiciona flags internas para fail-closed, handshake desabilitado, trust metadata gated e estado BearSSL ausente.
- Propaga `backend_plan_ready`, `handshake_allowed`, schema, engine, flags e fingerprint no backend stub para o caminho de trust store padrao.
- Integra o novo objeto em `CAPYLIBC_TLS_OBJS` e na suite host declarativa.

## Seguranca e compatibilidade

- O contrato publico de `libcapy-tls` permanece inalterado e fail-closed.
- O backend continua retornando `CAPY_TLS_EUNSUPPORTED` para entradas estruturalmente validas.
- Nenhum objeto BearSSL userland e construido, inicializado ou executado.
- O plano exige o manifesto v3 e o bundle metadata-only do trust store antes de qualquer futuro avanço de handshake.

## Validacao estatica

- Revisao estatica confirmou que o novo modulo nao chama BearSSL, VMware, OpenSSL, `make`, `git` ou subprocessos.
- Revisao estatica confirmou que `handshake_allowed` permanece `0` e `handshake_started` permanece `0`.
- Revisao estatica confirmou que scripts temporarios `tmp_alpha91*.py` foram removidos.
- Nao foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
