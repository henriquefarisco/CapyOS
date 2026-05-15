# CapyOS 0.8.0-alpha.124+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional fortalecendo o
contrato do futuro loginwindow GUI para credenciais. A entrega permanece
fail-closed: a GUI pode expor estado apresentavel e politica auditavel, mas nao
substitui o login textual nem ativa submissao grafica de senha.

## Entregue

- Adicionado `struct login_window_credential_policy` com versao, limite maximo de
  senha, caractere de mascara, exigencia de mascara, wipe obrigatorio e flags de
  autoridade textual.
- Adicionada `login_window_credential_policy_from_contract()` como unidade pura
  derivada de `login_window_contract`.
- O view model do loginwindow passa a carregar politica de credenciais, estado de
  mascara, wipe obrigatorio, submit desabilitado e login textual autoritativo.
- O preview textual do loginwindow mostra explicitamente que o campo e mascarado,
  que o submit GUI continua desabilitado e que recuperacao permanece textual.
- Testes planejados cobrem contrato ausente, contrato pronto, recuperacao em
  modo manutencao e bloqueio de recuperacao quando o runtime esta incompleto,
  sem ativar autenticacao grafica.

## Segurança

- `password_submit_allowed` permanece sempre `0` neste alpha.
- `password_wipe_required` permanece sempre `1` mesmo sem contrato valido.
- Recuperacao grafica requer modo manutencao, input disponivel, runtime
  completo e callback textual de recuperacao; nao coleta segredo pela GUI.
- O login textual continua sendo o caminho autoritativo de autenticacao.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
