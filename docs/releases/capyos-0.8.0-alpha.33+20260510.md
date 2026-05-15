# CapyOS 0.8.0-alpha.33+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança para o HTTP client de
`libcapy-net`: headers HTTP dobrados/continuados (`obs-fold`) agora são
rejeitados fail-closed em vez de serem ignorados parcialmente.

A versão alinhada é `0.8.0-alpha.33+20260510`.

## Principais entregas

### Obs-fold fail-closed

- `capy_http_parse_headers` rejeita qualquer linha de header iniciada por SP ou
  HTAB antes de um novo campo.
- A falha do parser usa `CAPY_NET_EPARSE`.
- Em `capy_http_get`, a resposta dobrada é tratada como HTTP malformado e falha
  com `CAPY_NET_EHTTP`.
- A API pública e `struct capy_http_response` permanecem inalteradas.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- parser de headers rejeitando bloco com linha continuada ` folded`;
- `capy_http_get` rejeitando resposta com header dobrado no bloco bruto.

## Compatibilidade

- Respostas HTTP normais sem linhas continuadas mantêm a semântica anterior.
- HTAB interno em valor de header continua permitido quando não inicia uma linha
  continuada.
- Como `libcapy-net` não normaliza `obs-fold`, aceitar essas linhas poderia criar
  interpretação ambígua de metadados HTTP.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- detecção de linha iniciada por SP/HTAB em `capy_http_parse_headers`;
- preservação de HTAB interno em valores normais;
- mapeamento `CAPY_NET_EPARSE` no parser e `CAPY_NET_EHTTP` no GET;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter o smoke `smoke-x64-tls-handshake` como validação de F4 seção d.
