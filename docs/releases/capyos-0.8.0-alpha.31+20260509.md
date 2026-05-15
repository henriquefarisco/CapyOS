# CapyOS 0.8.0-alpha.31+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de confiabilidade para o HTTP client de
`libcapy-net`: `capy_http_get` agora rejeita EOF antes de receber o
`Content-Length` completo, evitando sucesso parcial em respostas HTTP truncadas.

A versão alinhada é `0.8.0-alpha.31+20260509`.

## Principais entregas

### EOF curto fail-closed

- Respostas sem `Content-Length` continuam usando EOF como delimitador.
- Respostas com `Content-Length` conhecido precisam entregar exatamente esse
  volume de body octets.
- Se o peer fecha antes de `body_received >= content_length`, a chamada falha.
- A falha é reportada como `CAPY_NET_EHTTP`, pois a resposta HTTP está incompleta.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `Content-Length: 13` com apenas `Hello` no body, recusado com `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas completas com `Content-Length` mantêm a semântica anterior.
- Respostas sem `Content-Length` continuam lendo até EOF.
- A mudança torna fail-closed apenas respostas truncadas que antes podiam parecer
  sucesso parcial.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- tratamento de `capy_recv_all == 0` no drain de `capy_http_get`;
- condição `body_received < out->content_length` com `Content-Length` conhecido;
- propagação de `CAPY_NET_EHTTP`;
- preservação de EOF como delimitador quando `Content-Length` está ausente;
- regressão planejada em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
