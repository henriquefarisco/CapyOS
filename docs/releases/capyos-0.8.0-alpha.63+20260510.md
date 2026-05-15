# CapyOS 0.8.0-alpha.63+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de hardening do request-target HTTP em `libcapy-net`. O parser de URL e o builder público de requisições GET passam a rejeitar `\` e `%00` em path/query antes de montar a linha `GET ... HTTP/1.1`.

A versão alinhada é `0.8.0-alpha.63+20260510`.

## Principais entregas

- `capy_url_parse` rejeita backslash bruto no request-target.
- `capy_url_parse` rejeita `%00` em path e query.
- `capy_http_build_get_request` aplica a mesma política para callers diretos.
- Cinco regressões declarativas foram adicionadas em `tests/test_capylibc_net.c`.
- A ABI pública de `libcapy-net` permanece inalterada.

## Segurança e compatibilidade

- Backslash não atravessa mais para o request-target, evitando normalizações divergentes entre cliente, proxy e backend.
- `%00` não atravessa mais para o request-target, evitando truncamento ambíguo em componentes que decodificam percent-encoding antes de tratar strings C.
- Percent-encoding legítimo continua permitido exceto o caso específico `%00`.
- Fragmentos continuam removidos antes da requisição HTTP.
- HTTPS continua retornando `CAPY_NET_EUNSUPPORTED` até `libcapy-tls` aterrissar.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- helpers novos em `capy_net_url.c` não leem além do terminador NUL;
- path/query do parser de URL agora compartilham a política de request-target;
- fragmento continua não copiado para o request-target;
- builder HTTP direto rejeita `\`, `#`, controles brutos e `%00`;
- regressões foram registradas no runner de `tests/test_capylibc_net.c`;
- documentação, versionamento e release note estão alinhados.

## Próximos passos

- Avançar F4d com `libcapy-tls` userland e migração gradual do browser para `libcapy-net`/`libcapy-tls`.
- Avançar a chave offline oficial/CI do smoke VMware quando a infraestrutura externa de F2 estiver pronta.
