# CapyOS 0.8.0-alpha.41+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança para o parser HTTP de
`libcapy-net`: linhas de header não vazias sem `:` agora falham fechado.

A versão alinhada é `0.8.0-alpha.41+20260510`.

## Principais entregas

### Header separator fail-closed

- `capy_http_parse_headers` rejeita linha de header não vazia sem separador `:`.
- A rejeição limpa `header_count`, define `CAPY_NET_EPARSE` e retorna `-1`.
- `capy_http_get` preserva o mapeamento de erro de parser para `CAPY_NET_EHTTP`.
- A mudança elimina o comportamento anterior de ignorar silenciosamente uma linha
  malformada antes do framing de body.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- parser de headers recebendo `X-Broken-Header` sem `:`, esperando
  `CAPY_NET_EPARSE`;
- `capy_http_get` recebendo uma resposta com header sem `:`, esperando
  `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Headers bem formados continuam funcionando como antes.
- Headers além de `CAPY_HTTP_MAX_HEADERS` continuam validados mesmo quando não são
  armazenados no array público.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de linha não vazia sem `:` em `capy_http_parse_headers`;
- limpeza de `header_count` no erro;
- propagação de parser failure para `CAPY_NET_EHTTP` em `capy_http_get`;
- preservação de headers válidos e validações anteriores;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
