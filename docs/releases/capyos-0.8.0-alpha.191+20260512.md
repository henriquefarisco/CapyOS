# CapyOS 0.8.0-alpha.191+20260512

Data: 2026-05-12
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_plan` seguro da tela de credenciais do loginwindow. A nova fatia consome
somente `gui_plan` seguro e produz um ticket declarativo de janela para a futura
integracao com compositor/window manager sem criar janela real, vincular surface,
vincular input, escrever pixels, submeter GUI/window ou autenticar pela GUI.

## Entregas

- `struct login_window_credential_screen_window_plan` consolida o contrato
  publico da fatia de janela da tela de credenciais.
- `login_window_credential_screen_window_plan_build()` cria uma camada pura sobre
  `login_window_credential_screen_gui_plan`, preservando apenas campos seguros e
  redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-ticket`
  - `text-recovery-window-ticket`
  - `text-login-resume-window-ticket`
  - `text-login-fallback-window-ticket`
- O builder permanece fail-closed para GUI plan ausente, GUI plan inseguro,
  submit grafico e qualquer estado upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`, `length_redacted=1`,
  `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `gui_submitted`, `gui_pixels_written`,
  `gui_auth_submit_allowed`, `gui_auth_attempt_allowed`, estados reais de
  release/reclaim/compaction e qualquer criacao/vinculo real de janela.
- Preserva login textual como caminho autoritativo enquanto autenticacao grafica
  continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  compositor real, timers ou callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de campos
  seguros, mantendo custo constante e previsivel.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura planejada em `tests/test_login_runtime.c`
foi expandida para rotas seguras, fallback textual, upstream ausente, upstream
inseguro e bloqueio de estados submetidos/efeitos reais.

## Limites

- Nao cria janela real do loginwindow.
- Nao vincula surface, input, compositor, display ou callbacks de autenticacao.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
