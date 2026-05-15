# CapyOS 0.8.0-alpha.71+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de arquitetura em `libcapy-tls`: a validação de configuração foi separada para `capy_tls_config_resolve()`, que produz um snapshot interno normalizado antes de qualquer backend BearSSL userland. O handshake real continua desabilitado e o caminho estruturalmente válido permanece fail-closed com `CAPY_TLS_EUNSUPPORTED`.

A versão alinhada é `0.8.0-alpha.71+20260510`.

## Principais entregas

- Novo header interno `userland/lib/capylibc-tls/capy_tls_internal.h`.
- Novo módulo `userland/lib/capylibc-tls/capy_tls_config.c`.
- Nova estrutura interna `capy_tls_effective_config`.
- Nova função interna `capy_tls_config_resolve()`.
- `capy_tls_connect_tcp()` passa a consumir configuração efetiva normalizada.
- `Makefile` inclui `capy_tls_config.o` no build de `capylibc-tls` e no host test.
- `tests/test_capylibc_tls.c` cobre default seguro, snapshot explícito e rejeição de output nulo.

## Segurança, performance e compatibilidade

- Centraliza peer verification, CA e timeout em uma fonte interna única.
- Garante que `timeout_ms=0` vire `CAPY_TLS_TIMEOUT_DEFAULT_MS` antes do backend real.
- Evita que o futuro contexto TLS precise interpretar configuração pública crua em múltiplos pontos.
- Preserva a ABI pública de `capy_tls.h`.
- Preserva fail-closed: entradas válidas ainda retornam `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland existir.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_config_resolve()` inicializa defaults antes de validar configuração opcional;
- `config == NULL` gera snapshot seguro com `verify_peer=1`, sem CA e timeout default;
- configuração explícita preserva CA e timeout válidos no snapshot;
- configuração inválida mantém `CAPY_TLS_EINVAL` em `capy_tls_connect_tcp()`;
- `Makefile` compila o novo módulo no build userland e nos host tests declarativos.

## Próximos passos

- Materializar `struct capy_tls_context` com socket, hostname e `capy_tls_effective_config`.
- Manter handshake desabilitado até o backend BearSSL userland estar completo.
- Aplicar timeout efetivo ao transporte TCP/TLS quando o backend real existir.
