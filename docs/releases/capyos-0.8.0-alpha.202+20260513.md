# CapyOS 0.8.0-alpha.202+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_flip_plan` seguro da tela de credenciais do loginwindow. A nova fatia
consome somente `window_commit_plan` seguro e produz um ticket declarativo de
page flip de janela para futura integracao com DRM/KMS, compositor atomic e
display sem anexar buffer real, armar vblank real, armar evento real, submeter
flip async, anexar estado real, armar atomic commit real, armar callback de
frame real, submeter callback real, mapear buffer real, copiar pixels reais,
armar DMA real, submeter blit real, anexar conector real, armar modo real, armar
sinal real, submeter output real, submeter sinal real, anexar controlador real,
submeter display real, executar output real, submeter pipeline real, anexar
buffer real, submeter scanout real, executar display flip real, aguardar vsync
real, armar fence real, submeter wait real, agendar frame real, armar timer
real, acordar compositor, executar page flip, submeter schedule real,
apresentar frame real, submeter present real, enviar damage real, submeter
compositor real, surface real, window/GUI ou autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_FLIP_PLAN_VERSION` consolida a versao
  publica da fatia.
- `struct login_window_credential_screen_window_flip_plan` expoe o contrato
  publico declarativo de page flip de janela da tela de credenciais.
- `login_window_credential_screen_window_flip_plan_build()` cria uma camada
  pura sobre `login_window_credential_screen_window_commit_plan`, preservando
  apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-flip-ticket`
  - `text-recovery-window-flip-ticket`
  - `text-login-resume-window-flip-ticket`
  - `text-login-fallback-window-flip-ticket`
- O builder permanece fail-closed para window commit plan ausente, window
  commit plan inseguro, submit grafico, origem upstream forjada e qualquer
  estado upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `flip_submitted`, `flip_buffer_attached`,
  `flip_buffer_submitted`, `flip_vblank_armed`, `flip_vblank_submitted`,
  `flip_event_armed`, `flip_event_submitted`, `flip_async_allowed`,
  `flip_async_submitted`, `commit_submitted`, `commit_state_attached`,
  `commit_state_submitted`, `commit_atomic_allowed`, `commit_atomic_submitted`,
  `commit_callback_armed`, `commit_callback_submitted`, `blit_submitted`,
  `blit_dma_allowed`, `blit_dma_submitted`, `output_submitted`,
  `display_submitted`, `scanout_submitted`, `vsync_submitted`,
  `schedule_submitted`, `frame_timer_armed`, `page_flip_submitted`,
  `present_submitted`, `damage_submitted` e submits reais de
  compositor/surface/window/GUI.
- Preserva login textual como caminho autoritativo enquanto autenticacao grafica
  continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  DRM/KMS real, DMA real, compositor real, present real, damage real, timers,
  wait real, fences reais, page flip real, atomic commit ou callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de campos
  seguros, mantendo custo constante e previsivel.
- Campos de buffer, vblank, event e async ficam preparados para evolucao
  futura, mas `flip_buffer_attached`, `flip_vblank_armed`, `flip_event_armed` e
  `flip_async_allowed` permanecem `0` nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura estatica em `tests/test_login_runtime.c`
foi expandida com quatro novos testes para widgets de credenciais, rotas
textuais, fallback por submit e acao desconhecida, upstream ausente/inseguro,
origem upstream forjada e bloqueio de estados submetidos/efeitos reais herdados
de commit, blit, output, display, scanout, vsync, schedule, present, damage,
compositor, surface, window, GUI, release, reclaim e compaction.

## Limites

- Nao anexa buffer real de page flip do loginwindow.
- Nao arma vblank real, armar evento real, submeter flip async, anexar estado
  real, armar atomic commit real, armar callback de frame real, submeter
  callback real, mapear buffer real, copiar pixels reais, armar DMA real,
  submeter blit real, anexar conector real, armar modo real, armar sinal real,
  submeter output real, anexar controlador real, submeter display real,
  executar output real, anexar buffer de scanout real, submeter scanout real,
  executar display flip real, aguardar vsync real, armar fence real, submeter
  wait real, agendar frame real, armar timer real, acordar compositor, executar
  page flip real, submeter schedule real, present real, damage real,
  compositor real, surface real, display/window/GUI ou callbacks de
  autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real ou
  vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
