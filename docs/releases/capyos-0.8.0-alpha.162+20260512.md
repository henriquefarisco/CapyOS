# CapyOS 0.8.0-alpha.162+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
display plan seguro para a tela de credenciais do loginwindow. O display plan
consome o scanout plan seguro e publica somente um ticket declarativo final de
display para futura integracao GUI/compositor/display, sem anexar buffer real,
sem submeter display, sem commitar modo, sem fazer flip, sem enviar damage real,
sem submeter compositor real, sem autenticar pela GUI e sem carregar segredo,
mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_DISPLAY_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_display_plan`.
- Adicionada `login_window_credential_screen_display_plan_build()` para
  selecionar tickets `credential-screen-display-ticket`,
  `text-recovery-display-ticket`, `text-login-resume-display-ticket` ou
  `text-login-fallback-display-ticket`.
- Testes planejados cobrem display declarativo de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, scanout plan
  ausente, scanout plan inseguro e tentativa de propagar estado unsafe ja
  submetido.

## Segurança

- O display plan exige scanout plan seguro, scanout autorizado mas nao submetido,
  nenhum buffer anexado/submetido, modo de display nao commitado,
  vsync/schedule/present/damage/compositor nao submetidos, rota selecionada,
  credenciais seguras, storage limpo, redacao de segredo e comprimento,
  ausencia de exposicao bruta/mascarada, submit bloqueado/desabilitado,
  callbacks de submit/auth zerados e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-display-ticket` com `gui-submit-disabled`.
- `display_submitted`, `display_buffer_attached`, `display_buffer_submitted`,
  `display_mode_committed`, `display_flip_submitted`, `scanout_submitted`,
  `scanout_buffer_attached`, `scanout_buffer_submitted`, `vsync_submitted`,
  `schedule_submitted`, `present_submitted`, `damage_submitted`,
  `compositor_damage_submitted`, `frame_timer_armed`, `compositor_wake_submitted`
  e `page_flip_submitted` permanecem `0`; o contrato e declarativo e nao toca
  display real.
- `display_allowed`, `display_ticket_selected` e `display_target_selected`
  preparam escalabilidade futura sem executar buffer attach, display submit,
  mode commit, compositor ou flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
