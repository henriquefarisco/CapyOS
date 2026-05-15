# CapyOS 0.8.0-alpha.40+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de compatibilidade robusta para o HTTP
client de `libcapy-net`: `capy_http_get` agora detecta o fim do head por
`CRLFCRLF` ou por uma linha vazia LF-only.

A versão alinhada é `0.8.0-alpha.40+20260510`.

## Principais entregas

### Terminador LF-only no response head

- `CRLFCRLF` continua sendo aceito como terminador normal do head.
- `LF LF` também passa a delimitar o fim do head.
- O corpo continua começando imediatamente após o terminador detectado.
- A mudança alinha `capy_http_get` ao suporte LF-only já existente nos parsers de
  status-line e headers.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- resposta HTTP completa com status/header terminados por LF-only, esperando
  sucesso e body preservado;
- mesma resposta LF-only dividida em múltiplos `recv`, esperando reassembly
  correto do head e body preservado.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas CRLF continuam no caminho existente.
- O limite fixo de head (`HTTP_HEAD_BUF_CAP`) permanece inalterado.
- Headers e body continuam passando pelo mesmo parsing/framing endurecido das
  releases anteriores.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- detecção de `CRLFCRLF` preservada;
- detecção adicional de `LF LF`;
- cálculo de `header_end` após o terminador correto;
- compatibilidade com `capy_http_parse_status_line` e `capy_http_parse_headers`;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
