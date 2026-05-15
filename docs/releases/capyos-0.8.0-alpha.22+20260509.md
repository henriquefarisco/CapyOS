# CapyOS 0.8.0-alpha.22+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha mais um patch **Update/F5** com foco em segurança operacional
e usabilidade: `update-prepare-dry-run` valida os gates locais que seriam usados
antes de staging/arm, sem persistir staging, armar ativação ou aplicar boot slot.

A versão alinhada é `0.8.0-alpha.22+20260509`.

## Principais entregas

### Preflight sem efeitos persistentes de update

- Nova API `update_agent_prepare_dry_run()`.
- Novo comando shell `update-prepare-dry-run` registrado em system-control.
- O fluxo revisa o estado atual do `update-agent` via `update_agent_poll()`.
- O dry-run exige:
  - catálogo local presente;
  - versão nova disponível;
  - `payload_url` válido;
  - assinatura Ed25519 válida;
  - `payload_cache_sha256` compatível com `payload_sha256` do catálogo.
- O comando não faz fetch remoto, download de payload, staging, arm ou apply.
- Sucesso imprime versão disponível, `cache_sha256` e payload local conhecido.

### Guardrails preservados

- Falhas de catálogo/staged existentes continuam degradando o `update-agent`.
- Cache ausente ou divergente falha antes de qualquer persistência de staging.
- `update-prepare` continua sendo o fluxo alto nível que baixa, stageia e arma.
- `update-apply` continua separado e explícito.

## Regressões planejadas

`tests/test_update_agent.c` foi reforçado para revisar estaticamente:

- dry-run aprovado com catálogo local válido e cache SHA-256 verificado;
- ausência de staging após dry-run;
- ausência de ativação pendente após dry-run;
- ausência de fetch remoto e download de payload durante dry-run;
- recusa estável quando `payload_cache_sha256` verificado está ausente.

## Compatibilidade

- Fluxos existentes permanecem válidos.
- `update-prepare-dry-run` é opcional e destinado ao caminho manual
  `update-fetch` → `update-download-payload` antes de `update-stage`/`update-arm`.
- O comando não substitui `update-prepare`, porque não baixa artefatos remotos.
- HTTPS real ainda depende da conclusão F4/TLS no runtime.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- API pública `update_agent_prepare_dry_run()`;
- comando `update-prepare-dry-run` e registro system-control;
- contagem de comandos system-control;
- regressão planejada de dry-run em `tests/test_update_agent.c`;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Adicionar smoke `smoke-x64-update-fetch` com payload local/HTTP controlado.
- Concluir TLS F4 para `payload_url` HTTPS real.
- Avaliar diagnóstico detalhado por gate para explicar qual pré-condição falhou.
