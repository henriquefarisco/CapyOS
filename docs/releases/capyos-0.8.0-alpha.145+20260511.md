# CapyOS 0.8.0-alpha.145+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
controller seguro para a tela de credenciais do loginwindow. O controller consome
o plano seguro de rotas e publica somente decisoes finais de UI redigidas para a
futura janela grafica, sem carregar segredo, mascara, comprimento ou snapshots
internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_CONTROLLER_VERSION`.
- Adicionado `struct login_window_credential_screen_controller`.
- Adicionada `login_window_credential_screen_controller_build()` para decidir
  foco de credencial, abertura de recuperacao textual, retomada do login textual
  ou fallback forçado para login textual.
- Testes planejados cobrem foco de edicao, recuperacao textual, resume textual,
  submit bloqueado, acao desconhecida, route plan ausente e route plan inseguro.

## Segurança

- O controller exige route plan seguro, rota selecionada, credenciais seguras,
  storage limpo, redacao de segredo/comprimento, ausencia de exposicao
  bruta/mascarada, submit bloqueado/desabilitado e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para `force-text-login` com
  `gui-submit-disabled`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
