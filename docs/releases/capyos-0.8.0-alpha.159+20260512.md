# CapyOS 0.8.0-alpha.159+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
schedule plan seguro para a tela de credenciais do loginwindow. O schedule plan
consome o present plan seguro e publica somente um ticket declarativo de
agendamento para futura integracao GUI/compositor, sem apresentar frame real,
sem armar timer, sem fazer page flip, sem enviar damage real, sem submeter
compositor real, sem autenticar pela GUI e sem carregar segredo, mascara,
comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_SCHEDULE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_schedule_plan`.
- Adicionada `login_window_credential_screen_schedule_plan_build()` para
  selecionar tickets `credential-screen-schedule-ticket`,
  `text-recovery-schedule-ticket`, `text-login-resume-schedule-ticket` ou
  `text-login-fallback-schedule-ticket`.
- Testes planejados cobrem agendamento declarativo de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, present plan
  ausente, present plan inseguro e tentativa de propagar estado unsafe ja
  submetido.

## Segurança

- O schedule plan exige present plan seguro, apresentacao autorizada mas nao
  submetida, damage/compositor nao submetidos, present ticket selecionado, rota
  selecionada, credenciais seguras, storage limpo, redacao de segredo e
  comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-schedule-ticket` com `gui-submit-disabled`.
- `schedule_submitted`, `present_submitted`, `damage_submitted`,
  `compositor_damage_submitted`, `frame_timer_armed`, `compositor_wake_submitted`
  e `page_flip_submitted` permanecem `0`; o contrato e declarativo e nao agenda
  frame real.
- `schedule_incremental_allowed`, `schedule_cache_allowed`,
  `schedule_reuse_allowed`, `schedule_cache_hit=0` e `full_schedule_required`
  preparam escalabilidade futura sem executar agendamento, cache ou compositor
  real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
