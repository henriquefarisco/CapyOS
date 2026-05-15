# CapyOS 0.8.0-alpha.198+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_display_plan` seguro da tela de credenciais do loginwindow. A nova
fatia consome somente `window_scanout_plan` seguro e produz um ticket
declarativo de display de janela para futura integracao com o display
controller, output e pipeline sem anexar controlador real, submeter
display real, executar output real, submeter pipeline real, anexar buffer
real, submeter scanout real, executar display flip real, aguardar vsync
real, armar fence real, submeter wait real, agendar frame real, armar
timer real, acordar compositor, executar page flip, submeter schedule
real, present real, damage real, compositor real, surface real ou
autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_DISPLAY_PLAN_VERSION` consolida a
  versao publica da fatia.
- `struct login_window_credential_screen_window_display_plan` expoe o
  contrato publico declarativo de display de janela da tela de credenciais.
- `login_window_credential_screen_window_display_plan_build()` cria uma
  camada pura sobre `login_window_credential_screen_window_scanout_plan`,
  preservando apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-display-ticket`
  - `text-recovery-window-display-ticket`
  - `text-login-resume-window-display-ticket`
  - `text-login-fallback-window-display-ticket`
- O builder permanece fail-closed para window scanout plan ausente, window
  scanout plan inseguro, submit grafico, origem upstream forjada e qualquer
  estado upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `display_submitted`,
  `display_controller_attached`, `display_output_submitted`,
  `display_pipeline_submitted`, `scanout_submitted`,
  `scanout_buffer_attached`, `scanout_buffer_submitted`,
  `scanout_display_flip_allowed`, `scanout_display_flip_submitted`,
  `vsync_submitted`, `vsync_wait_submitted`, `vsync_fence_armed`,
  `schedule_submitted`, `frame_timer_armed`, `compositor_wake_submitted`,
  `page_flip_submitted`, `present_submitted`, `damage_submitted`, submits
  reais de compositor/surface/window/GUI, vinculos reais de
  surface/window/input, mapeamento de memoria, escrita de pixels e estados
  reais de release/reclaim/compaction.
- Preserva login textual como caminho autoritativo enquanto autenticacao
  grafica continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  display controller real, output real, pipeline real, scanout real,
  compositor real, present real, damage real, vsync real, timers, wait
  real, fences reais, page flip ou callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de
  campos seguros, mantendo custo constante e previsivel.
- Campos de display controller, output e pipeline ficam preparados para
  evolucao futura, mas `display_controller_attached`,
  `display_output_submitted` e `display_pipeline_submitted` permanecem `0`
  nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura em `tests/test_login_runtime.c` foi
expandida para widgets de credenciais, rotas textuais, fallback por submit
e acao desconhecida, upstream ausente/inseguro, origem upstream forjada e
bloqueio de estados submetidos/efeitos reais herdados de scanout, vsync,
schedule, present, damage, compositor, surface, window, GUI, release,
reclaim e compaction. As quatro funcoes de teste foram registradas em
`run_login_runtime_tests`.

## Limites

- Nao anexa display controller real, submete output real, submete pipeline
  real ao display do loginwindow.
- Nao anexa buffer real ao scanout, submete scanout real, executa display
  flip real, aguarda vsync real, arma fence real, submete wait real, agenda
  frame real, arma timer real, acorda compositor, executa page flip,
  submete vsync real, schedule real, present real, damage real, compositor
  real, surface real, display/window/GUI ou callbacks de autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real
  ou vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
