# CapyOS 0.8.0-alpha.72+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de arquitetura em `libcapy-tls`: o contexto TLS userland agora tem preparação interna via `capy_tls_context_prepare()`, armazenando socket, hostname validado e configuração efetiva normalizada em `struct capy_tls_context`. O handshake real continua desabilitado e conexões estruturalmente válidas seguem falhando fechado com `CAPY_TLS_EUNSUPPORTED`.

A versão alinhada é `0.8.0-alpha.72+20260510`.

## Principais entregas

- `struct capy_tls_context` deixa de ser placeholder e passa a viver no header interno.
- O contexto armazena `socket_fd`, `hostname` e `capy_tls_effective_config`.
- Novo módulo `userland/lib/capylibc-tls/capy_tls_context.c`.
- Nova função interna `capy_tls_context_prepare()`.
- `capy_tls_connect_tcp()` prepara um contexto stack-local antes de retornar unsupported.
- `Makefile` inclui `capy_tls_context.o` no build de `capylibc-tls` e nos host tests.
- `tests/test_capylibc_tls.c` cobre snapshot de contexto pronto e rejeição de entradas inválidas.

## Segurança, performance e compatibilidade

- Mantém a ABI pública opaca de `struct capy_tls_context`.
- Copia hostname para buffer interno limitado a 253 bytes mais terminador.
- Revalida hostname na preparação interna usando a política compartilhada.
- Evita heap e evita expor contexto parcialmente funcional antes do backend real.
- Mantém fail-closed: o contexto é preparado, mas não retornado ao caller enquanto BearSSL userland não existir.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_context_prepare()` rejeita contexto nulo, socket inválido, hostname inválido e configuração efetiva nula;
- hostname válido é copiado para buffer interno com limite explícito;
- configuração efetiva é armazenada no contexto preparado;
- `capy_tls_connect_tcp()` continua retornando `CAPY_TLS_EUNSUPPORTED` para entradas válidas;
- `Makefile` registra o novo módulo no build userland e nos host tests declarativos.

## Próximos passos

- Trocar o contexto stack-local por alocação real quando houver heap/allocator userland definido.
- Conectar o contexto preparado ao backend BearSSL userland.
- Aplicar timeout efetivo e SNI a partir do contexto preparado.
