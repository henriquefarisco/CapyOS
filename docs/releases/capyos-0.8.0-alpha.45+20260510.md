# CapyOS 0.8.0-alpha.45+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de privacidade e compatibilidade para
`libcapy-net`: o helper público `capy_http_build_get_request` passa a rejeitar
fragmentos (`#...`) em paths passados diretamente.

A versão alinhada é `0.8.0-alpha.45+20260510`.

## Principais entregas

### Request-target fragment fail-closed

- `capy_http_build_get_request` rejeita `#` em paths diretos.
- O parser de URL já remove fragmentos antes de preencher `path`; este patch
  fecha o bypass de consumidores que chamam o builder público diretamente.
- O erro reportado é `CAPY_NET_EPARSE`, mantendo a política fail-closed para
  request targets que poderiam vazar estado client-side.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- builder HTTP direto com `/path#secret`, esperando `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Paths absolutos comuns e query strings continuam aceitos.
- Fragmentos continuam sendo removidos pelo parser de URL e agora são rejeitados
  pelo builder direto.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de `#` em `http_path_safe`;
- preservação de paths absolutos e query strings;
- regressão planejada em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
