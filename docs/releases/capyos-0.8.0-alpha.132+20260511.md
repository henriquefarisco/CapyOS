# CapyOS 0.8.0-alpha.132+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
pipeline seguro de interacao de credenciais para o futuro loginwindow GUI. A
interacao aplica exatamente uma acao de input e reconstrói o painel mascarado em
um unico snapshot auditavel, sem retornar segredo bruto nem habilitar
autenticacao grafica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_INTERACTION_VERSION`.
- Adicionado `struct login_window_credential_interaction` para agrupar input,
  painel, wipe, mascara, estado e razao de bloqueio.
- Adicionada `login_window_credential_interaction_step()` para aplicar append,
  submit, cancel ou acoes bloqueadas e reconstruir o painel mascarado.
- Submit e cancel passam pelo mesmo pipeline e refletem wipe obrigatorio.
- Politica ausente e acao desconhecida geram snapshot auditavel sem mutar buffer.
- Testes planejados cobrem append, submit, cancel, politica ausente e acao
  desconhecida.

## Segurança

- `submit_allowed` e `auth_attempt_allowed` permanecem sempre `0`.
- O pipeline usa o painel/field view mascarados e nao retorna storage bruto.
- O output mascarado e limpo antes da aplicacao da interacao.
- Mesmo falhas de politica/input produzem estado fail-closed auditavel.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
