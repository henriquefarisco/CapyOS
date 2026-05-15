# CapyOS 0.8.0-alpha.75+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: `capy_tls_connect_tcp()` agora usa o slot interno gerenciado para preparar contexto real, libera esse slot e só então retorna fail-closed com `CAPY_TLS_EUNSUPPORTED`. O handshake BearSSL userland continua desabilitado e nenhum contexto válido é retornado ao caller.

A versão alinhada é `0.8.0-alpha.75+20260510`.

## Principais entregas

- `capy_tls_connect_tcp()` passa a chamar `capy_tls_context_acquire()`.
- O contexto gerenciado é preenchido via `capy_tls_context_prepare()`.
- Caminhos de erro após aquisição liberam o slot antes de retornar.
- Slot ocupado é reportado como `CAPY_TLS_ESTATE`/`CAPY_TLS_STATE_ERROR`.
- Entradas válidas continuam retornando `CAPY_TLS_EUNSUPPORTED` após liberar o slot.
- Regressões declarativas cobrem liberação após unsupported e slot ocupado.

## Segurança, performance e compatibilidade

- Mantém validações de hostname, peer verification, CA e timeout antes do preparo de contexto.
- Evita retenção de contexto gerenciado no caminho unsupported.
- Preserva ABI pública e comportamento fail-closed para callers existentes.
- Introduz semântica explícita de slot ocupado sem heap e sem handshake.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- o slot é adquirido somente depois de validação estrutural básica;
- falha de aquisição retorna `CAPY_TLS_ESTATE`;
- falha de preparo libera o slot antes de retornar `CAPY_TLS_EINVAL`;
- caminho unsupported libera o slot antes de registrar `CAPY_TLS_EUNSUPPORTED`;
- testes declarativos cobrem reaquisição limpa após connect unsupported.

## Próximos passos

- Introduzir um stub BearSSL userland que consuma o contexto gerenciado sem completar handshake.
- Definir ponto de transição onde `capy_tls_connect_tcp()` poderá retornar contexto válido.
- Expandir métricas/diagnósticos internos de estado TLS userland.
