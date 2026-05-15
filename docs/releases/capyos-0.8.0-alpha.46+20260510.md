# CapyOS 0.8.0-alpha.46+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança e compatibilidade para
`libcapy-net`: hosts agora passam por validação de limites de labels DNS antes
de DNS, connect ou construção do request HTTP.

A versão alinhada é `0.8.0-alpha.46+20260510`.

## Principais entregas

### DNS label boundary hardening

- `capy_url_parse` rejeita labels vazios, labels com mais de 63 bytes e labels
  que começam ou terminam com `-`.
- `capy_http_build_get_request` aplica a mesma regra para consumidores diretos
  do helper público.
- A validação mantém hostnames comuns e IPv4 dotted-decimal aceitos, mas fecha
  autoridades ambíguas como `good..example`, `-bad.example` e `bad-.example`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- URL com label vazio no host, esperando `CAPY_NET_EPARSE`;
- URL com label iniciando por hífen, esperando `CAPY_NET_EPARSE`;
- builder HTTP direto com label terminando por hífen, esperando `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Hostnames comuns e IPv4 dotted-decimal continuam aceitos.
- Hosts com trailing dot/root label explícito continuam fora do subconjunto desta
  iteração, porque não há normalização explícita de FQDN absoluto.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper `url_host_labels_safe` e chamada em `capy_url_parse`;
- regra equivalente em `http_host_safe`;
- preservação de hostnames comuns e IPv4 dotted-decimal;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
