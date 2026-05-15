# CapyOS 0.8.0-alpha.196+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_vsync_plan` seguro da tela de credenciais do loginwindow. A nova fatia
consome somente `window_schedule_plan` seguro e produz um ticket declarativo de
sincronizacao de janela para futura integracao com frame pacing, fences,
compositor e display sem aguardar vsync real, armar fence real, submeter vsync
real, submeter wait real, agendar frame real, armar timer real, acordar
compositor, executar page flip, submeter schedule real, apresentar frame real,
submeter present real, enviar damage real, submeter compositor real, surface
real, window/GUI ou autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_VSYNC_PLAN_VERSION` consolida a versao
  publica da fatia.
- `struct login_window_credential_screen_window_vsync_plan` expoe o contrato
  publico declarativo de sincronizacao de janela da tela de credenciais.
- `login_window_credential_screen_window_vsync_plan_build()` cria uma camada
  pura sobre `login_window_credential_screen_window_schedule_plan`, preservando
  apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-vsync-ticket`
  - `text-recovery-window-vsync-ticket`
  - `text-login-resume-window-vsync-ticket`
  - `text-login-fallback-window-vsync-ticket`
- O builder permanece fail-closed para window schedule plan ausente, window
  schedule plan inseguro, submit grafico, origem upstream forjada e qualquer
  estado upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `vsync_submitted`, `vsync_wait_submitted`,
  `vsync_fence_armed`, `schedule_submitted`, `frame_timer_armed`,
  `compositor_wake_submitted`, `page_flip_submitted`,
  `schedule_auth_submit_allowed`, `schedule_auth_attempt_allowed`,
  `present_submitted`, `present_auth_submit_allowed`,
  `present_auth_attempt_allowed`, `damage_submitted`, submits reais de
  compositor/surface/window/GUI, vinculos reais de surface/window/input,
  mapeamento de memoria, escrita de pixels e estados reais de
  release/reclaim/compaction.
- Preserva login textual como caminho autoritativo enquanto autenticacao grafica
  continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  compositor real, present real, damage real, timers, wait real, fences reais,
  page flip ou callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de campos
  seguros, mantendo custo constante e previsivel.
- Campos de vsync, fence e frame pacing ficam preparados para evolucao futura,
  mas `vsync_submitted`, `vsync_wait_submitted` e `vsync_fence_armed`
  permanecem `0` nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura planejada em `tests/test_login_runtime.c`
foi expandida para widgets de credenciais, rotas textuais, fallback por submit e
acao desconhecida, upstream ausente/inseguro, origem upstream forjada e bloqueio
de estados submetidos/efeitos reais herdados de schedule, present, damage,
compositor, surface, window, GUI, release, reclaim e compaction.

## Limites

- Nao aguarda vsync real do loginwindow.
- Nao arma fence real, submete wait real, agenda frame real, arma timer real,
  acorda compositor, executa page flip, submete vsync real, schedule real,
  present real, damage real, compositor real, surface real, display/window/GUI
  ou callbacks de autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real ou
  vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
