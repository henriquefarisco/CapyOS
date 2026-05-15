# CapyOS 0.8.0-alpha.160+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
vsync plan seguro para a tela de credenciais do loginwindow. O vsync plan consome
o schedule plan seguro e publica somente um ticket declarativo de sincronizacao
para futura integracao GUI/compositor, sem aguardar vsync real, sem armar fence,
sem armar timer, sem fazer page flip, sem enviar damage real, sem submeter
compositor real, sem autenticar pela GUI e sem carregar segredo, mascara,
comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_VSYNC_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_vsync_plan`.
- Adicionada `login_window_credential_screen_vsync_plan_build()` para selecionar
  tickets `credential-screen-vsync-ticket`, `text-recovery-vsync-ticket`,
  `text-login-resume-vsync-ticket` ou `text-login-fallback-vsync-ticket`.
- Testes planejados cobrem sincronizacao declarativa de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, schedule plan
  ausente, schedule plan inseguro e tentativa de propagar estado unsafe ja
  submetido.

## Segurança

- O vsync plan exige schedule plan seguro, schedule autorizado mas nao submetido,
  present/damage/compositor nao submetidos, schedule ticket selecionado, rota
  selecionada, credenciais seguras, storage limpo, redacao de segredo e
  comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-vsync-ticket` com `gui-submit-disabled`.
- `vsync_submitted`, `vsync_wait_submitted`, `vsync_fence_armed`,
  `schedule_submitted`, `present_submitted`, `damage_submitted`,
  `compositor_damage_submitted`, `frame_timer_armed`, `compositor_wake_submitted`
  e `page_flip_submitted` permanecem `0`; o contrato e declarativo e nao aguarda
  sincronizacao real.
- `vsync_allowed`, `vsync_ticket_selected`, `vsync_wait_allowed=0` e
  `vsync_fence_armed=0` preparam escalabilidade futura sem executar wait/fence,
  cache, compositor ou page flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
