# CapyOS 0.8.0-alpha.38+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **Network/F4** de correção de semântica HTTP para o
client de `libcapy-net`: respostas informacionais `1xx` agora falham fechado em
`capy_http_get`.

A versão alinhada é `0.8.0-alpha.38+20260510`.

## Principais entregas

### 1xx fail-closed

- Status `100 Continue` e `103 Early Hints` não são tratados como resposta final.
- `capy_http_get` retorna `CAPY_NET_EHTTP` para qualquer status `1xx`.
- Status `204` e `304` continuam definindo corpo vazio conhecido.
- A mudança evita interpretar resposta intermediária como sucesso final enquanto
  o cliente ainda não suporta múltiplos response heads.

## Regressões planejadas

`tests/test_capylibc_net.c` ganhou cobertura planejada para:

- `100 Continue` seguido por uma resposta `200 OK`, esperando `CAPY_NET_EHTTP`;
- `103 Early Hints` seguido por uma resposta `200 OK`, esperando
  `CAPY_NET_EHTTP`.

## Compatibilidade

- Nenhuma API pública foi alterada.
- Respostas finais `2xx`/`3xx`/`4xx` continuam no fluxo existente.
- `204` e `304` preservam o hardening anterior de corpo vazio conhecido.
- Suporte real a respostas informacionais pode ser adicionado depois com loop de
  leitura de múltiplos heads antes da resposta final.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- separação entre status informacional e status final sem body;
- rejeição de `1xx` logo após parse da status line;
- preservação do comportamento de `204`/`304`;
- regressões planejadas em `tests/test_capylibc_net.c`;
- contrato público em `userland/include/capylibc-net/capy_net.h`;
- versionamento em `VERSION.yaml`, `include/core/version.h` e `README.md`;
- documentação em release note, STATUS, screenshots e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Implementar `libcapy-tls` userland.
- Migrar `capybrowser` para `libcapy-net`/`libcapy-tls`.
- Adicionar suporte a múltiplos response heads se `capy_http_get` precisar seguir
  respostas informacionais no futuro.
