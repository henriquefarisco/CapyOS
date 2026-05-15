# CapyOS 0.8.0-alpha.27+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de robustez para o HTTP client de
`libcapy-net`: `capy_http_get` agora aceita `Content-Length` somente como decimal
estrito que cabe em `size_t` antes de usar o valor para governar leitura e drain
do corpo.

A versão alinhada é `0.8.0-alpha.27+20260509`.

## Principais entregas

### Content-Length estrito

- Header ausente continua significando `content_length = 0` e leitura até EOF.
- Header presente precisa começar com dígito decimal.
- Após os dígitos, apenas OWS final é aceito.
- Sufixos como `13x` falham como resposta HTTP malformada.
- Overflow de `size_t` falha antes do valor ser usado.
- Falhas retornam `CAPY_NET_EHTTP` em `capy_http_get`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `Content-Length: 13x` recusado com `CAPY_NET_EHTTP`;
- `Content-Length: 184467440737095516160` recusado com `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- `Content-Length` ausente mantém a semântica anterior.
- `Content-Length` decimal válido continua sendo aceito.
- OWS final em valor válido continua permitido.
- A mudança torna fail-closed apenas valores presentes que antes seriam truncados
  ou sofreriam overflow silencioso.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper `http_parse_content_length` com retorno explícito de erro;
- checagem de overflow com `((size_t)-1 - digit) / 10u`;
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
