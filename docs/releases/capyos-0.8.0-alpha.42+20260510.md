# CapyOS 0.8.0-alpha.42+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de robustez para o parser HTTP de
`libcapy-net`: blocos de header truncados agora falham fechado.

A versão alinhada é `0.8.0-alpha.42+20260510`.

## Principais entregas

### Header block truncation fail-closed

- `capy_http_parse_headers` rejeita linha de header sem terminador `LF`.
- O parser também exige a linha vazia final que delimita o fim do bloco.
- Em erro, `header_count` é limpo, `CAPY_NET_EPARSE` é definido e `-1` retorna.
- `capy_http_get` já detecta o fim do head antes de chamar o parser; a mudança
  endurece principalmente o helper público e mantém o contrato fail-closed.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- bloco `X-Test: ok` sem `LF`, esperando `CAPY_NET_EPARSE`;
- bloco `X-Test: ok\r\n` sem linha vazia final, esperando `CAPY_NET_EPARSE`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Blocos completos com `CRLF` ou LF-only continuam aceitos.
- O limite `CAPY_HTTP_MAX_HEADERS` continua restringindo só o armazenamento
  público, não a validação sintática.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- rejeição de linha sem `LF`;
- exigência de linha vazia final;
- limpeza de `header_count` no erro;
- preservação de blocos completos `CRLF`/LF-only;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Manter hardening de parsing/framing antes de tráfego HTTPS real.
