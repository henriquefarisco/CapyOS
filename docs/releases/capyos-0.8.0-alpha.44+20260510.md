# CapyOS 0.8.0-alpha.44+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança e compatibilidade para
`libcapy-net`: o parser de URL e o construtor de requests HTTP passam a rejeitar
bytes ambiguos no host antes que a autoridade seja resolvida ou enviada no
header `Host:`.

A versão alinhada é `0.8.0-alpha.44+20260510`.

## Principais entregas

### Host authority hardening

- `capy_url_parse` limita hosts a bytes DNS-label (`A-Z`, `a-z`, `0-9`, `.`,
  `-`) e continua rejeitando autoridade vazia.
- `capy_http_build_get_request` aplica a mesma regra para consumidores diretos
  do helper público.
- Caracteres de confusão como `\` e `%` viram `CAPY_NET_EPARSE` antes de DNS,
  connect ou construção do request HTTP.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- URL com backslash no host, esperando `CAPY_NET_EPARSE`;
- URL com `%` no host, esperando `CAPY_NET_EPARSE`;
- builder HTTP direto com host ambíguo, esperando `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Hostnames comuns e IPv4 dotted-decimal continuam aceitos.
- A mudança é fail-closed para autoridade que o parser desta iteração não
  normaliza de forma explícita.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper `url_host_char_safe` e uso em `capy_url_parse`;
- helper `http_host_char_safe` e uso em `capy_http_build_get_request`;
- preservação dos casos válidos existentes de host comum e IPv4;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
