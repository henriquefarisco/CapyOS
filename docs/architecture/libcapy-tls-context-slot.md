# CapyOS — Slot interno de contexto em libcapy-tls

## Objetivo

`libcapy-tls` precisa de um caminho de lifecycle real para contexto antes de ligar BearSSL userland. `0.8.0-alpha.74+20260510` introduz um slot interno estático para aquisição/liberação controlada sem heap e sem handshake.

## Contrato em `0.8.0-alpha.74+20260510`

- `CAPY_TLS_CONTEXT_SLOT_COUNT` define a capacidade interna inicial.
- `capy_tls_context_acquire()` retorna um contexto resetado quando há slot livre.
- Uma segunda aquisição simultânea falha enquanto o único slot está ocupado.
- `capy_tls_context_release()` limpa e libera apenas ponteiros pertencentes ao pool interno.
- `capy_tls_free()` delega para `capy_tls_context_release()`.
- `capy_tls_connect_tcp()` ainda não adquire nem retorna contexto real; continua fail-closed.

## Segurança e performance

O slot estático evita heap prematuro, torna o ciclo de vida testável e garante que hostname/configuração efetiva sejam limpos antes de reuso. Ponteiros externos são ignorados por release para evitar liberação inválida.

## Próximo incremento

Em `0.8.0-alpha.75+20260510`, `capy_tls_connect_tcp()` passou a adquirir, preparar e liberar o slot gerenciado antes de retornar `CAPY_TLS_EUNSUPPORTED`. O próximo incremento é introduzir um stub BearSSL userland que consuma esse contexto sem completar handshake real.
