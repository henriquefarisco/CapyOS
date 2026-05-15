# CapyOS 0.8.0-alpha.130+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
snapshot mascarado para o futuro campo de credenciais do loginwindow. A view
prepara a leitura segura pela GUI expondo somente mascara, estado e metadados
fail-closed, sem retornar segredo bruto nem habilitar autenticacao grafica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_FIELD_VIEW_VERSION`.
- Adicionado `struct login_window_credential_field_view` para representar estado
  seguro do campo de credenciais.
- Adicionada `login_window_credential_field_view_build()` para gerar texto
  mascarado em buffer fornecido pelo chamador.
- A view expõe politica, disponibilidade do buffer, inicializacao, presenca de
  segredo, comprimento, limite, truncamento, wipe e autoridade textual.
- Estados `empty` e `filled` sao derivados sem vazar storage bruto.
- Testes planejados cobrem buffer preenchido, buffer vazio, mascara truncada,
  politica ausente, buffer nao mascarado e overflow.

## Segurança

- Nenhum byte bruto da credencial e exposto pela view.
- `submit_allowed` e `auth_attempt_allowed` permanecem sempre `0`.
- Output ausente, politica insegura, buffer indisponivel, buffer nao mascarado e
  overflow falham fechado com motivos estaveis.
- O builder limpa o output mascarado antes de validar entradas.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
