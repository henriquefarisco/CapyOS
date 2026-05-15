# CapyOS 0.8.0-alpha.26+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de hardening do parser de headers HTTP
em `libcapy-net`: `capy_http_parse_headers` agora valida nomes como token HTTP e
rejeita valores com controles brutos perigosos antes de preencher
`capy_http_response`.

A versão alinhada é `0.8.0-alpha.26+20260509`.

## Principais entregas

### Parser de headers fail-closed

- Nome de header precisa ser não-vazio.
- Nome de header rejeita espaço, controles, DEL e separadores proibidos por
  `token` HTTP.
- Valor de header rejeita controles brutos e DEL.
- HTAB continua permitido em valores, preservando OWS/compatibilidade HTTP.
- Linhas sem `:` continuam ignoradas como antes.
- `capy_http_get` agora transforma falha de parse de headers em erro HTTP antes
  de expor metadata parcial.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- nome de header com espaço (`Bad Name: value`) recusado com `CAPY_NET_EPARSE`;
- valor com controle bruto (`X-Test: ok\x01bad`) recusado com `CAPY_NET_EPARSE`;
- HTAB interno em valor (`X-Test: ok\tvalue`) preservado como válido.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Headers válidos existentes continuam preservados.
- Overflow de quantidade de headers continua sendo ignorado de forma tolerante.
- Linhas sem separador `:` continuam ignoradas para compatibilidade com o parser
  anterior.
- A mudança só torna fail-closed os casos em que há header candidato com nome ou
  valor inseguro.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- helpers internos `http_header_name_char_safe`, `http_header_name_safe` e
  `http_header_value_safe`;
- gate de validação antes de copiar nome/valor para `capy_http_response`;
- propagação de erro em `capy_http_get`;
- regressões planejadas em `tests/test_capylibc_net.c`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
