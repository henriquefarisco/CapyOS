# CapyOS 0.8.0-alpha.37+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança para o HTTP client de
`libcapy-net`: respostas com `Content-Encoding` diferente de `identity` agora
são rejeitadas fail-closed.

A versão alinhada é `0.8.0-alpha.37+20260510`.

## Principais entregas

### Content-Encoding fail-closed

- `capy_http_get` aceita `Content-Encoding` ausente.
- `capy_http_get` aceita `Content-Encoding: identity`.
- Qualquer outro coding, como `gzip`, falha como `CAPY_NET_EUNSUPPORTED`.
- A verificação usa o bloco bruto de headers, mantendo consistência com os
  hardenings de `Transfer-Encoding` e `Content-Length`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `Content-Encoding: identity` com `Content-Length: 13`, esperando sucesso e body
  preservado;
- `Content-Encoding: gzip`, esperando `CAPY_NET_EUNSUPPORTED`;
- `Content-Encoding` presente mas vazio, esperando `CAPY_NET_EUNSUPPORTED`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas sem `Content-Encoding` continuam funcionando como antes.
- A mudança evita entregar ao chamador bytes codificados quando `libcapy-net` não
  possui decoder userland integrado.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- scanner raw de `Content-Encoding` em `capy_net_http.c`;
- aceitação explícita de ausência/`identity`;
- rejeição fail-closed de valor presente vazio ou lista malformada;
- rejeição fail-closed de codings que exigem decoder;
- mapeamento para `CAPY_NET_EUNSUPPORTED`;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Integrar decoder de content-encoding ao `libcapy-net` quando houver política de
  limite de saída userland equivalente ao stack HTTP kernel-side.
