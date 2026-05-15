# CapyOS 0.8.0-alpha.21+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha mais um patch **Update/F5**: o sistema agora possui um
fluxo seguro de preparo de update em um único comando. `update-prepare` encadeia
fetch de manifesto remoto assinado, download/verificação de payload, staging e
arm da ativação pendente sem aplicar o boot slot.

A versão alinhada é `0.8.0-alpha.21+20260509`.

## Principais entregas

### Prepare seguro

- Nova API `update_agent_prepare_staged_update()`.
- Novo comando shell `update-prepare` registrado em system-control.
- O fluxo executa:
  - `update_agent_fetch_remote_manifest()`;
  - `update_agent_download_payload()`;
  - `update_agent_stage_latest()`;
  - `update_agent_set_pending_activation(1)`.
- O comando não chama `update-apply` e não arma boot slot diretamente.
- Sucesso imprime versão staged, `cache_sha256` e payload staged.
- Histórico recebe evento `prepare`.

### Guardrails preservados

- Manifesto remoto continua passando por trilha, versão nova, `payload_sha256`,
  `payload_url` e `signature_ed25519`.
- Payload só vira cache persistente quando SHA-256 real bate com o manifesto.
- Staging continua exigindo `payload_cache_sha256` verificado.
- Apply permanece explícito via `update-apply`, usando cache-first por padrão.

## Regressões planejadas

`tests/test_update_agent.c` foi reforçado para revisar estaticamente:

- prepare bem-sucedido com fetch remoto + payload `abc` verificado;
- staging persistido após prepare;
- `pending_activation=1` persistido no state;
- remoção do manifesto temporário fetched;
- summary estável `update prepared and armed for activation`.

## Compatibilidade

- Fluxos manuais continuam válidos:
  `update-fetch` → `update-download-payload` → `update-stage` → `update-arm on`.
- Fluxo preferencial para preparo passa a ser `update-prepare`.
- `update-apply` segue separado para manter a aplicação do boot slot como ação
  explícita e auditável.
- HTTPS real ainda depende da conclusão F4/TLS no runtime.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- API pública `update_agent_prepare_staged_update()`;
- comando `update-prepare` e registro system-control;
- contagem de comandos system-control;
- regressão planejada de prepare em `tests/test_update_agent.c`;
- documentação operacional, CLI, release-signing, STATUS e plano mestre;
- ausência de scripts temporários após higienização.

## Próximos passos

- Adicionar smoke `smoke-x64-update-fetch` com payload local/HTTP controlado.
- Concluir TLS F4 para `payload_url` HTTPS real.
- Avaliar comando de diagnóstico dry-run para explicar cada gate antes de
  preparar update.
