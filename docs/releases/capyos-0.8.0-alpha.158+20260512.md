# CapyOS 0.8.0-alpha.158+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
present plan seguro para a tela de credenciais do loginwindow. O present plan
consome o damage plan seguro e publica somente um ticket declarativo de
apresentacao para futura integracao GUI/compositor, sem apresentar frame real,
sem enviar damage real, sem submeter compositor real, sem autenticar pela GUI e
sem carregar segredo, mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_PRESENT_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_present_plan`.
- Adicionada `login_window_credential_screen_present_plan_build()` para
  selecionar tickets `credential-screen-present-ticket`,
  `text-recovery-present-ticket`, `text-login-resume-present-ticket` ou
  `text-login-fallback-present-ticket`.
- Testes planejados cobrem apresentacao declarativa de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, damage plan
  ausente e damage plan inseguro.

## Segurança

- O present plan exige damage plan seguro, superficie autorizada mas nao
  submetida, damage planejado/autorizado mas nao submetido, damage ticket
  selecionado, rota selecionada, credenciais seguras, storage limpo, redacao de
  segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-present-ticket` com `gui-submit-disabled`.
- `present_submitted`, `damage_submitted` e `compositor_damage_submitted`
  permanecem `0`; o contrato e declarativo e nao apresenta frame real nem envia
  damage real.
- `present_incremental_allowed`, `present_cache_allowed`,
  `present_reuse_allowed`, `present_cache_hit=0` e `full_present_required`
  preparam escalabilidade futura sem executar cache/compositor real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
