# CapyOS 0.8.0-alpha.188+20260512

Data: 2026-05-12
Canal: alpha
Etapa: 2 — Sessão gráfica operacional

## Resumo

Este patch continua o pipeline declarativo fail-closed da tela de credenciais do
loginwindow e adiciona o `reclaim_plan`, uma camada pura sobre o
`compaction_plan`.

O incremento prepara somente tickets declarativos de reclaim para futura
integração GUI/compositor/display/auditoria persistente. Ele não poda storage,
não libera recursos, não persiste reclaim, não persiste compaction, não persiste
tombstone, não persiste purge, não persiste expiry, não persiste retention, não
persiste archive, não persiste journal, não persiste ledger, não persiste recibo,
não persiste registro, não apaga purge, não arma timer de expiry, não apaga
expiry, não anexa log, não escreve estado real, não executa sincronização
CPU/GPU, não submete display/output e não habilita autenticação gráfica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_RECLAIM_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_reclaim_plan`.
- Adicionada `login_window_credential_screen_reclaim_plan_build()` para
  selecionar tickets `credential-screen-reclaim-ticket`,
  `text-recovery-reclaim-ticket`, `text-login-resume-reclaim-ticket` ou
  `text-login-fallback-reclaim-ticket`.
- O builder é uma camada pura sobre
  `login_window_credential_screen_compaction_plan`.
- O plano público resume apenas flags e tickets seguros, sem expor storage
  mutável, segredo bruto, máscara, comprimento, snapshots internos ou ponteiros
  de callback.
- Adicionados testes planejados fail-closed em `tests/test_login_runtime.c` para:
  - widgets de credencial;
  - recuperação textual;
  - retorno ao login textual;
  - submit gráfico forçado para fallback textual;
  - ação desconhecida preservando fallback textual;
  - compaction plan ausente;
  - compaction plan inseguro;
  - compaction plan adulterado com estados já submetidos.

## Garantias de segurança

O `reclaim_plan` só marca `reclaim_plan_safe=1` quando o `compaction_plan` de
entrada está seguro e preserva os invariantes declarativos anteriores:

- compaction exigida e permitida, mas não submetida;
- compaction ticket/target selecionados;
- compactação de storage desabilitada;
- liberação de recursos por compaction desabilitada;
- sync CPU/GPU de compaction desabilitado;
- tombstone exigido e permitido, mas não submetido;
- persistência de tombstone desabilitada;
- sync CPU/GPU de tombstone desabilitado;
- purge exigido e permitido, mas não submetido;
- persistência, sync CPU/GPU e delete de purge desabilitados;
- expiry exigido e permitido, mas não submetido;
- timer e delete de expiry desabilitados;
- retention, archive, journal, ledger, receipt e record permitidos, mas não
  submetidos nem persistidos;
- audit permitido, mas não submetido e sem append de log;
- seal permitido, mas sem escrita de estado;
- cleanup e retire permitidos, mas sem liberação real de recursos;
- ack e completion permanecem declarativos;
- deadline permitido, mas não armado;
- reclaim permitido apenas como ticket declarativo;
- prune de storage por reclaim desabilitado;
- liberação de recursos por reclaim desabilitada;
- sync de reclaim desabilitado;
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
tela de credenciais ganha uma camada declarativa de reclaim após compaction,
preparando o futuro ciclo de recuperação/liberação controlada sem executar
prune, liberação de recurso, display real ou autenticação gráfica.
