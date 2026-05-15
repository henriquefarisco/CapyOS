# CapyOS 0.8.0-alpha.156+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
compositor plan seguro para a tela de credenciais do loginwindow. O compositor
plan consome o surface plan seguro e publica somente um ticket final declarativo
para futura integracao GUI/compositor, sem submeter superficie real, sem enviar
damage real ao compositor, sem autenticar pela GUI e sem carregar segredo,
mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPOSITOR_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_compositor_plan`.
- Adicionada `login_window_credential_screen_compositor_plan_build()` para
  selecionar tickets `credential-screen-compositor-ticket`,
  `text-recovery-compositor-ticket`, `text-login-resume-compositor-ticket` ou
  `text-login-fallback-compositor-ticket`.
- Testes planejados cobrem composicao declarativa de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, surface plan
  ausente e surface plan inseguro.

## Segurança

- O compositor plan exige surface plan seguro, superficie autorizada mas nao
  submetida, damage planejado mas nao submetido, ticket selecionado, rota
  selecionada, credenciais seguras, storage limpo, redacao de segredo/comprimento,
  ausencia de exposicao bruta/mascarada, submit bloqueado/desabilitado, callbacks
  de submit/auth zerados e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-compositor-ticket` com `gui-submit-disabled`.
- `compositor_surface_submitted` e `compositor_damage_submitted` permanecem `0`;
  o contrato e declarativo e nao submete superficie real nem damage real.
- `compositor_reuse_allowed`, `compositor_cache_allowed`,
  `compositor_damage_allowed` e `compositor_cache_hit=0` preparam escalabilidade
  futura sem executar cache/compositor real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
