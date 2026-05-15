# CapyOS 0.8.0-alpha.32+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de confiabilidade para o HTTP client de
`libcapy-net`: `capy_http_get` agora diferencia `Content-Length` conhecido do
valor numérico do header, tratando `Content-Length: 0` como corpo vazio conhecido.

A versão alinhada é `0.8.0-alpha.32+20260509`.

## Principais entregas

### Content-Length zero conhecido

- Header `Content-Length` ausente continua usando EOF como delimitador.
- Header `Content-Length: 0` agora é corpo conhecido de tamanho zero.
- Bytes extras recebidos no tail do head não são expostos como body quando o
  tamanho declarado é zero.
- A distinção é interna e não altera `struct capy_http_response`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `Content-Length: 0` seguido de bytes extras no tail do head, esperando sucesso
  com `body_len == 0`, `truncated == 0` e sem `recv` extra para EOF.

## Compatibilidade

- Nenhuma API pública foi alterada.
- `out->content_length` continua sendo 0 tanto para header ausente quanto para
  header presente com valor zero.
- A distinção de presença é interna e usada apenas para framing correto.
- Respostas sem `Content-Length` mantêm leitura até EOF.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- `http_resolve_content_length` retornando flag interna `known`;
- substituição de decisões `content_length > 0` por `content_length_known`;
- preservação do valor público `out->content_length`;
- regressão planejada em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
