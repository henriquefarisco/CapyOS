# CapyOS 0.8.0-alpha.140+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando uma
sessao one-shot segura da tela de credenciais para a futura GUI do loginwindow. O
contrato compoe runtime, login view, sessao de credenciais, recuperacao textual e
snapshot final de tela sem expor segredo, mascara ou comprimento.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_SESSION_VERSION`.
- Adicionado `struct login_window_credential_screen_session`.
- Adicionada `login_window_credential_screen_session_build()` para orquestrar
  contrato, login view, politica, credenciais, resume, recuperacao e tela final.
- Testes planejados cobrem tela pronta, recuperacao textual, resume pronto,
  storage ausente e `ops` ausente com limpeza de IO.

## Segurança

- A funcao usa snapshots intermediarios apenas como variaveis locais e exporta
  somente flags redigidas, titulo, estado, mensagem e motivo de bloqueio.
- Storage e scratch fornecidos sao limpos antes de compor a tela e novamente pela
  sessao de credenciais quando aplicavel.
- `submit_visible` permanece `0`, `submit_blocked` permanece `1`,
  `submit_enabled` permanece `0` e `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
