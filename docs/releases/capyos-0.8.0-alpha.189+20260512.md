# CapyOS 0.8.0-alpha.189+20260512

Data: 2026-05-12
Canal: alpha
Etapa: 2 — Sessão gráfica operacional

## Resumo

Este patch continua o pipeline declarativo fail-closed da tela de credenciais do
loginwindow e adiciona o `release_plan`, uma camada pura sobre o
`reclaim_plan`.

O incremento prepara somente tickets declarativos de release para futura
integração GUI/compositor/display/auditoria persistente. Ele não poda storage,
não libera recursos, não submete release, não persiste release, não persiste
reclaim, não persiste compaction, não persiste tombstone, não persiste purge,
não persiste expiry, não persiste retention, não persiste archive, não persiste
journal, não persiste ledger, não persiste recibo, não persiste registro, não
apaga purge, não arma timer de expiry, não apaga expiry, não anexa log, não
escreve estado real, não executa sincronização CPU/GPU, não submete
display/output e não habilita autenticação gráfica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_RELEASE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_release_plan`.
- Adicionada `login_window_credential_screen_release_plan_build()` para
  selecionar tickets `credential-screen-release-ticket`,
  `text-recovery-release-ticket`, `text-login-resume-release-ticket` ou
  `text-login-fallback-release-ticket`.
- O builder é uma camada pura sobre
  `login_window_credential_screen_reclaim_plan`.
- O plano público resume apenas flags e tickets seguros, sem expor storage
  mutável, segredo bruto, máscara, comprimento, snapshots internos ou ponteiros
  de callback.
- Adicionados testes planejados fail-closed em `tests/test_login_runtime.c` para:
  - widgets de credencial;
  - recuperação textual;
  - retorno ao login textual;
  - submit gráfico forçado para fallback textual;
  - ação desconhecida preservando fallback textual;
  - reclaim plan ausente;
  - reclaim plan inseguro;
  - reclaim plan adulterado com estados já submetidos.

## Garantias de segurança

O `release_plan` só marca `release_plan_safe=1` quando o `reclaim_plan` de
entrada está seguro e preserva os invariantes declarativos anteriores:

- compaction exigida e permitida, mas não submetida;
- compaction ticket/target selecionados;
- compactação de storage desabilitada;
- liberação de recursos por compaction desabilitada;
- sync CPU/GPU de compaction desabilitado;
- reclaim exigido e permitido, mas não submetido;
- reclaim ticket/target selecionados;
- prune de storage por reclaim desabilitado;
- liberação de recursos por reclaim desabilitada;
- sync de reclaim desabilitado;
- release permitido apenas como ticket declarativo;
- prune de storage por release desabilitado;
- liberação de recursos por release desabilitada;
- sync de release desabilitado;
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
tela de credenciais ganha uma camada declarativa de release após reclaim,
preparando o futuro ciclo de liberação controlada sem executar prune, liberação
de recurso, display real ou autenticação gráfica.
