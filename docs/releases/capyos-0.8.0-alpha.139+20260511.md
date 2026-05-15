# CapyOS 0.8.0-alpha.139+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
snapshot seguro de tela de credenciais para a futura GUI do loginwindow. O
contrato compoe login view, sessao one-shot de credenciais e recuperacao textual
sem expor segredo, mascara ou comprimento.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_VIEW_MODEL_VERSION`.
- Adicionado `struct login_window_credential_screen_view_model`.
- Adicionada `login_window_credential_screen_view_model_build()` para compor
  login view, sessao segura de credenciais e view de recuperacao.
- Testes planejados cobrem tela pronta, recuperacao textual, resume pronto,
  sessao insegura, recuperacao insegura e login view com submit grafico indevido.

## Segurança

- O snapshot exige login textual autoritativo e bloqueia qualquer login view que
  tente habilitar submit grafico.
- A sessao de credenciais precisa estar redigida, sem exposicao e com storage
  limpo para a tela renderizar.
- Recuperacao insegura limpa acoes visiveis/habilitadas e falha fechado.
- `submit_visible` permanece `0`, `submit_blocked` permanece `1`,
  `submit_enabled` permanece `0` e `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado ou comprimento de
  senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
