# CapyOS 0.8.0-alpha.201+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_commit_plan` seguro da tela de credenciais do loginwindow. A nova
fatia consome somente `window_blit_plan` seguro e produz um ticket declarativo
de commit atomico de janela para futura integracao com atomic modeset, page
flips coordenados, callbacks de frame e fila de KMS sem anexar estado real,
armar atomic commit real, armar callback de frame real, submeter callback
real, mapear buffer real, copiar pixels reais, armar DMA real, submeter blit
real, anexar conector real, armar modo real, armar sinal real, submeter
output real, submeter sinal real, anexar controlador real, submeter display
real, executar output real, submeter pipeline real, anexar buffer real,
submeter scanout real, executar display flip real, aguardar vsync real, armar
fence real, submeter vsync real, submeter wait real, agendar frame real,
armar timer real, acordar compositor, executar page flip, submeter schedule
real, apresentar frame real, submeter present real, enviar damage real,
submeter compositor real, surface real, window/GUI ou autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_COMMIT_PLAN_VERSION` consolida a
  versao publica da fatia.
- `struct login_window_credential_screen_window_commit_plan` expoe o contrato
  publico declarativo de commit atomico de janela da tela de credenciais.
- `login_window_credential_screen_window_commit_plan_build()` cria uma camada
  pura sobre `login_window_credential_screen_window_blit_plan`, preservando
  apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-commit-ticket`
  - `text-recovery-window-commit-ticket`
  - `text-login-resume-window-commit-ticket`
  - `text-login-fallback-window-commit-ticket`
- O builder permanece fail-closed para window blit plan ausente, window blit
  plan inseguro, submit grafico, origem upstream forjada e qualquer estado
  upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `commit_submitted`, `commit_state_attached`,
  `commit_state_submitted`, `commit_atomic_allowed`,
  `commit_atomic_submitted`, `commit_callback_armed`,
  `commit_callback_submitted`, `blit_submitted`, `blit_pixels_copied`,
  `blit_dma_submitted`, `output_submitted`, `output_signal_submitted`,
  `display_submitted`, `display_controller_attached`,
  `display_pipeline_submitted`, `scanout_submitted`,
  `scanout_buffer_attached`, `scanout_display_flip_submitted`,
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
  compositor real, present real, damage real, timers, wait real, fences
  reais, page flip, atomic commit real, callbacks de frame ou callbacks
  externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de
  campos seguros, mantendo custo constante e previsivel.
- Campos de commit atomico e callbacks de frame ficam preparados para
  evolucao futura, mas `commit_submitted`, `commit_state_attached`,
  `commit_state_submitted`, `commit_atomic_submitted` e
  `commit_callback_submitted` permanecem `0` nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura planejada em `tests/test_login_runtime.c`
foi expandida para widgets de credenciais, rotas textuais, fallback por
submit e acao desconhecida, upstream ausente/inseguro, origem upstream
forjada e bloqueio de estados submetidos/efeitos reais herdados de blit,
output, display, scanout, vsync, schedule, present, damage, compositor,
surface, window, GUI, release, reclaim e compaction.

## Limites

- Nao anexa estado real, arma atomic commit real, arma callback de frame
  real, submete callback real, mapeia buffer real, copia pixels reais ou arma
  DMA real do loginwindow.
- Nao submete blit real, output real, display real, scanout real, executa
  display flip real, aguarda vsync real, arma fence real, submete vsync real,
  submete wait real, agenda frame real, arma timer real, acorda compositor,
  executa page flip, submete schedule real, present real, damage real,
  compositor real, surface real, window/GUI ou callbacks de autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real
  ou vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
