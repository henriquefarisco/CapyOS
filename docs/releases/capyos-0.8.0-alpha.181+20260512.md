# CapyOS 0.8.0-alpha.181+20260512

Data: 2026-05-12
Canal: alpha
Etapa: 2 — Sessão gráfica operacional

## Resumo

Este patch continua o pipeline declarativo fail-closed da tela de credenciais do
loginwindow e adiciona o `journal_plan`, uma camada pura sobre o `ledger_plan`.

O incremento prepara somente tickets declarativos de journal para futura
integração GUI/compositor/display/auditoria persistente. Ele não persiste
journal, não persiste ledger, não persiste recibo, não persiste registro, não
anexa log, não escreve estado real, não executa sincronização CPU/GPU, não
submete display/output e não habilita autenticação gráfica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_JOURNAL_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_journal_plan`.
- Adicionada `login_window_credential_screen_journal_plan_build()` para
  selecionar tickets `credential-screen-journal-ticket`,
  `text-recovery-journal-ticket`, `text-login-resume-journal-ticket` ou
  `text-login-fallback-journal-ticket`.
- O builder é uma camada pura sobre `login_window_credential_screen_ledger_plan`.
- Adicionados testes planejados fail-closed em `tests/test_login_runtime.c` para:
  - widgets de credencial;
  - recuperação textual;
  - retorno ao login textual;
  - submit gráfico forçado para fallback textual;
  - ação desconhecida preservando fallback textual;
  - ledger plan ausente;
  - ledger plan inseguro;
  - ledger plan adulterado com estados já submetidos.

## Garantias de segurança

O `journal_plan` só marca `journal_plan_safe=1` quando o `ledger_plan` de entrada
está seguro e preserva todos os invariantes declarativos anteriores:

- ledger exigido e permitido, mas não submetido;
- ledger ticket/target selecionados;
- persistência de ledger desabilitada;
- sync CPU/GPU de ledger desabilitado;
- receipt exigido e permitido, mas não submetido;
- persistência de receipt desabilitada;
- record exigido e permitido, mas não submetido;
- persistência de record desabilitada;
- audit permitido, mas não submetido;
- append de log de auditoria desabilitado;
- seal, cleanup, retire e ack permitidos, mas não submetidos;
- completion permitido, mas não reportado nem acknowledged;
- deadline permitido, mas não armado;
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
integração real com journaling persistente, display ou autenticação gráfica.
