# CapyOS 0.8.0-alpha.125+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional endurecendo a saida
da recuperacao textual para o login normal. A transicao agora passa por uma
politica auditavel e fail-closed antes de resetar sessao, rerenderizar a tela de
login e permitir que o login textual volte a assumir.

## Entregue

- Adicionado `struct login_recovery_resume_policy` e
  `LOGIN_RECOVERY_RESUME_POLICY_VERSION`.
- Adicionada `login_recovery_resume_policy_evaluate()` para derivar se a
  recuperacao pode retornar ao login normal.
- O runtime de login agora exige sessao de recuperacao ativa, pedido consumido,
  runtime pronto e modo manutencao limpo antes de sair da recuperacao.
- Pedido de retorno bloqueado permanece na recuperacao e imprime o motivo
  estavel, como `maintenance-mode-active`.
- Testes planejados cobrem pedido ausente, manutencao ainda ativa, manutencao
  limpa, reset/rerender obrigatorios e bloqueio do login normal quando a
  manutencao nao foi limpa.

## Segurança

- A flag de pedido de retorno e consumida antes da avaliacao para evitar reuso
  implicito em loops futuros.
- A transicao falha fechado quando o runtime esta incompleto ou quando o modo
  manutencao ainda esta ativo.
- O login normal so retoma depois de reset de sessao, desativacao da sessao atual
  e rerender da tela de login.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
