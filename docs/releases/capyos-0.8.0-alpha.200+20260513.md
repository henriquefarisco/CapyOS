# CapyOS 0.8.0-alpha.200+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_blit_plan` seguro da tela de credenciais do loginwindow. A nova fatia
consome somente `window_output_plan` seguro e produz um ticket declarativo de
blit de janela para futura integracao com mapeamento de framebuffer, copia de
pixels, DMA e sincronizacao com scanout, sem mapear buffer real, copiar pixels
reais, armar DMA real, submeter blit real, anexar conector real, armar modo
real, armar sinal real, submeter output real, submeter sinal real, anexar
controlador real, submeter display real, executar output real, submeter
pipeline real, anexar buffer real, submeter scanout real, executar display
flip real, aguardar vsync real, armar fence real, submeter vsync real, submeter
wait real, agendar frame real, armar timer real, acordar compositor, executar
page flip, submeter schedule real, apresentar frame real, submeter present
real, enviar damage real, submeter compositor real, surface real, window/GUI ou
autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_BLIT_PLAN_VERSION` consolida a versao
  publica da fatia.
- `struct login_window_credential_screen_window_blit_plan` expoe o contrato
  publico declarativo de blit de janela da tela de credenciais.
- `login_window_credential_screen_window_blit_plan_build()` cria uma camada
  pura sobre `login_window_credential_screen_window_output_plan`, preservando
  apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-blit-ticket`
  - `text-recovery-window-blit-ticket`
  - `text-login-resume-window-blit-ticket`
  - `text-login-fallback-window-blit-ticket`
- O builder permanece fail-closed para window output plan ausente, window
  output plan inseguro, submit grafico, origem upstream forjada e qualquer
  estado upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `blit_submitted`, `blit_source_buffer_mapped`,
  `blit_destination_buffer_mapped`, `blit_pixels_copied`, `blit_dma_allowed`,
  `blit_dma_submitted`, `output_submitted`,
  `output_connector_attached`, `output_connector_submitted`,
  `output_mode_attached`, `output_mode_submitted`, `output_signal_armed`,
  `output_signal_submitted`, `display_submitted`,
  `display_controller_attached`, `display_controller_submitted`,
  `display_output_attached`, `display_output_submitted`,
  `display_pipeline_attached`, `display_pipeline_submitted`,
  `scanout_submitted`, `scanout_buffer_attached`, `scanout_buffer_submitted`,
  `scanout_display_flip_allowed`, `scanout_display_flip_submitted`,
  `vsync_submitted`, `vsync_wait_submitted`, `vsync_fence_armed`,
  `schedule_submitted`, `frame_timer_armed`, `compositor_wake_submitted`,
  `page_flip_submitted`, submits reais de compositor/surface/window/GUI,
  vinculos reais de surface/window/input, mapeamento de memoria, escrita de
  pixels e estados reais de release/reclaim/compaction.
- Preserva login textual como caminho autoritativo enquanto autenticacao grafica
  continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  compositor real, present real, damage real, timers, wait real, fences reais,
  page flip, scanout real, display flip real, output real, blit real ou
  callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de campos
  seguros, mantendo custo constante e previsivel.
- Campos de blit, buffer mapping, DMA e pixel copy ficam preparados para
  evolucao futura, mas `blit_submitted`, `blit_source_buffer_mapped`,
  `blit_destination_buffer_mapped`, `blit_pixels_copied`, `blit_dma_allowed` e
  `blit_dma_submitted` permanecem `0` nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura planejada em `tests/test_login_runtime.c`
foi expandida para widgets de credenciais, rotas textuais, fallback por submit e
acao desconhecida, upstream ausente/inseguro, origem upstream forjada e bloqueio
de estados submetidos/efeitos reais herdados de output, display, scanout,
vsync, schedule, present, damage, compositor, surface, window, GUI, release,
reclaim e compaction.

## Limites

- Nao mapeia buffer real, copia pixels reais, permite DMA real, submete DMA
  real ou submete blit real do loginwindow.
- Nao anexa conector real, arma modo real, arma sinal real, submete output
  real, submete sinal real.
- Nao anexa controlador real, submete display real, executa output real,
  submete pipeline real, anexa buffer real, submete scanout real, executa
  display flip real, aguarda vsync real, arma fence real, submete vsync real,
  submete wait real, agenda frame real, arma timer real, acorda compositor,
  executa page flip, submete schedule real, present real, damage real,
  compositor real, surface real, display/window/GUI ou callbacks de
  autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real ou
  vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
