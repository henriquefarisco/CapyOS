# CapyOS 0.8.0-alpha.29+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de robustez para o HTTP client de
`libcapy-net`: `capy_http_get` agora rejeita qualquer header `Transfer-Encoding`
presente no bloco de headers, não apenas valores contendo `chunked`.

A versão alinhada é `0.8.0-alpha.29+20260509`.

## Principais entregas

### Transfer-Encoding fail-closed

- `libcapy-net` segue suportando apenas corpo por `Content-Length` ou EOF.
- Qualquer `Transfer-Encoding` presente no bloco de headers retorna `CAPY_NET_EUNSUPPORTED`.
- `Transfer-Encoding: chunked` continua rejeitado.
- `Transfer-Encoding: identity` e outros valores também passam a ser rejeitados.
- A rejeição acontece antes de confiar em `Content-Length` para body framing.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `Transfer-Encoding: identity` recusado com `CAPY_NET_EUNSUPPORTED`.

A cobertura existente de `Transfer-Encoding: chunked` continua preservada.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas sem `Transfer-Encoding` mantêm a semântica anterior.
- Respostas com `Transfer-Encoding` passam a falhar de forma explícita até que
  `libcapy-net` ganhe decodificação real de transfer codings.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper `http_headers_have_transfer_encoding` varrendo o bloco bruto de headers;
- propagação de `CAPY_NET_EUNSUPPORTED` em `capy_http_get`;
- regressão planejada em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
