# CapyOS 0.8.0-alpha.190+20260512

Data: 2026-05-12
Canal: alpha
Etapa: 2 — Sessão gráfica operacional

## Resumo

Este patch continua o pipeline declarativo fail-closed da tela de credenciais do
loginwindow e adiciona o `gui_plan`, uma camada pura sobre o `release_plan`.

O incremento prepara somente tickets declarativos de GUI para futura integração
com compositor/display/auditoria persistente. Ele não escreve pixels, não submete
GUI, não submete release, não poda storage, não libera recursos, não persiste
release, não persiste reclaim, não persiste compaction, não persiste tombstone,
não persiste purge, não persiste expiry, não persiste retention, não persiste
archive, não persiste journal, não persiste ledger, não persiste recibo, não
persiste registro, não apaga purge, não arma timer de expiry, não apaga expiry,
não anexa log, não escreve estado real, não executa sincronização CPU/GPU, não
submete display/output e não habilita autenticação gráfica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_GUI_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_gui_plan`.
- Adicionada `login_window_credential_screen_gui_plan_build()` para selecionar
  tickets `credential-screen-gui-ticket`, `text-recovery-gui-ticket`,
  `text-login-resume-gui-ticket` ou `text-login-fallback-gui-ticket`.
- O builder é uma camada pura sobre
  `login_window_credential_screen_release_plan`.
- O plano público resume apenas flags e tickets seguros, sem expor storage
  mutável, segredo bruto, máscara, comprimento, snapshots internos ou ponteiros
  de callback.
- Adicionados testes planejados fail-closed em `tests/test_login_runtime.c` para:
  - widgets de credencial;
  - recuperação textual;
  - retorno ao login textual;
  - submit gráfico forçado para fallback textual;
  - ação desconhecida preservando fallback textual;
  - release plan ausente;
  - release plan inseguro;
  - release plan adulterado com estados já submetidos.

## Garantias de segurança

O `gui_plan` só marca `gui_plan_safe=1` quando o `release_plan` de entrada está
seguro e preserva os invariantes declarativos anteriores:

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
- release exigido e permitido, mas não submetido;
- release ticket/target selecionados;
- prune de storage por release desabilitado;
- liberação de recursos por release desabilitada;
- sync de release desabilitado;
- GUI permitido apenas como ticket declarativo;
- escrita de pixels desabilitada;
- submit/autenticação gráfica desabilitados;
- credenciais limpas e redigidas;
- segredo bruto e texto mascarado não expostos;
- callbacks de submit/auth não vinculados;
- login textual permanece autoritativo.

## Validação

Validação realizada somente por revisão estática de código e documentação.

Não foram executados comandos `make`, `git`, build ou suite de testes.

## Impacto

Para o usuário final, não há mudança visual imediata: o login textual continua
sendo o caminho autoritativo e seguro. Para a estrutura do sistema, o pipeline da
tela de credenciais ganha uma camada declarativa de GUI após release, preparando
o futuro encaixe com compositor/display sem executar desenho real, submit,
liberação de recurso, persistência ou autenticação gráfica.
