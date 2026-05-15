# CapyOS 0.8.0-alpha.126+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional preparando o buffer
efemero de credenciais do futuro loginwindow. A entrega segue fail-closed: o
buffer pode armazenar entrada em storage fornecido pelo chamador, mas so produz
texto mascarado, bloqueia overflow, exige wipe e nao habilita submit ou
autenticacao grafica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_BUFFER_VERSION`.
- Adicionado `struct login_window_credential_buffer` para representar storage,
  limite efetivo, mascara, estado de wipe e motivo de bloqueio.
- Adicionadas APIs puras de buffer: init, append, backspace, masked text e wipe.
- `login_window_credential_buffer_init()` limpa storage recebido antes de validar
  politica e falha fechado quando politica, storage ou limite nao sao seguros.
- Overflow preserva o conteudo anterior, marca `overflow_blocked` e retorna
  motivo estavel `max-password-chars`.
- Testes planejados cobrem politica ausente, limpeza de storage antigo, mascara,
  backspace, overflow fechado e wipe completo.

## Segurança

- Submit grafico permanece desabilitado no buffer mesmo se a politica futura for
  expandida.
- Sem politica segura, o buffer nao inicializa e zera storage recebido.
- O texto apresentavel e sempre mascarado; o valor bruto permanece interno ao
  storage fornecido e deve ser apagado por `login_window_credential_buffer_wipe()`.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
