# CapyOS — Ciclo de vida interno do contexto libcapy-tls

## Objetivo

`libcapy-tls` prepara contexto TLS antes do backend BearSSL userland existir. `0.8.0-alpha.73+20260510` adiciona reset e clear internos para impedir retenção de hostname, ponteiros de CA ou configuração efetiva em caminhos fail-closed.

## Contrato em `0.8.0-alpha.73+20260510`

- `capy_tls_context_reset()` zera o contexto e repõe defaults seguros.
- O estado resetado usa `socket_fd=-1`, hostname vazio, `verify_peer=1` e `CAPY_TLS_TIMEOUT_DEFAULT_MS`.
- `capy_tls_context_clear()` limpa contexto preparado usando o mesmo contrato de reset.
- `capy_tls_context_prepare()` sempre reseta antes de validar/preencher.
- Rejeições de preparo não preservam estado anterior do contexto.
- `capy_tls_connect_tcp()` limpa o contexto stack-local antes de retornar `CAPY_TLS_EUNSUPPORTED`.

## Segurança e privacidade

Mesmo sem handshake real, o caminho de TLS pode receber hostname e ponteiros de CA. A limpeza explícita reduz risco de retenção acidental em stack/local state e prepara o contrato para liberação real futura.

## Próximo incremento

Em `0.8.0-alpha.74+20260510`, o contexto passou a ter um slot interno estático com `capy_tls_context_acquire()`/`capy_tls_context_release()`, e `capy_tls_free()` delega para release seguro. O próximo passo é conectar esse slot ao primeiro stub BearSSL userland sem handshake completo.
