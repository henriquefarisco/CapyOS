# CapyOS 0.8.0-alpha.30+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de desempenho e confiabilidade para o
HTTP client de `libcapy-net`: `capy_http_get` agora separa bytes de corpo
recebidos no fio dos bytes efetivamente armazenados no buffer do caller.

A versão alinhada é `0.8.0-alpha.30+20260509`.

## Principais entregas

### Drain por body_received

- `out->body_len` continua representando apenas bytes gravados no buffer do caller.
- O novo contador interno `body_received` rastreia bytes consumidos do corpo no fio.
- Quando `Content-Length` é conhecido, a leitura para ao atingir esse valor mesmo
  se o corpo foi truncado ou descartado.
- Bytes já recebidos junto do head são limitados ao `Content-Length` conhecido.
- `body_buf_cap == 0` continua permitido para probes que descartam corpo.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- corpo completo já recebido no head com `body_buf` pequeno: `body_len` fica em 5,
  `truncated` fica 1, e não há `recv` extra apenas para observar EOF quando o
  `Content-Length` já foi satisfeito.

## Compatibilidade

- Nenhuma API pública foi alterada.
- O significado público de `body_len` não muda: continua sendo bytes armazenados.
- O flag `truncated` continua indicando que o corpo on-wire excedeu o buffer do caller.
- Respostas sem `Content-Length` continuam lendo até EOF.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- contador local `body_received` em `capy_http_get`;
- consumo do tail já lido junto do head;
- limite de leituras adicionais pelo `Content-Length` restante;
- preservação de `out->body_len` como bytes armazenados;
- regressão planejada em `tests/test_capylibc_net.c`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
