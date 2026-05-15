# CapyOS 0.8.0-alpha.76+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: foi introduzido o stub interno `capy_tls_backend_connect()`. Ele consome o `capy_tls_context` preparado, valida socket, hostname e configuração efetiva, e retorna `CAPY_TLS_EUNSUPPORTED` antes de qualquer handshake BearSSL userland.

A versão alinhada é `0.8.0-alpha.76+20260510`.

## Principais entregas

- Novo módulo `userland/lib/capylibc-tls/capy_tls_backend.c`.
- Nova função interna `capy_tls_backend_connect()`.
- `capy_tls_connect_tcp()` chama o backend stub após preparar o slot gerenciado.
- O slot continua sendo liberado antes de publicar o resultado unsupported.
- `Makefile` registra `capy_tls_backend.o` e o novo fonte nos host tests.
- Regressões declarativas cobrem contexto pronto e contexto incompleto/corrompido.

## Segurança, performance e compatibilidade

- Nenhum handshake TLS real foi habilitado.
- Nenhum contexto válido é retornado ao caller.
- O backend stub rejeita contexto nulo, socket inválido, hostname inválido e configuração efetiva inconsistente.
- Mantém a ABI pública opaca e comportamento fail-closed.
- Separa a fronteira de backend antes de integrar BearSSL userland real.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_backend_connect()` aceita somente contexto preparado;
- timeout efetivo precisa estar dentro da janela pública segura;
- CA custom exige ponteiro e tamanho consistentes;
- `capy_tls_connect_tcp()` libera o slot após chamada ao backend stub;
- `Makefile` inclui o novo módulo no build de `capylibc-tls` e nos host tests declarativos.

## Próximos passos

- Introduzir estrutura interna de estado BearSSL userland no contexto.
- Preparar SNI e timeout efetivo para consumo do backend real.
- Manter retorno unsupported até existir handshake real validável.
