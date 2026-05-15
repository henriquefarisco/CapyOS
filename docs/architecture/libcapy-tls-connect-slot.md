# CapyOS — Conexão TLS usando slot gerenciado

## Objetivo

`libcapy-tls` precisa exercitar o ciclo de vida real do contexto antes de integrar BearSSL userland. `0.8.0-alpha.75+20260510` conecta `capy_tls_connect_tcp()` ao slot interno gerenciado, ainda sem handshake e sem retornar contexto válido.

## Contrato em `0.8.0-alpha.75+20260510`

- `capy_tls_connect_tcp()` valida argumentos, hostname e configuração efetiva.
- Após validação estrutural, o connect adquire um slot via `capy_tls_context_acquire()`.
- Slot ocupado retorna `CAPY_TLS_ESTATE` e `CAPY_TLS_STATE_ERROR`.
- O slot adquirido é preparado por `capy_tls_context_prepare()`.
- Qualquer falha após aquisição libera o slot antes de retornar.
- Entradas válidas liberam o slot e retornam `CAPY_TLS_EUNSUPPORTED` até BearSSL userland existir.

## Segurança e compatibilidade

O caminho exercita aquisição, preparo, liberação e scrub do contexto gerenciado sem mudar a ABI pública e sem criar uma conexão TLS parcialmente funcional. Isso prepara a transição para backend real mantendo fail-closed.

## Próximo incremento

Em `0.8.0-alpha.76+20260510`, `capy_tls_backend_connect()` passou a consumir o contexto gerenciado preparado e retornar `CAPY_TLS_EUNSUPPORTED` antes de qualquer handshake. O próximo passo é adicionar estado interno de BearSSL/SNI/timeout ao contexto sem habilitar handshake completo.
