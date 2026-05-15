# CapyOS 0.8.0-alpha.155+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
surface plan seguro para a tela de credenciais do loginwindow. O surface plan
consome o frame plan seguro e publica somente um ticket final declarativo de
superficie para futura composicao GUI/compositor, sem submeter superficie real,
sem enviar damage real ao compositor, sem autenticar pela GUI e sem carregar
segredo, mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_SURFACE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_surface_plan`.
- Adicionada `login_window_credential_screen_surface_plan_build()` para
  selecionar tickets `credential-screen-surface-ticket`,
  `text-recovery-surface-ticket`, `text-login-resume-surface-ticket` ou
  `text-login-fallback-surface-ticket`.
- Testes planejados cobrem superficie declarativa de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, frame plan
  ausente e frame plan inseguro.

## Segurança

- O surface plan exige frame plan seguro, frame autorizado mas nao renderizado,
  ticket selecionado, rota selecionada, credenciais seguras, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-surface-ticket` com `gui-submit-disabled`.
- `window_surface_submitted` e `compositor_damage_submitted` permanecem `0`; o
  contrato e declarativo e nao submete superficie real nem damage real.
- `surface_reuse_allowed`, `surface_cache_allowed` e
  `compositor_damage_planned` preparam escalabilidade futura sem executar cache
  ou compositor real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
