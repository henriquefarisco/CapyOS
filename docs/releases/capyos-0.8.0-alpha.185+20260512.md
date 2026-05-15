# CapyOS 0.8.0-alpha.185+20260512

Data: 2026-05-12
Canal: alpha
Etapa: 2 — Sessão gráfica operacional

## Resumo

Este patch continua o pipeline declarativo fail-closed da tela de credenciais do
loginwindow e adiciona o `purge_plan`, uma camada pura sobre o `expiry_plan`.

O incremento prepara somente tickets declarativos de purge para futura
integração GUI/compositor/display/auditoria persistente. Ele não persiste purge,
não persiste expiry, não persiste retention, não persiste archive, não persiste
journal, não persiste ledger, não persiste recibo, não persiste registro, não
apaga purge, não arma timer de expiry, não apaga expiry, não anexa log, não
escreve estado real, não executa sincronização CPU/GPU, não submete
display/output e não habilita autenticação gráfica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_PURGE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_purge_plan`.
- Adicionada `login_window_credential_screen_purge_plan_build()` para selecionar
  tickets `credential-screen-purge-ticket`, `text-recovery-purge-ticket`,
  `text-login-resume-purge-ticket` ou `text-login-fallback-purge-ticket`.
- O builder é uma camada pura sobre `login_window_credential_screen_expiry_plan`.
- Adicionados testes planejados fail-closed em `tests/test_login_runtime.c` para:
  - widgets de credencial;
  - recuperação textual;
  - retorno ao login textual;
  - submit gráfico forçado para fallback textual;
  - ação desconhecida preservando fallback textual;
  - expiry plan ausente;
  - expiry plan inseguro;
  - expiry plan adulterado com estados já submetidos.

## Garantias de segurança

O `purge_plan` só marca `purge_plan_safe=1` quando o `expiry_plan` de entrada
está seguro e preserva todos os invariantes declarativos anteriores:

- expiry exigido e permitido, mas não submetido;
- expiry ticket/target selecionados;
- persistência de expiry desabilitada;
- sync CPU/GPU de expiry desabilitado;
- timer e delete de expiry desabilitados;
- retention exigido e permitido, mas não submetido;
- persistência de retention desabilitada;
- archive exigido e permitido, mas não submetido;
- persistência de archive desabilitada;
- journal exigido e permitido, mas não submetido;
- persistência de journal desabilitada;
- ledger exigido e permitido, mas não submetido;
- persistência de ledger desabilitada;
- receipt exigido e permitido, mas não submetido;
- persistência de receipt desabilitada;
- record exigido e permitido, mas não submetido;
- persistência de record desabilitada;
- audit permitido, mas não submetido;
- append de log de auditoria desabilitado;
- seal, cleanup, retire e ack permitidos, mas não submetidos;
- completion permitido, mas não reportado nem acknowledged;
- deadline permitido, mas não armado;
- purge permitido, mas não submetido;
- persistência de purge desabilitada;
- delete de purge desabilitado;
- sync, timeline, fence, barrier, flush, framebuffer, blit, output, display,
  scanout, vsync, schedule, present, damage, compositor e page flip não
  submetidos;
- credenciais limpas e redigidas;
- segredo bruto e texto mascarado não expostos;
- callbacks de submit/auth não vinculados;
- submit gráfico e tentativa de autenticação gráfica desabilitados;
- login textual permanece autoritativo.

## Validação

Validação realizada somente por revisão estática de código e documentação.

Não foram executados comandos `make`, `git`, build ou suite de testes.

## Impacto

Para o usuário final, não há mudança visual imediata: o login textual continua
sendo o caminho autoritativo e seguro. Para a estrutura do sistema, o pipeline da
tela de credenciais ganha mais uma camada declarativa auditável antes de qualquer
integração real com purge persistente, deleção, display ou autenticação gráfica.
