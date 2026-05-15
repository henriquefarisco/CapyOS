# CapyOS 0.8.0-alpha.20+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha mais um patch **Update/F5**: o apply transacional agora usa
o payload cacheado e verificado por padrão. Depois de `update-download-payload`
e `update-stage`, `update-apply` sem argumentos consome `payload_cache_sha256`,
compara contra o `payload_sha256` do manifesto staged e só então arma o boot
slot.

A versão alinhada é `0.8.0-alpha.20+20260509`.

## Principais entregas

### Apply cache-first

- Nova API `update_agent_apply_cached_payload()`.
- `update-apply` passa a aceitar `update-apply` e `update-apply <payload_sha256>`.
- Sem argumento, o comando usa `payload_cache_sha256` persistido no estado.
- Com argumento, o digest manual continua disponível como fallback explícito.
- Saída do comando passa a indicar `source=cache` ou `source=manual`.

### Gates preservados

- Cache ausente falha com `payload cache sha256 missing; refusing cached apply`.
- Cache/staged mismatch continua recusado pelo gate de SHA-256 já existente.
- Digest manual ausente, malformado ou divergente continua recusado antes de
  tocar o boot slot.
- `update-stage` permanece dependente de cache de payload verificado.

## Regressões planejadas

`tests/test_update_transact.c` foi reforçado para revisar estaticamente:

- apply cache-first com `payload_cache_sha256` igual ao manifesto staged;
- recusa estável quando o cache verificado está ausente;
- recusa quando o catálogo/cache mudou e não combina com o staged manifest;
- preservação do fallback manual por `update_agent_apply_boot_slot_verified()`.

## Compatibilidade

- Fluxos existentes com `update-apply <payload_sha256>` continuam válidos.
- O fluxo preferencial passa a ser:
  `update-fetch` → `update-download-payload` → `update-stage` → `update-arm on`
  → `update-apply`.
- O apply totalmente autônomo pós-download ainda fica para incremento posterior.
- HTTPS real ainda depende da conclusão F4/TLS no runtime.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- API pública `update_agent_apply_cached_payload()`;
- fluxo de `update-apply [payload_sha256]` no shell;
- summaries estáveis de cache ausente e mismatch;
- regressões planejadas de cache válido, cache ausente e mismatch cache/staged;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Automatizar a sequência fetch/download/stage/arm/apply em um comando de alto
  nível com guardrails.
- Adicionar smoke `smoke-x64-update-fetch` com payload local/HTTP controlado.
- Concluir TLS F4 para `payload_url` HTTPS real.
