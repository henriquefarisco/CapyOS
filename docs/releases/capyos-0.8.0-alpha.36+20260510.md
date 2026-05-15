# CapyOS 0.8.0-alpha.36+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de correção de framing para o HTTP
client de `libcapy-net`: `capy_http_get` agora trata status HTTP sem corpo como
corpo vazio conhecido.

A versão alinhada é `0.8.0-alpha.36+20260510`.

## Principais entregas

### Status no-body

- Status `1xx`, `204` e `304` definem corpo vazio conhecido.
- Tail de body já lido junto do head não é exposto nesses status.
- `Content-Length: 0` nesses status segue aceito.
- `Content-Length` não-zero nesses status falha como `CAPY_NET_EHTTP`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `204 No Content` sem `Content-Length` seguido por bytes no tail, esperando
  `body_len == 0`, `content_length == 0`, `truncated == 0` e sem `recv` extra;
- `304 Not Modified` com `Content-Length: 1`, esperando `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas normais com body continuam usando `Content-Length` conhecido ou EOF
  quando o header está ausente.
- A regra evita expor bytes indevidos como body em status que não carregam corpo.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper de status no-body em `capy_net_http.c`;
- aplicação após resolução raw de `Content-Length` e antes do body drain;
- conflito `Content-Length` não-zero versus status no-body;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
