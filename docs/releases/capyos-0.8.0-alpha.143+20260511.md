# CapyOS 0.8.0-alpha.143+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um evento
UI seguro para a tela de credenciais do loginwindow. O evento consome o action
plan seguro e registra uma classificacao auditavel da intencao da futura GUI sem
carregar segredo, mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_UI_EVENT_VERSION`.
- Adicionado `struct login_window_credential_screen_ui_event`.
- Adicionada `login_window_credential_screen_ui_event_build()` para classificar
  edit/focus, recuperacao textual, resume textual, submit bloqueado, fallback
  textual, acao bloqueada, action plan ausente e action plan inseguro.
- Testes planejados cobrem edit/focus, recovery, resume, submit bloqueado, acao
  desconhecida, action plan ausente e action plan inseguro.

## Segurança

- O evento exige action plan seguro, sessao de credenciais segura, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado e login textual autoritativo.
- Submit grafico permanece bloqueado como `credential-screen-submit-blocked` e
  `gui-submit-disabled`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
