# CapyOS 0.8.0-alpha.205+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_input_plan` seguro da tela de credenciais do loginwindow. A nova fatia
consome somente `window_event_plan` seguro e produz um ticket declarativo de
entrada de janela para futura integracao com teclado, pointer, foco, keymap,
decoder, roteamento, callbacks e grab sem armar teclado real, submeter teclado,
armar pointer real, submeter pointer, armar foco real, submeter foco, carregar
keymap real, submeter keymap, decodificar input real, rotear input real, armar
callback de input, submeter callback de input, permitir grab real, submeter
grab, armar handler real, armar fila real, despachar evento real, capturar
timestamp real, completar frame real, anexar buffer real, armar vblank real,
submeter flip async, anexar estado real, armar atomic commit real, mapear
buffer real, copiar pixels reais, armar DMA, submeter blit real, anexar
conector real, armar modo real, armar sinal real, submeter output real, anexar
controlador real, submeter display real, submeter scanout real, executar
display flip real, aguardar vsync real, armar fence real, submeter vsync real,
submeter wait real, agendar frame real, armar timer real, acordar compositor,
executar page flip, submeter schedule real, apresentar frame real, submeter
present real, enviar damage real, submeter compositor real, submeter surface
real, submeter window/GUI ou autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_INPUT_PLAN_VERSION` consolida a versao
  publica da fatia.
- `struct login_window_credential_screen_window_input_plan` expoe o contrato
  publico declarativo de input de janela da tela de credenciais.
- `login_window_credential_screen_window_input_plan_build()` cria uma camada
  pura sobre `login_window_credential_screen_window_event_plan`, preservando
  apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-input-ticket`
  - `text-recovery-window-input-ticket`
  - `text-login-resume-window-input-ticket`
  - `text-login-fallback-window-input-ticket`
- O builder permanece fail-closed para window event plan ausente, window event
  plan inseguro, submit grafico, origem upstream forjada e qualquer estado
  upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `input_submitted`, `input_keyboard_armed`,
  `input_keyboard_submitted`, `input_pointer_armed`, `input_pointer_submitted`,
  `input_focus_armed`, `input_focus_submitted`, `input_keymap_loaded`,
  `input_keymap_submitted`, `input_decode_submitted`, `input_route_submitted`,
  `input_callback_armed`, `input_callback_submitted`, `input_grab_allowed`,
  `input_grab_submitted`, `input_error`, `event_submitted`,
  `event_handler_armed`, `event_handler_submitted`, `event_queue_armed`,
  `event_queue_submitted`, `event_dispatch_submitted`, `event_callback_armed`,
  `event_callback_submitted`, `event_timestamp_captured`,
  `event_timestamp_submitted`, `event_frame_completed`,
  `event_frame_submitted`, e submits reais herdados de vblank, flip, commit,
  blit, output, display, scanout, vsync, schedule, present, damage,
  compositor, surface, window e GUI.
- Preserva login textual como caminho autoritativo enquanto autenticacao
  grafica continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  driver de teclado real, driver de pointer real, foco real, keymap real,
  decoder real, dispatcher real de input, callback real, grab real, DRM/KMS
  real, DMA real, compositor real, present real, damage real, timers, wait
  real, fences reais, page flip real ou atomic commit real.
- A validacao ocorre por predicados booleanos locais e copia seletiva de
  campos seguros, mantendo custo constante e previsivel.
- Campos de keyboard, pointer, focus, keymap, decode, route, callback e grab
  ficam preparados para evolucao futura, mas `input_keyboard_armed`,
  `input_pointer_armed`, `input_focus_armed`, `input_keymap_loaded`,
  `input_decode_submitted`, `input_route_submitted`, `input_callback_armed`,
  `input_grab_allowed` e `input_grab_submitted` permanecem `0` nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura estatica em `tests/test_login_runtime.c`
foi expandida com quatro novos testes para widgets de credenciais, rotas
textuais, fallback por submit e acao desconhecida, upstream ausente/inseguro,
origem upstream forjada e bloqueio de estados submetidos/efeitos reais
herdados de event, vblank, flip, commit, blit, output, display, scanout,
vsync, schedule, present, damage, compositor, surface, window, GUI, release,
reclaim e compaction.

## Limites

- Nao arma teclado real, pointer real, foco real, keymap real, decoder real,
  roteador real, callback real ou grab real do loginwindow.
- Nao arma handler real, fila real, despacha evento real, arma callback real,
  submete callback, captura timestamp real, submete timestamp, completa frame
  real, submete frame, arma evento de vblank real, anexa buffer real, arma
  vblank real, arma evento real, submete flip async, anexa estado real, arma
  atomic commit real, arma callback de frame real, submete callback real,
  mapeia buffer real, copia pixels reais, arma DMA real, submete blit real,
  anexa conector real, arma modo real, arma sinal real, submete output real,
  anexa controlador real, submete display real, executa output real, anexa
  buffer de scanout real, submete scanout real, executa display flip real,
  aguarda vsync real, arma fence real, submete vsync real, submete wait real,
  agenda frame real, arma timer real, acorda compositor, executa page flip
  real, submete schedule real, present real, damage real, compositor real,
  surface real, display/window/GUI ou callbacks de autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real
  ou vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
