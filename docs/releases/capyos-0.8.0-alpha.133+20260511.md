# CapyOS 0.8.0-alpha.133+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
snapshot de prontidao de credenciais para o futuro loginwindow GUI. O snapshot
resume politica, buffer, painel e interacao em flags auditaveis de renderizacao,
input, mascara, wipe, overflow e bloqueio de autenticacao grafica, sem expor
segredo bruto.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_READINESS_VERSION`.
- Adicionado `struct login_window_credential_readiness` para representar
  prontidao segura de credenciais.
- Adicionada `login_window_credential_readiness_evaluate()` para consolidar
  politica, buffer, painel e interacao em um snapshot fail-closed.
- O snapshot diferencia renderizacao, input e texto mascarado prontos sem abrir
  submit grafico.
- Testes planejados cobrem painel pronto, submit bloqueado com wipe, politica
  ausente e overflow bloqueado.

## Segurança

- `submit_blocked` permanece ativo e `submit_allowed`/`auth_attempt_allowed`
  permanecem sempre `0`.
- Nenhum storage bruto de credencial e lido ou exposto pelo snapshot.
- Politica ausente, buffer indisponivel, overflow e mascara truncada falham
  fechado com motivos estaveis.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
