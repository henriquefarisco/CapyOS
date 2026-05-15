# CapyOS 0.8.0-alpha.43+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de privacidade e compatibilidade para
`libcapy-net`: fragmentos de URL (`#...`) deixam de ser enviados no request
target HTTP.

A versão alinhada é `0.8.0-alpha.43+20260510`.

## Principais entregas

### URL fragment stripping

- `capy_url_parse` preserva query strings no `path`.
- Fragmentos `#...` são validados contra controles brutos, mas não são copiados
  para `path`.
- `capy_http_get` passa a construir a request line sem fragmentos, evitando
  vazamento de estado client-side para o servidor.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- URL com path e fragmento, esperando `path` sem `#...`;
- URL com query e fragmento, esperando query preservada e fragmento removido;
- `capy_http_get` com fragmento, esperando request line sem `#secret`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Query strings continuam preservadas no request target.
- URLs sem fragmento mantêm o comportamento anterior.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- remoção de fragmentos em paths normais;
- preservação de query strings antes do fragmento;
- request line de `capy_http_get` sem `#...`;
- validação de controles brutos no trecho descartado;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
