# CapyOS 0.8.0-alpha.157+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
damage plan seguro para a tela de credenciais do loginwindow. O damage plan
consome o compositor plan seguro e publica somente um ticket declarativo de
damage/cache para futura integracao GUI/compositor, sem enviar damage real, sem
submeter compositor real, sem autenticar pela GUI e sem carregar segredo,
mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_DAMAGE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_damage_plan`.
- Adicionada `login_window_credential_screen_damage_plan_build()` para selecionar
  tickets `credential-screen-damage-ticket`, `text-recovery-damage-ticket`,
  `text-login-resume-damage-ticket` ou `text-login-fallback-damage-ticket`.
- Testes planejados cobrem damage declarativo de credencial, recuperacao textual,
  resume textual, submit bloqueado, acao desconhecida, compositor plan ausente e
  compositor plan inseguro.

## Segurança

- O damage plan exige compositor plan seguro, superficie autorizada mas nao
  submetida, damage planejado/autorizado mas nao submetido, ticket selecionado,
  rota selecionada, credenciais seguras, storage limpo, redacao de
  segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-damage-ticket` com `gui-submit-disabled`.
- `damage_submitted` e `compositor_damage_submitted` permanecem `0`; o contrato e
  declarativo e nao envia damage real nem submete compositor real.
- `damage_incremental_allowed`, `damage_cache_allowed`, `damage_reuse_allowed`,
  `damage_cache_hit=0` e `full_damage_required` preparam escalabilidade futura
  sem executar cache/compositor real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
