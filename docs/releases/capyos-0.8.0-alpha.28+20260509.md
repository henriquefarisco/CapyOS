# CapyOS 0.8.0-alpha.28+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de robustez para o HTTP client de
`libcapy-net`: `capy_http_get` agora resolve todos os headers `Content-Length`
armazenados em `capy_http_response`, aceita duplicatas idênticas e rejeita
duplicatas conflitantes antes de usar o
valor para body framing.

A versão alinhada é `0.8.0-alpha.28+20260509`.

## Principais entregas

### Content-Length duplicado consistente

- Header ausente continua significando `content_length = 0` e leitura até EOF.
- Um único `Content-Length` continua seguindo a validação decimal estrita de
  `alpha.27`.
- Múltiplos `Content-Length` idênticos são aceitos.
- Múltiplos `Content-Length` conflitantes falham como resposta HTTP malformada.
- Falhas retornam `CAPY_NET_EHTTP` em `capy_http_get`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- duas entradas `Content-Length: 13` aceitas e body preservado;
- `Content-Length: 13` junto de `Content-Length: 12` recusado com
  `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas com `Content-Length` ausente ou único válido mantêm a semântica
  anterior.
- Duplicatas idênticas continuam aceitas para compatibilidade com servidores
  tolerantes.
- A mudança torna fail-closed apenas respostas com framing ambíguo.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper `http_resolve_content_length` iterando os headers armazenados da resposta;
- comparação de duplicatas após parsing decimal estrito;
- propagação de erro em `capy_http_get`;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
