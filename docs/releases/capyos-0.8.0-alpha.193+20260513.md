# CapyOS 0.8.0-alpha.193+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_compositor_plan` seguro da tela de credenciais do loginwindow. A nova
fatia consome somente `window_surface_plan` seguro e produz um ticket
declarativo de compositor de janela para a futura integracao com compositor,
damage tracking e display sem submeter compositor real, surface real, damage
real, window/GUI ou autenticacao pela GUI.

## Entregas

- `struct login_window_credential_screen_window_compositor_plan` consolida o
  contrato publico da fatia de compositor de janela da tela de credenciais.
- `login_window_credential_screen_window_compositor_plan_build()` cria uma
  camada pura sobre `login_window_credential_screen_window_surface_plan`,
  preservando apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-compositor-ticket`
  - `text-recovery-window-compositor-ticket`
  - `text-login-resume-window-compositor-ticket`
  - `text-login-fallback-window-compositor-ticket`
- O builder permanece fail-closed para window surface plan ausente, window
  surface plan inseguro, submit grafico e qualquer estado upstream que indique
  execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `surface_bound`, `surface_memory_mapped`,
  `surface_pixels_written`, `surface_compositor_submit_allowed`,
  `surface_compositor_submitted`, `surface_auth_submit_allowed`,
  `surface_auth_attempt_allowed`, `window_created`, `window_surface_bound`,
  `window_input_bound`, `gui_submitted`, estados reais de release/reclaim/
  compaction e qualquer submit real de compositor, surface ou damage.
- Preserva login textual como caminho autoritativo enquanto autenticacao grafica
  continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  compositor real, damage real, timers ou callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de campos
  seguros, mantendo custo constante e previsivel.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura planejada em `tests/test_login_runtime.c`
foi expandida para rotas seguras, fallback textual, upstream ausente, upstream
inseguro, origem window forjada e bloqueio de estados submetidos/efeitos reais
herdados de surface, window, GUI, release, reclaim e compaction.

## Limites

- Nao submete compositor real do loginwindow.
- Nao submete surface real, damage real, display/window/GUI ou callbacks de
  autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real ou
  vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
