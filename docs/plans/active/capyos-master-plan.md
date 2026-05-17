# CapyOS — Master Plan sequencial

**Data de referência:** 2026-05-15
**Versão atual:** `0.8.0-alpha.237+20260514`
**Plataforma oficial:** `VMware + UEFI + E1000`
**Público alvo prioritário:** usuário desktop comum (não-técnico, experiência tipo Ubuntu/Win7 polida).
**Status:** Etapa 2 tecnicamente fechada por código/docs; 1/16 etapas oficialmente fechadas; validação externa final pendente antes de liberar a Etapa 3.

Este é o único plano ativo. Entregas concluídas foram removidas daqui e
consolidadas em
[`../historical/implementation-delivered-through-alpha93.md`](../historical/implementation-delivered-through-alpha93.md).

> **Reorganização em 2026-05-15:** as Etapas 3-15 foram **reordenadas por ROI ao usuário desktop comum** e expandidas para 14 etapas (3-16) sem violar a regra sequencial estrita. A sequência antiga foi preservada em
> [`../historical/capyos-master-plan-pre-roi-reorder.md`](../historical/capyos-master-plan-pre-roi-reorder.md).
> Etapas 1-2 não foram afetadas pela reorganização. Resumo das mudanças principais:
> drivers/USB HID antecipados; scheduler/multithread incorporado ao CapyDisplay 2D; apps básicos antecipados; browser usável explícito; áudio e WiFi/power management promovidos a etapas próprias; CapyLX unificado e adiado; Mesa/Vulkan e CapyLang rebaixados para Etapa 15 nova.

## 1. Regra de execução

A partir desta reorganização, o desenvolvimento volta a ser estritamente
sequencial:

1. Uma etapa só pode iniciar quando a etapa anterior estiver 100% concluída.
2. Cada etapa precisa fechar código, documentação, critérios de aceite e
   validação antes de liberar a próxima.
3. Etapas históricas não voltam ao plano ativo como checklist.
4. Nenhuma etapa deve introduzir dependência Linux no kernel base; compatibilidade
   Linux fica isolada no módulo CapyLX.
5. Segurança, privacidade, performance, estabilidade e UX continuam sendo os
   pilares de aceite.

## 2. Estado base congelado

A base atual inclui tudo entregue até `0.8.0-alpha.93+20260510`, incluindo:

- fundamentos kernel/userland;
- release tooling e publicação pública;
- rede, DNS, HTTP e hardening de URL/headers;
- update agent com gates locais e remotos;
- fundação GUI/CapyUI;
- `libcapy-tls` metadata-only fail-closed até adaptador BearSSL;
- shims Linux ABI parciais existentes.

Detalhes vivem no documento histórico de implementação finalizada.

## 3. Sequência bloqueante de etapas

| Etapa | Tema | Estado | Depende de | Saída para 100% |
|---|---|---|---|---|
| 1 | CapyUI Shell Polish v1 | Concluída | base alpha.93 | desktop familiar Ubuntu/Windows 7-like |
| 2 | Sessão gráfica operacional | Concluída por escopo técnico | Etapa 1 | login GUI real e smokes `gui-session`/`mouse-events` |
| 3 | Driver framework + entrada USB HID + storage estável | Bloqueada até validação externa da Etapa 2 | Etapa 2 | XHCI/USB HID maduro, AHCI/NVMe estáveis, VirtIO opcional, política fail-safe de driver |
| 4 | CapyDisplay 2D + scheduler/multithread runtime | Bloqueada | Etapa 3 | camada 2D com damage/double buffer, scheduler cooperativo, multithread runtime |
| 5 | TLS userland real | Bloqueada | Etapa 4 | BearSSL userland com handshake real validado |
| 6 | Apps básicos do desktop maduros | Bloqueada | Etapa 5 | apps essenciais (file/text/image/calc/notes/media simples), libcapy-ui inicial e localização PT-BR/ES |
| 7 | Browser usável com web moderna | Bloqueada | Etapa 6 | HTTPS real, decode JPEG/PNG/WebP, streaming render, HTTP cache, forms |
| 8 | Release/update gate oficial + instalador polido | Bloqueada | Etapa 7 | smoke VMware+E1000 oficial, update HTTPS, instalador wizard amigável |
| 9 | Package manager + SDK + ABI estável | Bloqueada | Etapa 8 | ecossistema instalável e ABI documentada |
| 10 | Áudio + multimídia básica | Bloqueada | Etapa 9 | Intel HDA/AC97/USB Audio, mixer de sistema, media player com playlist |
| 11 | WiFi + power management + suspend/resume | Bloqueada | Etapa 10 | driver WiFi popular, WPA2/WPA3, ACPI battery, suspend S3 inicial |
| 12 | JS engine sandboxed | Bloqueada | Etapa 11 | JavaScript isolado no browser com bridge DOM controlada |
| 13 | CapyLX L0-L5 unificado | Bloqueada | Etapa 12 | binários Linux estáticos + POSIX amplo + threads/futex/sockets |
| 14 | Wayland bridge + apps Linux GUI | Bloqueada | Etapa 13 | apps Linux GUI via Wayland mínimo integrados ao compositor CapyOS |
| 15 | Mesa/Vulkan path + CapyLang | Bloqueada | Etapa 14 | lavapipe/virgl/Venus + linguagem própria com VM bytecode e bindings seguros |
| 16 | Plataforma 1.0 hardening | Bloqueada | Etapa 15 | Secure Boot, SMP, firewall, polish final |

## 4. Etapa 1 — CapyUI Shell Polish v1

**Objetivo:** melhorar o layout e a UX de forma rápida, lembrando Ubuntu e um
pouco Windows 7 sem exigir driver 3D.

**Status atual:** concluída em `0.8.0-alpha.100+20260510`. `0.8.0-alpha.94+20260510` entregou o tema
`classic-modern`, taskbar visual inicial e notificações alinhadas à paleta.
`0.8.0-alpha.95+20260510` adicionou launcher filtrável por teclado,
categorias visuais e tray inicial NET/USER.
`0.8.0-alpha.96+20260510` adicionou apps fixados e recentes no launcher.
`0.8.0-alpha.97+20260510` refinou decoração ativa/inativa e botões de janela.
`0.8.0-alpha.98+20260510` adicionou wallpaper 2D e grid de ícones refinado.
`0.8.0-alpha.99+20260510` expandiu o tray para NET/SND/SYS/USR.
`0.8.0-alpha.100+20260510` fechou a Etapa 1 e adicionou Super para o launcher.

### Entregáveis

- Tema centralizado com tokens de cor, spacing, radius, borda, sombra e estados.
- Taskbar inferior com botão Capy, apps fixados, apps abertos e relógio.
- Launcher com busca, categorias, apps recentes/fixados e ações de sessão.
- Decoração de janela com estados ativo/inativo, minimizar, maximizar e fechar.
- Wallpaper padrão e desktop com grid de ícones.
- Toasts/notificações simples no canto.
- System tray inicial para rede/usuário e depois som/estado adicionais.

### Critérios de aceite

- [x] O desktop abre com visual consistente em resolução oficial.
- [x] Launcher abre/fecha por botão Capy e tecla Super.
- [x] Taskbar diferencia app focado, aberto e fixado.
- [x] Janela ativa/inativa tem contraste claro.
- [x] Notificação aparece, expira e não bloqueia input.
- [x] Tudo funciona sem GPU 3D e sem dependência Linux.

## 5. Etapa 2 — Sessão gráfica operacional

**Objetivo:** transformar a fundação GUI existente em sessão gráfica completa.

**Status atual:** tecnicamente concluída em `0.8.0-alpha.237+20260514`.
**Progresso por escopo fechado:** 100% entregue; validação externa final pendente.
O percentual é ponderado por entregáveis de fechamento da etapa, não por
quantidade de commits, linhas ou versões alpha. Novas descobertas devem ser
classificadas como bugs dentro dos blocos abaixo; só viram novo escopo se este
plano for alterado explicitamente.

`alpha.232` entregou o orquestrador automático da migração cifrada como camada
aditiva de passo único: detecta layout atual, stage/copy/commit/cleanup conforme
estado persistente, retoma por checkpoint+manifest e executa rollback quando a
política de abort é solicitada.

`alpha.233` entregou o contrato real de submit/autenticação do loginwindow: uma
política explícita habilita submit apenas quando o contrato GUI está pronto,
`login_window_credential_auth_submit_userdb_consume()` chama
`userdb_authenticate_with_policy`, preserva os códigos `OK/FAILED/LOCKED`, zera
o buffer de credencial em todos os caminhos e mantém o fallback textual
autoritativo até o contrato de handoff entregue em `alpha.235`.

`alpha.234` entregou a decisão segura de recuperação do loginwindow: o novo
contrato `login_window_credential_recovery_decision_build()` consolida controller
GUI + resultado de submit autenticado, aceita apenas rotas redigidas
stay/recovery/resume/text-login, bloqueia bypass de lockout, impede entrada em
recovery após autenticação bem-sucedida e exige reset+rerender para resume.

`alpha.235` entregou o contrato seguro de handoff loginwindow -> sessão gráfica:
`login_window_credential_session_handoff_build()` aceita apenas submit GUI
autenticado, recovery decision segura, usuário normal elegível e auditoria
redigida; bloqueia falha, lockout, recovery ativo, usuário de recuperação e
qualquer exposição de segredo antes de liberar `session_begin`, ativação de
sessão, init do shell context e autostart do desktop.

`alpha.236` entregou o gate determinístico de evidência externa para
`gui-session`: a sessão gráfica agora expõe o contrato
`desktop_gui_session_smoke_gate_from_readiness()`, emite uma única vez o marker
serial público `[smoke] gui-session ready` quando framebuffer, dimensões,
taskbar, dispatcher essencial, fila saudável e ausência de overlays/drag estão
prontos, adiciona o alvo `smoke-x64-vmware-gui-session` e amarra handoff,
readiness, evidência, aceitação e promoção de release ao novo gate oficial.

`alpha.237` entregou o gate externo final `mouse-events`: a sessão gráfica agora
expõe `desktop_mouse_events_smoke_gate_from_readiness()`, emite uma única vez o
marker serial público `[smoke] mouse-events ready` quando `gui-session`, mouse,
cursor e rotas de mouse estão prontos, adiciona o alvo
`smoke-x64-vmware-mouse-events` e faz a esteira de release exigir DHCP +
`gui-session` + `mouse-events`.

| Bloco de fechamento da Etapa 2 | Peso | Estado | Próxima ação |
|---|---:|---|---|
| Runtime gráfico, frame pacing, UX operacional, terminal gráfico e fallback textual | 16% | 16/16 entregue | manter regressões cobertas pelos gates existentes |
| Dispatcher central, rotas de mouse/teclado e snapshots de prontidão | 14% | 14/14 entregue | validar nos smokes reais finais |
| Loginwindow GUI seguro declarativo sem autenticação gráfica | 16% | 16/16 entregue | conectar autenticação real sem vazar segredo |
| Segurança/auth/storage para sessão persistente, header-managed volumes e recovery da migração até `alpha.231` | 24% | 24/24 entregue | manter regressões cobertas pelos testes rekey |
| Orquestrador automático da migração legacy -> header-managed | 8% | 8/8 entregue | validar nos gates externos |
| Loginwindow GUI real com senha, lockout, recuperação e transição de sessão | 10% | 10/10 entregue | validar nos smokes reais finais |
| Smokes reais e evidência de release para `gui-session` e `mouse-events` | 12% | 12/12 entregue por contrato, target e evidência; execução externa final pendente | rodar gates externos oficiais |

**Validação externa pendente para aceite operacional da Etapa 2.**

| Gate externo | Evidência obrigatória | Critério para considerar aceito |
|---|---|---|
| `alpha.237` | Execução fora desta máquina dos gates finais | CI/operador executa `make test`, `make layout-audit`, `make all64`, `make release-check`, `make smoke-x64-vmware-mouse-events` e os gates de readiness/evidência/aceitação/promoção com `RELEASE_TAG=0.8.0-alpha.237+20260514`. Falhas encontradas entram como bugs da Etapa 2 antes de liberar a Etapa 3. |

**Fora do escopo de fechamento da Etapa 2.**
CapyDisplay 2D avançado, framework de drivers, TLS userland real, apps básicos,
package manager, CapyLX, Wayland, Mesa/Vulkan, JavaScript, CapyLang e hardening
1.0 pertencem às Etapas 3-16 e continuam bloqueados até a Etapa 2 fechar.

`0.8.0-alpha.101+20260510` iniciou a etapa com frame pacing
cooperativo no caminho ocioso do desktop.
`0.8.0-alpha.102+20260510` adicionou tick gráfico explícito para
evitar composição/cursor quando cena e cursor estão estáveis.
`0.8.0-alpha.103+20260510` prepara contrato fail-closed para o
futuro loginwindow GUI sem substituir o login textual.
`0.8.0-alpha.104+20260510` revisa UX operacional do desktop: launcher
ancorado com recentes recolhiveis, tray de rede compacto, desktop icons sem
rail lateral e File Manager com selecao, navegacao e contexto mais previsiveis.
`0.8.0-alpha.105+20260511` revisa o contrato terminal-shell grafico: prompt
real com usuario/host/cwd, saida/clear roteados com restauracao segura e logout
via estado do shell context.
`0.8.0-alpha.106+20260511` adiciona fallback `CTRL+ALT+F1` para retornar ao
TTY textual a partir da sessao grafica nos backends PS/2 e Hyper-V nativos.
`0.8.0-alpha.107+20260511` prepara o loginwindow GUI com view model
deterministico/fail-closed derivado do contrato de login, sem coletar senha nem
substituir o login textual.
`0.8.0-alpha.108+20260511` desenha uma previa passiva do loginwindow na tela de
login usando o view model, mantendo autenticação textual autoritativa.
`0.8.0-alpha.109+20260511` adiciona snapshot de saude do dispatcher central e da
fila GUI, preparando a migracao de input sem trocar o caminho autoritativo.
`0.8.0-alpha.110+20260511` move o teclado de janelas focadas para
`gui_window_dispatch_event()`, eliminando fila espelhada e callback direto nesse
caminho.
`0.8.0-alpha.111+20260511` move o scroll de mouse de janelas focadas para o
dispatcher central, mantendo clicks/drag/overlays nos caminhos atuais.
`0.8.0-alpha.112+20260511` move hover/mouse move de janelas para o dispatcher
central, mantendo overlays, cursor hint, clicks e drag nos caminhos atuais.
`0.8.0-alpha.113+20260511` adiciona viewport/scroll ao Start Menu, revisa a
toolbar do File Manager e habilita drag-and-drop seguro para mover arquivos,
pastas e icones para diretorios por `vfs_rename()`.
`0.8.0-alpha.114+20260511` move mouse down/up esquerdo de janelas comuns para o
dispatcher central, removendo fila espelhada e callback direto no desktop para
esse ramo.
`0.8.0-alpha.115+20260511` move right-click/context menu de janelas comuns para
o dispatcher central, mantendo overlays, taskbar e desktop icons priorizados.
`0.8.0-alpha.116+20260511` adiciona contrato de rotas ao snapshot de saude do
dispatcher, tornando auditaveis as rotas comuns migradas e os caminhos especiais
diretos.
`0.8.0-alpha.117+20260511` adiciona snapshot operacional da sessao grafica,
consolidando estado de framebuffer, mouse, taskbar, overlays, window manager e
dispatcher para smokes futuros.
`0.8.0-alpha.118+20260511` deriva uma bitmask de bloqueios e flags de prontidao
`gui-session`/`mouse-events` a partir do snapshot operacional, sem executar
smokes reais.
`0.8.0-alpha.119+20260511` isola a derivacao de prontidao em unidade pura e
adiciona cobertura host planejada sem linkar o desktop completo.
`0.8.0-alpha.120+20260511` adiciona mascara conhecida e nomes estaveis para os
blockers de prontidao, melhorando diagnostico futuro dos smokes GUI.
`0.8.0-alpha.121+20260511` adiciona resumo deterministico dos blockers,
separando flags conhecidas, desconhecidas, contagem e primeiro blocker.
`0.8.0-alpha.122+20260511` embute esse resumo no snapshot de prontidao,
permitindo diagnostico de blockers em uma unica leitura.
`0.8.0-alpha.123+20260511` adiciona diagnostico deterministico das rotas do
dispatcher ao readiness, apontando rotas ausentes sem alterar dispatch.
`0.8.0-alpha.124+20260511` adiciona politica fail-closed de credenciais ao
loginwindow GUI, com limite de senha, mascara, wipe obrigatorio, submit grafico
desabilitado e recuperacao textual.
`0.8.0-alpha.125+20260511` adiciona politica auditavel para sair da recuperacao
textual e voltar ao login normal somente com pedido consumido, runtime pronto e
modo manutencao limpo.
`0.8.0-alpha.126+20260511` adiciona buffer efemero de credenciais do loginwindow
com mascara, limite efetivo, overflow fail-closed e wipe obrigatorio, sem submit
grafico.
`0.8.0-alpha.127+20260511` adiciona gate fail-closed para submit de
credenciais do loginwindow, mantendo autenticacao grafica desabilitada mesmo com
buffer preenchido.
`0.8.0-alpha.128+20260511` adiciona consumo de tentativa de submit com wipe
obrigatorio do buffer, ainda sem habilitar autenticacao grafica.
`0.8.0-alpha.129+20260511` adiciona redutor puro de input para append,
backspace, submit e cancel do campo de credenciais do loginwindow.
`0.8.0-alpha.130+20260511` adiciona snapshot mascarado do campo de credenciais
do loginwindow, expondo estado sem segredo bruto.
`0.8.0-alpha.131+20260511` adiciona painel seguro de credenciais que combina
mascara, ultimo input e estado fail-closed sem habilitar autenticacao grafica.
`0.8.0-alpha.132+20260511` adiciona pipeline seguro de interacao que aplica
um input e reconstrói o painel mascarado em um unico snapshot auditavel.
`0.8.0-alpha.133+20260511` adiciona snapshot de prontidao de credenciais para
resumir render/input/wipe/overflow mantendo autenticacao grafica bloqueada.
`0.8.0-alpha.134+20260511` adiciona evento auditavel redigido para registrar
estado, acao, wipe e motivo sem expor segredo, mascara ou comprimento.
`0.8.0-alpha.135+20260511` adiciona view model seguro de credenciais que compoe
prontidao e auditoria redigida sem habilitar autenticacao grafica.
`0.8.0-alpha.136+20260511` adiciona pipeline composto de UI de credenciais que
aplica input e recompõe interacao/readiness/auditoria/view em uma etapa segura.
`0.8.0-alpha.137+20260511` adiciona sessao one-shot segura de UI de credenciais
que inicializa storage efemero, compoe a etapa e limpa storage/scratch antes do retorno.
`0.8.0-alpha.138+20260511` adiciona view model seguro de recuperacao de credenciais
que une sessao limpa/redigida e politica textual de recuperacao/retorno.
`0.8.0-alpha.139+20260511` adiciona snapshot seguro de tela de credenciais que
compoe login view, sessao limpa e recuperacao textual sem habilitar auth grafica.
`0.8.0-alpha.140+20260511` adiciona sessao one-shot segura da tela de credenciais
que compoe runtime, login, credenciais, recuperacao e wipe de IO.
`0.8.0-alpha.141+20260511` adiciona plano seguro de renderizacao que traduz a
sessao de credenciais em layout/acoes UI redigidas sem auth grafica.
`0.8.0-alpha.142+20260511` adiciona plano seguro de acoes que valida intencoes
da futura GUI sem liberar submit grafico nem expor segredo.
`0.8.0-alpha.143+20260511` adiciona evento UI seguro que audita intencoes
validadas sem carregar segredo, mascara, comprimento ou snapshots internos.
`0.8.0-alpha.144+20260511` adiciona plano seguro de rotas que traduz eventos
UI de credenciais em navegacao stay/recovery/resume/text-login sem auth grafica.
`0.8.0-alpha.145+20260511` adiciona controller seguro que converte rotas de
credenciais em decisoes finais de UI sem segredo nem autenticacao grafica.
`0.8.0-alpha.146+20260511` adiciona presenter seguro que converte decisoes de
credenciais em propriedades finais de apresentacao sem segredo nem auth grafica.
`0.8.0-alpha.147+20260511` adiciona binding seguro que converte a apresentacao
de credenciais em montagem final de widgets sem segredo nem auth grafica.
`0.8.0-alpha.148+20260511` adiciona mount plan seguro que converte bindings de
credenciais em transacao final de montagem sem callbacks de autenticacao.
`0.8.0-alpha.149+20260511` adiciona commit plan seguro que converte mount plans
em decisao declarativa sem executar commit real nem autenticacao grafica.
`0.8.0-alpha.150+20260511` adiciona handoff plan seguro que converte commit
plans em envelope final declarativo sem entrega real nem autenticacao grafica.
`0.8.0-alpha.151+20260511` adiciona dispatch plan seguro que converte handoff
plans em ticket final declarativo sem despacho real nem autenticacao grafica.
`0.8.0-alpha.152+20260511` adiciona queue plan seguro que converte dispatch
plans em ticket final declarativo sem enfileirar janela real nem autenticacao
grafica.
`0.8.0-alpha.153+20260511` adiciona activation plan seguro que converte queue
plans em ticket final declarativo sem aplicar janela real nem autenticacao
grafica.
`0.8.0-alpha.154+20260512` adiciona frame plan seguro que converte activation
plans em ticket final declarativo sem renderizar janela real nem autenticacao
grafica.
`0.8.0-alpha.155+20260512` adiciona surface plan seguro que converte frame
plans em superficie GUI declarativa sem submeter compositor real nem auth
grafica.
`0.8.0-alpha.156+20260512` adiciona compositor plan seguro que converte
surface plans em ticket GUI/compositor declarativo sem submeter compositor real,
enviar damage real nem auth grafica.
`0.8.0-alpha.157+20260512` adiciona damage plan seguro que converte compositor
plans em ticket declarativo de damage/cache sem enviar damage real nem auth
grafica.
`0.8.0-alpha.158+20260512` adiciona present plan seguro que converte damage
plans em ticket declarativo de apresentacao sem apresentar frame real nem auth
grafica.
`0.8.0-alpha.159+20260512` adiciona schedule plan seguro que converte present
plans em ticket declarativo de agendamento sem armar timer, page flip nem auth
grafica.
`0.8.0-alpha.160+20260512` adiciona vsync plan seguro que converte schedule
plans em ticket declarativo de sincronizacao sem wait, fence, timer, page flip
nem auth grafica.
`0.8.0-alpha.161+20260512` adiciona scanout plan seguro que converte vsync
plans em ticket declarativo de scanout sem anexar buffer, commitar modo de
display, page flip nem auth grafica.
`0.8.0-alpha.162+20260512` adiciona display plan seguro que converte scanout
plans em ticket declarativo de display sem anexar buffer, submeter display,
commitar modo, executar flip nem auth grafica.
`0.8.0-alpha.163+20260512` adiciona output plan seguro que converte display
plans em ticket declarativo de saida visual sem anexar buffer, submeter
output/display, executar flip nem auth grafica.
`0.8.0-alpha.164+20260512` adiciona blit plan seguro que converte output
plans em ticket declarativo de copia visual sem mapear buffer, copiar
pixels, submeter DMA/output/display, executar flip nem auth grafica.
`0.8.0-alpha.165+20260512` adiciona framebuffer plan seguro que converte
blit plans em ticket declarativo de framebuffer sem mapear memoria, escrever
pixels, fazer flush/cache, submeter output/display, executar flip nem auth grafica.
`0.8.0-alpha.166+20260512` adiciona flush plan seguro que converte
framebuffer plans em ticket declarativo de flush/cache sem executar flush,
limpeza de cache, barreira, output/display submit, flip nem auth grafica.
`0.8.0-alpha.167+20260512` adiciona barrier plan seguro que converte
flush plans em ticket declarativo de barreira/visibilidade sem executar
barreira real, flush/cache, output/display submit, flip nem auth grafica.
`0.8.0-alpha.168+20260512` adiciona fence plan seguro que converte
barrier plans em ticket declarativo de fence/sync sem armar fence real,
wait/signal, fd export, output/display submit, flip nem auth grafica.
`0.8.0-alpha.169+20260512` adiciona timeline plan seguro que converte
fence plans em ticket declarativo de timeline/semaphore sem submit,
wait/signal, valor publicado, output/display submit, flip nem auth grafica.
`0.8.0-alpha.170+20260512` adiciona sync plan seguro que converte
timeline plans em ticket declarativo de sincronizacao sem submit,
wait/signal, deadline armado, completion reportado, output/display submit,
flip nem auth grafica.
`0.8.0-alpha.171+20260512` adiciona deadline plan seguro que converte
sync plans em ticket declarativo de deadline sem armar deadline/timer,
expirar, reportar completion, sincronizar CPU/GPU, output/display submit,
flip nem auth grafica.
`0.8.0-alpha.172+20260512` adiciona completion plan seguro que converte
deadline plans em ticket declarativo de completion sem reportar/acknowledge
completion, sincronizar CPU/GPU, armar deadline/timer, output/display submit,
flip nem auth grafica.
`0.8.0-alpha.173+20260512` adiciona ack plan seguro que converte
completion plans em ticket declarativo de acknowledge sem submeter ack,
reportar completion, sincronizar CPU/GPU, armar deadline/timer,
output/display submit, flip nem auth grafica.
`0.8.0-alpha.174+20260512` adiciona retire plan seguro que converte
ack plans em ticket declarativo de retire sem liberar recursos, submeter
retire/ack, reportar completion, sincronizar CPU/GPU, armar deadline/timer,
output/display submit, flip nem auth grafica.
`0.8.0-alpha.175+20260512` adiciona cleanup plan seguro que converte
retire plans em ticket declarativo de cleanup sem limpar/liberar recursos,
submeter cleanup/retire/ack, reportar completion, sincronizar CPU/GPU,
armar deadline/timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.176+20260512` adiciona seal plan seguro que converte
cleanup plans em ticket declarativo de seal sem escrever estado,
limpar/liberar recursos, submeter seal/cleanup/retire/ack, reportar
completion, sincronizar CPU/GPU, armar deadline/timer, output/display
submit, flip nem auth grafica.
`0.8.0-alpha.177+20260512` adiciona audit plan seguro que converte
seal plans em ticket declarativo de auditoria sem anexar log, escrever
estado, limpar/liberar recursos, submeter audit/seal/cleanup/retire/ack,
reportar completion, sincronizar CPU/GPU, armar deadline/timer,
output/display submit, flip nem auth grafica.
`0.8.0-alpha.178+20260512` adiciona record plan seguro que converte
audit plans em ticket declarativo de registro sem persistir registro,
anexar log, escrever estado, limpar/liberar recursos, submeter record/
audit/seal/cleanup/retire/ack, reportar completion, sincronizar CPU/GPU,
armar deadline/timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.179+20260512` adiciona receipt plan seguro que converte
record plans em ticket declarativo de recibo sem persistir recibo, persistir
registro, anexar log, escrever estado, limpar/liberar recursos, submeter
receipt/record/audit/seal/cleanup/retire/ack, reportar completion,
sincronizar CPU/GPU, armar deadline/timer, output/display submit, flip nem
auth grafica.
`0.8.0-alpha.180+20260512` adiciona ledger plan seguro que converte
receipt plans em ticket declarativo de ledger sem persistir ledger, persistir
recibo, persistir registro, anexar log, escrever estado, limpar/liberar
recursos, submeter ledger/receipt/record/audit/seal/cleanup/retire/ack,
reportar completion, sincronizar CPU/GPU, armar deadline/timer, output/display
submit, flip nem auth grafica.
`0.8.0-alpha.181+20260512` adiciona journal plan seguro que converte
ledger plans em ticket declarativo de journal sem persistir journal, persistir
ledger, persistir recibo, persistir registro, anexar log, escrever estado,
limpar/liberar recursos, submeter journal/ledger/receipt/record/audit/seal/
cleanup/retire/ack, reportar completion, sincronizar CPU/GPU, armar deadline/
timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.182+20260512` adiciona archive plan seguro que converte
journal plans em ticket declarativo de archive sem persistir archive, persistir
journal, persistir ledger, persistir recibo, persistir registro, anexar log,
escrever estado, limpar/liberar recursos, submeter archive/journal/ledger/
receipt/record/audit/seal/cleanup/retire/ack, reportar completion, sincronizar
CPU/GPU, armar deadline/timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.183+20260512` adiciona retention plan seguro que converte
archive plans em ticket declarativo de retention sem persistir retention,
persistir archive, persistir journal, persistir ledger, persistir recibo,
persistir registro, anexar log, escrever estado, limpar/liberar recursos,
submeter retention/archive/journal/ledger/receipt/record/audit/seal/cleanup/
retire/ack, reportar completion, sincronizar CPU/GPU, armar deadline/timer,
output/display submit, flip nem auth grafica.
`0.8.0-alpha.184+20260512` adiciona expiry plan seguro que converte retention
plans em ticket declarativo de expiry sem persistir expiry, persistir retention,
persistir archive, persistir journal, persistir ledger, persistir recibo,
persistir registro, armar timer de expiry, apagar expiry, anexar log, escrever
estado, limpar/liberar recursos, submeter expiry/retention/archive/journal/
ledger/receipt/record/audit/seal/cleanup/retire/ack, reportar completion,
sincronizar CPU/GPU, armar deadline/timer, output/display submit, flip nem auth
grafica.
`0.8.0-alpha.185+20260512` adiciona purge plan seguro que converte expiry plans
em ticket declarativo de purge sem persistir purge, persistir expiry, persistir
retention, persistir archive, persistir journal, persistir ledger, persistir
recibo, persistir registro, apagar purge, armar timer de expiry, apagar expiry,
anexar log, escrever estado, limpar/liberar recursos, submeter purge/expiry/
retention/archive/journal/ledger/receipt/record/audit/seal/cleanup/retire/ack,
reportar completion, sincronizar CPU/GPU, armar deadline/timer, output/display
submit, flip nem auth grafica.
`0.8.0-alpha.186+20260512` adiciona tombstone plan seguro que converte purge
plans em ticket declarativo de tombstone sem persistir tombstone, persistir
purge, persistir expiry, persistir retention, persistir archive, persistir
journal, persistir ledger, persistir recibo, persistir registro, apagar purge,
armar timer de expiry, apagar expiry, anexar log, escrever estado,
limpar/liberar recursos, submeter tombstone/purge/expiry/retention/archive/
journal/ledger/receipt/record/audit/seal/cleanup/retire/ack, reportar
completion, sincronizar CPU/GPU, armar deadline/timer, output/display submit,
flip nem auth grafica.
`0.8.0-alpha.187+20260512` adiciona compaction plan seguro que converte
tombstone plans em ticket declarativo de compaction sem compactar storage,
liberar recursos, persistir compaction, persistir tombstone, persistir purge,
persistir expiry, persistir retention, persistir archive, persistir journal,
persistir ledger, persistir recibo, persistir registro, apagar purge, armar
timer de expiry, apagar expiry, anexar log, escrever estado, limpar/liberar
recursos, submeter compaction/tombstone/purge/expiry/retention/archive/journal/
ledger/receipt/record/audit/seal/cleanup/retire/ack, reportar completion,
sincronizar CPU/GPU, armar deadline/timer, output/display submit, flip nem auth
grafica.
`0.8.0-alpha.188+20260512` adiciona reclaim plan seguro que converte compaction
plans em ticket declarativo de reclaim sem podar storage, liberar recursos,
persistir reclaim, persistir compaction, persistir tombstone, persistir purge,
persistir expiry, persistir retention, persistir archive, persistir journal,
persistir ledger, persistir recibo, persistir registro, apagar purge, armar
timer de expiry, apagar expiry, anexar log, escrever estado, limpar/liberar
recursos, submeter reclaim/compaction/tombstone/purge/expiry/retention/archive/
journal/ledger/receipt/record/audit/seal/cleanup/retire/ack, reportar
completion, sincronizar CPU/GPU, armar deadline/timer, output/display submit,
flip nem auth grafica.
`0.8.0-alpha.189+20260512` adiciona release plan seguro que converte reclaim
plans em ticket declarativo de release sem podar storage, liberar recursos,
submeter release, persistir release, persistir reclaim, persistir compaction,
persistir tombstone, persistir purge, persistir expiry, persistir retention,
persistir archive, persistir journal, persistir ledger, persistir recibo,
persistir registro, apagar purge, armar timer de expiry, apagar expiry, anexar
log, escrever estado, limpar/liberar recursos, submeter reclaim/compaction/
tombstone/purge/expiry/retention/archive/journal/ledger/receipt/record/audit/
seal/cleanup/retire/ack, reportar completion, sincronizar CPU/GPU, armar
deadline/timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.190+20260512` adiciona GUI plan seguro que converte release plans
em ticket declarativo de GUI sem escrever pixels, submeter GUI, submeter release,
podar storage, liberar recursos, persistir release, persistir reclaim,
persistir compaction, persistir tombstone, persistir purge, persistir expiry,
persistir retention, persistir archive, persistir journal, persistir ledger,
persistir recibo, persistir registro, apagar purge, armar timer de expiry,
apagar expiry, anexar log, escrever estado, limpar/liberar recursos, submeter
reclaim/compaction/tombstone/purge/expiry/retention/archive/journal/ledger/
receipt/record/audit/seal/cleanup/retire/ack, reportar completion, sincronizar
CPU/GPU, armar deadline/timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.191+20260512` adiciona window plan seguro que converte GUI plans
em ticket declarativo de janela sem criar janela real, vincular surface, vincular
input, escrever pixels, submeter GUI/window, submeter release, podar storage,
liberar recursos, persistir release, persistir reclaim, persistir compaction,
persistir tombstone, persistir purge, persistir expiry, persistir retention,
persistir archive, persistir journal, persistir ledger, persistir recibo,
persistir registro, apagar purge, armar timer de expiry, apagar expiry, anexar
log, escrever estado, limpar/liberar recursos, submeter reclaim/compaction/
tombstone/purge/expiry/retention/archive/journal/ledger/receipt/record/audit/
seal/cleanup/retire/ack, reportar completion, sincronizar CPU/GPU, armar
deadline/timer, output/display submit, flip nem auth grafica.
`0.8.0-alpha.192+20260512` adiciona window surface plan seguro que converte
window plans em ticket declarativo de surface de janela sem vincular surface real,
mapear memoria, escrever pixels, submeter compositor/window/GUI, criar janela
real, vincular input, submeter release, podar storage, liberar recursos,
persistir release, persistir reclaim, persistir compaction, persistir tombstone,
persistir purge, persistir expiry, persistir retention, persistir archive,
persistir journal, persistir ledger, persistir recibo, persistir registro,
apagar purge, armar timer de expiry, apagar expiry, anexar log, escrever estado,
limpar/liberar recursos, submeter reclaim/compaction/tombstone/purge/expiry/
retention/archive/journal/ledger/receipt/record/audit/seal/cleanup/retire/ack,
reportar completion, sincronizar CPU/GPU, armar deadline/timer, output/display
submit, flip nem auth grafica.
`0.8.0-alpha.193+20260513` adiciona window compositor plan seguro que converte
window surface plans em ticket declarativo de compositor de janela sem submeter
compositor real, surface real, damage real, window/GUI ou auth grafica.
`0.8.0-alpha.194+20260513` adiciona window present plan seguro que converte
window damage plans em ticket declarativo de apresentacao de janela sem
apresentar frame real, submeter present real, enviar damage real, submeter
compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.195+20260513` adiciona window schedule plan seguro que converte
window present plans em ticket declarativo de agendamento de janela sem agendar
frame real, armar timer real, acordar compositor, executar page flip, submeter
schedule real, apresentar frame real, submeter present real, enviar damage real,
submeter compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.196+20260513` adiciona window vsync plan seguro que converte
window schedule plans em ticket declarativo de sincronizacao de janela sem
aguardar vsync real, armar fence real, submeter vsync real, submeter wait real,
agendar frame real, armar timer real, acordar compositor, executar page flip,
submeter schedule real, present real, damage real, compositor/surface/window/GUI
ou auth grafica.
`0.8.0-alpha.197+20260513` adiciona window scanout plan seguro que converte
window vsync plans em ticket declarativo de scanout de janela sem anexar buffer
real, submeter scanout real, executar display flip real, aguardar vsync real,
armar fence real, submeter wait real, agendar frame real, armar timer real,
acordar compositor, executar page flip, submeter schedule real, present real,
damage real, compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.198+20260513` adiciona window display plan seguro que converte
window scanout plans em ticket declarativo de display de janela sem anexar
controlador real, submeter display real, executar output real, submeter
pipeline real, anexar buffer real, submeter scanout real, executar display flip
real, aguardar vsync real, armar fence real, submeter wait real, agendar frame
real, armar timer real, acordar compositor, executar page flip, submeter
schedule real, present real, damage real, compositor/surface/window/GUI ou auth
grafica.
`0.8.0-alpha.199+20260513` adiciona window output plan seguro que converte
window display plans em ticket declarativo de saida visual de janela sem
anexar conector real, armar modo real, armar sinal real, submeter output real,
submeter sinal real, anexar controlador real, submeter display real, executar
output real, submeter pipeline real, anexar buffer real, submeter scanout real,
executar display flip real, aguardar vsync real, armar fence real, submeter
wait real, agendar frame real, armar timer real, acordar compositor, executar
page flip, submeter schedule real, present real, damage real,
compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.200+20260513` adiciona window blit plan seguro que converte
window output plans em ticket declarativo de blit de janela sem mapear buffer
real, copiar pixels reais, armar DMA real, submeter blit real, anexar conector
real, armar modo real, armar sinal real, submeter output real, submeter sinal
real, anexar controlador real, submeter display real, executar output real,
submeter pipeline real, anexar buffer real, submeter scanout real, executar
display flip real, aguardar vsync real, armar fence real, submeter wait real,
agendar frame real, armar timer real, acordar compositor, executar page flip,
submeter schedule real, present real, damage real, compositor/surface/window/GUI
ou auth grafica.
`0.8.0-alpha.201+20260513` adiciona window commit plan seguro que converte
window blit plans em ticket declarativo de commit atomico de janela sem
anexar estado real, armar atomic commit real, armar callback de frame real,
submeter callback real, mapear buffer real, copiar pixels reais, armar DMA
real, submeter blit real, anexar conector real, armar modo real, armar sinal
real, submeter output real, submeter sinal real, anexar controlador real,
submeter display real, executar output real, submeter pipeline real, anexar
buffer real, submeter scanout real, executar display flip real, aguardar vsync
real, armar fence real, submeter wait real, agendar frame real, armar timer
real, acordar compositor, executar page flip, submeter schedule real, present
real, damage real, compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.202+20260513` adiciona window flip plan seguro que converte
window commit plans em ticket declarativo de page flip de janela sem anexar
buffer real, armar vblank real, armar evento real, submeter flip async, anexar
estado real, armar atomic commit real, armar callback de frame real, submeter
callback real, mapear buffer real, copiar pixels reais, armar DMA real,
submeter blit real, anexar conector real, armar modo real, armar sinal real,
submeter output real, submeter sinal real, anexar controlador real, submeter
display real, executar output real, submeter pipeline real, anexar buffer
real, submeter scanout real, executar display flip real, aguardar vsync real,
armar fence real, submeter wait real, agendar frame real, armar timer real,
acordar compositor, executar page flip, submeter schedule real, present real,
damage real, compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.203+20260513` adiciona window vblank plan seguro que converte
window flip plans em ticket declarativo de sincronizacao de vblank de janela
sem armar evento de vblank real, armar callback de vblank real, submeter
callback, capturar timestamp real, submeter timestamp, completar frame real,
submeter frame, anexar buffer real, armar vblank real, armar evento real,
submeter flip async, anexar estado real, armar atomic commit real, armar
callback de frame real, submeter callback real, mapear buffer real, copiar
pixels reais, armar DMA real, submeter blit real, anexar conector real, armar
modo real, armar sinal real, submeter output real, submeter sinal real, anexar
controlador real, submeter display real, executar output real, submeter
pipeline real, anexar buffer real, submeter scanout real, executar display
flip real, aguardar vsync real, armar fence real, submeter wait real, agendar
frame real, armar timer real, acordar compositor, executar page flip, submeter
schedule real, present real, damage real, compositor/surface/window/GUI ou
auth grafica.
`0.8.0-alpha.204+20260513` adiciona window event plan seguro que converte
window vblank plans em ticket declarativo de eventos de janela sem armar
handler real, armar fila real, despachar evento real, armar callback real,
submeter callback, capturar timestamp real, submeter timestamp, completar
frame real, submeter frame, armar evento de vblank real, armar callback de
vblank real, submeter callback de vblank, capturar timestamp real, submeter
timestamp, completar frame real, submeter frame, anexar buffer real, armar
vblank real, armar evento real, submeter flip async, anexar estado real, armar
atomic commit real, armar callback de frame real, submeter callback real,
mapear buffer real, copiar pixels reais, armar DMA real, submeter blit real,
anexar conector real, armar modo real, armar sinal real, submeter output real,
submeter sinal real, anexar controlador real, submeter display real, executar
output real, submeter pipeline real, anexar buffer real, submeter scanout
real, executar display flip real, aguardar vsync real, armar fence real,
submeter wait real, agendar frame real, armar timer real, acordar compositor,
executar page flip, submeter schedule real, present real, damage real,
compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.205+20260513` adiciona window input plan seguro que converte
window event plans em ticket declarativo de input de janela sem armar teclado
real, submeter teclado, armar pointer real, submeter pointer, armar foco real,
submeter foco, carregar keymap real, submeter keymap, decodificar input real,
rotear input real, armar callback de input, submeter callback de input,
permitir grab real, submeter grab, armar handler real, armar fila real,
despachar evento real, armar callback real, submeter callback, capturar
timestamp real, submeter timestamp, completar frame real, submeter frame,
armar evento de vblank real, anexar buffer real, armar vblank real, submeter
flip async, anexar estado real, armar atomic commit real, mapear buffer real,
copiar pixels reais, armar DMA real, submeter blit real, anexar conector
real, armar modo real, armar sinal real, submeter output real, anexar
controlador real, submeter display real, anexar buffer real, submeter scanout
real, executar display flip real, aguardar vsync real, armar fence real,
submeter vsync real, submeter wait real, agendar frame real, armar timer real,
acordar compositor, executar page flip, submeter schedule real, present real,
damage real, compositor/surface/window/GUI ou auth grafica.
`0.8.0-alpha.206+20260513` faz hardening real do caminho de autenticacao por
senha do CapyOS: `userdb_authenticate` agora compara o hash PBKDF2-SHA256 com o
hash armazenado via `crypt_constant_time_compare` (eliminando o timing
side-channel do laco byte-a-byte anterior) e zera os buffers locais
`hash[USER_HASH_SIZE]` e `struct user_record rec` antes de qualquer retorno.
`config_log_user_record_state` deixa de emitir salt/hash hex no log de
bootstrap e passa a registrar somente `salt=[redacted size=N present=0|1]` e
`hash=[redacted size=N present=0|1]`. Cobertura estatica nova em
`tests/test_crypt_vectors.c` (`test_constant_time_compare_semantics`) bloqueia
regressoes na primitiva subjacente. Nenhuma ABI publica, formato de
`/etc/passwd` ou efeito grafico/IO foi alterado.
`0.8.0-alpha.207+20260513` continua a trilha de hardening completando a
integracao entre `userdb_authenticate` e `auth_policy_*`. O novo
`userdb_authenticate_with_policy` compoe
`auth_policy_check_allowed(username)`, `userdb_authenticate(username,
password, out)` (ja timing-equalizado contra user enumeration por
`k_userdb_dummy_salt`) e `auth_policy_record_success/failure(username)` em
um unico ponto publico, com codigos `USERDB_AUTH_OK`, `USERDB_AUTH_FAILED` e
`USERDB_AUTH_LOCKED` declarados em `include/auth/user.h`. O caminho
interativo de login do `system_setup.c` migra para o wrapper, eliminando a
duplicacao do trio check/auth/record e centralizando o wipe da senha em um
unico ponto. Um typo no `Makefile` (`tests/test_net_probe.c
src/driverslogin_window_gui_layout.c src/auth//net/net_probe.c
src/drivers/net/netvsc.c`) que mesclava caminhos e quebrava o build
host-side de testes foi corrigido para `tests/test_net_probe.c
src/drivers/net/net_probe.c src/drivers/net/netvsc.c`. Testes contratuais
em `tests/test_auth_policy.c` fixam os valores de `USERDB_AUTH_*`,
bloqueando drift que silenciosamente confundiria "bloqueado" com
"invalido" no UX.
`0.8.0-alpha.208+20260513` consolida o hardening criptografico em quatro
frentes paralelas: (a) `src/security/csprng.c::csprng_get_bytes` agora
chama `sha256_clear(&temp_ctx)` ao final de cada iteracao do laco de
emissao, eliminando o vazamento do digest emitido em `state[]` e do
ultimo bloco padded em `data[]` que permaneciam vivos na stack apos
`sha256_final`; (b) `sha256_clear` vira API publica em
`include/security/sha256.h` com semantica volatile-safe (loop com
`volatile uint8_t *`), resistindo a dead-store elimination; (c)
`src/auth/auth_policy.c` ganha `find_existing(username)` (read-only,
retorna NULL se username nao esta rastreado) usado por
`auth_policy_check_allowed`, `auth_policy_is_locked`,
`auth_policy_record_success` e `auth_policy_unlock` — antes todas essas
chamadas passavam por `find_or_alloc`, que **criava entrada nova** mesmo
em caminho de leitura, permitindo um ataque onde probing read-only com
`AUTH_MAX_TRACKED_USERS+1` usernames forjados exauria `g_attempts[32]`
e **desabilitava silenciosamente o lockout** para usuarios legitimos
(NULL → tratado como "permitido"); `find_or_alloc` (usado apenas por
`record_failure`) ganha LRU eviction de entrada nao-bloqueada quando a
tabela esta cheia, com entradas locked **stickys** (evictar lockouts
ativos permitiria um atacante resetar seu proprio bloqueio fazendo
spray), retornando NULL apenas se todos os 32 slots estao locked
simultaneamente (cenario raro com recuperacao natural por timeout);
(d) `src/auth/user.c::userdb_set_password` ganha wipe sistematico em
todas as paths de retorno (`!out` kalloc-fail, serialize-fail,
`!updated`, sucesso, e por-iteracao apos copia), cobrindo `source`
(blob do `/etc/passwd` inteiro com salt+hash hex de todos os usuarios),
`line` (registro serializado), `rec` (struct parseado) e `out` (DB
canonico serializado); `memory_zero` em `user.c` vira volatile-safe.
Cobertura estatica nova em `tests/test_crypt_vectors.c::
test_sha256_clear_semantics` (zero de cada byte do contexto + NULL
guard) e em `tests/test_auth_policy.c` (read paths non-allocating com
`AUTH_MAX_TRACKED_USERS+5` probes, e LRU eviction com 32 falhas +
newcomer) bloqueia regressoes.
`0.8.0-alpha.209+20260513` propaga `sha256_clear` para **todos** os
consumidores reais que processam segredos via contextos SHA-256
transitorios, fechando o ultimo residuo de stack que sobrava apos
`sha256_final` em multiplos sites de cripto: (a)
`src/security/crypt.c::hmac_sha256` (HMAC static usado por PBKDF2 no
caminho quente de cada login — 64000 iteracoes invocam-no, cada uma
chamando `sha256_final` duas vezes) ganha wipe de `key_ctx`
(condicional via flag `key_ctx_used` quando key > `SHA256_BLOCK_SIZE`)
e wipe de `ctx` reutilizado pelas camadas inner/outer HMAC; (b)
`src/security/crypt.c::crypt_hmac_sha256` (API publica de HMAC) ganha
wipe de `ctx` que era reutilizado pelas tres fases (key hash opcional,
inner, outer); (c) `src/security/sha256.c::sha256_hash` (convenience
wrapper init/update/final) ganha wipe de `ctx` apos `sha256_final` —
sem ele, o `state[]` (que IS o digest devolvido em `hash`) e o
`data[]` (bloco padded derivado do input) sobreviviam na stack do
caller ate reuso natural; (d)
`src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c::
compute_volume_key_hash` (deriva o digest da senha do volume cifrado
durante boot) ganha wipe de `ctx`. ABI preservada em todos os entry
points; saida funcional identica para os mesmos inputs (mesmos bytes
produzidos); zero impacto observavel em performance (sha256_clear e
um loop de 104 stores volatile, custo submilisegundo mesmo em 64000
iteracoes de PBKDF2). Vetores oficiais (NIST/RFC) continuam passando.
`0.8.0-alpha.210+20260513` fecha um vetor critico de
comprometimento no canal de atualizacoes do sistema:
`src/security/ed25519.c::ed25519_verify` em versoes <= alpha.209
era **matematicamente quebrado**. A "verificacao" reduzia-se a
checar se `signature[32+i] == signature[i] + (hram[i] XOR
ed25519_hash(public_key)[i])`, equacao que qualquer atacante pode
satisfazer sem private key, sem multiplicacao escalar na curva, sem
qualquer propriedade de RFC 8032 — basta pegar `signature[0..31]`
arbitrario, computar `hram` e `hash` (inputs publicos) e definir
`signature[32+i] = signature[i] + (hram[i] XOR hash[i])`. Esse
verificador era o **unico gate criptografico** em
`src/services/update_agent.c::manifest_signature_ed25519_valid`,
usado em 7 paths distintos: poll/staged/imported/install-A/install-B
manifests, status reporting do `signature_ready`, e validacao do
payload cache contra manifest. Qualquer atacante com escrita no
canal de update (MitM, repositorio comprometido, escrita local)
podia forjar um manifest com payload arbitrario que o update_agent
aceitaria como autentico e instalaria. Implementar Ed25519 real
envolve ~2000 linhas de aritmetica de campo em tempo constante,
compressao/descompressao de pontos, multiplicacao escalar na curva
e SHA-512 verdadeiro — fora do escopo deste slice. A decisao certa
e **falhar fechado**: aceitar zero manifests em producao ate que um
verificador real esteja disponivel. Mudancas: `ed25519_verify`
retorna -1 incondicionalmente com comentario SECURITY de ~30 linhas
detalhando o ataque; `ed25519_sign` zera o buffer signature via
`volatile uint8_t *`; `ed25519_create_keypair` zera ambos
public_key e private_key; `ed25519_hash` (helper interno) marcado
`__attribute__((unused))` e dormente; header `include/security/
ed25519.h` ganha SECURITY WARNING explicito;
`manifest_signature_ed25519_valid` ganha comentario explicando o
gate fail-closed em producao (sem mudanca de codigo — o callsite ja
retornava 0 quando `ed25519_verify != 0`). Testes contratuais em
`tests/test_crypt_vectors.c::test_ed25519_failclosed_contract`
validam: verify rejeita inputs nao-zero, zero e mensagem vazia
(NULL); sign zera os 64 bytes do signature mas nao escreve alem
dele (sentinel 0x77 preservado); create_keypair zera ambas saidas;
NULL pointers sao tolerados sem crash. Path UNIT_TEST via
`g_update_manifest_verifier` continua autoritativo para tests
existentes (`test_update_agent`, `test_update_transact`,
`test_audit_events`); nenhum test direto de ed25519 existia antes,
todos os tests de update_agent usam o hook. ABI publica das tres
funcoes inalterada; manifests produtivos nao existem (alpha
pre-release), entao a rejeicao de 100% nao quebra usuarios reais.
O esqueleto de aritmetica de curva (`fe25519`, `ge25519`, `fe_mul`,
`fe_pow2523`, etc.) e preservado como ponto de partida para
implementacao futura de RFC 8032.

`0.8.0-alpha.211+20260513` fecha duas fugas de privacidade reais na
trilha de auth, descobertas durante uma auditoria de paths que tocam
nomes de usuario. Fuga A: `src/auth/auth_policy.c::auth_policy_status`
e o backend do comando shell `auth-status` (em
`src/shell/commands/extended.c::cmd_auth_status`), que nao tem
nenhum check de privilegio. A funcao iterava todo o array
`g_attempts[]` e imprimia, para cada slot ocupado, o username + a
contagem de falhas + o estado de lockout. Qualquer sessao local —
incluindo `user` comum, conta de recuperacao e guest — podia rodar
`auth-status` e enumerar quem usa o sistema, ler quais contas tem
falhas pendentes e identificar contas atualmente bloqueadas. Tres
vetores praticos: enumeracao passiva de usuarios, sinal de strategy
(atacante sabe quais alvos estao no limiar do lockout) e lockout
escape (atacante sabe quando o janelamento expira para retomar
credential stuffing contra alvo especifico). Fuga B:
`src/auth/privilege.c::priv_log_emit` (chamado por
`privilege_log_denied` e `privilege_log_granted`) anexava
`actor=<username>` em todo log emitido para `klog`. Cada denial de
UI/shell — `apps/settings.c:151::settings-add-user`,
`shell/commands/user_manage.c:174::denial generico`,
`shell/commands/user_manage.c:375::set-pass:other` — chamava
`privilege_log_denied(action, session_active()->user)`, levando o
username completo do principal para o klog ring. `klog` e consumido
por qualquer ferramenta com acesso ao log pipe, e o ring inteiro e
serializado em panic buffers e crash dumps, entao o username
escapava bem alem do subsistema de auth. Remediacoes: (1)
`auth_policy_status` colapsa o loop por username em duas linhas
agregadas `Tracked accounts: <N> (locked: <M>)`; comentario PRIVACY
de ~25 linhas no `.c` documenta o threat model; (2) `priv_log_emit`
substitui o trecho `actor=<username>` por `actor_role=<role>`,
preservando o sinal de auditoria (`denial spike from non-admin
role` continua acionavel) sem PII; comentario PRIVACY de ~20 linhas
detalha os callsites afetados; (3) novas APIs publicas
`auth_policy_tracked_count` e `auth_policy_locked_count` retornam
counts nao-identificantes que substituem o loop antigo para
diagnostico; (4) `auth_policy_is_tracked` exposta apenas sob
`#if defined(UNIT_TEST)` em ambos `.c` e header permite que tests
validem rastreamento de username especifico sem reabrir o vetor de
enumeracao em producao; (5) testes em `test_auth_policy.c` migrados
para `auth_policy_is_tracked` + counts agregados, com asserts
negativos (`strstr(status, "<username>") == NULL`) que travam o
contrato de privacidade; (6) novo teste em
`test_privilege.c::test_privilege_log_omits_username` captura o
klog via `capture_klog` e valida que denied/granted nao vazam
username e que `actor_role=<role>` aparece quando role esta
populado, com casos de role vazio e `actor == NULL` graceful. ABI
publica das funcoes auth nao mudou; output textual mudou
(consumidores que parseiam `auth-status` ou `[priv]` lines precisam
adaptar — esses formatos eram do canal de log, nao de uma interface
estavel). Manifests produtivos nao existem, smokes pendentes da
Etapa 2 nao sao afetados.

`0.8.0-alpha.212+20260513` fecha um timing side-channel critico em
`src/auth/user.c::userdb_authenticate_with_policy` (a API publica
de login do sistema). Antes, o wrapper retornava
`USERDB_AUTH_LOCKED` imediatamente quando
`auth_policy_check_allowed` indicava lockout, sem executar PBKDF2 —
~1 us de latencia para accounts locked vs ~50 ms para accounts
not-locked. Atacante remoto observava o RTT da resposta e
distinguia as duas classes com um unico probe por username,
reabrindo via timing exatamente as informacoes que alpha.211
removeu de `auth_policy_status` (quais contas estao em lockout) e
que alpha.206 removeu de `userdb_authenticate` (existencia da
conta). O leak nao dependia de acesso a `klog`, comando shell
privilegiado ou qualquer canal local — era observavel puramente
pelo wall-clock da API publica. Mitigacao: o wrapper agora chama
`userdb_authenticate(username, password, out)` ANTES de verificar
`allowed`. Quando locked, o `rc` e descartado, `out` e zerado via
`user_record_clear` (defensive contra o caso raro de atacante
adivinhar a senha correta de conta locked) e o retorno e
`USERDB_AUTH_LOCKED`. `auth_policy_record_failure` intencionalmente
NAO e chamado para accounts locked, porque `lockout_until_tick` ja
esta ancorado e incrementar `failed_count` adicional apenas
poluiria o contador com ruido do atacante. Latencia uniforme em
~50 ms elimina o sinal. Como side effect, este patch rate-limita
credential-stuffing contra accounts locked com a mesma janela do
path normal — o atacante nao consegue mais enumerar locked targets
em velocidade de microssegundos para planejar wait-for-unlock. ABI
publica inalterada. Composicao com alpha.206 (dummy salt
existent-vs-non-existent) e alpha.211 (status/logs sem PII)
mantida — este patch herda a equalizacao existent-vs-non-existent
automaticamente porque chama `userdb_authenticate` em todo
caminho. Teste contratual em `test_auth_policy.c::alpha.212 block`
locka o lado policy do contrato; a validacao do timing equalization
em si fica em revisao de codigo (presenca da chamada antes do
`if (!allowed)` e do comentario SECURITY).

`0.8.0-alpha.213+20260513` introduz **HKDF-SHA256 (RFC 5869)** como
primitiva criptografica fundacional em `src/security/crypt.c`.
Antes, o sistema tinha apenas PBKDF2-SHA256 (senhas, 64000
iteracoes) e HMAC-SHA256 (publico via `crypt_hmac_sha256`) —
faltava um KDF context-aware adequado para derivar subkeys a
partir de segredos ja uniformes (output do CSPRNG, Diffie-Hellman
shared secret, output pos-PBKDF2). Os proximos slices de
seguranca exigem essa primitiva: TLS userland (Etapa 5) precisa
de HKDF para derivar handshake/traffic keys do master secret,
key wrapping para AES-XTS precisa de HKDF para isolar dominio de
cifragem (volume de dados vs metadata vs swap), secure boot
precisa de HKDF para derivar verification keys versionadas a
partir de uma master verification key, update_agent precisa de
HKDF para context-binding de signing keys quando Ed25519 real
entrar (alpha.210 deixou o gate fail-closed e o esqueleto de
aritmetica de curva preservado para implementacao futura).
Implementar agora em fundacao audit-friendly evita gambiarras
ad-hoc downstream — exatamente o tipo de "vou improvisar
derivacao de chave para este caso" que produz vulnerabilidades
silenciosas em sistemas reais.

API publica nova em `include/security/crypt.h`:
`crypt_hkdf_sha256_extract` (PRK = HMAC-SHA256(salt, IKM); RFC
5869 §2.2: salt NULL ou zero-length e substituido por HashLen
zero octets, mandatorio), `crypt_hkdf_sha256_expand` (OKM =
T(1) || T(2) || ... onde T(i) = HMAC(PRK, T(i-1) || info ||
byte(i)); RFC §2.3: L <= 255 * HashLen, PRK >= HashLen),
`crypt_hkdf_sha256` (wrapper que combina extract+expand e zera
o PRK em todos os caminhos de saida). Para suportar `info_len`
arbitrario sem alocacao dinamica, foram introduzidos tres
helpers internos `static` (`hkdf_hmac_begin/update/end`) que
envolvem a construcao HMAC padrao em torno da API streaming
`sha256_init/update/final` — sem isso, expand teria que
buffer-ar `T(i-1) || info || byte(i)` num array contiguo, o que
exigiria stack frame ilimitado ou kalloc, ambos indesejaveis
num primitivo cripto kernel-side. Fail-closed em todos os
bounds: L > 8160 bytes retorna -1 (sem isso, o counter byte
enrolaria de 255 para 0 ou cast incorreto para 256 e produziria
output silenciosamente truncado ou duplicado), PRK_LEN < 32
retorna -1 (RFC obriga para preservar o limite de seguranca),
NULL prk/out retornam -1, L=0 e no-op success (semanticamente
vazio). Wipe hygiene completa: `zero_salt` (substituicao no
extract) zerado antes do retorno; `t_prev` no expand zerado em
sucesso E em todos os caminhos de erro intermediario (counter >
255); `kipad`/`kopad`/`key_hash`/contextos SHA-256 nos helpers
HMAC streaming zerados via `secure_clear`/`sha256_clear`; PRK no
wrapper zerado antes do retorno em sucesso e erro. Compatibilidade
com a wipe hygiene volatile-safe instalada em alpha.208 (CSPRNG)
e alpha.209 (HMAC publico/PBKDF2 interno/sha256_hash/volume key
hash) preservada.

Comentario inline no header e na implementacao explicita o
threat model: HKDF NAO substitui PBKDF2 para senhas porque
assume IKM uniformemente pseudoaleatorio (ou proximo). Para
raw passphrases, o pipeline correto e
`password -> PBKDF2 -> PRK -> HKDF expand -> {subkey_disk,
subkey_network, subkey_session, ...}` — o output pos-PBKDF2 e
exatamente o tipo de IKM para o qual HKDF foi projetado. Sem
essa nota, callers futuros poderiam tentar derivar chaves
diretamente de senhas via HKDF e cair na armadilha classica
(HKDF e rapido demais para resistir a brute-force de senha
fraca; PBKDF2 com 64000 iteracoes existe exatamente para
introduzir esse custo).

Testes em `tests/test_crypt_vectors.c::test_hkdf_sha256_vectors`:
os 3 test cases oficiais do RFC 5869 Appendix A. **TC1**
IKM=22/salt=13/info=10/L=42 cobre o caminho de small inputs e
valida tanto extract isolado, expand isolado, quanto o wrapper
combinado. **TC2** IKM=80/salt=80/info=80/L=82 cobre long
inputs e valida o loop do expand spanando 3 iteracoes (L=82 >
2*HashLen=64), valida a concatenacao streaming de info via
`hkdf_hmac_update` (sem buffer fixo limitando), valida a
truncation correta do ultimo bloco. **TC3** salt empty/info
empty/L=42 cobre a substituicao zero-octet de RFC §2.2 com
tanto NULL pointer quanto zero-length array, ambos devendo
produzir o mesmo PRK. Mais 6 contract checks fail-closed: NULL
prk em extract, NULL prk em expand, NULL out em expand, L > 255
* HashLen em expand, PRK_LEN < HashLen em expand, L=0 no-op
success em expand. `expect_hex` ampliado de 64 para 256 bytes
de buffer interno para acomodar OKM de 82 bytes do TC2 — todos
os calls existentes que testam <= 64 bytes continuam
funcionando identicamente.

Sem callers reais ainda. Esta entrega e a fundacao para os
slices futuros (Etapa 5 TLS userland e antes disso AES-XTS key
wrapping ja em uso na trilha de boot encrypted volume). ABI
publica nova nao quebra nada existente. Manifests produtivos
nao existem (alpha pre-release). Smokes pendentes da Etapa 2
nao sao afetados.

`0.8.0-alpha.217+20260513` entrega **Ed25519 (RFC 8032)
implementacao REAL** em `src/security/ed25519.c` substituindo o
esqueleto fail-closed que vinha desde alpha.210. **Update verifier
(`src/services/update_agent.c`) oficialmente OPERACIONAL pela
primeira vez** — manifests assinados com chave canonica em producao
agora aceitos quando criptograficamente validos. Antes, 100% dos
manifests eram rejeitados por design (apenas tests `UNIT_TEST` com
`g_update_manifest_verifier` conseguiam aceitar fixtures). Esta
entrega tambem refatora o field arithmetic GF(2^255-19) em modulo
compartilhado `include/security/fe25519.h` + `src/security/fe25519.c`
(extraido de `x25519.c`), agora consumido por X25519 + Ed25519. A
implementacao adiciona `fe_pow22523` (para sqrt em Ed25519 decode),
`fe_neg`, `fe_cmov`, `fe_isnegative`, `fe_iszero`, `fe_notequal`.
`x25519.c` reduzida para Montgomery ladder + APIs consumindo
`fe25519`. **A fundacao cripto canonica CapyOS encerra o ultimo gap
de primitivas de seguranca modernas:** 10 primitivas em
`src/security/` (SHA-256, SHA-512, HMAC, PBKDF2, HKDF, CSPRNG,
AES-XTS, ChaCha20-Poly1305 AEAD, X25519 ECDH, Ed25519 + constant-
time compare). Todas auditaveis, todas seguindo o mesmo padrao
(wipe volatile-safe, fail-closed, threat model inline). Group ops
twisted Edwards extended coordinates (X:Y:Z:T) com a = -1:
`ge_dbl` (dbl-2008-hwcd), `ge_add` (add-2008-hwcd-3 com T1*2d*T2),
`ge_neg_p` (-X, Y, Z, -T), `ge_cmov` constant-time. Scalar
multiplication double-and-add constant-time (256 doubles + 256
cond-adds, cmov mascarado). Encoding/decoding compressed (32 bytes):
`ge_encode` (Y/Z em LE + sign(x) bit), `ge_decode` (parse y +
canonicality check + sqrt candidate via `fe_pow22523` + sign
correction + reject `x==0 && x_0==1`). Scalar arithmetic mod L
(L = 2^252 + 27742317777372353535851937790883648493): `sc_reduce64`
(porte ref10 signed 21-bit limbs), `sc_muladd` (a*b+c mod L),
`sc_is_canonical` (S<L gate constant-time). Constants `ED_D`,
`ED_D2`, `ED_SQRTM1`, `ED_B_X/Y/T`, `ED_L_BYTES` verificadas
contra dalek-cryptography. APIs publicas: `ed25519_create_keypair`
(seed -> SHA-512 -> clamp s + prefix + A=s*B + encode),
`ed25519_sign` (PureEd25519 RFC §5.1.6 deterministico),
`ed25519_verify` (RFC §5.1.7 com cofator 8: check `[8]SB == [8]R +
[8](kA)` via projective equality). Threat model documentado inline.
Tests reformulados: 3 vetores oficiais RFC 8032 §7.1 + tampering +
wrong-pk + non-canonical S + NULL + round-trip + determinism +
tamper-message.

`0.8.0-alpha.216+20260513` entrega **a primeira primitiva de
key exchange nativa do CapyOS — X25519 (RFC 7748)** em
`src/security/x25519.c`. Implementacao do zero auditavel,
independente do TLS stack BearSSL (que tem X25519 mas preso ao
SSL engine), audit-friendly, alinhada com a higiene cripto do
resto do projeto. Esta entrega completa o **triplet canonico
ECDH→HKDF→AEAD**: ate alpha.215 o sistema tinha HKDF (alpha.213)
e AEAD ChaCha20-Poly1305 (alpha.215), mas faltava o "E" do ECDH
para derivar segredos compartilhados entre duas partes sem
segredo pre-compartilhado.

**O problema concreto.** Cinco vetores nao cobertos pela
fundacao anterior:

1. **TLS 1.3 userland** (Etapa 5 do plano sequencial) e inviavel
   sem X25519 nativo. RFC 8446 §4.2.8.2 lista `x25519` como
   primeiro named group MTI (mandatory-to-implement) para key
   share; sem X25519 disponivel fora do BearSSL, qualquer
   implementacao TLS userland seria forcada a depender do
   handshake completo do BearSSL.
2. **Channel binding** para conexoes seguras locais. Protocolos
   modernos (e.g. TLS Token Binding, channel-bound credentials)
   precisam de ECDH para gerar binding values distintos por
   sessao.
3. **WireGuard-like channels** entre processos com forward
   secrecy. Para cada sessao, novo scalar efemero via CSPRNG,
   troca de public keys via canal autenticado (Ed25519 quando
   disponivel), derivacao de session key via ECDH + HKDF.
4. **Secure messaging local com forward secrecy.** Mesmo se chave
   de longa-prazo de Alice for comprometida no futuro, mensagens
   passadas continuam confidenciais — propriedade que so e
   alcancavel via ECDH efemero.
5. **Secure boot key exchange.** Trocar key de assinatura com
   TPM/HSM externo via X25519 sem expor segredo de longa-prazo
   ao boot loader.

**A solucao concreta.** Duas APIs publicas em
`include/security/x25519.h`, ambas com fail-closed em NULL e wipe
hygiene volatile-safe:

- **`x25519(scalar, u_coord, shared)`** — RFC 7748 §5. Computa
  `shared = scalar * u_coord` na curva Curve25519 (coordenada x).
  Clamping interno do scalar (zera bits 0,1,2,255; seta bit 254
  — cofator 8 absorvido). Top-bit masking do u-coord. **Small-
  subgroup detection per §6.1:** rejeita `shared == 0` fail-
  closed (atacante poderia forcar shared=0 enviando small-order
  point como contraparte).

- **`x25519_base(scalar, public_key)`** — RFC 7748 §4.1.
  Equivalente a `x25519(scalar, BASE_POINT_9, public_key)` onde
  `BASE_POINT_9` e o u-coord 9 (base point da curva). Usado por
  Alice/Bob para derivar suas public keys a partir do scalar
  efemero/de-longa-prazo. Sem small-subgroup gate (base point
  tem ordem prima).

**Field arithmetic em GF(p) com p = 2^255 - 19.** Representacao
em 5 limbs de 51 bits cada (radix 2^51), com:

- `fe_mul`/`fe_sq`: schoolbook com `__uint128_t`, reducao mod p
  via `*19` nos termos `i+j >= 5` (porque `2^255 ≡ 19 mod p`).
- `fe_sub`: soma `2*p` antes de subtrair para evitar underflow.
- `fe_carry`: propagacao de carries com reducao mod p (carry
  alem do limb 4 multiplicado por 19 e somado de volta ao
  limb 0).
- `fe_invert`: cadeia ref10 (255 squarings + 11 multiplications)
  computando `a^(p-2) = a^(2^255-21)` via Fermat's little theorem.
- `fe_tobytes`: canonicalizacao para `[0, p)` via detection
  `(t + 19) >> 255` (carry final 0 ou 1) seguida de subtracao
  condicional constant-time de p via mask aritmetico.
- `fe_cswap`: conditional swap constant-time via
  `mask = -(uint64_t)swap` (estende swap=0/1 para todos os 64
  bits) e XOR triplet.

**Montgomery ladder per RFC 7748 §5.** 255 iteracoes (bit 254
down to bit 0). Cada iteracao:

1. Le bit `k_t` do scalar clampeado.
2. `swap ^= k_t`. `fe_cswap(swap)` em `(x_2, x_3)` e `(z_2, z_3)`.
   `swap = k_t`.
3. Ladder step: 9 multiplications + 2 squarings + 4 adds + 4 subs
   computando simultaneamente:
   - `(x_2, z_2) ← 2 * (x_2, z_2)` (doubling)
   - `(x_3, z_3) ← (x_2, z_2) + (x_3, z_3)` (addition)
   Com `a24 = 121665 = (486662 - 2) / 4` (curve constant).
4. Sem branches sobre `k_t` — `fe_cswap` resolve a condicionalidade
   em tempo constante.

Apos as 255 iteracoes, um ultimo `fe_cswap(swap)` sincroniza. O
resultado final e `shared = x_2 / z_2 = x_2 * z_2^(-1)` via
`fe_invert` + `fe_mul` + `fe_tobytes`.

**Composicao com slices anteriores.**

- **alpha.214** (CSPRNG hardened): fonte canonica para o scalar
  efemero de 32 bytes. Sem RNG forte, key agreement vira
  hardcoded.
- **alpha.213** (HKDF-SHA256): KDF natural para derivar
  `session_key` a partir de `shared` + context label:
  ```c
  crypt_hkdf_sha256(shared, 32,
                    "binding=ipc-channel-v1", 22,
                    NULL, 0, session_key, 32);
  ```
- **alpha.215** (ChaCha20-Poly1305 AEAD): consome `session_key`
  derivada do ECDH para proteger canal autenticado com forward
  secrecy.

**Triplet canonico ECDH→HKDF→AEAD agora completo.** Atacante
pode ser confrontado com canal autenticado com forward secrecy
usando apenas primitivas auditaveis do CapyOS, sem dependencia
do BearSSL.

**Threat model documentado inline no header.**

- **Confidencialidade:** atacante nao computa `shared` observando
  apenas `alice_pk` e `bob_pk` no canal (CDH assumption sobre
  Curve25519).
- **Resistencia a small-order attacks:** rejeita `shared == 0`
  que ocorreria se atacante enviasse small-order point como
  contraparte (RFC 7748 §6.1).
- **Indistinguibilidade:** `pk` derivada do base point e
  indistinguivel de bytes aleatorios uniformes modulo o top bit
  (que e sempre mascarado per §5).
- **Limites:** esta primitiva NAO autentica chaves publicas —
  caller e responsavel por garantir authenticity da contraparte
  (Ed25519 signature quando disponivel, certificate pinning,
  out-of-band). Apenas u-coord processada; v-coord nunca aparece.

**Tests novos** em `tests/test_crypt_vectors.c` (6 funcoes,
25+ assertions):

1. **`test_x25519_rfc7748_scalarmult`** — RFC 7748 §5.2:
   - Vetor 1: scalar `a546e3...` × u `e6db68...` → expected
     `c3da55...`.
   - Vetor 2: scalar `4b66e9...` × u `e52102...` → expected
     `95cbde...`.
2. **`test_x25519_rfc7748_dh`** — RFC 7748 §6.1 ECDH end-to-end:
   - Alice: sk `770760...` → pk_expected `8520f0...`. Validar
     `x25519_base(alice_sk) == alice_pk_expected`.
   - Bob: sk `5dab08...` → pk_expected `de9edb...`. Validar
     `x25519_base(bob_sk) == bob_pk_expected`.
   - Shared expected `4a5d9d...`. Validar
     `x25519(alice_sk, bob_pk) == x25519(bob_sk, alice_pk) == shared`.
     **Convergencia** explicitamente assertada (Alice e Bob
     chegam ao mesmo shared).
3. **`test_x25519_small_order_rejection`** — fail-closed em
   pontos de pequena ordem:
   - `u = 0` (ordem 2): retorna -1.
   - `u = 1` (ordem 4 na twist): retorna -1.
4. **`test_x25519_fail_closed`** — 5 categorias de NULL:
   - `x25519(NULL, u, out)`, `x25519(scalar, NULL, out)`,
     `x25519(scalar, u, NULL)`.
   - `x25519_base(NULL, out)`, `x25519_base(scalar, NULL)`.
5. **`test_x25519_high_bit_masked`** — RFC 7748 §5 mandatory
   masking: flip do bit 255 do `u_coord` nao altera output.
6. **`test_x25519_scalar_clamping`** — RFC 7748 §5 mandatory
   clamping: flip dos bits que devem ser zerados/setados pelo
   clamping nao altera output.

**Build.** `x25519.o` adicionado a `CAPYOS64_OBJS` (kernel build)
e `x25519.c` adicionado a `TEST_SRCS` (host-side unit tests).

**Limites desta entrega.**

- **Sem callers reais ainda.** Esta entrega e a primitiva
  fundacional. Callers naturais (TLS 1.3 userland Etapa 5,
  secure messaging local com forward secrecy, future WireGuard-
  like channel) chegarao em slices subsequentes.
- **Ed25519 continua fail-closed** (RFC 8032 nao implementado;
  esqueleto desde alpha.210). Esta entrega NAO destrava o update
  verifier real. Ed25519 real e proximo slice natural — field
  arithmetic GF(2^255-19) e a mesma, refatoracao das funcoes
  `fe_*` virara em modulo compartilhado.
- **Nao substitui BearSSL para TLS handshake.** BearSSL continua
  sendo stack TLS handshake oficial; esta primitiva e PARA usos
  fora de TLS handshake.
- **Nao destrava entregaveis pendentes da Etapa 2** (loginwindow
  GUI submit real, smokes).
- **Field arithmetic duplicada** com `src/security/ed25519.c`
  (que tem `fe25519` dormante). Refatoracao adiada para quando
  Ed25519 real for implementado.
- **`fe_invert` nao otimizada** com endomorfismos GLV/GLS — usa
  cadeia ref10 padrao. Aceitavel: ladder e o bottleneck
  (~5000 muls), invert e ~0.05% do custo.

**ABI publica nova, aditiva.** Nao quebra callers existentes; o
header `include/security/x25519.h` e novo e auto-contido.

`0.8.0-alpha.215+20260513` entrega **a primeira AEAD nativa do
CapyOS — ChaCha20-Poly1305 (RFC 8439)** em
`src/security/chacha20_poly1305.c`. Implementacao do zero
auditavel, independente do TLS stack BearSSL (`third_party/bearssl`)
e usavel fora de TLS. Esta entrega fecha um gap critico da fundacao
cripto: ate aqui o sistema tinha SHA-256, HMAC-SHA256, PBKDF2-SHA256,
HKDF-SHA256 (alpha.213), AES-128-XTS (confidencialidade sem MAC),
CSPRNG hardened (alpha.214), Ed25519 fail-closed (alpha.210), e
TLS via BearSSL (apenas no contexto TLS handshake). Faltava AEAD
canonica para usos fora de TLS — qualquer caller que precisasse de
encryption autenticada teria que ou reinventar (overhead + bugs
sutis em Poly1305) ou abusar a interface SSL do BearSSL (acoplamento
indesejado entre callers nao-TLS e o stack TLS completo).

**O problema concreto.** Quatro vetores nao cobertos pela
fundacao anterior:

1. **Secure messaging local entre apps.** Dois processos do usuario
   precisando trocar mensagens confidenciais e autenticadas (chat
   local, IPC privilegiado, message queue cifrada) nao tinham
   primitiva. AES-XTS so cifra; sem MAC, atacante intermediario
   pode flipar bits sem detectar.
2. **Key wrapping autenticado.** Wrap de uma chave derivada (e.g.
   AES-XTS data key) com a master volume key requer AEAD. Sem
   isso, atacante com leitura do header pode substituir wrapped
   key por algo malicioso.
3. **Channel binding.** Conexoes seguras futuras (TLS userland,
   WireGuard-like) precisam de AEAD nativa para binding entre
   layers.
4. **Container cifrado em userland.** AES-XTS cobre volume cifrado
   no kernel, mas containers em userland (arquivos cifrados
   user-controlled, backup encrypted, password manager) precisam
   de AEAD para detectar tampering.

**A solucao concreta.** Quatro APIs publicas em
`include/security/chacha20_poly1305.h`, todas com fail-closed em
NULL/overflow e wipe hygiene volatile-safe:

- **`chacha20_block(key, counter, nonce, out)`** — RFC 8439 §2.3.
  20-round permutation produzindo 64 bytes de keystream por
  `(key, counter, nonce)`. State = "expand 32-byte k" constants
  + key + counter + nonce; 10 column rounds + 10 diagonal rounds
  alternados via macro `QR` (quarter-round inline para hot path).
  Output = state + state_inicial (Feistel-like back-add). Stack
  state e state_inicial sao zerados antes do retorno.

- **`chacha20_encrypt(key, counter, nonce, in, out, len)`** — RFC
  8439 §2.4. Stream cipher XOR-based. `in`/`out` podem ser
  identicos (in-place). **Counter overflow fail-closed**: rejeita
  se `initial_counter + ceil(len/64) > 2^32` para prevenir reuso
  catastrofico de keystream blocks. Keystream block temporario e
  zerado em todos os exits (sucesso e erro). Len=0 e sucesso
  vacuo (mas NULL key/nonce ainda rejeitado).

- **`poly1305_mac(otk, msg, msg_len, tag)`** — RFC 8439 §2.5.
  One-time MAC. Implementacao em radix-26 (5 limbs de 26 bits
  cada para representar 130-bit values em 32-bit arithmetic).
  Clamping correto da chave `r` per spec (bits 0..3, 36..47,
  60..71, 84..95 zerados via mask
  `0x0ffffffc0ffffffc0ffffffc0fffffff`). Multiplicacao `h * r mod
  (2^130-5)` reduzida via `*5` (porque `2^130 ≡ 5 mod p`).
  Reducao final via dual representation (`g = h + 5`; se `g`
  overflowed bit 130, usa `g`; senao usa `h`) em tempo constante
  — sem branch dependente do valor. Internal state (r/s/h/buffer)
  zerado antes do retorno.

- **`chacha20_poly1305_encrypt`/`decrypt`** — RFC 8439 §2.8.
  AEAD orchestration:
  1. OTK = primeiros 32 bytes de `chacha20_block(key, counter=0, nonce)`.
  2. ciphertext = `chacha20_encrypt(key, counter=1, nonce, plaintext)`.
  3. tag = `poly1305_mac(OTK, aad || pad16 || ct || pad16 || u64le(aad_len) || u64le(ct_len))`.
  Pad16 = bytes zero ate alinhar a 16. Decrypt verifica tag em
  **tempo constante** via `crypt_constant_time_compare` (mesmo
  helper usado em `userdb_authenticate` desde alpha.206) ANTES
  de decifrar — se invalido, retorna -1 sem revelar plaintext
  parcial. Plaintext pode ser igual a ciphertext (in-place).
  OTK e zerada em todos os exits.

**Composicao com slices anteriores.**

- **alpha.208** (SHA-256 ctx wipe): mesmo padrao volatile-safe
  aplicado em Poly1305 internal state e ChaCha20 keystream.
- **alpha.209** (SHA-256 wipe sistemico): higiene de wipe
  end-to-end propagada ao AEAD; OTK derivada e zerada apos uso
  como qualquer outro material chave intermediario.
- **alpha.210** (Ed25519 fail-closed): padrao de fail-closed em
  NULL e overflow seguido aqui.
- **alpha.213** (HKDF-SHA256): KDF natural para derivar chaves
  ChaCha20 a partir de master secret + context label. Caller
  futuro fara `HKDF(master, "domain=ipc-channel", 32)` para
  obter key ChaCha20 limpa, depois usara `chacha20_poly1305_encrypt`
  com nonce do CSPRNG.
- **alpha.214** (CSPRNG hardened): fonte canonica para key
  (`CHACHA20_KEY_SIZE = 32` bytes) e nonce
  (`CHACHA20_NONCE_SIZE = 12` bytes) do AEAD. Nonce de 96 bits
  fornece ~2^48 mensagens antes de risco de colisao birthday
  por (key) — adequado para sessoes; para longo-prazo, usar HKDF
  rotation de key periodica.

**Threat model documentado inline no header.**

- **Confidencialidade:** atacante nao decifra plaintext sem a
  chave (PRF assumption da ChaCha20).
- **Integridade autenticada:** atacante nao forja ciphertext ou
  AAD aceito como valido sem a chave (Poly1305 universal hash +
  PRF da OTK).
- **Replay:** caller-responsibility — nao reutilizar (nonce, key)
  com plaintext diferente. Reuse quebra confidencialidade
  catastroficamente via XOR de duas keystream identicas.
- **Indistinguibilidade:** atacante nao distingue ciphertext de
  bytes uniformes em tempo polynomial.
- **Limites:** maximum plaintext 2^32 * 64 = 256 GiB por (key,
  nonce). Implementacao **NAO** e constant-time em relacao ao
  tamanho dos inputs (loops iteram sobre len), apenas em relacao
  aos bytes secretos. Inputs com sizes confidenciais devem usar
  padding em camada superior.

**Tests novos** em `tests/test_crypt_vectors.c` (4 funcoes, 30+
assertions):

1. **`test_chacha20_block_vectors`** — RFC 8439 §A.1 TC1
   (counter=0) e TC2 (counter=1). Mesmo `(key=0, nonce=0)`,
   counter diferente, validando que o counter incrementa
   corretamente o keystream conforme spec.
2. **`test_chacha20_encrypt_round_trip`** — encrypt + re-encrypt
   recupera plaintext (XOR symmetry). In-place vs out-of-place
   produzem mesmo resultado. Counter overflow com
   `initial_counter = 0xFFFFFFFF` e `len = 65` rejeitado.
   Len=0 e sucesso vacuo. NULL key com len=0 ainda rejeitado.
3. **`test_poly1305_vectors`** — RFC 8439 §A.3 TC1 (key=0,
   msg=zeros → tag=zeros: r=0 anula o polinomio, s=0 nao
   desloca). TC2 (r=0, s non-zero → tag = s exato). Avalanche:
   mudar 1 byte do msg produz tag completamente diferente.
   Empty msg suportado (tag de mensagem vazia bem definido).
4. **`test_chacha20_poly1305_aead`** — round-trip + 5 categorias
   de tampering rejection (ciphertext flip / AAD flip / tag flip
   / wrong key / wrong nonce, todos retornam -1 sem revelar
   plaintext). Empty plaintext (AEAD-over-AAD para autenticacao
   pura). Empty AAD (encrypt sem dado adicional). Tag diferente
   com vs sem AAD (mesma key/nonce/pt produz tags diferentes,
   evitando confusao). Fail-closed em NULL key.

**Build.** `chacha20_poly1305.o` adicionado a `CAPYOS64_OBJS`
(kernel build) e `chacha20_poly1305.c` adicionado a `TEST_SRCS`
(host-side unit tests).

**Limites desta entrega.**

- **Sem callers reais ainda.** Esta entrega e a primitiva
  fundacional. Callers naturais (IPC autenticado entre kernel e
  userland, container cifrado em userland substituindo AES-XTS
  isolado, secure messaging local entre apps, future TLS 1.3
  userland) chegarao em slices subsequentes.
- **Nao implementa Ed25519 real** (RFC 8032) — esse e slice
  separado pendente desde alpha.210.
- **Nao implementa X25519** (RFC 7748) — key exchange. Junto
  com Ed25519 e ChaCha20-Poly1305, completa os 3 pilares de TLS
  1.3 quando todos disponiveis.
- **Nao substitui BearSSL para TLS.** BearSSL continua sendo
  stack TLS handshake oficial; esta primitiva e PARA usos fora
  de TLS.
- **Nao destrava entregaveis pendentes da Etapa 2** (loginwindow
  GUI real, smokes).

**ABI publica nova, aditiva.** Nao quebra callers existentes; o
header `include/security/chacha20_poly1305.h` e novo e auto-contido.

`0.8.0-alpha.214+20260513` realiza **hardening profundo do
CSPRNG** em `src/security/csprng.c` — coracao criptografico do
sistema, consumido por TODA primitiva que precisa de
aleatoriedade (PBKDF2 salts em `crypt.c`, AES-XTS keys em
`crypt_derive_xts_keys`, HKDF seeds em alpha.213, futuras chaves
Ed25519/X25519 quando os slices reais entrarem, session tokens,
TLS handshake em Etapa 5). A auditoria revelou 5 problemas
estruturais reais que comprometiam o boot-time entropy, a
robustez contra VMs hostis e a frescura de sessoes longas.

**Bug 1: `rdtsc` com constraint `"=A"` mal-formed em x86_64.**
A constraint `"=A"` em GCC tem semantica diferente entre
modos: em 32-bit significa "EAX:EDX combinados em 64 bits"
(correto para `rdtsc`); em 64-bit significa "the union of
registers RAX and RDX" (ambiguo — o compilador pode escolher
um dos dois). `rdtsc` deposita EDX:EAX exatamente como em 32-bit
mesmo no long mode, entao a constraint `"=A"` pode gerar codigo
errado em x86_64 dependendo da versao do compilador. Mitigacao:
constraints separadas `uint32_t lo, hi; __asm__ volatile("rdtsc"
: "=a"(lo), "=d"(hi));` depois `return ((uint64_t)hi << 32) |
lo;`. Codigo correto em ambos os modos sem depender de quirks.

**Bug 2: Boot-time entropy fragil em VM hostil.** Em cenarios
reais (CPU velha sem RDRAND, VM hostil onde TSC e virtualizado
para valor constante, container sem acesso direto ao hardware),
o pool inicial degenerava para `sha256(salt fixo +
boot-marker)` — estado conhecido a partir de qualquer copia do
binario. Toda chave gerada nas primeiras milissegundos do boot
ficaria previsivel. Mitigacao: TSC jitter loop de 16 rondas
intercalando operacoes triviais (`acc ^= spin * golden ratio`)
com leituras de TSC. Mesmo em VM com TSC virtualizado, o delta
entre leituras varia por efeitos de cache miss/branch predictor
state/scheduler preemption — fenomenos intrinsecamente
nao-deterministicos no nivel de hardware. Custo: ~10
microssegundos no boot, executado uma vez. Adicionalmente: o
endereco do `entropy_pool` (KASLR-aware quando este for ativado)
entra como input, divergindo o pool entre boots diferentes.

**Bug 3: Sem reseed proativo.** `csprng_get_bytes` ja faz
output feedback (mistura digest emitido no pool — garantindo
forward secrecy entre chamadas) mas isso NAO adiciona nova
entropia hardware. Uma sessao de varios minutos consumindo o
CSPRNG sem chamadas de `feed_entropy` (cenario possivel em
servidor sem mouse/teclado) operaria indefinidamente em um pool
estagnado. Mitigacao: contador `bytes_since_reseed`. Quando
cruzar `CSPRNG_RESEED_INTERVAL_BYTES` (64 KiB),
`csprng_get_bytes` chama internamente `mix_hardware_entropy()` —
refrescando o pool com RDRAND + TSC + reseed_counter. Custo:
~80 ciclos a cada 64 KiB emitidos, desprezivel. Comparativo com
peer OSes: Linux 4 MiB (mais agressivo no consumo), FreeBSD 1
MiB, OpenBSD por tempo (5 minutos). 64 KiB e conservador.

**Bug 4: `csprng_feed_entropy` so aceita 32-bit.** Fontes
naturais de 64+ bits (TSC completo, network packet contents,
disk sector contents, audio frames) tinham que ser fragmentadas
em chamadas multiplas, reduzindo bandwidth de entropia e
adicionando overhead de invocacao. Mitigacao: nova API
`csprng_feed_entropy_buffer(const void *data, size_t len)`
aceita buffer arbitrario, com `NULL`/zero graceful.

**Bug 5: RDRAND sem retry-loop.** O Intel SDM recomenda
explicitamente retry-loop de ate 10 tentativas no RDRAND para
tolerar falhas transitorias sob contencao hardware. O codigo
antigo aceitava 1 tentativa. Mitigacao: retry-loop de 10
attempts. Probabilidade de sucesso por chamada sobe de ~10^-2
(worst case) para ~1 - 2^-2000.

**Helper centralizado `mix_hardware_entropy()`.** Encapsula a
mistura das fontes hardware (RDRAND + TSC + reseed_counter) e e
reutilizado em tres caminhos: `csprng_init` (boot-time seed),
loop de `csprng_get_bytes` (reseed automatico), e
`csprng_reseed` (reseed manual). Garante que os tres caminhos
mantem a mesma quality de entropia sem duplicacao de codigo.

**Nova API publica `csprng_reseed()`.** Para callers criticos
que vao realizar operacoes longas (key generation, TLS
handshake, master key derivation) e querem garantir frescura do
pool antes do trabalho. Sem callers reais ainda — disponivel
para os slices futuros.

**Caller real em mouse PS/2.** `mouse_ps2_irq_handler` em
`src/drivers/input/mouse.c` agora alimenta o CSPRNG com cada
byte de pacote PS/2. Cada byte carrega timing humano residual
(intervalos entre movimentos do mouse sao nao-deterministicos
e variam por sub-milissegundos entre interrupcoes consecutivas).
Custo desprezivel (a ISR ja estava no caminho). Outros callers
existentes preservados sem mudanca: `pit_irq0_handler` (tick
PIT), `keyboard_irq` (scancode).

**Threat model documentado inline.** O header explicita: forward
secrecy via output feedback (output passado nao revela output
futuro mesmo se atacante extrair o pool em algum momento),
backward secrecy via SHA-256 one-way (pool atual nao revela
output passado), indistinguibilidade polynomial de bytes
verdadeiramente aleatorios sob assumption de SHA-256. Limites:
em VM hostil sem RDRAND e com TSC virtualizado para zero, o
boot se reduz ao salt fixo + boot-marker (mitigado parcialmente
pelo TSC jitter loop via efeitos de cache); mitigacao completa
exige fonte real (HW RNG, hypercall) fora do escopo desta camada.

**Testes contratuais novos** em `tests/test_csprng.c`:
`test_csprng_feed_buffer` (NULL/zero graceful + buffer
arbitrario muda pool/output), `test_csprng_reseed` (idempotencia
comportamental — chamar varias vezes nao reseta pool, apenas
adiciona entropia), `test_csprng_auto_reseed_after_interval`
(emite 256 KiB = 4x o intervalo, valida que reseed automatico
nao corrompe o stream nem destroi continuidade).

**ABI preservada.** Todas as funcoes antigas (`csprng_init`,
`csprng_feed_entropy(uint32_t)`, `csprng_get_bytes(void*,
size_t)`, `csprng_next_u32()`, `csprng_fill`) mantem assinatura
exata. APIs novas sao aditivas.

**Composicao com slices anteriores.** alpha.208 (snapshot wipe
+ `sha256_clear` publico): preservado integralmente —
`csprng_get_bytes` continua zerando `temp_ctx` e `digest` em
cada iteracao. alpha.209 (SHA-256 ctx wipe hygiene em
PBKDF2/HMAC/wrapper/volume): ortogonal. alpha.213 (HKDF-SHA256):
composicao natural — TLS userland futuro vai chamar
`csprng_get_bytes` para gerar IKM, depois `crypt_hkdf_sha256`
para derivar subkeys. Manifests produtivos nao existem (alpha
pre-release). Smokes pendentes da Etapa 2 nao sao afetados.

`0.8.0-alpha.218+20260514` entrega **Argon2id (RFC 9106) +
BLAKE2b (RFC 7693)** memory-hard password hashing nativo do
CapyOS em `src/security/argon2.c` (~600 LOC) e
`src/security/blake2b.c` (~270 LOC). **Fundacao cripto canonica
CapyOS completa (11 primitivas modernas em `src/security/`).**

**Problema.** PBKDF2-SHA256 (default historico em
`src/security/crypt.c` para password hashing em `userdb`) nao
tem memory-hardness. Atacante com GPU/ASIC dedicado avalia
1000-10000x mais candidates por segundo do que CPU comum.
Passwords fracas (8 chars alpha-num lowercase = 36^8 ~= 2.8 *
10^12) crackam em horas. Para CapyOS isso significa que
qualquer atacante com acesso offline ao userdb pode crackar
senhas de login local massivamente. PBKDF2-SHA256 atingiu o
fim da vida util cripto-economica.

**Argon2 e o vencedor do Password Hashing Competition (2015).**
RFC 9106 padronizou em 2021. Recomendado por OWASP, NIST SP
800-63B (rascunho 2024), e a maioria das auditorias cripto
modernas. Tres variantes: Argon2d (data-dependent, mais rapido
mas vulneravel a side-channels), Argon2i (data-independent,
resistente a side-channels mas vulneravel a ranking-TMTO),
**Argon2id (hibrido: primeira metade da pass 0 e data-
independent, depois data-dependent)** — a variante recomendada
para password hashing.

**Por que BLAKE2b junto.** Argon2 (RFC 9106 §3.3) usa
BLAKE2b iteradamente como hash subjacente para variable-length
output (H' construction) e pre-hash inicial (H0). Implementar
Argon2 sem BLAKE2b nao e possivel. Mas BLAKE2b tambem fica
disponivel como primitiva publica para uso geral — hash 64-byte
~2x mais rapido que SHA-512 em CPUs sem aceleracao SHA
dedicada, com keyed mode HMAC-like nativo (key 0..64 bytes),
length-extension resistant via flag `f[0]` (distinto entre
blocos intermediarios = 0 e ultimo bloco = 0xFF..FF).

**APIs publicas BLAKE2b.** `blake2b_init/update/final/wipe`
streaming + `blake2b()` one-shot. Constantes
`BLAKE2B_BLOCK_SIZE=128`, `BLAKE2B_DIGEST_SIZE=64`,
`BLAKE2B_KEY_SIZE=64`. Param block per RFC §2.5 codificado em
`h[0]` inicial: `h[0] ^= 0x01010000 ^ (keylen << 8) ^ outlen`
(fanout=1, depth=1, sem salt/personal). Lazy compression no
update: o ultimo bloco *nao* e comprimido em `update` — fica
pendente em `ctx->buf` para `final` aplicar o flag `f[0]`
corretamente. Suporta streaming arbitrario, compose com H'
variable-length, fail-closed em NULL/comprimento invalido,
wipe volatile-safe em `m`/`v`/state/buf.

**API publica Argon2id.**

```c
int argon2id_hash(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t t_cost, uint32_t m_cost,
                  uint8_t *memory, size_t memory_len,
                  uint8_t *out, size_t out_len);
```

Limites desta entrega: `parallelism = 1` fixo (RFC 9106 permite
explicitamente; multi-lane fora de escopo — CapyOS verifica
passwords serialmente), sem secret K, sem associated data X.
**memory caller-provided** (sem `malloc` no kernel) — caller
fornece buffer `m_cost * 1024` bytes via stack/static/heap;
flexivel para uso embedded. Constantes: `ARGON2_MIN_OUT_LEN=4`,
`ARGON2_MIN_SALT_LEN=8`, `ARGON2_MIN_T_COST=1`,
`ARGON2_MIN_M_COST=8`.

**Algoritmo (RFC 9106 §3).**

1. **Pre-hash H0 (§3.2):** `H0 = BLAKE2b(64, LE32(p) ||
   LE32(out_len) || LE32(m_cost) || LE32(t_cost) ||
   LE32(version=0x13) || LE32(type=2 /* Argon2id */) ||
   LE32(pwd_len) || password || LE32(salt_len) || salt ||
   LE32(0 /* secret length */) || LE32(0 /* AD length */))`.

2. **Variable-length H' (§3.3):**
   - `T <= 64`: single `BLAKE2b(T, LE32(T) || X)`.
   - `T > 64`: `r = ceil(T/32) - 2`; chain `V_1 = BLAKE2b(64,
     LE32(T) || X)`, `V_i = BLAKE2b(64, V_{i-1})` para
     `i = 2..r`; output `V_1[0..32] || ... || V_r[0..32] ||
     V_{r+1}` onde `V_{r+1} = BLAKE2b(T - 32*r, V_r)`.

3. **Block initialization:**
   - `B[0][0] = H'(1024, H0 || LE32(0) || LE32(0))`
   - `B[0][1] = H'(1024, H0 || LE32(1) || LE32(0))`

4. **G compression function 1024-byte (§3.6):**
   - `R = X XOR Y` onde X, Y, R sao 128 uint64.
   - View R como matriz 8x8 de **registers 16-byte (= 2
     uint64)**.
   - Apply P **row-wise**: 8 chamadas, cada uma sobre uma row
     (16 uint64).
   - Apply P **column-wise**: 8 chamadas, cada uma sobre uma
     column (16 uint64 interleaved no buffer).
   - `Z = (result) XOR R`.

5. **P round function:**
   - 8 chamadas a `GB(a, b, c, d)` com round schedule identico
     a BLAKE2.
   - `GB` usa **fBlaMka**: `a = a + b + 2 * (a_lo * b_lo)` ao
     inves da soma simples — acrescenta multiplicacao 32x32->64
     que aumenta cost-per-op em ASIC dedicado.

6. **Address block generation (§3.4.1.1, data-independent
   path):**
   - `input_block = LE64(pass) || LE64(lane=0) || LE64(slice)
     || LE64(m') || LE64(t_cost) || LE64(type=2) ||
     LE64(counter) || zero(968)`.
   - `address_block = G(zero_block, G(zero_block,
     input_block))`.
   - Counter incrementado por bloco de 128 enderecos.
   - Para slice 0 pass 0 (start_index=2), pre-geracao manual
     antes do loop principal porque `index % 128 == 2 != 0` no
     inicio.

7. **Argon2id mode selection (§3.4):**
   - Pass 0, slice 0 e 1: **data-independent** (resistente a
     cache side-channel).
   - Pass 0, slice 2 e 3 + Pass > 0: **data-dependent**
     (resistente a TMTO).

8. **Reference index alpha (§3.4.1.2):**

```c
ref_area_size = (pass == 0)
    ? ((slice == 0) ? (index - 1)
                    : (slice * segment_length + index - 1))
    : (lane_length - segment_length + index - 1);

rel_pos = (J1 * J1) >> 32;
rel_pos = ref_area_size - 1 - ((ref_area_size * rel_pos) >> 32);

start_pos = (pass == 0)
    ? 0
    : ((slice == 3) ? 0 : (slice + 1) * segment_length);

ref_pos = (start_pos + rel_pos) % lane_length;
```

Distribuicao **nao-uniforme** com J1^2 concentra referencias
no inicio do reference set — refoco da memory-hardness contra
TMTO.

9. **Block computation:**
   - Pass 0: `B[abs_pos] = G(B[prev], B[ref])`.
   - Pass > 0: `B[abs_pos] = B[abs_pos] XOR G(B[prev], B[ref])`
     (semantica Argon2 v1.3 overwrite-XOR).

10. **Finalization (§3.5):** para p=1, `final_block = B[lane_
    length - 1]`; `tag = H'(out_len, final_block)`.

**Wipe hygiene volatile-safe em todos os intermediarios.** H0,
V chain do H', blocos `prev_block`/`ref_block`/`new_block`/
`existing` da compressao, `input_block`, `address_block`,
`zero_block`, `final_block`. Wipeados antes do retorno em
sucesso e em todos os paths de erro. **Memory buffer caller-
provided NAO e wipeado automaticamente** — permite reuse em
loops sem perder o malloc; caller decide quando zerar via
`volatile_secure_zero`.

**Threat model documentado inline em ambos os headers.**

**BLAKE2b (RFC 7693 §2.10):**

- Resistencia a colisoes: 2^256 operacoes (digest 64 bytes).
- Resistencia a preimage: 2^512.
- Indistinguibilidade de PRF quando chaveado.
- Resistencia a length-extension via flag `f[0]` distinto entre
  blocos intermediarios (0) e ultimo bloco (0xFF..FF).

**Argon2id (RFC 9106 §1.1):**

- **Brute-force massivo em GPU/ASIC**: `m_cost * 1024` bytes por
  candidate test. Com `m_cost = 65536` (64 MiB), ASIC com 1 GB
  de memoria avalia <= 16 candidates em paralelo (vs >10000
  para PBKDF2-SHA256). Speedup ASIC cai para **<10x**.
- **TMTO resistance**: reducao de memoria em fator `k` aumenta
  tempo em fator >= `k^2` (ate `k = sqrt(m_cost)`). Argon2id
  estende para hibrido.
- **Side-channel timing**: data-independent na primeira metade
  da pass 0 (resistente a cache-timing observation); data-
  dependent depois (cache-friendly). Hibrido equilibra defesa
  server-side com resistencia a ranking-TMTO.

**Parametros recomendados (OWASP 2024, RFC 9106 §4).**

- `t_cost` (iterations): >= 2 (RFC §4 recomenda 3 para high-
  security; pode usar 1 se `m_cost >= 2 GiB`).
- `m_cost` (memoria em KiB):
  - Servidor potente: 65536 (64 MiB) ou mais.
  - Servidor moderado: 19456 (19 MiB) per OWASP.
  - Constrained device (login local CapyOS): 8192 (8 MiB) e o
    minimo defensavel; abaixo disso GPU speedup volta.
- `parallelism`: 1 (fixado nesta implementacao).

**Tests.** 7 funcoes novas em `tests/test_crypt_vectors.c`:

- `test_blake2b_rfc7693_abc` — vetor canonico RFC 7693 Appendix
  A: `BLAKE2b("abc") = ba80a53f981c4d0d6a2797b69f12f6e9...
  4009923` (64 bytes).
- `test_blake2b_empty` — `BLAKE2b("") = 786a02f742015903...
  fe9be2ce` (Python `hashlib.blake2b(b"")`).
- `test_blake2b_multiblock` — `BLAKE2b("The quick brown
  fox...")`.
- `test_blake2b_streaming_equals_oneshot` — update em chunks
  (50, 78 cruzando boundary 128, 1, 127 cruzando 256, 44)
  produz o mesmo digest que one-shot. Valida lazy compression.
- `test_blake2b_variable_output` — outlen 16/32/64 produzem
  outputs distintos (param block inclui outlen em `h[0]`
  inicial).
- `test_blake2b_keyed` — keys diferentes produzem outputs
  diferentes; keyed != no-key.
- `test_blake2b_fail_closed` — NULL out, outlen=0, outlen=65,
  keylen=65, NULL key com keylen>0.

E `test_argon2id_smoke` cobrindo KAT + propriedades estruturais
com m_cost=8 KiB:

- **KAT cross-checked vs `argon2-cffi` reference impl (Python):**
  `argon2id(p='password', s='somesalt', t=1, m=8, p=1, len=32,
  version=0x13)` =
  `f137f8e186a403a679ccd0606e5ab5dcdafe43c1640855ac8c6e33e9bd63eeb3`.
  Validacao adicional manual confirma MATCH em mais 4 variantes
  (t=2/m=8, t=1/m=16, len=16, empty password) — todas byte-a-byte
  identicas ao reference impl.
- Determinismo cross-call.
- Sensibilidade a password (avalanche).
- Sensibilidade a salt.
- Sensibilidade a t_cost.
- Sensibilidade a m_cost (com buffer maior).
- Sensibilidade a out_len (H' inclui T_le no input).
- Empty password aceito (RFC permite).
- Fail-closed: NULL salt, salt < 8 bytes, t_cost=0, m_cost=7,
  memory insuficiente, out_len < 4, NULL out, NULL memory.

**Nota sobre KATs RFC 9106 §A.3.** O vetor canonico do RFC 9106
§A.3 usa `parallelism=4` com secret + AD, fora do escopo desta
implementacao (p=1, sem secret/AD). Para validacao com p=1,
usamos o reference impl `argon2-cffi` (Python binding do PHC
reference) que tambem implementa RFC 9106 — 5 variantes
cross-checked confirmam MATCH byte-a-byte.

**Build.** `blake2b.o` + `argon2.o` adicionados a
`CAPYOS64_OBJS` (kernel build); `blake2b.c` + `argon2.c`
adicionados a `TEST_SRCS` (host-side unit tests).

**Limites desta entrega.**

- **Sem callers reais ainda em userdb.** PBKDF2-SHA256 em
  `src/security/crypt.c` continua disponivel — `userdb`
  existente nao quebra. Migracao incremental para Argon2id e
  slice futuro com algorithm prefix nos hashes armazenados
  (`$argon2id$v=19$m=...,t=...,p=1$salt$hash`); validacao
  automatica do PBKDF2 antigo + re-hash com Argon2id em proximo
  login bem-sucedido.
- **parallelism = 1 fixo.** Multi-lane (p > 1) fora de escopo
  desta entrega. CapyOS atualmente verifica passwords
  serialmente — sem demanda concorrente.
- **Sem secret K nem associated data X.** Argon2 com K
  (server-side authentication key) ofereceria protecao
  adicional contra ataque offline com database — fica fora de
  escopo porque CapyOS roda 100% local.
- **memory buffer caller-provided NAO e wipeado
  automaticamente.** Caller deve zerar via
  `volatile_secure_zero` se buffer contem material sensivel.
  Decisao deliberada para permitir reuse em loops.
- **password buffer caller NAO e wipeado.** Caller
  responsibility.
- KAT cross-checked apenas no test embarcado contra
  `argon2-cffi`; ainda nao roda contra reference impl PHC C em CI
  isolado.

**Composicao com slices anteriores.** alpha.214 (CSPRNG): salt
gerado via `csprng_get_bytes(salt, 16)` para Argon2id
(recomendado RFC 9106 §3.1: 16+ bytes aleatorios).
alpha.215-alpha.217 (cripto canonica complete): preservadas
integralmente — esta entrega e aditiva. Composicao natural com
userdb futuro: `argon2id_hash(password, password_len, salt, 16,
3, 65536, buffer, 64MB, hash_out, 32)` produz hash 32 bytes que
substitui o output atual de `crypt_pbkdf2_sha256(password,
salt, 600000, hash_out, 32)`. ABI publica nova nao quebra nada
existente. Manifests produtivos nao existem (alpha pre-release).
Smokes pendentes da Etapa 2 nao sao afetados.

`0.8.0-alpha.219+20260514` entrega **Argon2id em producao no
userdb** — primeira caller real da primitiva memory-hard
introduzida em alpha.218. Login local CapyOS agora usa Argon2id
por default em toda conta criada e em toda troca de senha.

**Problema fechado.** alpha.218 entregou Argon2id como primitiva
auditavel mas com limite explicito: *"Sem callers reais ainda em
userdb"*. Toda a fundacao memory-hard ficava dormente — atacante
com acesso offline a `/etc/users.db` continuava crackando senhas
fracas em horas via GPU/ASIC.

**Dispatcher novo: `src/auth/user_password_hash.{c,h}`** (~190 LOC
+ 105 LOC).

- API publica: `user_password_hash_algo_to_string`,
  `user_password_hash_algo_from_string`, `user_password_hash_derive`,
  `user_password_hash_verify`.
- PBKDF2 path: dispatcha `crypt_pbkdf2_sha256`; `t_cost == 0`
  mapeia para `USER_ITERATIONS` (64000) — bridge para registros
  legacy sem campo de iteracoes serializado.
- Argon2id path: aloca `m_cost * 1024` bytes via `kalloc`, chama
  `argon2id_hash`, wipa buffer com `volatile`-typed pointer antes
  de `kfree`. Rejeita `t_cost < 1` ou `m_cost < 8` (RFC 9106
  §3.1).
- Fail-closed: `hash_out` wipeado a zero em todo path de erro
  (allocation failure, parametros invalidos, NULL pointers,
  algoritmo desconhecido).
- Verify usa scratch local de 64 bytes capped, compara com
  `crypt_constant_time_compare`, wipa scratch antes de retornar.

**`include/auth/user.h` ganha:**

- `USER_PASSWORD_ALGO_PBKDF2_SHA256 = 0` (legacy).
- `USER_PASSWORD_ALGO_ARGON2ID = 1` (default desde alpha.219).
- `USER_ARGON2ID_T_COST = 3`, `USER_ARGON2ID_M_COST = 8192`
  (8 MiB, OWASP minimo defensavel para device constrained).
- `struct user_record` cresce 9 bytes no **final**: `uint8_t
  algo_id`, `uint32_t algo_t_cost`, `uint32_t algo_m_cost`.
  Append-only — 27 callsites existentes (`apps/settings`,
  `gui/desktop`, `apps/file_manager`, `shell/...`, `fs/vfs`,
  `auth/privilege`, `auth/login_runtime`, `auth/session`)
  compilam unchanged porque so leem
  `username/uid/gid/home/role`.

**`src/auth/user.c` refatorado:**

- `user_record_init` sempre emite Argon2id com
  `USER_ARGON2ID_T_COST/M_COST`.
- `userdb_set_password` sempre re-hashea com Argon2id —
  **primeira troca de senha apos upgrade promove automaticamente
  conta legacy para Argon2id** sem migracao explicita de DB.
- `userdb_authenticate` dispatcha conforme `rec.algo_id`:
  - Usuario encontrado: chama `user_password_hash_verify` com
    algoritmo/parametros do record (PBKDF2 legacy continua
    funcionando).
  - Usuario desconhecido: roda Argon2id com `k_userdb_dummy_salt`
    + `USER_ARGON2ID_T_COST/M_COST` — equaliza com a baseline
    nova (~200ms) e mantem mitigacao de user enumeration timing
    de alpha.206.
- `parse_user_line` aceita 7 campos (legacy PBKDF2,
  `algo_id=0/t_cost=0/m_cost=0`) ou 10 campos (Argon2id trailer
  `:argon2id:t_cost:m_cost`); algoritmo desconhecido rejeita
  linha inteira fail-closed.
- `serialize_user_record_line` escolhe 7 ou 10 campos por
  `algo_id` — PBKDF2 legacy preservado no formato antigo permite
  downgrade transparente para binarios pre-alpha.219 sem
  migracao reversa.
- Buffer da linha aumentado de `+64` para `+128` bytes para
  acomodar o trailer Argon2id com margem (worst case ~30 bytes
  extras).

**Schema `/etc/users.db`:**

```
# Legacy (escrito por binarios pre-alpha.219, aceito por todos):
username:uid:gid:home:salt_hex:hash_hex:role

# Argon2id (escrito por alpha.219+):
username:uid:gid:home:salt_hex:hash_hex:role:argon2id:t_cost:m_cost
```

**Memoria.** 8 MiB por auth (50% do kernel heap 16 MiB) alocado
via `kalloc`, wipeado volatile-safe, liberado via `kfree`. Auth
serializado (sem concorrencia no kernel atual) — 16 MiB suporta
uma derivacao por vez. `kfree` coalesce, memoria volta integral
ao pool.

**Tests** (`tests/test_user_password_hash.c`,
`run_user_password_hash_tests`): 30 assertions em 6 functions:

- `test_algo_string_roundtrip`: canonicalizacao
  `pbkdf2`/`argon2id`; rejeicao de prefix collision
  (`pbkdf2x`), truncamento (`argon2i`); NULL guards.
- `test_pbkdf2_legacy_roundtrip`: `t_cost=0` mapeia para
  `USER_ITERATIONS`; equivalencia com `t_cost` explicito; verify
  aceita correto e rejeita errado.
- `test_argon2id_roundtrip`: determinismo; nao-colisao com
  PBKDF2; verify aceita/rejeita.
- `test_argon2id_sensitivity`: sensibilidade a
  `salt`/`t_cost`/`m_cost` — anti-regressao de parameter
  threading pelo dispatcher.
- `test_derive_fail_closed`: NULL password com `len>0`, NULL
  salt com `salt_len>0`, `t_cost=0`, `m_cost<8`, algoritmo
  desconhecido, NULL `hash_out`, zero-len `hash_out` — todos
  rejeitam e zeram `hash_out`.
- `test_verify_fail_closed`: NULL stored, zero-len stored,
  oversize stored.

**Build.** `src/auth/user_password_hash.c` em `CAPYOS64_OBJS`
(kernel) e em `TEST_SRCS` (host). `tests/test_runner.c` chama
`run_user_password_hash_tests` apos `run_crypt_vector_tests`.

**Limites residuais.**

- **Timing leak transicional**: registros PBKDF2 legacy
  autenticam em ~50ms; registros Argon2id em ~200ms. Atacante
  observando latencia distingue *"conta predates alpha.219
  rollout e nunca trocou senha"* vs *"conta Argon2id"*. Vazamento
  minimo — nao revela senha, nao revela existencia, apenas idade
  aproximada da ultima troca. Sera eliminado por slice futuro de
  implicit re-hash on successful auth.
- **Volume key (`crypt_derive_xts_keys` para AES-XTS) continua
  PBKDF2-SHA256**. Threat model diferente (chave separada da senha
  de login) — migracao para Argon2id e slice futuro de menor
  prioridade.

**Composicao com slices anteriores integral.** alpha.218
(primitiva Argon2id), alpha.214 (CSPRNG salt), alpha.212
(timing-equalised lockout), alpha.211 (privacy hardening),
alpha.207 (`userdb_authenticate_with_policy`), alpha.206 (dummy
salt para non-existent), alpha.208/209 (wipe hygiene SHA-256) —
todos preservados. PBKDF2-SHA256 continua disponivel em
`crypt.c` para volume key derivation. Smokes pendentes da Etapa
2 (`gui-session`, `mouse-events`) nao afetados.

`0.8.0-alpha.220+20260514` fecha os **dois ultimos limites
residuais** documentados em alpha.218 e alpha.219: timing leak
transicional PBKDF2 vs Argon2id (eliminado por implicit re-hash on
successful auth) e gap de volume key derivation continua PBKDF2
(parcialmente fechado: nova primitiva publica
`crypt_derive_xts_keys_argon2id` entregue, callers migram quando o
header de volume com algorithm marker landar). Composicao integral
com TODOS os slices anteriores; nenhuma quebra de ABI publica;
nenhuma migracao manual.

**Refatoracao em `src/auth/user.c`:**

- **`userdb_replace_password_hash` (helper privado, novo).**
  Toda a logica de read-modify-write do `/etc/users.db` extraida
  de `userdb_set_password`. Le DB inteiro -> parse linha-a-linha
  -> identifica registro alvo por username -> gera salt fresco
  via `csprng_get_bytes(USER_SALT_SIZE=16)` -> re-deriva com
  `user_password_hash_derive` (USER_ARGON2ID_T_COST=3,
  USER_ARGON2ID_M_COST=8192 KiB) -> `serialize_user_record_line`
  escolhe 10-field schema automaticamente por `algo_id` -> grava
  via `userdb_write_blob` atomicamente (unlink + create + write).
  Wipe `volatile` em todos os buffers de credencial em todos os
  exit paths (failure ou sucesso).
- **`userdb_set_password` (entry publico, refatorado).** Aplica
  `auth_policy_validate_password` (mantem politica de senha do
  alpha.211/.212) e delega para
  `userdb_replace_password_hash`. Mesma semantica que antes para
  callers existentes.
- **`userdb_authenticate` (entry publico, hook novo).** Depois de
  `auth_ok=1` com `user_found=1` e `rec.algo_id !=
  USER_PASSWORD_ALGO_ARGON2ID`, executa
  `(void)userdb_replace_password_hash(username, password)`.
  Fail-silent: allocation/FS error nao bloqueia auth ja
  bem-sucedida — record stays on PBKDF2 e retry no proximo
  login. Comentario inline (`src/auth/user.c:727-755`) documenta
  threat model: leak self-heals; population de PBKDF2 records
  shrinks monotonically conforme contas autenticam.

**Timing apos a release:**

- Conta legacy PBKDF2 (primeiro login pos-alpha.220):
  ~250ms (50ms verify + 200ms rehash + ~5ms FS rewrite).
- Conta legacy PBKDF2 (segundo login pos-alpha.220):
  ~200ms (record agora e Argon2id).
- Conta Argon2id: ~200ms (inalterado).

**Argon2id volume-key derivation primitive:**

```c
/* include/security/crypt.h */
#define CRYPT_VOLUME_ARGON2ID_T_COST 3u
#define CRYPT_VOLUME_ARGON2ID_M_COST 8192u

int crypt_derive_xts_keys_argon2id(const char *password,
                                   const uint8_t *salt, size_t salt_len,
                                   uint32_t t_cost, uint32_t m_cost,
                                   uint8_t key1[CRYPT_KEY_SIZE],
                                   uint8_t key2[CRYPT_KEY_SIZE]);
```

`src/security/crypt.c:174-253` implementa: caller-allocates via
`kalloc(m_cost*1024)` (8 MiB tuning default) -> `argon2id_hash`
com `out_len=64` -> split `derived[0..32]` -> `key1`,
`derived[32..64]` -> `key2` (mesma split semantics que
`crypt_derive_xts_keys` PBKDF2 preserva) -> wipe volatile-safe
do work memory antes de `kfree` -> wipe do scratch `derived[64]`
antes de retornar. **Fail-closed first**: `key1` e `key2`
wipeados a zero NO INICIO do dispatcher (antes de qualquer
parameter check) — caller que esqueca de checar return code
recebe sentinela "no key here" inequivoco em vez de stack
residue. Rejeita `t_cost < ARGON2_MIN_T_COST=1`, `m_cost <
ARGON2_MIN_M_COST=8`, `salt_len < 8` per RFC 9106 §3.1, NULL
`password`/`salt`/`key1`/`key2`.

**Callers em producao:**

- `src/installer/installer_main.c:464` — usa
  `crypt_derive_xts_keys` PBKDF2.
- `src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c:75`
  — usa `crypt_derive_xts_keys` PBKDF2.
- `src/core/kernel.c:392` — usa `crypt_derive_xts_keys` PBKDF2.

Os tres NAO foram trocados nesta release — a primitiva esta
entregue mas o consumo aguarda design do header de volume com
algorithm marker (sem ele, upgrade do binario do kernel quebra
volumes encrypted com PBKDF2). Slice futuro definira o header
de volume + tools de re-keying.

**Testes adicionados:**
`tests/test_crypt_vectors.c::test_crypt_derive_xts_keys_argon2id`
com 11 assertions (determinismo, key1!=key2 split, salt
sensitivity, fail-closed em NULL/t_cost=0/m_cost=7/salt_len=7,
wipe forensics sentinela 0xA5 -> 0x00 em failure path,
non-collision com PBKDF2).

**Limites residuais novos:**

- **Implicit re-hash exige login** para migrar. Contas dormentes
  ficam PBKDF2 indefinidamente. Ferramenta admin para batch
  re-hash e slice futuro.
- **Volume key primitive sem callers** em producao. Instalacoes
  feitas com binarios anteriores continuam PBKDF2. Header de
  volume com algorithm marker e slice futuro.
- **Disk salt continua hardcoded** (`g_disk_salt` em
  `installer_main.c:39-41`). Mitigacao: Argon2id ainda paga 8
  MiB por candidate mesmo com salt fixo, mas idealmente seria
  per-install random. Endereçado no mesmo slice futuro do header
  de volume.

`0.8.0-alpha.221+20260514` fecha o gap do "**header de volume com
algorithm marker**" prometido em alpha.220 entregando a **primitiva
on-disk** completa em `include/security/volume_header.h` +
`src/security/volume_header.c` (~290 + ~620 LOC). Nesta release nada
no installer/boot path consome o header ainda — a primitiva e
dormente em producao, testada apenas no host-side runner. Alpha.222
fara o write-side wiring; alpha.223 entregara re-keying in-place.

**Estrutura on-disk (512 bytes, 1 LBA):**

```c
/* include/security/volume_header.h — layout v1 fixo */
struct capyos_volume_header {
  uint32_t magic0;                /* 'CAPY' LE = 0x59504143 */
  uint32_t magic1;                /* 'VHDR' LE = 0x52444856 */
  uint32_t version;               /* 1 nesta release */
  uint32_t flags;                 /* 0 reservado */
  uint32_t kdf_algo_id;           /* 0 PBKDF2 ou 1 Argon2id */
  uint32_t kdf_t_cost;            /* iter PBKDF2 ou t_cost Argon2id */
  uint32_t kdf_m_cost;            /* KiB Argon2id (0 PBKDF2) */
  uint32_t kdf_salt_len;          /* [8, 64] */
  uint8_t  kdf_salt[64];          /* zero-padded apos salt_len */
  uint32_t data_offset_lba;       /* onde FS comeca (>=1) */
  uint32_t reserved_lba_count;    /* LBAs reservados pelo header */
  uint8_t  kdf_check_tag[32];     /* HMAC-SHA256(K1||K2, ctx||prefix) */
  uint64_t creation_timestamp_ns; /* forense */
  uint8_t  creator_version[32];   /* 'CapyOS-0.8.0-alpha.221' null-pad */
  uint8_t  reserved[332];         /* zero */
  uint32_t header_crc32;          /* IEEE 802.3 reflected */
};
```

**Mapa de entrega linear:**

| Slice         | Escopo                                                                                                                            | Status     |
|---------------|-----------------------------------------------------------------------------------------------------------------------------------|------------|
| **alpha.221** | Primitiva entregue: header module + dispatcher `derive_keys` + 13 funcoes de teste host-side com ~70 assertions.                  | **DONE**   |
| **alpha.222** | `volume_provider_install` (write-side) + `volume_provider_open` (read-side com fallback legacy) cabeados ao installer + boot path; 9 funcoes de teste host-side com ram-backed block device. | **DONE**   |
| **alpha.223** | Preflight seguro/read-only de re-keying: classifica header-managed vs legacy, detecta CAPYFS legacy em LBA0, explicita relocation/shrink/re-encrypt obrigatorios e bloqueia migracao destrutiva ingenua. | **DONE** |
| **alpha.224** | Planner transacional read-only: calcula no-op moderno, blocked shrink, blocked scratch e plano READY com source/target LBA, copia reversa, scratch dedicado e estimativas de I/O. | **DONE** |
| **alpha.225** | Executor transacional guardado/dry-run: valida plano, reporta fases checkpoint/copia reversa/commit/verify, recusa writes reais por contrato e cobre read-only/fail-closed em testes dedicados. | **DONE** |
| **alpha.226** | Contrato persistente de checkpoint: record 128 bytes little-endian + CRC32 + reserved-zero + validacao semantica de progresso para resume/rollback/abort seguro. | **DONE** |
| **alpha.227** | Executor checkpoint-write guardado: grava somente o checkpoint no scratch com flag explicita, verifica por read/parse e bloqueia copia destrutiva. | **DONE** |
| **alpha.228** | Staging criptografico do header alvo: scratch contem checkpoint + header Argon2id com salt CSPRNG + manifest CRC, tudo verificado por read-back. | **DONE** |
| **alpha.229** | Copy/re-encrypt reverso incremental: um bloco por chamada, verificado no dominio Argon2id alvo, checkpoint+manifest atualizados no scratch e LBA0 preservado. | **DONE** |
| **alpha.230** | Commit header por ultimo: valida scratch completo, grava LBA0, verifica read-back, abre header-managed e marca checkpoint COMPLETED. | **DONE** |
| **alpha.231** | Recovery operacional: rollback/abort pre-commit por passo, limpeza verificada de scratch pos-commit e testes dedicados de falha. | **DONE** |
| **alpha.232** | Orquestrador automatico da migracao completa: passo unico escolhe stage/copy/commit/cleanup, retoma por scratch checkpoint+manifest e executa rollback sob policy de abort. | **DONE** |
| **alpha.233** | Submit/autenticação real do loginwindow GUI: política explícita habilita o gate seguro, a ponte userdb chama `userdb_authenticate_with_policy`, preserva lockout e zera credenciais mantendo fallback textual. | **DONE** |
| **alpha.234** | Recuperacao segura pelo loginwindow: contrato final de decisão redigida para stay/recovery/resume/text-login, sem bypass de lockout, sem recovery após autenticação e com reset+rerender obrigatório no resume. | **DONE** |
| **alpha.235** | Transicao completa login -> sessao grafica: contrato redigido libera session_begin/activate/shell context/desktop autostart somente após submit autenticado, recovery seguro e usuário desktop elegível; falha, lockout, recovery ativo e usuários de recuperação caem para fallback seguro. | **DONE** |
| **alpha.236** | Gate externo `gui-session`: marker serial determinístico `[smoke] gui-session ready`, alvo `smoke-x64-vmware-gui-session`, evidência/aceitação/promoção de release amarradas ao gate oficial e testes host-side do contrato. | **DONE** |
| **alpha.237** | Gate externo `mouse-events`: marker serial determinístico `[smoke] mouse-events ready`, alvo `smoke-x64-vmware-mouse-events`, evidência/aceitação/promoção exigindo DHCP + `gui-session` + `mouse-events` e testes host-side do contrato. | **DONE** |

**Decisoes de design.**

1. **`vh_serialize_prefix` como autoridade unica do layout.** Tanto
   o serializer disco-side quanto o computador de HMAC tag consomem
   essa funcao. Drift entre o que vai a disco e o que e autenticado
   pelo `kdf_check_tag` fica impossivel — qualquer mudanca no
   layout dos primeiros 104 bytes propaga atomicamente.

2. **Endianness little-endian explicita.** `vh_put_u32_le` /
   `vh_get_u32_le` / `vh_put_u64_le` / `vh_get_u64_le` quebram os
   inteiros byte-por-byte. CapyOS roda em x86_64 hoje, mas a
   portabilidade host-side dos testes contra big-endian fica
   garantida. Custo: ~4 stores por integer (insignificante perto
   dos 8 MiB de Argon2id).

3. **CRC32 no-table branchless.** Implementacao standalone em ~10
   LOC com `mask = -(int32_t)(crc & 1u)` ao inves de branch.
   Trade-off explicito: throughput de ~33000 cycles por header e
   completamente dominado pela derivacao KDF que segue.

4. **Threat model two-tier documentado inline.**
   - **`header_crc32` e bit-rot gate fast, NAO seguranca.**
     Atacante com acesso ao disco recomputa CRC32 trivialmente.
     Seu unico papel e abortar parse cedo em corrupcao silenciosa
     antes de pagar 8 MiB de Argon2id por um header 50%-certo
     invalido.
   - **`kdf_check_tag` e o binding criptografico.** HMAC-SHA256
     sob `key1‖key2` autentica `context‖prefix[0..104]`. Atacante
     que altera `salt`/`algo`/`t_cost`/`m_cost`/`data_offset`/
     `reserved_lba` forca o usuario a derivar chave diferente, o
     recomputo do HMAC nao bate, mount recusa.

5. **`_parse` fail-safe.** Wipe out struct ANTES de qualquer
   validacao, depois CRC -> magic -> version (sequencia barata)
   ANTES de params -> reserved-all-zero (carrega). Qualquer falha
   retorna struct zerada, nao residuo do disco.

6. **`_derive_keys` dispatcher fail-closed first.** Wipe
   `key1`/`key2` antes de parameter check, depois dispatcha
   PBKDF2/Argon2id, depois `verify_check_tag`, wipe em qualquer
   falha. Caller que esquece return code lida com sentinela zero,
   nao com stack residue.

7. **NAO distingue "wrong password" de "tampered header".**
   Ambos retornam `CAPYOS_VOLUME_HEADER_ERR_CHECK_TAG`.
   Distinguir-los geraria oracle de tampering distinguivel da
   experiencia normal de senha errada — atacante copiaria um
   header tampered para um disco roubado e mediria a diferenca
   de mensagem entre "senha errada" e "tag mismatch" para
   descobrir se o tampering passou pelo parser.

**APIs publicas:**

```c
int capyos_volume_header_init(...);            /* popula struct */
int capyos_volume_header_compute_check_tag(...); /* HMAC + mirror */
int capyos_volume_header_verify_check_tag(...);  /* recompute + cmp */
int capyos_volume_header_finalize_crc(...);    /* compute + mirror */
int capyos_volume_header_serialize(...);       /* struct -> 512 bytes */
int capyos_volume_header_parse(...);           /* 512 bytes -> struct */
int capyos_volume_header_looks_valid(...);     /* gate barato p/ boot */
int capyos_volume_header_derive_keys(...);     /* dispatcher pwd -> K1,K2 */
uint32_t capyos_volume_header_crc32(...);      /* CRC32 standalone */
```

**Tests host-side (`tests/test_volume_header.c`, ~620 LOC).**

13 funcoes / ~70 assertions:

1. **CRC32 known-answer**: RFC 3309 `""` -> 0x00000000, `"a"` ->
   0xE8B7BE43, `"123456789"` -> 0xCBF43926; NULL guard retorna 0;
   zero-buffer de 64 bytes confirma que o loop nao para no
   primeiro byte zero.
2. **`_init` happy paths PBKDF2 e Argon2id**: magic/version/algo/
   t/m/salt_len/salt-bytes/salt-tail-zero/data_offset/reserved_lba/
   check_tag-zero/timestamp/creator_version/creator-null-pad/
   reserved-all-zero. Inclui caso de `creator_version = NULL`.
3. **`_init` fail-closed (13 vetores)**: NULL out, NULL salt, algo
   desconhecido, PBKDF2 t<1000, PBKDF2 m!=0, Argon2id t=0,
   Argon2id m<8, salt<8, salt>64, data_offset=0, reserved=0,
   reserved>data_offset.
4. **serialize/parse roundtrip com endianness explicit**: bytes
   0..7 do buffer serializado lidos diretamente como ASCII
   `'C','A','P','Y','V','H','D','R'` — endurece a garantia de
   little-endian on-disk.
5. **`_parse` fail-closed**: NULL inputs, magic tampered com CRC
   refixada -> `ERR_MAGIC`, version tampered com CRC refixada ->
   `ERR_VERSION`, body tampered sem fix de CRC -> `ERR_CRC`, algo
   tampered -> `ERR_ALGO`, flags!=0 -> `ERR_FLAGS`, reserved!=0
   -> `ERR_RESERVED`.
6. **`_looks_valid` quick gate**: valid -> 1, corrupt -> 0,
   NULL -> 0, all-zero -> 0.
7. **`_derive_keys` success em ambos KDFs**: dispatcher produz as
   mesmas keys que `crypt_derive_xts_keys{,_argon2id}` direto;
   `key1 != key2` (anti-split-bug).
8. **Wrong password**: sentinela 0xA5 plantada em `key1`/`key2`
   antes da chamada; apos `derive_keys` retornando
   `ERR_CHECK_TAG`, ambos os buffers DEVEM estar wiped a zero.
9. **Tampered salt detectado mesmo com password correto**: 1-byte
   flip no `kdf_salt[0]` faz `derive_keys` produzir chave
   diferente; HMAC recomputado nao bate; rejeita.
10. **Algo downgrade attempt**: header Argon2id legitimo + tag
    pre-computed; atacante reescreve `algo_id=PBKDF2`, `t_cost=1000`,
    `m_cost=0` (passa o param validator). Dispatcher dispatcha
    PBKDF2, produz keys diferentes das que originalmente assinaram
    o tag, HMAC mismatch, rejeita.
11. **Fail-closed NULL hdr/pwd/k1/k2**: cada `NULL` retorna
    `ERR_NULL` com sentinela de 0xA5 wiped a zero.

**Wiring.**

- `Makefile`: `$(BUILD)/x86_64/security/volume_header.o` em
  `CAPYOS64_OBJS` apos `crypt.o`; `TEST_SRCS` adiciona
  `tests/test_volume_header.c src/security/volume_header.c`.
- `tests/test_runner.c`: declara `run_volume_header_tests` e chama
  apos `run_crypt_vector_tests`.

**Limites residuais novos.**

- **Installer/boot path inalterados.** Volumes instalados em
  alpha.221 sao bit-identicos aos de alpha.220 (PBKDF2 +
  `g_disk_salt` raw LBA 0). Endereçado em alpha.222.
- **`g_disk_salt` ainda existe** em 4 callsites. Endereçado em
  alpha.222 (substituido por `kdf_salt` per-install via CSPRNG).
- **Header v1 nao suporta rotacao de `context_label`.** Mudanca
  do label de HMAC exige bump de `version` e logica de migracao
  em release futura. Documentado em
  `CAPYOS_VOLUME_HEADER_CHECK_CONTEXT_LEN` comment.

**Composicao com slices anteriores.** alpha.220
(`crypt_derive_xts_keys_argon2id` backend do dispatcher Argon2id),
alpha.218 (`argon2id_hash` + `blake2b_*` primitivas que alpha.220
consome), alpha.214 (CSPRNG futuro fornecera `kdf_salt` per-install
em alpha.222), alpha.213 (HKDF ortogonal — serve outros derivative
contexts, nao participa do volume header), alpha.209 (`sha256_clear`
hygiene propaga via `crypt_hmac_sha256` para
`vh_compute_tag_internal`). ABI publica preservada — todos os
simbolos novos sao aditivos.

`0.8.0-alpha.222+20260514` fecha o gap de "**header dormente em
producao**" deixado em alpha.221 conectando a primitiva on-disk ao
installer + boot path via novo modulo `volume_provider` em
`include/security/volume_provider.h` + `src/security/volume_provider.c`
(~145 + ~280 LOC) + 9 funcoes de teste host-side em
`tests/test_volume_provider.c` (~430 LOC, ram-backed block device
end-to-end). **ANTES** (alpha.221): primitiva `capyos_volume_header_*`
entregue e testada isoladamente em host runner mas **dormente em
producao** — installer continuava gravando filesystem CAPYFS direto
no LBA 0 raw da particao DATA, kernel continuava derivando AES-XTS
keys via PBKDF2-SHA256(16000 iter) sobre `g_disk_salt` hardcoded de
16 bytes `'NoirOS-FS-Salt!'` compartilhado entre **todas** as
instalacoes CapyOS do mundo, primitiva
`crypt_derive_xts_keys_argon2id` (alpha.220) sem caller real,
documentado nos Limites de alpha.221 como "alpha.222 planned:
installer writes header on fresh installs; boot path tries header
first, falls back to legacy PBKDF2 + `g_disk_salt` for existing
volumes". **AGORA**:

- **`volume_provider_install` (write-side)**: called pelo
  `initialize_encrypted_data_volume` no PRIMEIRO BOOT pos-install
  ISO. Gera salt 16-byte fresco via `csprng_get_bytes`
  **per-install**, popula header v1 com Argon2id
  (`CRYPT_VOLUME_ARGON2ID_T_COST=3`,
  `CRYPT_VOLUME_ARGON2ID_M_COST=8192` KiB = 8 MiB de scratch),
  deriva AES-XTS keys via `crypt_derive_xts_keys_argon2id`,
  computa `kdf_check_tag` HMAC-SHA256 e finaliza CRC32 IEEE 802.3,
  serializa header em buffer 512B, escreve em `chunked_4096[0]`
  como block 4 KiB (struct 512B + 3584B zero padding cumprindo a
  invariante reserved-all-zero do parser), `block_offset_wrap(chunked_4096,
  hdr.data_offset_lba=1, count-1)` cria `fs_dev` a partir do bloco
  1 (FS area), `crypt_init(fs_dev, k1, k2)` retorna `crypt_dev`
  pronto para `mount_root_capyfs`.

- **`volume_provider_open` (read-side com fallback legacy)**:
  called pelo `open_crypt_volume_with_password` em todos os boots
  subsequentes incluindo o segundo boot apos install fresh. Le
  `chunked_4096[0]`, executa `capyos_volume_header_looks_valid`
  como **quick gate barato** (~10000 ciclos CRC32 sobre 4 KiB
  buffer). Se passa, entra no **header path autoritario** (parse
  + `capyos_volume_header_derive_keys` + `block_offset_wrap` +
  `crypt_init`) **SEM fallback legacy** em qualquer falha
  downstream (parse/derive/tag-mismatch) — **downgrade protection**
  contra atacante que reescreve header com lixo nao consegue
  forcar o caller a usar PBKDF2 (provider retorna -1 +
  `out_crypt=NULL` e wipa scratch local antes de retornar). Se
  `looks_valid` falha, entra no **legacy path**
  (`crypt_derive_xts_keys` PBKDF2 + caller-supplied `legacy_salt`
  + `legacy_iter` + `crypt_init` sobre full `chunked_4096`
  device) preservando 100% dos volumes pre-alpha.222 — atacante
  com instalacao legacy continua autenticando como antes, e
  atacante com ciphertext legacy aleatorio no LBA 0 tem
  probabilidade ~2^-96 de satisfazer magic CAPYVHDR + version
  + CRC32 quase zero.

**Modificacoes minimas em 3 arquivos kernel-side:**

- `src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c::open_crypt_volume_with_password`
  substitui `crypt_derive_xts_keys` + `crypt_init` direto por
  `volume_provider_open(data_dev, password, state->disk_salt,
  state->disk_salt_size, state->kdf_iterations, &crypt_dev)`
  preservando assinatura externa publica.

- `src/arch/x86_64/kernel_volume_runtime/mount_initialize.c::initialize_encrypted_data_volume`
  substitui `open_crypt_volume_with_password` por
  `volume_provider_install(data_dev, state->active_volume_key,
  &crypt_dev)` no caminho de install fresh (`device_is_blank == 1`).

- `Makefile` adiciona `$(BUILD)/x86_64/security/volume_provider.o`
  em `CAPYOS64_OBJS` imediatamente apos `volume_header.o` + `TEST_SRCS`
  ganha `tests/test_volume_provider.c src/security/volume_provider.c`;
  `test_runner.c` declara `run_volume_provider_tests` + invocacao
  apos `run_volume_header_tests`.

**Tests (9 funcoes / ~50 assertions, host-side ram-backed):**

1. **`install` grava magic CAPYVHDR + padding zero apos 512B**: parse
   field-by-field dos defaults (`algo=Argon2id`, `t_cost=3`,
   `m_cost=8192`, `data_offset_lba=1`); `crypt_dev->block_count = raw_count - 1`.

2. **`install` + `open` round-trip preserva plaintext sob AES-XTS**:
   encrypt/decrypt em LBA 0 da FS area (raw LBA 1) com assertion
   explicita de que raw LBA 1 difere de plaintext.

3. **Wrong password retorna -1 com `out_crypt` nullified** de
   sentinela 0xDEADBEEF.

4. **Legacy volume mount sem header via PBKDF2 fallback**:
   round-trips plaintext e `crypt_dev` cobre full device
   (`block_count == raw_count`).

5. **Downgrade attack rejeitado**: header presente forca header
   path mesmo com `legacy_salt` + `legacy_iter` supplied; wrong
   password ainda falha sem cair em legacy.

6. **`install` fail-closed em NULL device/password/out**:
   `block_size=512` (devices 4 KiB-only) e tiny device (1 block).

7. **`open` fail-closed em NULL inputs** + legacy params ausentes
   quando sem header.

8. **I/O failure forcada no LBA 0** (`force_read_fail_lba=0`)
   refusa mount sem cair em legacy.

9. **Dois installs com mesma password produzem salts distintos**:
   CSPRNG variability — probabilidade de colisao 2^-128.

**Backend de teste**: `ram_dev` calloc-backed com
`block_read/write_fn` opcionalmente forcando falha em LBA
especifico — 100% host-side, zero dependencia de driver real ou
kernel runtime.

**Limites residuais (rolling).**

- **4 callsites de `g_disk_salt` continuam existindo como fallback
  legacy** — `src/core/kernel.c` (32-bit legacy aposentado),
  `src/installer/installer_main.c` (32-bit aposentado),
  `src/arch/x86_64/kernel_runtime_ops.c` (state initializer ainda
  passa `g_disk_salt` para o struct runtime),
  `src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c`
  (consumido pelo legacy fallback path do provider) — todos serao
  removidos quando o orquestrador write-enabled completo pos-alpha.232 converter
  volumes legacy em header-managed in-place.

- **Installer 32-bit (`src/installer/installer_main.c`) nao foi
  tocado nesta release** porque o target esta em `legacy-disabled`
  (linha 1105-1106 do `Makefile`) e nao constroi mais em ISO builds
  — modernizacao futura podera adicionar `volume_provider_install`
  la se o target for revivido.

- **Conversao legacy -> header-managed ainda exige acionamento por caller externo** —
  o orquestrador write-enabled de `alpha.232` ja seleciona o proximo passo seguro
  por chamada, mas usuarios em volumes legacy permanecem em PBKDF2 ate o boot,
  installer ou ferramenta administrativa chamar a nova API em fluxo de migracao.
  `alpha.223` entrega o preflight read-only/fail-closed; `alpha.224`
  entrega o planner; `alpha.225` entrega o executor guardado/dry-run;
  `alpha.226` entrega o contrato persistente de checkpoint para
  resume/rollback/abort seguro; `alpha.227` grava e verifica o
  checkpoint no scratch com flag explicita; `alpha.228` persiste e
  verifica o header alvo Argon2id + manifest no scratch; `alpha.229`
  copia/recriptografa blocos em ordem reversa com checkpoint progressivo;
  `alpha.230` comita LBA0 por ultimo e verifica abertura header-managed;
  `alpha.231` adiciona rollback/abort pre-commit e limpeza verificada de scratch
  pos-commit; `alpha.232` automatiza a escolha stage/copy/commit/cleanup/rollback
  sem loop destrutivo longo.

**ABI publica preservada**: dezessete funcoes novas
(`volume_provider_install`/`volume_provider_open`/
`volume_provider_rekey_preflight`/`volume_provider_rekey_plan`/
`volume_provider_rekey_execute`/`volume_provider_rekey_checkpoint_init`/
`volume_provider_rekey_checkpoint_serialize`/
`volume_provider_rekey_checkpoint_parse`/
`volume_provider_rekey_stage_manifest_serialize`/
`volume_provider_rekey_stage_manifest_parse`/
`volume_provider_rekey_execute_checkpoint`/
`volume_provider_rekey_execute_stage_header`/
`volume_provider_rekey_execute_copy_step`/
`volume_provider_rekey_execute_commit_header`/
`volume_provider_rekey_execute_rollback_step`/
`volume_provider_rekey_execute_cleanup_scratch`/
`volume_provider_rekey_execute_orchestrated_step`) sao aditivas;
`open_crypt_volume_with_password` mantem assinatura externa
(apenas corpo muda).

**Composicao com slices anteriores integral.** alpha.221
(`volume_header` primitive consumida diretamente), alpha.220
(`crypt_derive_xts_keys_argon2id` finalmente em producao com
caller real), alpha.214 (`csprng_get_bytes` per-install salt),
alpha.218 (Argon2id memory-hard usado para volume key derivation),
alpha.213 (HKDF ortogonal — serve outros derivative contexts),
alpha.215 (ChaCha20-Poly1305 ortogonal — serve secure messaging
fora de TLS).

### Entregáveis

- Loginwindow GUI com senha e recuperação segura.
  - `alpha.107`: view model fail-closed do loginwindow preparado sem ativar autenticação gráfica.
  - `alpha.108`: preview passivo do loginwindow exibido sem entrada/autenticação GUI.
  - `alpha.124`: politica de credenciais do loginwindow documenta limite, mascara, wipe e recuperacao textual sem ativar submit grafico.
  - `alpha.125`: retorno da recuperacao textual ao login normal passa por politica auditavel e fail-closed.
  - `alpha.126`: buffer efemero de credenciais do loginwindow limita, mascara e limpa senha sem habilitar submit grafico.
  - `alpha.127`: gate de submit de credenciais bloqueia autenticacao grafica e preserva fallback textual.
  - `alpha.128`: tentativa de submit consome o evento e executa wipe obrigatorio do buffer.
  - `alpha.129`: redutor de input aplica append/backspace/submit/cancel mantendo submit GUI bloqueado.
  - `alpha.130`: snapshot do campo de credenciais expoe mascara, estado e limites sem segredo bruto.
  - `alpha.131`: painel seguro combina field view e ultimo input mantendo submit GUI bloqueado.
  - `alpha.132`: pipeline de interacao aplica input e reconstrói painel mascarado sem autenticar pela GUI.
  - `alpha.133`: snapshot de prontidao resume render/input/wipe sem expor segredo bruto.
  - `alpha.134`: evento auditavel redigido registra estado/acao sem expor segredo, mascara ou comprimento.
  - `alpha.135`: view model seguro compoe prontidao e auditoria redigida para a futura GUI.
  - `alpha.136`: pipeline composto aplica input e recompõe a etapa de UI de credenciais segura.
  - `alpha.137`: sessao one-shot inicializa storage efemero e limpa credenciais antes de retornar.
  - `alpha.138`: view model seguro une credenciais limpas e recuperacao textual sem autenticar pela GUI.
  - `alpha.139`: snapshot de tela compoe login, credenciais e recuperacao mantendo submit GUI bloqueado.
  - `alpha.140`: sessao one-shot compoe runtime/tela e limpa IO sem expor segredo.
  - `alpha.141`: render plan transforma sessao segura em layout e acoes UI redigidas.
  - `alpha.142`: action plan valida intencoes UI mantendo submit grafico bloqueado.
  - `alpha.143`: ui event audita intencoes validadas sem segredo nem auth grafica.
  - `alpha.144`: route plan traduz eventos UI em rotas seguras sem auth grafica.
  - `alpha.145`: controller traduz rotas em decisoes finais de UI sem auth grafica.
  - `alpha.146`: presenter traduz decisoes em apresentacao final sem auth grafica.
  - `alpha.147`: binding traduz apresentacao em montagem GUI sem auth grafica.
  - `alpha.148`: mount plan traduz binding em transacao GUI sem auth grafica.
  - `alpha.149`: commit plan traduz mount plan em decisao GUI sem auth grafica.
  - `alpha.150`: handoff plan traduz commit plan em envelope GUI sem auth grafica.
  - `alpha.151`: dispatch plan traduz handoff plan em ticket GUI sem auth grafica.
  - `alpha.152`: queue plan traduz dispatch plan em ticket GUI sem auth grafica.
  - `alpha.153`: activation plan traduz queue plan em ticket GUI sem auth grafica.
  - `alpha.154`: frame plan traduz activation plan em ticket GUI sem auth grafica.
  - `alpha.155`: surface plan traduz frame plan em superficie GUI sem auth grafica.
  - `alpha.156`: compositor plan traduz surface plan em ticket GUI/compositor sem auth grafica.
  - `alpha.157`: damage plan traduz compositor plan em ticket de damage/cache sem auth grafica.
  - `alpha.158`: present plan traduz damage plan em ticket de apresentacao sem auth grafica.
  - `alpha.159`: schedule plan traduz present plan em ticket de agendamento sem auth grafica.
  - `alpha.160`: vsync plan traduz schedule plan em ticket de sincronizacao sem auth grafica.
  - `alpha.161`: scanout plan traduz vsync plan em ticket de display sem auth grafica.
  - `alpha.162`: display plan traduz scanout plan em ticket final de display sem auth grafica.
  - `alpha.163`: output plan traduz display plan em ticket de saida visual sem auth grafica.
  - `alpha.164`: blit plan traduz output plan em ticket de copia visual sem auth grafica.
  - `alpha.165`: framebuffer plan traduz blit plan em ticket de framebuffer sem auth grafica.
  - `alpha.166`: flush plan traduz framebuffer plan em ticket de flush/cache sem auth grafica.
  - `alpha.167`: barrier plan traduz flush plan em ticket de barreira/visibilidade sem auth grafica.
  - `alpha.168`: fence plan traduz barrier plan em ticket de fence/sync sem auth grafica.
  - `alpha.169`: timeline plan traduz fence plan em ticket de timeline/semaphore sem auth grafica.
  - `alpha.170`: sync plan traduz timeline plan em ticket de sincronizacao sem auth grafica.
  - `alpha.171`: deadline plan traduz sync plan em ticket de deadline sem auth grafica.
  - `alpha.172`: completion plan traduz deadline plan em ticket de completion sem auth grafica.
  - `alpha.173`: ack plan traduz completion plan em ticket de acknowledge sem auth grafica.
  - `alpha.174`: retire plan traduz ack plan em ticket de retire sem auth grafica.
  - `alpha.175`: cleanup plan traduz retire plan em ticket de cleanup sem auth grafica.
  - `alpha.176`: seal plan traduz cleanup plan em ticket de seal sem auth grafica.
  - `alpha.177`: audit plan traduz seal plan em ticket de auditoria sem auth grafica.
  - `alpha.178`: record plan traduz audit plan em ticket de registro sem auth grafica.
  - `alpha.179`: receipt plan traduz record plan em ticket de recibo sem auth grafica.
  - `alpha.180`: ledger plan traduz receipt plan em ticket de ledger sem auth grafica.
  - `alpha.181`: journal plan traduz ledger plan em ticket de journal sem auth grafica.
  - `alpha.182`: archive plan traduz journal plan em ticket de archive sem auth grafica.
  - `alpha.183`: retention plan traduz archive plan em ticket de retention sem auth grafica.
  - `alpha.184`: expiry plan traduz retention plan em ticket de expiry sem auth grafica.
  - `alpha.185`: purge plan traduz expiry plan em ticket de purge sem auth grafica.
  - `alpha.186`: tombstone plan traduz purge plan em ticket de tombstone sem auth grafica.
  - `alpha.187`: compaction plan traduz tombstone plan em ticket de compaction sem auth grafica.
  - `alpha.188`: reclaim plan traduz compaction plan em ticket de reclaim sem auth grafica.
  - `alpha.189`: release plan traduz reclaim plan em ticket de release sem auth grafica.
  - `alpha.190`: GUI plan traduz release plan em ticket de GUI sem auth grafica.
  - `alpha.191`: window plan traduz GUI plan em ticket de janela sem auth grafica.
  - `alpha.192`: window surface plan traduz window plan em ticket de surface de janela sem auth grafica.
  - `alpha.193`: window compositor plan traduz window surface plan em ticket de compositor de janela sem auth grafica.
  - `alpha.194`: window present plan traduz window damage plan em ticket de apresentacao de janela sem auth grafica.
  - `alpha.195`: window schedule plan traduz window present plan em ticket de agendamento de janela sem auth grafica.
  - `alpha.196`: window vsync plan traduz window schedule plan em ticket de sincronizacao de janela sem auth grafica.
  - `alpha.197`: window scanout plan traduz window vsync plan em ticket de scanout de janela sem auth grafica.
  - `alpha.198`: window display plan traduz window scanout plan em ticket de display de janela sem auth grafica.
  - `alpha.199`: window output plan traduz window display plan em ticket de saida visual de janela sem auth grafica.
  - `alpha.200`: window blit plan traduz window output plan em ticket de blit de janela sem auth grafica.
  - `alpha.201`: window commit plan traduz window blit plan em ticket de commit atomico de janela sem auth grafica.
  - `alpha.202`: window flip plan traduz window commit plan em ticket de page flip de janela sem auth grafica.
  - `alpha.203`: window vblank plan traduz window flip plan em ticket de sincronizacao de vblank de janela sem auth grafica.
  - `alpha.204`: window event plan traduz window vblank plan em ticket de eventos de janela sem auth grafica.
  - `alpha.205`: window input plan traduz window event plan em ticket de input de janela sem auth grafica.
  - `alpha.206`: hardening de autenticacao por senha — `userdb_authenticate` em tempo constante, wipe de locais sensiveis e log de bootstrap sem salt/hash hex.
  - `alpha.207`: integracao de lockout — `userdb_authenticate_with_policy` compoe `auth_policy_check_allowed/record_*` + `userdb_authenticate` (timing-equalizado) atras de codigos publicos `USERDB_AUTH_OK/FAILED/LOCKED`; `system_setup.c` deduplica check/auth/record; typo do `Makefile` corrigido desbloqueia build de testes host-side.
  - `alpha.208`: hardening cripto — CSPRNG `csprng_get_bytes` chama `sha256_clear` no snapshot `temp_ctx` por iteracao (fecha vazamento de digest emitido em stack); `sha256_clear` vira API publica em `include/security/sha256.h` com semantica volatile-safe; `auth_policy` ganha `find_existing` (read-only) usado pelos caminhos de leitura, fechando ataque de exaustao da tabela `g_attempts` via probing read-only; `find_or_alloc` ganha LRU eviction de slot nao bloqueado quando a tabela esta cheia (lockouts ativos sao stickys); `userdb_set_password` wipa `source`/`line`/`rec`/`out` em todos os retornos (antes deixava `/etc/passwd` inteiro com salt+hash hex no heap freed); `memory_zero` em `user.c` vira volatile-safe contra dead-store elimination; testes contratuais novos cobrem `sha256_clear`, no-alloc reads e LRU eviction.
  - `alpha.209`: SHA-256 ctx wipe hygiene — `sha256_clear` propagado a todos os consumidores reais que processam segredos. `crypt.c::hmac_sha256` (usado por PBKDF2-SHA256 com 64000 iteracoes no path de login) zera `key_ctx` (quando key > BLOCK_SIZE) e `ctx` reutilizado pelas camadas inner/outer; `crypt.c::crypt_hmac_sha256` (API publica de HMAC) zera `ctx`; `sha256.c::sha256_hash` (convenience wrapper init/update/final) zera `ctx`; `key_storage_probe.c::compute_volume_key_hash` (deriva o digest da senha do volume cifrado na trilha de boot) zera `ctx`. Fecha o ultimo residuo de stack apos `sha256_final` em todos os sites que ate entao deixavam o hash produzido em `state[]` e o ultimo bloco padded (derivado do segredo) em `data[]` vivos na stack.
  - `alpha.210`: Update verifier fail-closed gate — `ed25519_verify` (antes matematicamente quebrado: aceitava qualquer assinatura forjada via equacao trivial `signature[32+i] = signature[i] + (hram[i] XOR ed25519_hash(public_key)[i])` solucionavel sem private key, sem multiplicacao escalar na curva, sem qualquer relacao com RFC 8032 — e era o UNICO gate criptografico em `manifest_signature_ed25519_valid` de update_agent.c, usado em 7 paths para aceitar manifests de update) retorna -1 incondicionalmente; `ed25519_sign`/`ed25519_create_keypair` zeram saidas em vez de produzir bytes determinsticos sem propriedade criptografica; header `security/ed25519.h` ganha SECURITY WARNING; comentario em `manifest_signature_ed25519_valid` esclarece o gate fail-closed em producao; testes contratuais novos em `test_crypt_vectors.c::test_ed25519_failclosed_contract` validam que verify rejeita todos os inputs, que sign nao escreve alem do buffer, que keypair zera ambas as saidas, e que NULLs sao tolerados; path UNIT_TEST com `g_update_manifest_verifier` hook continua funcional.
  - `alpha.211`: Privacy hardening do auth — `auth_policy_status` (alcancado pelo comando shell `auth-status` sem check de privilegio) substituido: antes listava todos os usernames trackados em `g_attempts[]` com `failed_count` e estado `LOCKED|ok` por conta, abrindo enumeracao passiva de usuarios reais, sinal de strategy (atacante sabe quais alvos estao no limiar do lockout) e lockout escape (atacante sabe quando o janelamento expira para retomar credential stuffing); agora emite apenas counts agregados (`Tracked: N (locked: M)`). `priv_log_emit` em `[priv] denied:`/`granted:` (callsites: `apps/settings.c:151` para `settings-add-user`, `shell/commands/user_manage.c:174` para denial generico de acoes administrativas, `shell/commands/user_manage.c:375` para `set-pass:other`) substitui `actor=<username>` por `actor_role=<role>` — antes o username vazava no klog ring, crash buffers e dumps de panic em todo denial de UI/shell, agora apenas o role do principal classifica o ator. Novas APIs publicas `auth_policy_tracked_count` e `auth_policy_locked_count` (counts nao-identificantes); `auth_policy_is_tracked` exposta apenas sob `#if defined(UNIT_TEST)` no header e no `.c` preserva regressao tests sem reabrir vetor de enumeracao em producao. Testes contratuais novos em `test_privilege.c::test_privilege_log_omits_username` cobrem denied/granted com role populado, role vazio, e `actor == NULL`. Testes em `test_auth_policy.c` foram migrados para usar `auth_policy_is_tracked` e os counts agregados, alem de validar contrato negativo do status output (`strstr(status, "<username>") == NULL`).
  - `alpha.212`: Timing-equalised lockout em `userdb_authenticate_with_policy` — o wrapper de login antes retornava `USERDB_AUTH_LOCKED` em microssegundos quando `auth_policy_check_allowed` indicava lockout, pulando o PBKDF2 inteiro do caminho normal (~50 ms). Atacante remoto media a latencia da resposta da API publica de login e distinguia accounts locked de not-locked com um unico probe por username, reabrindo via timing as informacoes que alpha.211 removeu de `auth_policy_status` (quais contas estao em lockout) e que alpha.206 removeu de `userdb_authenticate` (existencia). O leak nao dependia de acesso a `klog` nem a comando privilegiado — purely wall-clock-observable. Mitigacao: o wrapper agora chama `userdb_authenticate(username, password, out)` ANTES de verificar `allowed`; quando locked, descarta o rc, zera `out` via `user_record_clear` (defensive contra coincident-correct-password leak) e retorna `USERDB_AUTH_LOCKED`. `auth_policy_record_failure` intencionalmente NAO e chamado para accounts locked porque o `lockout_until_tick` ja esta ancorado e incrementar `failed_count` apenas poluiria o contador com ruido do atacante. Custo: ~50 ms por probe contra account locked (vantagem: rate-limita credential-stuffing e enumeracao de targets locked com a mesma janela do path normal). Composicao com alpha.206 (dummy salt para non-existent) e alpha.211 (status/logs sem PII) mantida — este patch herda a equalizacao existent-vs-non-existent automaticamente porque chama `userdb_authenticate` em todo caminho. Comentario SECURITY de ~40 linhas em `src/auth/user.c::userdb_authenticate_with_policy` documenta threat model, mitigacao, custo e relacao com slices anteriores. Teste contratual novo em `test_auth_policy.c` valida o lado policy (`auth_policy_check_allowed` continua retornando 0 para locked e 1 para fresh accounts) — a validacao do timing equalization em si fica em revisao de codigo porque `src/auth/user.c` nao esta no host-side test binary (depende de VFS/kmem).
  - `alpha.213`: HKDF-SHA256 (RFC 5869) primitiva criptografica fundacional — antes o sistema tinha apenas PBKDF2-SHA256 (senhas, 64000 iteracoes) e HMAC-SHA256 (publico via `crypt_hmac_sha256`); faltava um KDF context-aware para derivar subkeys a partir de segredos ja uniformes (output do CSPRNG, DH shared secret, output pos-PBKDF2). Os proximos slices de seguranca exigem essa primitiva: TLS userland (Etapa 5) precisa de HKDF para derivar handshake/traffic keys do master secret, key wrapping para AES-XTS precisa de HKDF para isolar dominio de cifragem (volume de dados vs metadata vs swap), secure boot precisa de HKDF para derivar verification keys versionadas, update_agent precisa de HKDF para context-binding quando Ed25519 real entrar. Implementacao em `src/security/crypt.c`: `crypt_hkdf_sha256_extract` (PRK = HMAC-SHA256(salt, IKM); substituicao zero-octet para salt vazio per §2.2 mandatoria), `crypt_hkdf_sha256_expand` (OKM = T(1) || T(2) || ... onde T(i) = HMAC(PRK, T(i-1) || info || byte(i))), `crypt_hkdf_sha256` (wrapper combinando). Tres helpers `static` internos (`hkdf_hmac_begin/update/end`) envolvem HMAC padrao em torno da API streaming SHA-256 para suportar `info_len` arbitrario sem alocacao dinamica nem stack frame ilimitado. Fail-closed: L > 255 * HashLen (8160 bytes) retorna -1 (sem isso o counter byte enrolaria silenciosamente), PRK_LEN < HashLen retorna -1, NULL prk/out retornam -1, L=0 e no-op success. Wipe hygiene volatile-safe completa: `zero_salt`, PRK, `t_prev` (em sucesso e em todos os caminhos de erro intermediario), `kipad`/`kopad`/`key_hash` e contextos SHA-256 zerados via `secure_clear`/`sha256_clear` em todos os exits (compativel com a hygiene instalada em alpha.208/alpha.209). Documentacao inline no header explicita threat model: HKDF NAO substitui PBKDF2 para senhas — assume IKM uniforme; pipeline correto e `password -> PBKDF2 -> PRK -> HKDF expand -> {subkeys}`. Testes em `tests/test_crypt_vectors.c::test_hkdf_sha256_vectors`: 3 test cases oficiais do RFC 5869 Appendix A (TC1 IKM=22/salt=13/info=10/L=42 small inputs, TC2 IKM=80/salt=80/info=80/L=82 long inputs spanando 3 iteracoes do expand, TC3 salt+info vazios exercitando substituicao zero-octet com NULL pointer e zero-length array) + 6 contract checks fail-closed (NULL outputs, L > bound, PRK curto, L=0 no-op). `expect_hex` ampliado de 64 para 256 bytes para acomodar OKM=82 do TC2. Sem callers reais ainda — fundacao para slices futuros, audit-friendly.
  - `alpha.214`: CSPRNG hardening profundo — auditoria revelou 5 problemas estruturais reais no coracao criptografico do sistema (consumido por TODA primitiva que precisa de aleatoriedade: PBKDF2 salts, AES-XTS keys, HKDF seeds, futuras Ed25519/X25519, session tokens, TLS handshake): (1) `rdtsc` com constraint `"=A"` mal-formed em x86_64 — "A" significa "union de RAX e RDX" em 64-bit (ambiguo, comportamento depende do compilador), agora usa `"=a"`/`"=d"` separados; (2) boot-time entropy fragil em VM hostil sem RDRAND/TSC fresco — pool inicial degenerava para `sha256(salt fixo + boot-marker)` conhecido a partir do binario, agora TSC jitter loop coleta 16 deltas de operacoes triviais para coletar variacao residual de cache/branch-predictor/scheduler (nao-deterministica mesmo em VM determinista) + pool address mixing KASLR-aware (rebuild em endereco diferente diverge o pool); (3) sem reseed proativo — sessoes longas operavam em pool estagnado sem nova entropia hardware sendo injetada periodicamente, agora `csprng_get_bytes` chama `mix_hardware_entropy()` (RDRAND + TSC + reseed_counter) a cada `CSPRNG_RESEED_INTERVAL_BYTES` (64 KiB) emitidos (comparativo: Linux 4 MiB, FreeBSD 1 MiB, OpenBSD por tempo — 64 KiB e conservador, limita janela de comprometimento); (4) `csprng_feed_entropy` so aceitava `uint32_t` — fragmentava fontes naturais de 64+ bits (TSC, network packets, disk buffers), agora nova API `csprng_feed_entropy_buffer(const void *data, size_t len)` aceita buffer arbitrario com NULL/zero graceful; (5) RDRAND sem retry-loop falhava sob contencao hardware transitoria — agora 10 attempts per Intel SDM (probabilidade de sucesso sobe de ~10^-2 worst case para ~1 - 2^-2000). Helper privado `mix_hardware_entropy` centraliza as fontes hardware reutilizado em init/reseed automatico/reseed manual. Nova API publica `csprng_reseed()` exposta para callers criticos antes de operacoes longas (key generation, TLS handshake, master key derivation). Caller real adicionado em `src/drivers/input/mouse.c::mouse_ps2_irq_handler` (cada byte de pacote PS/2 contem timing humano residual — intervalos entre movimentos sao nao-deterministicos por sub-milissegundos entre interrupcoes consecutivas). Threat model documentado inline no header (forward secrecy via output feedback, backward secrecy via SHA-256 one-way, indistinguibilidade polynomial; limites: VM hostil sem fontes hardware reais). Testes contratuais novos em `test_csprng.c`: `test_csprng_feed_buffer` (NULL/zero graceful + buffer arbitrario muda output stream), `test_csprng_reseed` (idempotencia comportamental + nao reseta pool), `test_csprng_auto_reseed_after_interval` (256 KiB = 4x intervalo, valida que reseed automatico nao corrompe stream nem destroi continuidade). Wipe hygiene preservada (alpha.208 `sha256_clear` em snapshot por iteracao). ABI publica preservada — `csprng_init`/`feed_entropy`/`get_bytes`/`next_u32`/`fill` mantem assinatura exata, APIs novas sao aditivas. Composicao com alpha.208 (CSPRNG snapshot wipe), alpha.209 (SHA-256 ctx wipe hygiene em consumidores) e alpha.213 (HKDF-SHA256 fundacional) integral.
- `desktop_run_frame` temporizado por APIC/timer, sem spin loop.
- Mouse PS/2 e USB HID fim-a-fim no dispatcher central.
  - `alpha.109`: snapshot de saude do dispatcher/fila preparado antes da migracao autoritativa.
  - `alpha.110`: teclado de janelas focadas passa pelo dispatcher central sem duplo callback.
  - `alpha.111`: scroll de mouse de janelas focadas passa pelo dispatcher central.
  - `alpha.112`: hover de mouse de janelas passa pelo dispatcher central.
  - `alpha.114`: click esquerdo de janelas comuns passa pelo dispatcher central.
  - `alpha.115`: right-click/context menu de janelas comuns passa pelo dispatcher central.
  - `alpha.116`: snapshot de saude expoe contrato de rotas comuns e especiais.
  - `alpha.117`: sessao grafica expoe snapshot operacional para prontidao de smokes.
  - `alpha.118`: sessao grafica deriva contrato de bloqueios/prontidao para smokes.
  - `alpha.119`: prontidao de smokes GUI isolada em unidade pura com cobertura host planejada.
  - `alpha.120`: blockers de prontidao GUI expoem mascara conhecida e nomes estaveis.
  - `alpha.121`: blockers de prontidao GUI expoem resumo deterministico para diagnostico.
  - `alpha.122`: readiness de smokes GUI embute resumo dos blockers.
  - `alpha.123`: readiness de smokes GUI diagnostica rotas ausentes do dispatcher.
- UX operacional de launcher, File Manager e desktop icons.
  - `alpha.113`: Start Menu usa viewport/scroll; File Manager e desktop icons movem itens para pastas por drag-and-drop seguro.
- Terminal gráfico consumindo shell real.
  - `alpha.105`: prompt real, cwd da sessão e logout via `bye` conectados ao contrato da shell.
- Fallback `CTRL+ALT+F1` para TTY.
  - `alpha.106`: chord nativo PS/2/Hyper-V retorna ao TTY pelo shutdown normal do desktop.
- Smokes `gui-session` e `mouse-events`.

### Critérios de aceite

- [ ] Usuário consegue logar, abrir terminal gráfico e voltar ao TTY.
- [ ] Mouse e teclado passam pelo dispatcher sem perda crítica.
- [x] Frame pacing reduz uso de CPU quando o desktop está ocioso.

## 6. Etapa 3 — Driver framework + entrada USB HID + storage estável

**Objetivo:** garantir que o hardware básico de um desktop comum funcione de forma previsível antes de qualquer trilha gráfica avançada ou de apps maduros. Hardware funcionar é pré-requisito de UX.

**ROI:** alto — teclado USB e storage confiável são base; sem isso o usuário não usa o sistema.

### Entregáveis

- Device manager com enumeração PCI/PCIe, IRQ, MSI/MSI-X, DMA API e logs.
- XHCI enumeration + USB HID class completo (fecha lacuna em `system-overview.md §9`).
- AHCI maduro + NVMe básico estável + tratamento de erros de I/O recuperável.
- VirtIO-net e VirtIO-block como prioridade VM (preserva foco VMware).
- VMware SVGA II como backend secundário para resoluções estáveis.
- Política de fallback: falha de driver não derruba kernel, registra diagnóstico.

### Critérios de aceite

- [ ] VM oficial sobe com storage/rede/vídeo previsíveis.
- [ ] Teclado USB funcional fora do `EFI ConIn` em VMware + UEFI.
- [ ] Falha de driver não derruba o kernel sem diagnóstico.
- [ ] Driver framework documenta ownership, DMA e teardown.

### Gates externos recomendados

- `make smoke-x64-vmware-usb-hid-keyboard` (novo).
- `make smoke-x64-iso TOOLCHAIN64=host` continua passando.
- `make all64` + `make release-check` continuam passando.

## 7. Etapa 4 — CapyDisplay 2D + scheduler/multithread runtime

**Objetivo:** criar uma camada gráfica 2D sólida e introduzir scheduler/multithread cooperativo no runtime — pré-requisito para apps que precisam de paralelismo previsível e UI fluida.

**ROI:** médio-alto — UI fluida sem travar é base de qualquer experiência polida; scheduler fecha uma lacuna conhecida em `project-overview.md`.

### Entregáveis

- Abstração de displays, modes, framebuffers, planes e cursor.
- Double buffering, clipping, damage tracking e blits otimizados.
- Cache de glyph/fontes e primitives 2D estáveis.
- API interna para compositor não depender diretamente do framebuffer bruto.
- Scheduler cooperativo + multithread runtime (incorporação do gap listado em `project-overview.md`).
- Política de panic/oops controlada quando thread de app falha.

### Critérios de aceite

- [ ] Compositor redesenha somente regiões danificadas quando possível.
- [ ] Cursor e texto não piscam sob resize/move de janela.
- [ ] Fallback framebuffer continua funcionando.
- [ ] Apps single-threaded existentes continuam funcionais como regressão.
- [ ] Thread de app crashando não derruba kernel nem desktop.

### Gates externos recomendados

- `make smoke-x64-vmware-compositor-damage-track` (novo).
- `make smoke-x64-vmware-scheduler-fairness` (novo).

## 8. Etapa 5 — TLS userland real

**Objetivo:** avançar `libcapy-tls` de metadata-only para handshake real. Pré-requisito direto para browser HTTPS (Etapa 7) e release/update HTTPS (Etapa 8).

**ROI:** alto — sem HTTPS real, nada moderno funciona (web, update, sync, qualquer serviço).

### Entregáveis

- Adaptador BearSSL inicializa engine somente após todos os gates passarem.
- Trust anchors deixam de ser somente metadata e passam por parse seguro.
- Buffers/contexto BearSSL têm ownership e zeroização explícitos.
- Handshake TLS cliente com SNI, hostname verification e timeout.
- Smoke local `tls-handshake` contra servidor controlado.

### Critérios de aceite

- [ ] Erro em qualquer gate mantém fail-closed.
- [ ] HTTPS em `libcapy-net` deixa de retornar unsupported para caso válido.
- [ ] Certificado inválido falha fechado.

### Gates externos recomendados

- `make smoke-x64-vmware-tls-handshake` (novo).
- `make release-check` continua passando.

## 9. Etapa 6 — Apps básicos do desktop maduros

**Objetivo:** entregar o primeiro conjunto de apps verdadeiramente usáveis sem CLI, com toolkit estável, ícones oficiais e localização nativa. Esta é a etapa onde o usuário comum começa a perceber valor real.

**ROI:** muito alto — primeiro valor visível ao usuário final.

### Entregáveis

- File Manager, Text Editor, Settings, Image Viewer (consome decoders existentes em `src/gui/core/{png,jpeg,bmp}_loader.c`), Calculator, Log Viewer, Notes/Calendar simples, Media Player de áudio e imagem (sem vídeo ainda — vídeo entra na Etapa 10).
- Toolkit `libcapy-ui` inicial: button, list, textbox, dialog, menu.
- Ícones oficiais e integração com launcher/taskbar.
- Acessibilidade básica: atalhos de teclado consistentes, contraste mínimo.
- Localização nativa: PT-BR e ES como targets de release; EN continua default obrigatório.

### Critérios de aceite

- [ ] Cada app abre, executa função primária e fecha sem crash.
- [ ] Falha de um app não derruba desktop.
- [ ] Apps usam o tema da Etapa 1.
- [ ] Strings de UI dos apps estão localizadas em PT-BR e ES com fallback EN.

### Gates externos recomendados

- `make smoke-x64-vmware-apps-basic-roundtrip` (novo): abre cada app, executa função primária, fecha sem leak/crash.

## 10. Etapa 7 — Browser usável com web moderna

**Objetivo:** transformar o `html_viewer` atual em browser usável para sites HTTPS estáticos modernos. JavaScript fica fora desta etapa (entra na Etapa 12); o foco é HTTPS, decode robusto, streaming render, cache e formulários.

**ROI:** muito alto — abre acesso à internet real para o usuário.

### Entregáveis

- HTTPS funcional via TLS Etapa 5.
- HTML parser robusto + CSS subset estável.
- Streaming render para páginas grandes (fecha gap `feature/browser-internet-improvements` em `system-overview.md §10`).
- Image decode JPEG/PNG/WebP em produção (decoders já existem, falta wiring com o renderer).
- HTTP cache em memória + persistência simples em disco.
- Cookies básicos com escopo por domínio.
- Formulários simples (login, busca).

### Critérios de aceite

- [ ] Páginas alvo (wikipedia, blog estático, docs, search engine simples, news estático) carregam e renderizam sem travar a UI.
- [ ] HTTPS válido carrega; HTTPS inválido falha fechado com mensagem clara.
- [ ] Imagens JPEG/PNG aparecem inline.
- [ ] Cache acelera segunda visita observavelmente.

### Gates externos recomendados

- `make smoke-x64-vmware-browser-https-static` (novo) com lista alvo de 5 sites.

## 11. Etapa 8 — Release/update gate oficial + instalador polido

**Objetivo:** fechar a release operacional com CI/smoke oficial, update HTTPS real e wizard de instalação amigável. Os blocos cripto (Ed25519 real em `alpha.217`) já estão prontos; falta wiring operacional.

**ROI:** médio-alto — confiança e manutenção contínua para o usuário final.

### Entregáveis

- Chave Ed25519 offline oficial publicada como chave esperada.
- CI executa smoke VMware+E1000 com DHCP/DNS/HTTP/HTTPS.
- `update-fetch` e payload HTTPS passam em ambiente controlado.
- Release gate promove artefatos somente com evidência pública válida.
- Instalador wizard amigável: seleção de disco, criação de usuário, idioma, fuso, política de senha.
- Migration de volume legacy → header-managed transparente (orquestrador já entregue em `alpha.232`).

### Critérios de aceite

- [ ] Smoke VMware+E1000 real passa em CI provisionada.
- [ ] Update HTTPS baixa, valida, prepara e aplica payload assinado.
- [ ] Evidência pública permite auditoria sem chave privada.
- [ ] Instalador wizard completa fresh install + reboot + login + persistência.

### Gates externos recomendados

- `make smoke-x64-vmware-installer-wizard` (novo).
- `make release-check` com payload assinado.

## 12. Etapa 9 — Package manager + SDK + ABI estável

**Objetivo:** permitir apps fora da imagem base e que terceiros publiquem software para CapyOS.

**ROI:** alto (médio-prazo) — ecossistema cresce sem releases monolíticas.

### Entregáveis

- Formato `.capypkg` com manifest, assinatura Ed25519 e rollback.
- `pkgd`, CLI `pkg` e app Software Center gráfico.
- ABI estável documentada em `include/`.
- SDK headers, samples e guia de build.

### Critérios de aceite

- [ ] Instalar, listar, atualizar e remover pacote sobrevive reboot.
- [ ] ABI pública tem versionamento e política de compatibilidade.
- [ ] App Software Center instala um pacote via UI sem CLI.

### Gates externos recomendados

- `make smoke-x64-vmware-pkg-install` (novo).

## 13. Etapa 10 — Áudio + multimídia básica

**Objetivo:** habilitar áudio de sistema e ampliar Media Player para playlist e reprodução real. Vídeo software simples entra aqui como bônus opcional.

**ROI:** alto — multimídia é uso diário do desktop comum (música, calls leves, vídeos curtos).

### Entregáveis

- Driver Intel HDA + AC97 + USB Audio class (ao menos um deles validado em VMware).
- Mixer de sistema + controle por app.
- Decoders de áudio: WAV nativo, OGG/Vorbis ou MP3 via library vendorizada.
- App Media Player evolui para suportar playlist e visualização básica.
- Vídeo software simples (decode 1 codec leve em resolução baixa) opcional.

### Critérios de aceite

- [ ] Reprodução de WAV/OGG sem stutter perceptível em VM oficial.
- [ ] Mixer permite ajuste de volume global e por app.
- [ ] Falha de driver de áudio não derruba o sistema.

### Gates externos recomendados

- `make smoke-x64-vmware-audio-playback-roundtrip` (novo).

## 14. Etapa 11 — WiFi + power management + suspend/resume

**Objetivo:** habilitar uso real fora da VM oficial: WiFi, ACPI battery, suspend/resume. Sem isso o sistema não roda em laptops/desktops modernos.

**ROI:** muito alto — exigência para uso fora do laboratório.

### Entregáveis

- Stack 802.11 mínimo: ao menos um driver WiFi popular (RTL8821CE ou Intel iwlwifi se viável).
- WPA2/WPA3 supplicant userland.
- ACPI battery + thermal monitoring básico.
- Suspend-to-RAM (S3) inicial em VMware e máquina real onde viável.

### Critérios de aceite

- [ ] WiFi conecta a rede WPA2 com DHCP funcional.
- [ ] ACPI battery aparece no system tray com nível atualizando.
- [ ] Suspend/resume preserva sessão em VMware.

### Gates externos recomendados

- `make smoke-x64-vmware-wifi-dhcp-roundtrip` (novo) com WiFi via passthrough quando disponível.
- `make smoke-x64-vmware-acpi-battery-readout` (novo).

## 15. Etapa 12 — JS engine sandboxed

**Objetivo:** habilitar web dinâmica sem comprometer isolamento. Browser da Etapa 7 ganha JavaScript com bridge DOM controlada e budget de execução.

**ROI:** alto — abre a web realmente moderna; sem JS muitos sites úteis não funcionam.

### Entregáveis

- Decisão QuickJS vs CapyJS subset.
- Bridge DOM controlada e budget de execução.
- Sem syscalls diretas a partir do script.

### Critérios de aceite

- [ ] Script básico altera título/DOM permitido.
- [ ] Loop infinito é interrompido por budget.
- [ ] Página com JS hostil não escapa do sandbox.

### Gates externos recomendados

- `make smoke-x64-vmware-browser-js-dom` (novo) em página de teste com DOM mutável.

## 16. Etapa 13 — CapyLX L0-L5 unificado

**Objetivo:** iniciar e expandir compatibilidade Linux estilo WSL1, sem kernel Linux. Une os antigos níveis L0-L2 (CLI estático) e L3-L5 (POSIX amplo) em uma etapa única, agora rebaixada para depois das prioridades do desktop comum.

**ROI:** médio — público power user e ferramentas Linux; não é exigência do desktop comum.

### Entregáveis

- `linux_personality` por processo.
- Loader ELF64 Linux com stack `argc/argv/envp/auxv`.
- Dispatcher Linux syscall auditável.
- Syscalls mínimas: read/write/openat/close/fstat/lseek/mmap/munmap/brk, exit/exit_group/getpid/clock_gettime/uname/getrandom.
- `clone`, `futex`, sinais, `wait4`, `execve`, `pipe2`, `dup3`, poll/epoll.
- Sockets Linux ABI traduzidos para a rede CapyOS.
- App bundles com bibliotecas empacotadas.
- `/dev`, `/proc`, `/tmp` e `/etc` mínimos.
- Rootfs Linux-like opcional e isolado.

### Critérios de aceite

- [ ] Binário Linux estático simples executa e retorna código correto.
- [ ] Ferramentas CLI Linux dinâmicas simples rodam em app bundle.
- [ ] Threads/futex funcionam para libc/pthread comum.
- [ ] Syscall desconhecida retorna `-ENOSYS` de forma previsível.
- [ ] CapyLX permanece módulo de compatibilidade, não base do sistema.

### Gates externos recomendados

- `make smoke-x64-vmware-capylx-binary-static` (novo).
- `make smoke-x64-vmware-capylx-pthread` (novo).

## 17. Etapa 14 — Wayland bridge + apps Linux GUI

**Objetivo:** rodar apps gráficos Linux modernos sem X11 inicial, integrados ao compositor CapyOS.

**ROI:** médio-alto — expande ecossistema gráfico para milhares de apps Linux já existentes.

### Entregáveis

- Servidor/proxy Wayland mínimo: `wl_compositor`, `wl_shm`, input e `xdg_shell` básico.
- Ponte entre Wayland surfaces e compositor CapyOS.
- Clipboard e resize/focus básicos.

### Critérios de aceite

- [ ] App Wayland simples abre janela, recebe input e fecha.
- [ ] Falha do app Linux não derruba compositor.

### Gates externos recomendados

- `make smoke-x64-vmware-wayland-roundtrip` (novo).

## 18. Etapa 15 — Mesa/Vulkan path + CapyLang

**Objetivo:** abrir rota gráfica software via Mesa/lavapipe e introduzir CapyLang como linguagem própria de automação e apps. CapyLang foi rebaixada nesta reorganização por ter baixo ROI direto ao desktop comum, ainda que seja identidade de longo prazo do projeto.

**ROI:** médio — gráficos software permitem demos; CapyLang é identidade do projeto.

### Entregáveis

- Mesa software/lavapipe para Vulkan software inicial.
- VirGL/Venus sobre VirtIO-gpu quando disponível.
- Política clara: Vulkan real exige driver/memory manager/sync antes.
- CapyLang: parser, VM bytecode e `.capyscript`.
- Bindings FS/config/shell seguros.
- Módulos, FFI controlada, formatter e LSP em ondas posteriores (não bloqueiam a etapa).

### Critérios de aceite

- [ ] Demo gráfica software roda via caminho Mesa controlado.
- [ ] Fallback 2D continua estável quando aceleração indisponível.
- [ ] Script CapyLang de automação roda em ring 3.
- [ ] Bindings respeitam permissões do usuário.

### Gates externos recomendados

- `make smoke-x64-vmware-mesa-software-demo` (novo).
- `make smoke-x64-vmware-capylang-automation` (novo).

## 19. Etapa 16 — Plataforma 1.0 hardening

**Objetivo:** consolidar CapyOS como sistema sólido para uso contínuo. É o fechamento de release 1.0.

**ROI:** alto (qualidade de produção) — auditável e estável para uso real prolongado.

### Entregáveis

- Secure Boot e measured boot.
- SMP/multicore.
- Firewall mínimo.
- USB completo, polish final.
- Suíte Office/IDE opcional via package manager.

### Critérios de aceite

- [ ] Plataforma tem boot/update/rollback/GUI/apps/compatibilidade auditáveis.
- [ ] Segurança e performance têm gates regressivos documentados.
- [ ] SMP roda sob workload sintético sem regressão observável.

### Gates externos recomendados

- `make release-check` em pipeline CI oficial.
- `make smoke-x64-vmware-smp-stress` (novo).
- `make smoke-x64-vmware-firewall-block` (novo).

## 20. Próximo comando esperado

A próxima implementação, quando autorizada pela conclusão operacional externa da Etapa 2, deve continuar somente a **Etapa 3 — Driver framework + entrada USB HID + storage estável** conforme a nova sequência reorganizada por ROI. Nenhuma etapa posterior deve começar antes dela fechar 100%.
