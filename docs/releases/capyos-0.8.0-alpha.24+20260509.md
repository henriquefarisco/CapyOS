# CapyOS 0.8.0-alpha.24+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança para `libcapy-net`:
`capy_url_parse` agora rejeita CR/LF, tabs, DEL e demais controles/espacos
brutos em host, path e query antes que `capy_http_build_get_request` construa a
request line HTTP.

A versão alinhada é `0.8.0-alpha.24+20260509`.

## Principais entregas

### Hardening do URL parser

- Novo gate interno para octetos brutos `<= 0x20` e `0x7f`.
- Host continua rejeitando userinfo, IPv6 literal, espaço bruto e agora também
  qualquer controle bruto.
- Path/query/fragmento preservam percent-encoding, mas recusam CR/LF/tabs
  literais.
- O comportamento de `http://`, `https://`, porta padrão, query string e
  fragmento preservado permanece compatível.

### Risco mitigado

`capy_http_build_get_request` escreve `path` diretamente em:

```text
GET <path> HTTP/1.1\r\n
```

Sem esse gate, uma URL contendo CR/LF bruto poderia tentar injetar nova linha de
request/header. O parser agora falha cedo com `CAPY_NET_EPARSE`.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- CRLF bruto no path (`http://x.example/a\r\nHost:evil`) recusado com
  `CAPY_NET_EPARSE`;
- tab bruto na query (`http://x.example/search?q=ok\tbad`) recusado com
  `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Percent-encoding continua permitido e é preservado verbatim no request target.
- HTTPS ainda retorna `CAPY_NET_EUNSUPPORTED` até `libcapy-tls` userland.
- O patch reduz superfície de request-line/header injection antes da migração do
  browser para `libcapy-net`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helper interno `url_is_raw_ctl_or_space`;
- rejeição de controles brutos no host;
- rejeição de controles brutos no path e query-only/fragment-only path;
- regressões planejadas em `tests/test_capylibc_net.c`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
