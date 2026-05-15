# CapyOS 0.8.0-alpha.187+20260512

Data: 2026-05-12
Canal: alpha
Etapa: 2 — Sessão gráfica operacional

## Resumo

Este patch continua o pipeline declarativo fail-closed da tela de credenciais do
loginwindow e adiciona o `compaction_plan`, uma camada pura sobre o
`tombstone_plan`.

O incremento prepara somente tickets declarativos de compaction para futura
integração GUI/compositor/display/auditoria persistente. Ele não compacta
storage, não libera recursos, não persiste compaction, não persiste tombstone,
não persiste purge, não persiste expiry, não persiste retention, não persiste
archive, não persiste journal, não persiste ledger, não persiste recibo, não
persiste registro, não apaga purge, não arma timer de expiry, não apaga expiry,
não anexa log, não escreve estado real, não executa sincronização CPU/GPU, não
submete display/output e não habilita autenticação gráfica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_COMPACTION_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_compaction_plan`.
- Adicionada `login_window_credential_screen_compaction_plan_build()` para
  selecionar tickets `credential-screen-compaction-ticket`,
  `text-recovery-compaction-ticket`, `text-login-resume-compaction-ticket` ou
  `text-login-fallback-compaction-ticket`.
- O builder é uma camada pura sobre
  `login_window_credential_screen_tombstone_plan`.
- O plano público resume apenas flags e tickets seguros, sem expor storage
  mutável, segredo bruto, máscara, comprimento, snapshots internos ou ponteiros
  de callback.
- Adicionados testes planejados fail-closed em `tests/test_login_runtime.c` para:
  - widgets de credencial;
  - recuperação textual;
  - retorno ao login textual;
  - submit gráfico forçado para fallback textual;
  - ação desconhecida preservando fallback textual;
  - tombstone plan ausente;
  - tombstone plan inseguro;
  - tombstone plan adulterado com estados já submetidos.

## Garantias de segurança

O `compaction_plan` só marca `compaction_plan_safe=1` quando o `tombstone_plan`
de entrada está seguro e preserva os invariantes declarativos anteriores:

- tombstone exigido e permitido, mas não submetido;
- tombstone ticket/target selecionados;
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
- compaction permitida apenas como ticket declarativo;
- compactação de storage desabilitada;
- liberação de recursos por compaction desabilitada;
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
tela de credenciais ganha uma camada declarativa de compaction após tombstone,
preparando o futuro ciclo de compactação sem executar escrita, liberação de
recurso, display real ou autenticação gráfica.
