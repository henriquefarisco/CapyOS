# CapyOS 0.8.0-alpha.16+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **Update/F5**: o ciclo operacional
pós-apply agora possui comandos shell para confirmar saúde do boot atualizado ou
executar rollback assistido quando o boot permanecer sem confirmação.

A versão alinhada é `0.8.0-alpha.16+20260509`.

## Principais entregas

### Comando `update-confirm-health`

- Confirma que o boot atual está saudável.
- Chama `update_agent_confirm_health()`.
- Limpa ativação pendente persistente quando aplicável.
- Publica summary estável: `boot health confirmed; update committed`.
- Registra `event=confirm-health` no histórico de update.

### Comando `update-rollback-check`

- Verifica se existe rollback de boot pendente.
- Chama `update_agent_check_rollback()`.
- Se não houver rollback, publica `no boot rollback pending`.
- Se houver rollback pendente, chama rollback de boot, limpa staging persistente e
  publica `boot rollback completed; staged update cleared`.
- Registra `event=rollback-check` no no-op e `event=rollback` quando rollback é
  executado.

### Status auditável

`update_agent_confirm_health()` e `update_agent_check_rollback()` agora deixam
`last_result` e `summary` previsíveis para shell, supervisor de serviço e
histórico operacional.

### Testes planejados

`tests/test_update_transact.c` foi reforçado por revisão estática para validar:

- summary e `last_result=0` após confirmação de saúde;
- summary e `last_result=1` após rollback executado;
- summary e `last_result=0` quando não há rollback pendente.

## Compatibilidade

- `update-fetch`, `update-stage`, `update-arm` e `update-apply` permanecem
  compatíveis.
- Download automático de payload remoto ainda não faz parte desta release.
- O fluxo pós-apply fica operacional e auditável sem exigir novos formatos de
  manifesto.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e
documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python de validação.

Pontos revisados estaticamente:

- summaries em `update_agent_confirm_health()`;
- summaries em `update_agent_check_rollback()`;
- comandos `cmd_update_confirm_health` e `cmd_update_rollback_check`;
- registro no array `g_system_control_commands[25]`;
- regressões planejadas em `tests/test_update_transact.c`;
- documentação CLI/operacional/plano/release alinhada.

## Próximos passos

- Baixar payload remoto declarado no manifesto.
- Adicionar smoke `smoke-x64-update-fetch`/`update-apply`/health com servidor HTTP local.
- Integrar TLS userland completo quando F4 entregar `libcapy-tls`.
