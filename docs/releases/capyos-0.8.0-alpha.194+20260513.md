# CapyOS 0.8.0-alpha.194+20260513

Data: 2026-05-13
Canal: alpha
Trilha: UEFI/GPT/x86_64

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional com o
`window_present_plan` seguro da tela de credenciais do loginwindow. A nova fatia
consome somente `window_damage_plan` seguro e produz um ticket declarativo de
apresentacao de janela para a futura integracao com compositor, present e
display sem apresentar frame real, submeter present real, enviar damage real,
submeter compositor real, surface real, window/GUI ou autenticacao pela GUI.

## Entregas

- `LOGIN_WINDOW_CREDENTIAL_SCREEN_WINDOW_PRESENT_PLAN_VERSION` consolida a versao
  publica da fatia.
- `struct login_window_credential_screen_window_present_plan` expõe o contrato
  publico declarativo de apresentacao de janela da tela de credenciais.
- `login_window_credential_screen_window_present_plan_build()` cria uma camada
  pura sobre `login_window_credential_screen_window_damage_plan`, preservando
  apenas campos seguros e redigidos.
- Tickets declarativos selecionados:
  - `credential-screen-window-present-ticket`
  - `text-recovery-window-present-ticket`
  - `text-login-resume-window-present-ticket`
  - `text-login-fallback-window-present-ticket`
- O builder permanece fail-closed para window damage plan ausente, window damage
  plan inseguro, submit grafico, origem upstream forjada e qualquer estado
  upstream que indique execucao real.

## Seguranca e privacidade

- Mantem `submit_enabled=0`, `auth_attempt_allowed=0`,
  `submit_callback_bound=0` e `auth_callback_bound=0`.
- Mantem credenciais redigidas com `credential_redacted=1`,
  `length_redacted=1`, `raw_secret_exposed=0` e `masked_text_exposed=0`.
- Bloqueia propagacao de `present_submitted`, `present_auth_submit_allowed`,
  `present_auth_attempt_allowed`, `damage_submitted`,
  `damage_auth_submit_allowed`, `damage_auth_attempt_allowed`,
  `damage_cache_hit`, submits reais de compositor/surface, vinculos reais de
  surface/window/input, mapeamento de memoria, escrita de pixels, submit GUI e
  estados reais de release/reclaim/compaction.
- Preserva login textual como caminho autoritativo enquanto autenticacao grafica
  continua desabilitada.

## Desempenho e escalabilidade

- A fatia e puramente declarativa, sem alocacao dinamica, IO, storage, GPU,
  compositor real, present real, damage real, timers ou callbacks externos.
- A validacao ocorre por predicados booleanos locais e copia seletiva de campos
  seguros, mantendo custo constante e previsivel.
- Campos de cache/reuso de present ficam preparados para evolucao futura, mas
  `present_cache_hit` permanece `0` nesta entrega.

## Validacao

Validado por revisao estatica de codigo, sem execucao de terminal, build,
`make`, `git` ou testes. A cobertura planejada em `tests/test_login_runtime.c`
foi expandida para widgets de credenciais, rotas textuais, fallback por submit e
acao desconhecida, upstream ausente/inseguro, origem upstream forjada e bloqueio
de estados submetidos/efeitos reais herdados de damage, compositor, surface,
window, GUI, release, reclaim e compaction.

## Limites

- Nao apresenta frame real do loginwindow.
- Nao submete present real, damage real, compositor real, surface real,
  display/window/GUI ou callbacks de autenticacao.
- Nao vincula surface real, mapeia memoria, escreve pixels, cria janela real ou
  vincula input real.
- Nao substitui os smokes reais de GUI; a validacao deste patch e apenas
  estatica.
