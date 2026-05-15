# CapyOS 0.8.0-alpha.144+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um plano
seguro de rotas para a tela de credenciais do loginwindow. O plano consome o
evento UI seguro e traduz a intencao validada em navegacao redigida para a futura
GUI sem carregar segredo, mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_ROUTE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_route_plan`.
- Adicionada `login_window_credential_screen_route_plan_build()` para escolher
  rotas `stay-on-credential-screen`, `open-text-recovery`, `resume-text-login`
  ou `force-text-login`.
- Testes planejados cobrem edicao/stay, recovery, resume, submit bloqueado, acao
  desconhecida, evento ausente e evento inseguro.

## Segurança

- O plano exige evento UI seguro, sessao de credenciais segura, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado e login textual autoritativo.
- Submit grafico e acoes desconhecidas convergem para `force-text-login`, sem
  habilitar foco de input, submit ou tentativa de autenticacao.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
