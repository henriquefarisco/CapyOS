# CapyOS 0.8.0-alpha.64+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** que cria a superfície pública mínima de `libcapy-tls` em userland. A API expõe configuração, estado, erro, metadados de segurança e operações `connect/send/recv/close`, mas as operações de tráfego falham fechado com `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland ser integrado.

A versão alinhada é `0.8.0-alpha.64+20260510`.

## Principais entregas

- Novo header `userland/include/capylibc-tls/capy_tls.h`.
- Novo módulo `userland/lib/capylibc-tls/capy_tls.c`.
- Novo alvo `capylibc-tls` no `Makefile`.
- Novo teste declarativo `tests/test_capylibc_tls.c` registrado no runner.
- API de erro/estado estável para integração futura com `libcapy-net` e browser.

## Segurança e compatibilidade

- HTTPS continua sem downgrade: não há fallback para plaintext.
- `capy_tls_connect_tcp` valida argumentos e configuração de CA antes de retornar unsupported.
- `send`, `recv` e `close` falham fechado enquanto não existe contexto TLS real.
- Informações de segurança ficam zeradas quando a sessão não foi negociada.
- Nenhuma dependência BearSSL userland é ligada nesta etapa.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- API pública é independente do TLS kernel-side atual;
- implementação não aloca memória e não dereferencia contexto fake;
- falhas válidas usam `CAPY_TLS_EINVAL` ou `CAPY_TLS_EUNSUPPORTED`;
- metadados de segurança permanecem zerados antes do handshake real;
- `Makefile` referencia o objeto novo e o teste novo;
- `tests/test_runner.c` chama o novo conjunto declarativo.

## Próximos passos

- Recompilar BearSSL como blob userland e substituir o fail-closed por handshake TLS 1.2 real.
- Integrar `capy_http_get` HTTPS a `libcapy-tls` quando o backend estiver disponível.
- Migrar gradualmente o browser para `libcapy-net`/`libcapy-tls`.
