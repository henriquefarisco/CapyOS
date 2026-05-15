# CapyOS 0.8.0-alpha.65+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** que conecta `libcapy-net` à API mínima de `libcapy-tls` por um adaptador interno de HTTPS fail-closed. `capy_http_get("https://...")` continua retornando `CAPY_NET_EUNSUPPORTED`, mas agora passa por uma fronteira TLS explícita e rejeita a URL antes de qualquer DNS, socket, connect, send ou recv.

A versão alinhada é `0.8.0-alpha.65+20260510`.

## Principais entregas

- Novo módulo `userland/lib/capylibc-net/capy_net_tls.c`.
- `capy_http_get` usa `capy_net_internal_https_fail_closed()` para URLs HTTPS.
- Erros de `libcapy-tls` são mapeados para a superfície `CAPY_NET_*`.
- `CAPYLIBC_NET_OBJS` passa a carregar o stub TLS necessário para o adaptador.
- Regressões declarativas em `tests/test_capylibc_net.c` cobrem mapeamento de erro e ausência de I/O em HTTPS.

## Segurança e compatibilidade

- Não há downgrade silencioso de HTTPS para HTTP.
- HTTPS é rejeitado antes de resolver hostname, abrir socket ou enviar bytes.
- A integração preserva o contrato público de `capy_http_get`: retorno `-1` e `CAPY_NET_EUNSUPPORTED`.
- O backend BearSSL userland real continua pendente; a mudança apenas cria a fronteira correta para a próxima etapa.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_net_tls.c` não abre rede e não chama `capy_tls_connect_tcp` sem socket real;
- `capy_http_get` retorna antes de `capy_tcp_connect_host` quando `is_https` está ativo;
- os contadores fake de DNS/socket/connect/send/recv/close permanecem em zero no teste declarativo;
- `Makefile` inclui `capy_net_tls.c` e `capy_tls.c` nos objetos/test sources necessários;
- a documentação ativa aponta para `0.8.0-alpha.65+20260510`.

## Próximos passos

- Implementar transporte HTTPS real em `capy_http_get` quando `libcapy-tls` tiver backend BearSSL userland.
- Conectar `capy_tls_connect_tcp()` a um socket TCP já aberto, sem alterar a semântica fail-closed.
- Migrar gradualmente o browser para consumir `libcapy-net`/`libcapy-tls`.
