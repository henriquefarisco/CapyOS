# CapyOS 0.8.0-alpha.34+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança para o HTTP client de
`libcapy-net`: `capy_http_get` agora valida `Content-Length` no bloco bruto
completo de headers, inclusive além do limite de headers armazenados em
`capy_http_response`.

A versão alinhada é `0.8.0-alpha.34+20260510`.

## Principais entregas

### Content-Length raw completo

- `CAPY_HTTP_MAX_HEADERS` continua limitando apenas os metadados expostos em
  `out->headers`.
- O framing do corpo usa scanner no bloco bruto de headers.
- `Content-Length` válido além do limite armazenado ainda governa o drain.
- `Content-Length` conflitante ou malformado além do limite armazenado falha como
  `CAPY_NET_EHTTP`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `Content-Length: 13` após 16 headers preenchendo `CAPY_HTTP_MAX_HEADERS`,
  esperando `body_len == 13`, `content_length == 13` e sem `recv` extra para EOF;
- conflito `Content-Length: 13` versus `Content-Length: 12` quando o segundo valor
  aparece além do limite armazenado, esperando `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- `capy_http_parse_headers` segue armazenando no máximo `CAPY_HTTP_MAX_HEADERS`.
- A mudança afeta apenas o framing interno de `capy_http_get`, tornando-o
  consistente com o scanner bruto já usado para `Transfer-Encoding`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- scanner raw de `Content-Length` em `http_resolve_content_length`;
- parsing estrito/overflow-safe de valor por slice sem depender de NUL;
- detecção de duplicatas conflitantes em todo o bloco bruto;
- preservação do limite público `CAPY_HTTP_MAX_HEADERS` como limite de storage;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
