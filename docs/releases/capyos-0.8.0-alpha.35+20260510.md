# CapyOS 0.8.0-alpha.35+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança para o HTTP client de
`libcapy-net`: `capy_http_parse_headers` agora valida nome e valor de todas as
linhas de header com `:`, mesmo além de `CAPY_HTTP_MAX_HEADERS`.

A versão alinhada é `0.8.0-alpha.35+20260510`.

## Principais entregas

### Validação pós-cap de headers

- `CAPY_HTTP_MAX_HEADERS` continua limitando apenas quantos headers são expostos
  em `out->headers`.
- Headers adicionais bem formados seguem ignorados no output público.
- Headers adicionais com nome inválido ou valor com controle bruto falham como
  `CAPY_NET_EPARSE` no parser.
- Em `capy_http_get`, essa falha é mapeada para `CAPY_NET_EHTTP`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- parser rejeitando nome de header inválido após 16 headers armazenáveis;
- `capy_http_get` rejeitando valor de header com controle bruto após o limite
  armazenado.

## Compatibilidade

- Nenhuma API pública foi alterada.
- O limite de metadados públicos continua `CAPY_HTTP_MAX_HEADERS`.
- A mudança reduz ambiguidade: o limite de armazenamento não é mais um limite de
  validação sintática.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- ordem de validação em `capy_http_parse_headers`;
- preservação de headers adicionais bem formados como ignorados no output;
- rejeição de nome/valor malformados além de `CAPY_HTTP_MAX_HEADERS`;
- mapeamento `CAPY_NET_EPARSE` no parser e `CAPY_NET_EHTTP` no GET;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
