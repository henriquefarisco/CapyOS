# CapyOS 0.8.0-alpha.39+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de robustez para o parser HTTP de
`libcapy-net`: `capy_http_parse_status_line` agora valida a status-line de forma
mais estrita.

A versão alinhada é `0.8.0-alpha.39+20260510`.

## Principais entregas

### Status-line estrita

- Status code deve estar na faixa HTTP `100..599`.
- O status code deve ser seguido por `SP` antes da reason phrase.
- A reason phrase pode terminar em `CRLF` ou `LF`, mas não pode conter controles
  brutos; `HTAB` permanece aceito como whitespace de linha.
- Em `capy_http_get`, falha no parser de status continua mapeada para
  `CAPY_NET_EHTTP`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- rejeição de status `099`;
- rejeição de status `600`;
- rejeição de `HTTP/1.1 200OK` sem separador antes da reason phrase;
- rejeição de controle bruto na reason phrase.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Status-lines válidas `HTTP/1.0`/`HTTP/1.1` com `CRLF` ou `LF` continuam aceitas.
- A mudança reduz ambiguidade antes do parsing de headers e framing de body.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- validação da faixa `100..599`;
- separador obrigatório após o status code;
- reason phrase sem controles brutos;
- preservação de `CRLF` e `LF` como terminadores aceitos;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing antes de expor o HTTP client a tráfego HTTPS real.
