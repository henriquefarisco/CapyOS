# CapyOS 0.8.0-alpha.77+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: o contexto TLS interno agora inclui estado de backend com SNI, timeout efetivo e flags de lifecycle. O stub `capy_tls_backend_connect()` prepara esse estado e ainda retorna `CAPY_TLS_EUNSUPPORTED` antes de qualquer handshake BearSSL userland.

A versão alinhada é `0.8.0-alpha.77+20260510`.

## Principais entregas

- Novo `struct capy_tls_backend_state` em `capy_tls_context`.
- Estado interno registra `context_ready`, `sni_ready`, `timeout_ready` e `handshake_started`.
- SNI é copiado de hostname validado para buffer interno limitado.
- Timeout efetivo normalizado é propagado para o estado do backend.
- Rejeições limpam estado de backend antes de retornar erro.
- Regressões declarativas cobrem preparo, scrub e rejeição do estado backend.

## Segurança, performance e compatibilidade

- Nenhum handshake TLS real foi habilitado.
- Nenhum contexto válido é retornado ao caller.
- O estado backend é zerado por reset/release e antes de cada tentativa do stub.
- SNI usa a mesma política compartilhada de hostname e o mesmo limite de 253 bytes.
- Mantém a ABI pública opaca e comportamento fail-closed.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_context_reset()` continua limpando todo o contexto, incluindo backend state;
- `capy_tls_backend_connect()` zera estado antes de validar e antes de preparar;
- contexto pronto recebe SNI e timeout efetivo sem marcar handshake iniciado;
- rejeições limpam `context_ready`, SNI e timeout de backend;
- testes declarativos cobrem scrub por clear/release e rejeição do backend.

## Próximos passos

- Separar preparação SNI/timeout em função interna reutilizável para o futuro BearSSL real.
- Adicionar estado mínimo de trust anchors userland sem carregar certificados ainda.
- Manter retorno unsupported até existir handshake real validável.
