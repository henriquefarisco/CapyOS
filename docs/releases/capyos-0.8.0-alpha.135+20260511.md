# CapyOS 0.8.0-alpha.135+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um view
model seguro de credenciais para a futura GUI do loginwindow. O view model compoe
prontidao e auditoria redigida em flags de renderizacao, input, cancelamento,
wipe, fallback e mensagens, sem expor segredo bruto, texto mascarado ou
comprimento da credencial.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_VIEW_MODEL_VERSION`.
- Adicionado `struct login_window_credential_view_model` para representar estado
  consumivel pela futura GUI de credenciais.
- Adicionada `login_window_credential_view_model_build()` para compor readiness
  e auditoria redigida.
- O view model renderiza apenas quando readiness e auditoria segura estao
  disponiveis.
- Testes planejados cobrem estado pronto redigido, submit bloqueado, input
  bloqueado e falha fechada sem auditoria segura.

## Segurança

- O view model nao carrega storage bruto, texto mascarado nem comprimento de
  senha.
- Auditoria nao redigida bloqueia renderizacao com `credential-audit-unsafe`.
- Submit grafico permanece invisivel, desabilitado e bloqueado.
- `auth_attempt_allowed` permanece sempre `0`; o login textual continua
  autoritativo.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
