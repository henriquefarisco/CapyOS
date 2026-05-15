# CapyOS 0.8.0-alpha.47+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de segurança e compatibilidade para
`libcapy-net`: o helper público `capy_http_build_get_request` passa a rejeitar
porta zero antes de montar a request line HTTP.

A versão alinhada é `0.8.0-alpha.47+20260510`.

## Principais entregas

### Request builder port-zero fail-closed

- `capy_http_build_get_request` rejeita `port == 0` com `CAPY_NET_EPARSE`.
- O comportamento fica alinhado a `capy_url_parse`, que já rejeita `:0`.
- A validação ocorre antes de escrever qualquer byte no buffer de request.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- builder HTTP direto com porta zero, esperando `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Porta 80 default e portas customizadas válidas continuam aceitas.
- Porta zero passa a ser tratada como request malformado no helper direto.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- guarda `port == 0` em `capy_http_build_get_request`;
- preservação do caminho de sucesso para porta 80 e portas customizadas;
- regressão planejada em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
