# CapyOS 0.8.0-alpha.23+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha mais um patch **Update/F5** com foco em usabilidade segura:
`update-prepare-explain` mostra os gates locais que bloqueiam ou liberam o
preparo de update, sem executar fetch, download, staging, arm ou apply.

A versão alinhada é `0.8.0-alpha.23+20260509`.

## Principais entregas

### Diagnóstico explicável de preparo

- Nova struct pública `update_prepare_explain`.
- Nova API `update_agent_prepare_explain()`.
- Novo comando shell `update-prepare-explain` registrado em system-control.
- Gates expostos:
  - `poll`;
  - `catalog`;
  - `repository`;
  - `version`;
  - `payload_sha`;
  - `payload_url`;
  - `signature`;
  - `cache`;
  - `stage_safe`.
- O comando imprime o primeiro gate bloqueador em `failing=`.
- O fluxo não gera evento em `update-history` por não persistir efeitos de
  update.

### Guardrails preservados

- `update-prepare-explain` não substitui `update-prepare-dry-run`; ele explica
  os gates antes da validação operacional.
- `update-prepare-dry-run` continua sendo o preflight que falha/sucesso com
  summaries estáveis.
- `update-prepare` continua sendo o fluxo alto nível com fetch/download/stage/arm.
- `update-apply` continua separado e explícito.

## Regressões planejadas

`tests/test_update_agent.c` foi reforçado para revisar estaticamente:

- explain aprovado com catálogo local válido e cache SHA-256 verificado;
- gates primários (`poll`, `catalog`, `repository`, `version`) aprovados;
- gates de payload (`payload_sha256`, `payload_url`, `signature`, `cache`) aprovados;
- `stage_safe=1` quando todos os gates passam;
- `failing=cache` e `stage_safe=0` quando o cache verificado está ausente.

## Compatibilidade

- Fluxos existentes permanecem válidos.
- `update-prepare-explain` é opcional e não altera arquivos de update.
- O comando é recomendado antes de `update-prepare-dry-run` no caminho manual
  quando o operador precisa entender qual gate bloqueou o preparo.
- HTTPS real ainda depende da conclusão F4/TLS no runtime.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- struct pública `update_prepare_explain`;
- API pública `update_agent_prepare_explain()`;
- comando `update-prepare-explain` e registro system-control;
- contagem de comandos system-control;
- regressão planejada de explain em `tests/test_update_agent.c`;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Adicionar smoke `smoke-x64-update-fetch` com payload local/HTTP controlado.
- Concluir TLS F4 para `payload_url` HTTPS real.
- Avaliar persistência opcional de relatório de preflight somente sob comando explícito.
