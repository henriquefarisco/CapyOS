# CapyOS — Sessão gráfica operacional

## Objetivo

A Etapa 2 transforma a fundação GUI existente em uma sessão gráfica mais
operacional, com foco em login GUI, frame pacing, dispatcher de input, terminal
gráfico real e fallback seguro para TTY.

## Incremento `0.8.0-alpha.101+20260510`

- O loop do desktop reduz CPU ociosa com espera cooperativa de 1 tick quando o
  scheduler já observou tick e tem alvo seguro para troca de contexto.
- O fallback x86_64 usa `hlt` após tick observado para aguardar a próxima
  interrupção em vez de executar um spin longo por frame.
- O fallback host/unit-test mantém espera PIT/pause curta e limitada.
- O scheduler ganhou `scheduler_can_sleep_current()` para evitar dormir a tarefa
  atual sem tick observado, peer pronto ou idle preemptável.

## Incremento `0.8.0-alpha.102+20260510`

- O compositor expõe `compositor_needs_render()` como consulta somente leitura.
- `desktop_run_frame()` chama composição apenas com cena suja.
- O cursor fica cacheado por posição e também respeita invalidação interna
  do compositor.
- Cena recomposta ainda força cursor por cima do frontbuffer.

## Incremento `0.8.0-alpha.103+20260510`

- O runtime de login expõe `login_window_contract_evaluate()`.
- O contrato declara prontidão do futuro loginwindow GUI sem coletar senha.
- Modo manutenção/recovery dinâmico, ausência de input e callbacks
  incompletos bloqueiam a oferta GUI de forma determinística.
- O login textual atual permanece como caminho operacional seguro.

## Incremento `0.8.0-alpha.104+20260510`

- O launcher permanece ancorado ao canto inferior esquerdo e separa ações de
  sessão em rodapé fixo.
- Apps recentes ficam em grupo recolhível, mantendo busca e navegação por
  teclado.
- A taskbar apresenta estado de rede compacto derivado de `net_stack_status()`.
- O desktop remove o rail lateral de ícones para reduzir ruído visual.
- O File Manager adota seleção por clique simples, abertura por clique seguinte,
  navegação de histórico, subir pasta e menu de contexto operacional.
- Bateria permanece sem indicador até existir backend confiável de ACPI/bateria
  para evitar telemetria visual falsa.

## Incremento `0.8.0-alpha.105+20260511`

- O terminal grafico usa `shell_build_prompt()` para refletir usuario, host e
  diretorio atual da sessao real.
- O runtime expõe a sessao da shell do desktop para evitar dependencia em
  `session_active()` durante prompt e `open terminal here`.
- A saida de comandos instala callbacks de terminal apenas durante o dispatch e
  restaura o sink global ao final.
- `bye` encerra a sessao grafica pelo estado real do `shell_context`; `exit`
  continua retornando ao CLI textual.

## Incremento `0.8.0-alpha.106+20260511`

- `KEY_TTY_FALLBACK` reserva um sentinel interno para retorno seguro ao TTY.
- O runtime x64 acompanha Ctrl/Alt nos backends PS/2 e Hyper-V e emite o
  sentinel quando `CTRL+ALT+F1` e detectado.
- O loop do desktop trata o sentinel antes do dispatch para janelas e encerra a
  GUI pelo fluxo normal de `desktop_stop()`.
- EFI ConIn e COM1 permanecem sem simulacao de modificadores; nao ha chord falso
  nesses caminhos textuais.

## Incremento `0.8.0-alpha.107+20260511`

- `login_window_view_model_build()` deriva estado apresentavel a partir do
  contrato fail-closed do loginwindow.
- O view model separa estados `ready`, `maintenance`, `input-unavailable`,
  `runtime-incomplete` e `blocked`.
- `password_enabled` so e habilitado quando o contrato esta `ready`.
- Sem input disponivel, o modelo bloqueia recuperacao grafica e exige fallback
  textual.

## Incremento `0.8.0-alpha.108+20260511`

- `login_render_window_preview()` renderiza uma previa textual/passiva do futuro
  loginwindow com dados do view model.
- A previa mostra estado, motivo, campo de senha desabilitado/contrato pronto,
  recuperacao e fallback textual.
- O renderer usa somente `ops->print()` e nao chama `readline()` nem
  `system_login()`.
- O login textual permanece autoritativo e inalterado.

## Incremento `0.8.0-alpha.109+20260511`

- `gui_window_dispatcher_health_snapshot()` agrega metricas do dispatcher e da
  fila `gui_event`.
- O snapshot expõe avisos de backlog, drops e captura de mouse obsoleta.
- `desktop_run_frame()` amostra a saude do dispatcher sem chamar
  `gui_window_dispatch()` e sem drenar eventos espelhados.
- O caminho autoritativo atual de teclado/mouse permanece inalterado neste alpha.

## Incremento `0.8.0-alpha.110+20260511`

- O teclado destinado a janelas focadas agora e entregue por
  `gui_window_dispatch_event()` como evento `GUI_EVENT_KEY_DOWN`.
- O desktop preserva antes do dispatcher os atalhos globais, inline prompt,
  Escape de overlays e Start Menu.
- O caminho antigo de fila espelhada mais `focused->on_key()` direto foi removido
  para evitar backlog e duplo callback.
- Mouse permanece no caminho atual ate a proxima migracao com guardrails.

## Incremento `0.8.0-alpha.111+20260511`

- O scroll de mouse destinado a janelas focadas agora e entregue por
  `gui_window_dispatch_event()` como evento `GUI_EVENT_MOUSE_SCROLL`.
- `desktop_overlay_active()` continua bloqueando scroll antes do dispatcher.
- O caminho antigo de fila espelhada mais `scroll_win->on_scroll()` direto foi
  removido para este caso.
- Click, drag, resize, titlebar, taskbar, context menu e desktop icons permanecem
  nos caminhos atuais ate a proxima migracao.

## Incremento `0.8.0-alpha.112+20260511`

- Hover/mouse move de janelas agora e entregue por `gui_window_dispatch_event()`
  como evento `GUI_EVENT_MOUSE_MOVE`.
- Context menu e Start Menu continuam recebendo hover antes do dispatcher.
- `wm_handle_mouse_move()` e cursor hint permanecem no desktop para preservar
  drag/move/resize e UX de cursor.
- O caminho antigo de fila espelhada mais `hov->on_hover()` direto foi removido
  para hover de janelas comuns.

## Incremento `0.8.0-alpha.113+20260511`

- O Start Menu calcula altura visivel limitada pela tela e usa scroll interno
  para recentes/apps antes de atingir o rodape de sessao.
- Wheel sobre o launcher e consumido pelo proprio `taskbar` antes do dispatch de
  scroll para janelas comuns.
- O File Manager ganhou toolbar com `Refresh` explicito e realce de diretorio alvo
  durante drag-and-drop.
- File Manager e desktop icons movem itens para pastas usando `vfs_rename()`
  somente quando o alvo de drop e diretorio.
- Overlays continuam bloqueando DnD e mantem prioridade sobre janelas comuns.

## Incremento `0.8.0-alpha.114+20260511`

- Mouse down/up esquerdo de janelas comuns agora e entregue por
  `gui_window_dispatch_event()` como `GUI_EVENT_MOUSE_DOWN` e
  `GUI_EVENT_MOUSE_UP`.
- O desktop preserva antes do dispatcher inline prompt, context menu, Start Menu,
  taskbar, botoes de titlebar e o window manager para move/resize.
- O caminho antigo de fila espelhada mais `win->on_mouse()` direto foi removido
  para click esquerdo de janelas comuns.
- Captura de mouse no dispatcher e opt-in por janela via `capture_mouse`; o
  File Manager opta por captura e finaliza drag-and-drop pelo mouse-up capturado.
- Desktop icons permanecem no caminho direto porque pertencem ao plano de fundo,
  nao a uma janela comum.

## Incremento `0.8.0-alpha.115+20260511`

- Right-click/context menu de janelas comuns agora e entregue por
  `gui_window_dispatch_event()` como `GUI_EVENT_MOUSE_DOWN` com botao direito.
- `dispatch_mouse_button()` permanece como ponto autoritativo para chamar
  `win->on_context_menu()` com coordenadas locais.
- O desktop preserva antes do dispatcher inline prompt, context menu aberto,
  Start Menu, taskbar e o caminho direto dos desktop icons.
- O caminho antigo de `rwin->on_context_menu()` direto no desktop foi removido
  para janelas comuns.

## Incremento `0.8.0-alpha.116+20260511`

- `gui_window_dispatcher_health_snapshot()` passa a expor `routes`, um contrato
  estatico de rotas de input comuns ja migradas para o dispatcher.
- O contrato registra teclado, scroll, hover, click esquerdo, right-click/context
  menu, captura opt-in e ausencia de fila espelhada.
- O mesmo contrato registra overlays, window manager, titlebar, taskbar e desktop
  icons como caminhos diretos especiais preservados por prioridade modal ou por
  pertencerem ao plano de fundo.
- A mudanca nao altera dispatch em runtime; apenas torna a prontidao fim-a-fim
  auditavel antes dos smokes reais.

## Incremento `0.8.0-alpha.117+20260511`

- `desktop_session_health_snapshot()` expõe prontidão operacional da sessão
  gráfica sem alterar o loop do desktop.
- O snapshot consolida framebuffer, dimensões, mouse, cursor, taskbar, overlays,
  menu iniciar, drag do window manager, janela focada e amostras do dispatcher.
- A estrutura inclui `gui_window_dispatcher_health`, reaproveitando o contrato de
  rotas do `alpha.116` para futuras validações `gui-session` e `mouse-events`.
- A mudança é observacional e não adiciona dispatch, fila, render ou callback.

## Incremento `0.8.0-alpha.118+20260511`

- `desktop_session_smoke_readiness_snapshot()` transforma o health snapshot em
  contrato de prontidao para smokes futuros.
- `DESKTOP_SMOKE_BLOCK_*` enumera bloqueios operacionais de sessao, framebuffer,
  dimensoes, mouse, cursor, taskbar, dispatcher, rotas, fila, overlays e drag de
  janela.
- `gui_session_ready` e `mouse_events_ready` ficam disponiveis como flags
  derivadas, sem executar smoke real nem alterar runtime.
- A funcao e observacional, zera a saida antes do calculo e retorna 0 para entrada
  invalida.

## Incremento `0.8.0-alpha.119+20260511`

- `desktop_session_smoke_readiness_from_health()` concentra a derivacao pura de
  prontidao a partir de `desktop_session_health`.
- `desktop_session_smoke_readiness_snapshot()` permanece como coletor do estado
  real e delega a derivacao para a unidade pura.
- `tests/test_desktop_smoke_readiness.c` cobre caminhos prontos e bloqueados sem
  exigir o link do desktop runtime completo.
- A unidade pura nao acessa compositor, fila, apps, rede, shell ou renderizacao.

## Incremento `0.8.0-alpha.120+20260511`

- `DESKTOP_SMOKE_BLOCK_KNOWN_MASK` documenta todos os blockers conhecidos do
  contrato de prontidao.
- `desktop_smoke_block_known_mask()` permite consultar a mascara por API.
- `desktop_smoke_block_name()` fornece labels estaveis para diagnostico e retorna
  `unknown` para bits fora do contrato conhecido.
- A cobertura host planejada verifica mascara, labels e fallback desconhecido sem
  executar smokes reais.

## Incremento `0.8.0-alpha.121+20260511`

- `desktop_smoke_blocker_summary()` transforma a bitmask de blockers em resumo
  deterministico para diagnostico.
- O resumo separa `known_blocker_flags` e `unknown_blocker_flags`, preservando
  bits fora do contrato conhecido.
- `blocker_count` conta blockers conhecidos individualmente e flags desconhecidas
  como um grupo `unknown`.
- `first_blocker_name` segue a ordem estavel do contrato ou retorna `none` quando
  nao ha blockers.

## Incremento `0.8.0-alpha.122+20260511`

- `struct desktop_session_smoke_readiness` carrega `blocker_summary` junto de
  `blocker_flags`.
- `desktop_session_smoke_readiness_from_health()` deriva o resumo embutido depois
  de calcular a bitmask de blockers.
- Consumidores futuros de smoke podem ler readiness, bitmask e resumo em uma unica
  estrutura, sem segunda chamada obrigatoria.
- A mudanca preserva o helper independente `desktop_smoke_blocker_summary()`.

## Incremento `0.8.0-alpha.123+20260511`

- `DESKTOP_DISPATCHER_ROUTE_*` torna auditavel cada rota do contrato do dispatcher.
- `desktop_dispatcher_route_summary()` separa rotas esperadas, prontas e ausentes.
- `struct desktop_session_smoke_readiness` carrega `route_summary`, permitindo que
  futuros smokes apontem a primeira rota ausente junto ao blocker `dispatcher-routes`.
- Rotas nulas falham fechado com todas as rotas esperadas marcadas como ausentes.
- A mudanca nao chama `gui_window_dispatch_event()` nem altera fluxo de input.

## Incremento `0.8.0-alpha.124+20260511`

- `login_window_credential_policy_from_contract()` deriva uma politica pura de
  credenciais a partir do contrato fail-closed do loginwindow.
- A politica fixa versao, limite de senha, caractere de mascara, wipe obrigatorio
  e autoridade do login textual.
- Mesmo quando o contrato esta `ready`, o campo de senha pode ser renderizado,
  mas `password_submit_allowed` permanece desabilitado nesta etapa.
- Recuperacao grafica continua restrita a uma acao textual em modo manutencao,
  sem coletar segredo pela GUI.

## Incremento `0.8.0-alpha.125+20260511`

- `login_recovery_resume_policy_evaluate()` concentra a decisao de sair da
  recuperacao textual e voltar ao login normal.
- A politica exige sessao de recuperacao ativa, pedido consumido, runtime pronto
  e modo manutencao ja limpo antes de permitir a transicao.
- Quando o pedido existe mas a manutencao ainda esta ativa, o runtime permanece
  na recuperacao e imprime o motivo `maintenance-mode-active`.
- A transicao aprovada exige reset de sessao e rerender da tela de login antes
  de iniciar o login textual normal.

## Incremento `0.8.0-alpha.126+20260511`

- `struct login_window_credential_buffer` representa storage efemero para o
  futuro campo de senha do loginwindow.
- `login_window_credential_buffer_init()` limpa storage recebido antes de validar
  politica, falha fechado sem politica segura e nunca habilita submit grafico.
- Append/backspace preservam limite efetivo, NUL terminator e motivo estavel de
  bloqueio; overflow nao altera o segredo armazenado.
- `login_window_credential_buffer_masked_text()` produz somente mascara e
  `login_window_credential_buffer_wipe()` zera todo o storage conhecido.

## Incremento `0.8.0-alpha.127+20260511`

- `struct login_window_credential_submit_gate` resume a tentativa futura de
  submit de credenciais do loginwindow sem acionar autenticacao grafica.
- `login_window_credential_submit_gate_evaluate()` aceita politica e buffer,
  expõe diagnostico de policy/buffer e sempre mantem `submit_allowed` e
  `auth_attempt_allowed` em `0` neste alpha.
- O gate rejeita politica ausente, campo desabilitado, mascara ausente, wipe
  ausente, buffer vazio, buffer nao mascarado, buffer ja limpo e overflow.
- Mesmo com buffer preenchido e mascarado, o motivo final permanece
  `gui-submit-disabled`, preservando o login textual como caminho autoritativo.

## Incremento `0.8.0-alpha.128+20260511`

- `struct login_window_credential_submit_attempt` registra uma tentativa futura
  de submit sem abrir rota de autenticacao grafica.
- `login_window_credential_submit_attempt_consume()` avalia o gate fail-closed,
  preserva `submit_allowed` e `auth_attempt_allowed` em `0` e herda o motivo
  estavel do gate.
- Quando um buffer existe, a tentativa sempre chama
  `login_window_credential_buffer_wipe()`, inclusive se a politica estiver
  ausente ou se o gate bloquear por overflow.
- O resultado expõe se havia segredo, se o gate foi avaliado, se o wipe foi
  tentado e se o wipe concluiu com sucesso.

## Incremento `0.8.0-alpha.129+20260511`

- `struct login_window_credential_input_result` consolida o resultado de uma
  acao de input do futuro campo de senha do loginwindow.
- `login_window_credential_input_apply()` reduz append, backspace, submit e
  cancel para mudancas deterministicas no buffer, sem acionar autenticacao GUI.
- Append/backspace exigem politica segura e buffer inicializado; submit reutiliza
  a tentativa fail-closed com wipe obrigatorio; cancel limpa o buffer sem tentar
  submit.
- O resultado sempre preserva `submit_allowed` e `auth_attempt_allowed` em `0`,
  exige texto mascarado e carrega motivos estaveis como `policy-unavailable`,
  `gui-submit-disabled`, `cancelled` e `input-action-unknown`.

## Incremento `0.8.0-alpha.130+20260511`

- `struct login_window_credential_field_view` consolida uma leitura segura do
  futuro campo de senha do loginwindow.
- `login_window_credential_field_view_build()` escreve apenas texto mascarado em
  buffer fornecido pelo chamador e nunca retorna storage bruto de credencial.
- A view expõe politica, disponibilidade do buffer, comprimento, limite,
  truncamento, estado `empty`/`filled` e motivos fail-closed estaveis.
- Politica ausente, output indisponivel, buffer ausente, buffer nao mascarado,
  wipe ausente e overflow mantem `submit_allowed`/`auth_attempt_allowed` em `0`.

## Incremento `0.8.0-alpha.131+20260511`

- `struct login_window_credential_panel` consolida estado renderizavel do futuro
  painel de credenciais do loginwindow sem expor segredo bruto.
- `login_window_credential_panel_build()` combina a field view mascarada com o
  ultimo resultado de input para estados `filled`, `editing`, `submit-blocked`,
  `cancelled` e `input-blocked`.
- O painel propaga mascara, truncamento, comprimento, limite, wipe e flags de
  input, mas preserva `submit_allowed` e `auth_attempt_allowed` em `0`.
- Campo bloqueado, input bloqueado, submit e cancel mantem motivos estaveis e
  nao abrem rota de autenticacao grafica.

## Incremento `0.8.0-alpha.132+20260511`

- `struct login_window_credential_interaction` consolida uma etapa completa de
  interacao do futuro campo de credenciais do loginwindow.
- `login_window_credential_interaction_step()` aplica exatamente uma acao de
  input, reconstrói o painel mascarado depois da mutacao ou wipe e devolve
  input + painel em um snapshot auditavel.
- Append, submit, cancel, politica ausente e acao desconhecida passam pelo mesmo
  pipeline, preservando mascara e motivos estaveis.
- A interacao mantém `submit_allowed` e `auth_attempt_allowed` em `0`, sem expor
  storage bruto e sem abrir rota de autenticacao grafica.

## Incremento `0.8.0-alpha.133+20260511`

- `struct login_window_credential_readiness` consolida diagnostico puro de
  prontidao do futuro campo de credenciais do loginwindow.
- `login_window_credential_readiness_evaluate()` resume politica, buffer, painel
  e interacao em flags de renderizacao, input, mascara, wipe e overflow.
- O snapshot marca `submit_blocked` e preserva `submit_allowed`/
  `auth_attempt_allowed` em `0`, inclusive quando render/input estao prontos.
- Politica ausente, buffer indisponivel, overflow, mascara truncada e input
  bloqueado geram motivos estaveis sem expor storage bruto.

## Incremento `0.8.0-alpha.134+20260511`

- `struct login_window_credential_audit_event` consolida evento seguro para
  auditoria do futuro fluxo de credenciais do loginwindow.
- `login_window_credential_audit_event_build()` consome readiness e interacao
  opcional para classificar eventos como `credential-ready`,
  `credential-input-accepted`, `credential-input-blocked`,
  `credential-submit-blocked`, `credential-cancelled` ou `credential-blocked`.
- O evento registra acao, input, wipe, estado e motivo de bloqueio, mas mantém
  segredo, texto mascarado e comprimento redigidos.
- `submit_blocked` fica ativo e `submit_allowed`/`auth_attempt_allowed` ficam em
  `0` em todos os caminhos, inclusive sem readiness.

## Incremento `0.8.0-alpha.135+20260511`

- `struct login_window_credential_view_model` consolida estado consumivel pela
  futura GUI de credenciais do loginwindow.
- `login_window_credential_view_model_build()` compoe readiness e auditoria
  redigida em flags de renderizacao, input, cancelamento, wipe, fallback e
  mensagens seguras.
- O view model exige auditoria redigida para renderizar e falha fechado quando
  auditoria/readiness estao ausentes ou inseguros.
- O submit grafico permanece invisivel, desabilitado e bloqueado; autenticacao
  continua exclusiva do login textual.

## Incremento `0.8.0-alpha.136+20260511`

- `struct login_window_credential_ui_step` consolida uma etapa completa e
  auditavel para a futura UI de credenciais do loginwindow.
- `login_window_credential_ui_step_build()` aplica exatamente uma acao de input
  e encadeia interacao, readiness, auditoria redigida e view model.
- O snapshot composto propaga apenas flags seguras, mensagem, estado e motivo de
  bloqueio; storage bruto, texto mascarado e comprimento seguem fora do contrato.
- Submit grafico continua bloqueado em todos os caminhos, inclusive append,
  submit, cancel e acao desconhecida.

## Incremento `0.8.0-alpha.137+20260511`

- `struct login_window_credential_ui_session` representa uma sessao one-shot
  segura para a futura UI de credenciais do loginwindow.
- `login_window_credential_ui_session_build()` inicializa storage efemero,
  compoe uma etapa de UI, propaga apenas flags redigidas e executa wipe antes de
  retornar.
- O scratch de mascara tambem e limpo antes da saida, e o contrato publico nao
  carrega storage bruto, texto mascarado ou comprimento.
- Submit grafico continua bloqueado; a autenticacao segue exclusiva do login
  textual.

## Incremento `0.8.0-alpha.138+20260511`

- `struct login_window_credential_recovery_view_model` une a sessao segura de
  credenciais com a politica textual de recuperacao/retorno.
- `login_window_credential_recovery_view_model_build()` exige sessao de
  credenciais segura, redigida e com storage limpo antes de expor recuperacao ou
  retorno ao login normal.
- Recuperacao e resume permanecem text-session-only; submit e autenticacao
  grafica continuam bloqueados em todos os estados.
- O contrato publico nao carrega storage bruto, texto mascarado, comprimento ou
  snapshots internos.

## Incremento `0.8.0-alpha.139+20260511`

- `struct login_window_credential_screen_view_model` consolida login view,
  sessao one-shot de credenciais e recuperacao textual em um snapshot publico
  seguro para a futura tela do loginwindow.
- `login_window_credential_screen_view_model_build()` exige login textual
  autoritativo, submit grafico desabilitado, sessao de credenciais limpa/redigida
  e recuperacao segura antes de renderizar acoes.
- A tela propaga apenas flags, titulo, estado, mensagem e motivo; nao carrega
  storage bruto, texto mascarado, comprimento ou snapshots internos.
- Submit e autenticacao grafica continuam bloqueados em todos os caminhos.

## Incremento `0.8.0-alpha.140+20260511`

- `struct login_window_credential_screen_session` consolida a chamada one-shot
  que a futura GUI podera usar para obter uma tela de credenciais segura.
- `login_window_credential_screen_session_build()` compoe contrato, login view,
  politica, sessao one-shot de credenciais, politica de resume, recuperacao e
  snapshot final de tela usando apenas variaveis locais.
- O contrato publico final expõe somente flags redigidas, titulo, estado,
  mensagem e motivo de bloqueio; nao aninha snapshots que carregam politica,
  storage, texto mascarado ou comprimento.
- Storage e scratch recebidos sao limpos mesmo em falha inicial, e submit/auth
  grafico permanecem bloqueados.

## Incremento `0.8.0-alpha.141+20260511`

- `struct login_window_credential_screen_render_plan` traduz a sessao one-shot
  segura de credenciais em um plano de UI para a futura GUI do loginwindow.
- `login_window_credential_screen_render_plan_build()` deriva layout, painel de
  senha, foco, painel de recuperacao, botoes de recuperacao/resume, avisos e
  acao primaria sem carregar segredo, mascara, comprimento ou snapshots internos.
- O plano so marca a sessao como segura quando a tela foi construida, a sessao de
  credenciais esta limpa/redigida, submit permanece bloqueado e o login textual
  segue autoritativo.
- `submit_button_visible` e `submit_button_enabled` ficam sempre desligados;
  `auth_attempt_allowed` permanece `0` em todos os caminhos.

## Incremento `0.8.0-alpha.142+20260511`

- `struct login_window_credential_screen_action_plan` valida uma intencao da
  futura GUI contra o render plan seguro da tela de credenciais.
- `login_window_credential_screen_action_plan_build()` aceita apenas acoes
  redigidas: foco de credencial, abrir recuperacao textual, retomar login
  textual ou usar login textual autoritativo.
- Submit grafico vira sempre bloqueio estavel `gui-submit-disabled`; acoes
  desconhecidas retornam `credential-action-unknown`.
- O plano exige render plan seguro, credenciais limpas/redigidas, submit
  invisivel/desabilitado/bloqueado e login textual autoritativo antes de liberar
  qualquer intencao visual.

## Incremento `0.8.0-alpha.143+20260511`

- `struct login_window_credential_screen_ui_event` registra um evento UI
  redigido a partir do action plan seguro da tela de credenciais.
- `login_window_credential_screen_ui_event_build()` classifica edit/focus,
  abertura de recuperacao textual, resume textual, submit bloqueado, fallback
  textual e acoes bloqueadas sem carregar segredo, mascara, comprimento ou
  snapshots internos.
- O evento exige action plan seguro, credenciais limpas/redigidas, submit
  bloqueado/desabilitado e login textual autoritativo antes de marcar
  `ui_event_safe`.
- Submit grafico continua mapeado para `credential-screen-submit-blocked` e
  `gui-submit-disabled`, com `auth_attempt_allowed` fixo em `0`.

## Incremento `0.8.0-alpha.144+20260511`

- `struct login_window_credential_screen_route_plan` traduz o evento UI seguro
  da tela de credenciais em uma rota de navegacao da futura GUI.
- `login_window_credential_screen_route_plan_build()` seleciona somente rotas
  redigidas: permanecer na tela de credenciais, abrir recuperacao textual,
  retomar login textual ou forcar login textual autoritativo.
- O plano exige evento UI seguro, credenciais limpas/redigidas, submit bloqueado,
  submit desabilitado e login textual autoritativo antes de marcar
  `route_plan_safe`.
- Submit grafico e acoes desconhecidas nunca viram autenticacao: ambos convergem
  para `force-text-login`, preservando razoes como `gui-submit-disabled` ou
  `credential-action-unknown`.

## Incremento `0.8.0-alpha.145+20260511`

- `struct login_window_credential_screen_controller` consome o route plan seguro
  da tela de credenciais e publica somente decisoes finais de UI redigidas.
- `login_window_credential_screen_controller_build()` decide foco de credencial,
  abertura de recuperacao textual, retorno ao login textual ou forca login
  textual autoritativo sem carregar segredo, mascara, comprimento ou snapshots.
- O controller exige route plan seguro, rota selecionada, credenciais limpas e
  redigidas, submit bloqueado/desabilitado e login textual autoritativo antes de
  marcar `controller_safe`.
- Submit grafico, rotas inseguras e eventos ausentes permanecem fail-closed em
  `force-text-login`, com `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.146+20260511`

- `struct login_window_credential_screen_presenter` consome o controller seguro
  da tela de credenciais e publica somente propriedades finais de apresentacao.
- `login_window_credential_screen_presenter_build()` seleciona a visao
  `credential-screen`, `text-recovery`, `text-login` ou `text-login-fallback`
  sem carregar segredo, mascara, comprimento ou snapshots internos.
- O presenter exige controller seguro, rota selecionada, credenciais limpas e
  redigidas, submit bloqueado/desabilitado e login textual autoritativo antes de
  marcar `presenter_safe`.
- Submit grafico, controller ausente e controller inseguro permanecem
  fail-closed em `text-login-fallback`, com `submit_enabled=0` e
  `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.147+20260511`

- `struct login_window_credential_screen_binding` consome o presenter seguro da
  tela de credenciais e publica somente bindings finais de widgets para a futura
  GUI.
- `login_window_credential_screen_binding_build()` seleciona arvores
  `credential-screen-bindings`, `text-recovery-bindings`,
  `text-login-resume-bindings` ou `text-login-fallback-bindings` sem carregar
  segredo, mascara, comprimento ou snapshots internos.
- O binding exige presenter seguro, rota selecionada, credenciais limpas e
  redigidas, submit bloqueado/desabilitado e login textual autoritativo antes de
  marcar `binding_safe`.
- Submit grafico, presenter ausente e presenter inseguro permanecem fail-closed
  em `text-login-fallback-bindings`, com `submit_enabled=0` e
  `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.148+20260511`

- `struct login_window_credential_screen_mount_plan` consome o binding seguro
  da tela de credenciais e publica somente a transacao final de montagem para a
  futura GUI.
- `login_window_credential_screen_mount_plan_build()` seleciona transacoes
  `credential-screen-mount-plan`, `text-recovery-mount-plan`,
  `text-login-resume-mount-plan` ou `text-login-fallback-mount-plan` sem
  carregar segredo, mascara, comprimento ou snapshots internos.
- O mount plan exige binding seguro, rota selecionada, credenciais limpas e
  redigidas, submit bloqueado/desabilitado e login textual autoritativo antes de
  marcar `mount_plan_safe`.
- Submit grafico, binding ausente e binding inseguro permanecem fail-closed em
  `text-login-fallback-mount-plan`, com `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.149+20260511`

- `struct login_window_credential_screen_commit_plan` consome o mount plan
  seguro da tela de credenciais e publica somente uma decisao final declarativa
  para a futura janela GUI.
- `login_window_credential_screen_commit_plan_build()` seleciona transacoes
  `credential-screen-commit-plan`, `text-recovery-commit-plan`,
  `text-login-resume-commit-plan` ou `text-login-fallback-commit-plan` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O commit plan exige mount plan seguro, arvore de widgets selecionada, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `commit_plan_safe`.
- Submit grafico, mount plan ausente e mount plan inseguro permanecem
  fail-closed em `text-login-fallback-commit-plan`, com
  `window_commit_executed=0`, `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.150+20260511`

- `struct login_window_credential_screen_handoff_plan` consome o commit plan
  seguro da tela de credenciais e publica somente um envelope final declarativo
  para a futura GUI/compositor.
- `login_window_credential_screen_handoff_plan_build()` seleciona envelopes
  `credential-screen-handoff-envelope`, `text-recovery-handoff-envelope`,
  `text-login-resume-handoff-envelope` ou
  `text-login-fallback-handoff-envelope` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O handoff plan exige commit plan seguro, commit autorizado mas nao executado,
  envelope selecionado, rota selecionada, credenciais limpas e redigidas,
  callbacks de submit/auth zerados e login textual autoritativo antes de marcar
  `handoff_plan_safe`.
- Submit grafico, commit plan ausente e commit plan inseguro permanecem
  fail-closed em `text-login-fallback-handoff-envelope`, com
  `window_handoff_delivered=0`, `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.151+20260511`

- `struct login_window_credential_screen_dispatch_plan` consome o handoff plan
  seguro da tela de credenciais e publica somente um ticket final declarativo
  para a futura GUI/compositor.
- `login_window_credential_screen_dispatch_plan_build()` seleciona tickets
  `credential-screen-dispatch-ticket`, `text-recovery-dispatch-ticket`,
  `text-login-resume-dispatch-ticket` ou
  `text-login-fallback-dispatch-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O dispatch plan exige handoff plan seguro, handoff autorizado mas nao entregue,
  ticket selecionado, rota selecionada, credenciais limpas e redigidas, callbacks
  de submit/auth zerados e login textual autoritativo antes de marcar
  `dispatch_plan_safe`.
- Submit grafico, handoff plan ausente e handoff plan inseguro permanecem
  fail-closed em `text-login-fallback-dispatch-ticket`, com
  `window_dispatch_delivered=0`, `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.152+20260511`

- `struct login_window_credential_screen_queue_plan` consome o dispatch plan
  seguro da tela de credenciais e publica somente um ticket final declarativo
  para uma futura fila GUI/compositor.
- `login_window_credential_screen_queue_plan_build()` seleciona tickets
  `credential-screen-queue-ticket`, `text-recovery-queue-ticket`,
  `text-login-resume-queue-ticket` ou `text-login-fallback-queue-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O queue plan exige dispatch plan seguro, dispatch autorizado mas nao entregue,
  ticket selecionado, rota selecionada, credenciais limpas e redigidas,
  callbacks de submit/auth zerados e login textual autoritativo antes de marcar
  `queue_plan_safe`.
- Submit grafico, dispatch plan ausente e dispatch plan inseguro permanecem
  fail-closed em `text-login-fallback-queue-ticket`, com
  `window_queue_enqueued=0`, `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.153+20260511`

- `struct login_window_credential_screen_activation_plan` consome o queue plan
  seguro da tela de credenciais e publica somente um ticket final declarativo
  para uma futura ativacao GUI/compositor.
- `login_window_credential_screen_activation_plan_build()` seleciona tickets
  `credential-screen-activation-ticket`, `text-recovery-activation-ticket`,
  `text-login-resume-activation-ticket` ou
  `text-login-fallback-activation-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O activation plan exige queue plan seguro, queue autorizado mas nao enfileirado,
  ticket selecionado, rota selecionada, credenciais limpas e redigidas, callbacks
  de submit/auth zerados e login textual autoritativo antes de marcar
  `activation_plan_safe`.
- Submit grafico, queue plan ausente e queue plan inseguro permanecem fail-closed
  em `text-login-fallback-activation-ticket`, com
  `window_activation_applied=0`, `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.

## Incremento `0.8.0-alpha.154+20260512`

- `struct login_window_credential_screen_frame_plan` consome o activation plan
  seguro da tela de credenciais e publica somente um ticket final declarativo de
  moldura visual para uma futura composicao GUI/compositor.
- `login_window_credential_screen_frame_plan_build()` seleciona tickets
  `credential-screen-frame-ticket`, `text-recovery-frame-ticket`,
  `text-login-resume-frame-ticket` ou `text-login-fallback-frame-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O frame plan exige activation plan seguro, ativacao autorizada mas nao aplicada,
  ticket selecionado, rota selecionada, credenciais limpas e redigidas, callbacks
  de submit/auth zerados e login textual autoritativo antes de marcar
  `frame_plan_safe`.
- Submit grafico, activation plan ausente e activation plan inseguro permanecem
  fail-closed em `text-login-fallback-frame-ticket`, com
  `window_frame_rendered=0`, `submit_callback_bound=0`,
  `auth_callback_bound=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.


## Incremento `0.8.0-alpha.155+20260512`

- `struct login_window_credential_screen_surface_plan` consome o frame plan
  seguro da tela de credenciais e publica somente um ticket final declarativo de
  superficie para uma futura composicao GUI/compositor.
- `login_window_credential_screen_surface_plan_build()` seleciona tickets
  `credential-screen-surface-ticket`, `text-recovery-surface-ticket`,
  `text-login-resume-surface-ticket` ou `text-login-fallback-surface-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O surface plan exige frame plan seguro, frame autorizado mas nao renderizado,
  ticket selecionado, rota selecionada, credenciais limpas e redigidas, callbacks
  de submit/auth zerados e login textual autoritativo antes de marcar
  `surface_plan_safe`.
- Submit grafico, frame plan ausente e frame plan inseguro permanecem
  fail-closed em `text-login-fallback-surface-ticket`, com
  `window_surface_submitted=0`, `compositor_damage_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `surface_reuse_allowed`, `surface_cache_allowed` e
  `compositor_damage_planned` preparam cache/damage incremental futuro sem
  executar compositor real neste patch.


## Incremento `0.8.0-alpha.156+20260512`

- `struct login_window_credential_screen_compositor_plan` consome o surface plan
  seguro da tela de credenciais e publica somente um ticket final declarativo de
  integracao GUI/compositor.
- `login_window_credential_screen_compositor_plan_build()` seleciona tickets
  `credential-screen-compositor-ticket`, `text-recovery-compositor-ticket`,
  `text-login-resume-compositor-ticket` ou `text-login-fallback-compositor-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O compositor plan exige surface plan seguro, superficie autorizada mas nao
  submetida, damage planejado mas nao submetido, ticket selecionado, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados e
  login textual autoritativo antes de marcar `compositor_plan_safe`.
- Submit grafico, surface plan ausente e surface plan inseguro permanecem
  fail-closed em `text-login-fallback-compositor-ticket`, com
  `compositor_surface_submitted=0`, `compositor_damage_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `compositor_reuse_allowed`, `compositor_cache_allowed`,
  `compositor_damage_allowed` e `compositor_cache_hit=0` preservam planejamento
  de cache/damage incremental futuro sem executar compositor real neste patch.


## Incremento `0.8.0-alpha.157+20260512`

- `struct login_window_credential_screen_damage_plan` consome o compositor plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  damage/cache para futura integracao GUI/compositor.
- `login_window_credential_screen_damage_plan_build()` seleciona tickets
  `credential-screen-damage-ticket`, `text-recovery-damage-ticket`,
  `text-login-resume-damage-ticket` ou `text-login-fallback-damage-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O damage plan exige compositor plan seguro, superficie autorizada mas nao
  submetida, damage planejado/autorizado mas nao submetido, ticket selecionado,
  rota selecionada, credenciais limpas e redigidas, callbacks de submit/auth
  zerados e login textual autoritativo antes de marcar `damage_plan_safe`.
- Submit grafico, compositor plan ausente e compositor plan inseguro permanecem
  fail-closed em `text-login-fallback-damage-ticket`, com `damage_submitted=0`,
  `compositor_damage_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `damage_incremental_allowed`, `damage_cache_allowed`, `damage_reuse_allowed`,
  `damage_cache_hit=0` e `full_damage_required` preservam planejamento de
  cache/damage incremental futuro sem executar compositor real neste patch.


## Incremento `0.8.0-alpha.158+20260512`

- `struct login_window_credential_screen_present_plan` consome o damage plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  apresentacao para futura integracao GUI/compositor.
- `login_window_credential_screen_present_plan_build()` seleciona tickets
  `credential-screen-present-ticket`, `text-recovery-present-ticket`,
  `text-login-resume-present-ticket` ou `text-login-fallback-present-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O present plan exige damage plan seguro, superficie autorizada mas nao
  submetida, damage planejado/autorizado mas nao submetido, damage ticket
  selecionado, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `present_plan_safe`.
- Submit grafico, damage plan ausente e damage plan inseguro permanecem
  fail-closed em `text-login-fallback-present-ticket`, com
  `present_submitted=0`, `damage_submitted=0`,
  `compositor_damage_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `present_incremental_allowed`, `present_cache_allowed`,
  `present_reuse_allowed`, `present_cache_hit=0` e `full_present_required`
  preservam planejamento de apresentacao incremental futura sem executar
  compositor real neste patch.


## Incremento `0.8.0-alpha.159+20260512`

- `struct login_window_credential_screen_schedule_plan` consome o present plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  agendamento para futura integracao GUI/compositor.
- `login_window_credential_screen_schedule_plan_build()` seleciona tickets
  `credential-screen-schedule-ticket`, `text-recovery-schedule-ticket`,
  `text-login-resume-schedule-ticket` ou `text-login-fallback-schedule-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O schedule plan exige present plan seguro, apresentacao autorizada mas nao
  submetida, damage/compositor nao submetidos, present ticket selecionado, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `schedule_plan_safe`.
- Submit grafico, present plan ausente e present plan inseguro permanecem
  fail-closed em `text-login-fallback-schedule-ticket`, com
  `schedule_submitted=0`, `present_submitted=0`, `damage_submitted=0`,
  `frame_timer_armed=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `schedule_incremental_allowed`, `schedule_cache_allowed`,
  `schedule_reuse_allowed`, `schedule_cache_hit=0` e `full_schedule_required`
  preservam planejamento de agendamento incremental futuro sem armar timer,
  acordar compositor ou executar page flip neste patch.


## Incremento `0.8.0-alpha.160+20260512`

- `struct login_window_credential_screen_vsync_plan` consome o schedule plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  sincronizacao para futura integracao GUI/compositor.
- `login_window_credential_screen_vsync_plan_build()` seleciona tickets
  `credential-screen-vsync-ticket`, `text-recovery-vsync-ticket`,
  `text-login-resume-vsync-ticket` ou `text-login-fallback-vsync-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O vsync plan exige schedule plan seguro, agendamento autorizado mas nao
  submetido, present/damage/compositor nao submetidos, ticket de schedule
  selecionado, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `vsync_plan_safe`.
- Submit grafico, schedule plan ausente e schedule plan inseguro permanecem
  fail-closed em `text-login-fallback-vsync-ticket`, com `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`, `schedule_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `frame_timer_armed=0`,
  `page_flip_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `vsync_allowed`, `vsync_ticket_selected`, `vsync_wait_allowed=0` e
  `vsync_fence_armed=0` preservam planejamento de sincronizacao futura sem
  aguardar vsync real, armar fence/timer ou executar page flip neste patch.


## Incremento `0.8.0-alpha.161+20260512`

- `struct login_window_credential_screen_scanout_plan` consome o vsync plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  scanout para futura integracao GUI/compositor/display.
- `login_window_credential_screen_scanout_plan_build()` seleciona tickets
  `credential-screen-scanout-ticket`, `text-recovery-scanout-ticket`,
  `text-login-resume-scanout-ticket` ou `text-login-fallback-scanout-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O scanout plan exige vsync plan seguro, sincronizacao autorizada mas nao
  submetida, schedule/present/damage/compositor nao submetidos, ticket de vsync
  selecionado, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `scanout_plan_safe`.
- Submit grafico, vsync plan ausente e vsync plan inseguro permanecem
  fail-closed em `text-login-fallback-scanout-ticket`, com
  `scanout_submitted=0`, `scanout_buffer_attached=0`,
  `scanout_buffer_submitted=0`, `display_mode_committed=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `scanout_allowed`, `scanout_ticket_selected` e `scanout_target_selected`
  preservam planejamento de display futuro sem anexar buffer real, commitar modo
  de display, acordar compositor ou executar page flip neste patch.


## Incremento `0.8.0-alpha.162+20260512`

- `struct login_window_credential_screen_display_plan` consome o scanout plan
  seguro da tela de credenciais e publica somente um ticket declarativo final de
  display para futura integracao GUI/compositor/display.
- `login_window_credential_screen_display_plan_build()` seleciona tickets
  `credential-screen-display-ticket`, `text-recovery-display-ticket`,
  `text-login-resume-display-ticket` ou `text-login-fallback-display-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O display plan exige scanout plan seguro, scanout autorizado mas nao submetido,
  nenhum buffer anexado/submetido, modo de display nao commitado,
  vsync/schedule/present/damage/compositor nao submetidos, rota selecionada,
  credenciais limpas e redigidas, callbacks de submit/auth zerados e login
  textual autoritativo antes de marcar `display_plan_safe`.
- Submit grafico, scanout plan ausente e scanout plan inseguro permanecem
  fail-closed em `text-login-fallback-display-ticket`, com
  `display_submitted=0`, `display_buffer_attached=0`,
  `display_buffer_submitted=0`, `display_mode_committed=0`,
  `display_flip_submitted=0`, `scanout_submitted=0`, `vsync_submitted=0`,
  `schedule_submitted=0`, `present_submitted=0`, `damage_submitted=0`,
  `page_flip_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `display_allowed`, `display_ticket_selected` e `display_target_selected`
  preservam planejamento de display futuro sem anexar buffer real, submeter
  display, commitar modo, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.163+20260512`

- `struct login_window_credential_screen_output_plan` consome o display plan
  seguro da tela de credenciais e publica somente um ticket declarativo de saida
  visual para futura integracao GUI/compositor/display.
- `login_window_credential_screen_output_plan_build()` seleciona tickets
  `credential-screen-output-ticket`, `text-recovery-output-ticket`,
  `text-login-resume-output-ticket` ou `text-login-fallback-output-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O output plan exige display plan seguro, display autorizado mas nao submetido,
  nenhum buffer de display/output anexado ou submetido, modo de display nao
  commitado, flips desabilitados, scanout/vsync/schedule/present/damage/compositor
  nao submetidos, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `output_plan_safe`.
- Submit grafico, display plan ausente e display plan inseguro permanecem
  fail-closed em `text-login-fallback-output-ticket`, com
  `output_submitted=0`, `output_buffer_attached=0`,
  `output_buffer_submitted=0`, `output_flip_submitted=0`,
  `display_submitted=0`, `display_buffer_attached=0`,
  `display_mode_committed=0`, `display_flip_submitted=0`,
  `scanout_submitted=0`, `vsync_submitted=0`, `schedule_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `page_flip_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `output_allowed`, `output_ticket_selected` e `output_target_selected`
  preservam planejamento de saida visual futura sem anexar buffer real, submeter
  output/display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.164+20260512`

- `struct login_window_credential_screen_blit_plan` consome o output plan seguro
  da tela de credenciais e publica somente um ticket declarativo de copia visual
  para futura integracao GUI/compositor/display.
- `login_window_credential_screen_blit_plan_build()` seleciona tickets
  `credential-screen-blit-ticket`, `text-recovery-blit-ticket`,
  `text-login-resume-blit-ticket` ou `text-login-fallback-blit-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O blit plan exige output plan seguro, output autorizado mas nao submetido,
  nenhum buffer de output/display/scanout anexado ou submetido, nenhum buffer de
  blit mapeado, nenhum pixel copiado, DMA desabilitado, flips desabilitados,
  modo de display nao commitado, scanout/vsync/schedule/present/damage/compositor
  nao submetidos, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `blit_plan_safe`.
- Submit grafico, output plan ausente e output plan inseguro permanecem
  fail-closed em `text-login-fallback-blit-ticket`, com `blit_submitted=0`,
  `blit_source_buffer_mapped=0`, `blit_destination_buffer_mapped=0`,
  `blit_pixels_copied=0`, `blit_dma_allowed=0`, `blit_dma_submitted=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `blit_allowed`, `blit_ticket_selected` e `blit_target_selected` preservam
  planejamento de copia visual futura sem mapear framebuffer, copiar pixels,
  submeter DMA/output/display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.165+20260512`

- `struct login_window_credential_screen_framebuffer_plan` consome o blit plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  framebuffer para futura integracao GUI/compositor/display.
- `login_window_credential_screen_framebuffer_plan_build()` seleciona tickets
  `credential-screen-framebuffer-ticket`, `text-recovery-framebuffer-ticket`,
  `text-login-resume-framebuffer-ticket` ou
  `text-login-fallback-framebuffer-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O framebuffer plan exige blit plan seguro, blit autorizado mas nao submetido,
  nenhum buffer de blit/output/display/scanout mapeado, anexado ou submetido,
  nenhum pixel copiado, DMA desabilitado, framebuffer nao mapeado, escrita de
  framebuffer desabilitada, nenhum flush/cache executado, flips desabilitados,
  modo de display nao commitado, scanout/vsync/schedule/present/damage/compositor
  nao submetidos, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `framebuffer_plan_safe`.
- Submit grafico, blit plan ausente e blit plan inseguro permanecem fail-closed
  em `text-login-fallback-framebuffer-ticket`, com `framebuffer_submitted=0`,
  `framebuffer_mapped=0`, `framebuffer_write_allowed=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`,
  `framebuffer_cache_cleaned=0`, `blit_submitted=0`,
  `blit_source_buffer_mapped=0`, `blit_destination_buffer_mapped=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `scanout_submitted=0`, `vsync_submitted=0`, `schedule_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `page_flip_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `framebuffer_allowed`, `framebuffer_ticket_selected` e
  `framebuffer_target_selected` preservam planejamento de framebuffer futuro sem
  mapear memoria real, escrever pixels, fazer flush/cache, submeter
  output/display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.166+20260512`

- `struct login_window_credential_screen_flush_plan` consome o framebuffer plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  flush/cache para futura integracao GUI/compositor/display.
- `login_window_credential_screen_flush_plan_build()` seleciona tickets
  `credential-screen-flush-ticket`, `text-recovery-flush-ticket`,
  `text-login-resume-flush-ticket` ou `text-login-fallback-flush-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O flush plan exige framebuffer plan seguro, framebuffer autorizado mas nao
  submetido, framebuffer nao mapeado, escrita desabilitada, nenhum pixel escrito,
  flush e cache clean ainda nao executados, nenhum buffer de blit/output/display/
  scanout mapeado, anexado ou submetido, nenhum pixel copiado, DMA desabilitado,
  flush/cache real e barreira de memoria desabilitados, flips desabilitados,
  modo de display nao commitado, scanout/vsync/schedule/present/damage/compositor
  nao submetidos, rota selecionada, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `flush_plan_safe`.
- Submit grafico, framebuffer plan ausente e framebuffer plan inseguro permanecem
  fail-closed em `text-login-fallback-flush-ticket`, com `flush_submitted=0`,
  `flush_cache_clean_allowed=0`, `flush_cache_cleaned=0`,
  `flush_memory_barrier_allowed=0`, `flush_memory_barrier_submitted=0`,
  `framebuffer_submitted=0`, `framebuffer_mapped=0`,
  `framebuffer_write_allowed=0`, `framebuffer_written=0`,
  `framebuffer_flushed=0`, `framebuffer_cache_cleaned=0`, `blit_submitted=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `scanout_submitted=0`, `vsync_submitted=0`, `schedule_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `page_flip_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `flush_allowed`, `flush_ticket_selected` e `flush_target_selected` preservam
  planejamento de flush/cache futuro sem executar flush/cache real, barreira de
  memoria, output/display, compositor ou flip neste patch.


## Incremento `0.8.0-alpha.167+20260512`

- `struct login_window_credential_screen_barrier_plan` consome o flush plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  barreira/visibilidade para futura integracao GUI/compositor/display.
- `login_window_credential_screen_barrier_plan_build()` seleciona tickets
  `credential-screen-barrier-ticket`, `text-recovery-barrier-ticket`,
  `text-login-resume-barrier-ticket` ou `text-login-fallback-barrier-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O barrier plan exige flush plan seguro, flush autorizado mas nao submetido,
  cache clean e memory barrier desabilitados/nao executados, framebuffer nao
  mapeado, escrita e flush de framebuffer nao executados, nenhum buffer de
  blit/output/display/scanout mapeado, anexado ou submetido, nenhum pixel copiado,
  DMA desabilitado, output/display/scanout/vsync/schedule/present/damage/
  compositor nao submetidos, timer/fence/wake/flip desabilitados, rota selecionada,
  credenciais limpas e redigidas, callbacks de submit/auth zerados e login textual
  autoritativo antes de marcar `barrier_plan_safe`.
- Submit grafico, flush plan ausente e flush plan inseguro permanecem fail-closed
  em `text-login-fallback-barrier-ticket`, com `barrier_submitted=0`,
  `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `barrier_cpu_gpu_sync_allowed=0`,
  `barrier_cpu_gpu_sync_submitted=0`, `flush_submitted=0`,
  `flush_cache_clean_allowed=0`, `flush_cache_cleaned=0`,
  `flush_memory_barrier_allowed=0`, `flush_memory_barrier_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `barrier_allowed`, `barrier_ticket_selected` e `barrier_target_selected`
  preservam planejamento de barreira/visibilidade futura sem executar barreira
  real, flush/cache, CPU/GPU sync, output/display, compositor ou flip neste patch.


## Incremento `0.8.0-alpha.168+20260512`

- `struct login_window_credential_screen_fence_plan` consome o barrier plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  fence/sync para futura integracao GUI/compositor/display.
- `login_window_credential_screen_fence_plan_build()` seleciona tickets
  `credential-screen-fence-ticket`, `text-recovery-fence-ticket`,
  `text-login-resume-fence-ticket` ou `text-login-fallback-fence-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O fence plan exige barrier plan seguro, barreira autorizada mas nao submetida,
  visibilidade de memoria/cache ainda nao estabelecida, CPU/GPU sync desabilitado,
  flush/cache/barreira real nao executados, framebuffer nao mapeado, escrita e
  flush de framebuffer nao executados, nenhum buffer de blit/output/display/
  scanout mapeado, anexado ou submetido, nenhum pixel copiado, DMA desabilitado,
  output/display/scanout/vsync/schedule/present/damage/compositor nao submetidos,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `fence_plan_safe`.
- Submit grafico, barrier plan ausente e barrier plan inseguro permanecem
  fail-closed em `text-login-fallback-fence-ticket`, com `fence_submitted=0`,
  `fence_wait_allowed=0`, `fence_wait_submitted=0`, `fence_signal_allowed=0`,
  `fence_signal_submitted=0`, `fence_fd_export_allowed=0`, `fence_fd_exported=0`,
  `fence_cpu_gpu_sync_allowed=0`, `fence_cpu_gpu_sync_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `barrier_cpu_gpu_sync_submitted=0`,
  `flush_submitted=0`, `framebuffer_written=0`, `framebuffer_flushed=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `scanout_submitted=0`, `vsync_submitted=0`, `schedule_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `page_flip_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `fence_allowed`, `fence_ticket_selected` e `fence_target_selected` preservam
  planejamento de fence/sync futuro sem armar fence real, aguardar/sinalizar
  fence, exportar fd, sincronizar CPU/GPU, submeter output/display, acordar
  compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.169+20260512`

- `struct login_window_credential_screen_timeline_plan` consome o fence plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  timeline/semaphore para futura integracao GUI/compositor/display.
- `login_window_credential_screen_timeline_plan_build()` seleciona tickets
  `credential-screen-timeline-ticket`, `text-recovery-timeline-ticket`,
  `text-login-resume-timeline-ticket` ou `text-login-fallback-timeline-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O timeline plan exige fence plan seguro, fence autorizado mas nao submetido,
  wait/signal/fd export/CPU-GPU sync desabilitados, timeline ainda nao submetida,
  wait/signal/semaphore de timeline desabilitados, valor de timeline exigido mas
  nao alocado nem publicado, flush/cache/barreira real nao executados,
  framebuffer nao mapeado, escrita e flush de framebuffer nao executados, nenhum
  buffer de blit/output/display/scanout mapeado, anexado ou submetido, nenhum
  pixel copiado, DMA desabilitado, output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados e
  login textual autoritativo antes de marcar `timeline_plan_safe`.
- Submit grafico, fence plan ausente e fence plan inseguro permanecem fail-closed
  em `text-login-fallback-timeline-ticket`, com `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_allowed=0`, `timeline_cpu_gpu_sync_submitted=0`,
  `fence_submitted=0`, `fence_wait_allowed=0`, `fence_signal_allowed=0`,
  `fence_fd_exported=0`, `fence_cpu_gpu_sync_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `timeline_allowed`, `timeline_ticket_selected` e `timeline_target_selected`
  preservam planejamento de timeline/semaphore futuro sem submeter timeline real,
  aguardar/sinalizar timeline, alocar/publicar valor, submeter semaphore,
  sincronizar CPU/GPU, submeter output/display, acordar compositor ou executar
  flip neste patch.


## Incremento `0.8.0-alpha.170+20260512`

- `struct login_window_credential_screen_sync_plan` consome o timeline plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  sincronizacao para futura integracao GUI/compositor/display.
- `login_window_credential_screen_sync_plan_build()` seleciona tickets
  `credential-screen-sync-ticket`, `text-recovery-sync-ticket`,
  `text-login-resume-sync-ticket` ou `text-login-fallback-sync-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O sync plan exige timeline plan seguro, timeline autorizada mas nao submetida,
  wait/signal/semaphore de timeline desabilitados, valor de timeline exigido mas
  nao alocado nem publicado, CPU/GPU sync desabilitado, sync ainda nao submetido,
  wait/signal de sync desabilitados, deadline exigido mas nao armado, completion
  exigido mas nao reportado, flush/cache/barreira real nao executados,
  framebuffer nao mapeado, escrita e flush de framebuffer nao executados, nenhum
  buffer de blit/output/display/scanout mapeado, anexado ou submetido, nenhum
  pixel copiado, DMA desabilitado, output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados e
  login textual autoritativo antes de marcar `sync_plan_safe`.
- Submit grafico, timeline plan ausente e timeline plan inseguro permanecem
  fail-closed em `text-login-fallback-sync-ticket`, com `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `sync_allowed`, `sync_ticket_selected` e `sync_target_selected` preservam
  planejamento de sincronizacao futuro sem submeter sync real, aguardar/sinalizar,
  armar deadline, reportar completion, sincronizar CPU/GPU, submeter output/
  display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.171+20260512`

- `struct login_window_credential_screen_deadline_plan` consome o sync plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  deadline para futura integracao GUI/compositor/display.
- `login_window_credential_screen_deadline_plan_build()` seleciona tickets
  `credential-screen-deadline-ticket`, `text-recovery-deadline-ticket`,
  `text-login-resume-deadline-ticket` ou `text-login-fallback-deadline-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O deadline plan exige sync plan seguro, sync autorizado mas nao submetido,
  wait/signal de sync desabilitados, deadline de sync exigido mas nao armado,
  completion de sync exigido mas nao reportado, CPU/GPU sync desabilitado,
  deadline ainda nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline exigido mas nao reportado, flush/cache/
  barreira real nao executados, framebuffer nao mapeado, escrita e flush de
  framebuffer nao executados, nenhum buffer de blit/output/display/scanout
  mapeado, anexado ou submetido, nenhum pixel copiado, DMA desabilitado, output/
  display/scanout/vsync/schedule/present/damage/compositor nao submetidos,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `deadline_plan_safe`.
- Submit grafico, sync plan ausente e sync plan inseguro permanecem fail-closed
  em `text-login-fallback-deadline-ticket`, com `deadline_armed=0`,
  `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `deadline_allowed`, `deadline_ticket_selected` e `deadline_target_selected`
  preservam planejamento futuro de deadlines sem armar deadline real, temporizador,
  expirar, reportar completion, sincronizar CPU/GPU, submeter output/display,
  acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.172+20260512`

- `struct login_window_credential_screen_completion_plan` consome o deadline
  plan seguro da tela de credenciais e publica somente um ticket declarativo de
  completion para futura integracao GUI/compositor/display.
- `login_window_credential_screen_completion_plan_build()` seleciona tickets
  `credential-screen-completion-ticket`, `text-recovery-completion-ticket`,
  `text-login-resume-completion-ticket` ou
  `text-login-fallback-completion-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O completion plan exige deadline plan seguro, deadline permitido mas nao
  armado, timer de deadline exigido mas nao armado, deadline nao expirado,
  completion de deadline exigido mas nao reportado, CPU/GPU sync de deadline
  desabilitado, sync autorizado mas nao submetido, wait/signal/deadline/
  completion de sync nao executados, timeline nao submetida, valor de timeline
  nao alocado nem publicado, flush/cache/barreira real nao executados,
  framebuffer nao mapeado, escrita e flush de framebuffer nao executados, nenhum
  buffer de blit/output/display/scanout mapeado, anexado ou submetido, nenhum
  pixel copiado, DMA desabilitado, output/display/scanout/vsync/schedule/
  present/damage/compositor nao submetidos, timer/fence/wake/flip desabilitados,
  rota selecionada, credenciais limpas e redigidas, callbacks de submit/auth
  zerados e login textual autoritativo antes de marcar `completion_plan_safe`.
- Submit grafico, deadline plan ausente e deadline plan inseguro permanecem
  fail-closed em `text-login-fallback-completion-ticket`, com
  `completion_reported=0`, `completion_acknowledged=0`,
  `completion_cpu_gpu_sync_allowed=0`, `completion_cpu_gpu_sync_submitted=0`,
  `deadline_armed=0`, `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `completion_allowed`, `completion_ticket_selected` e
  `completion_target_selected` preservam planejamento futuro de completion sem
  reportar completion real, acknowledge, sincronizar CPU/GPU, armar deadline,
  submeter output/display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.173+20260512`

- `struct login_window_credential_screen_ack_plan` consome o completion plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  acknowledge para futura integracao GUI/compositor/display.
- `login_window_credential_screen_ack_plan_build()` seleciona tickets
  `credential-screen-ack-ticket`, `text-recovery-ack-ticket`,
  `text-login-resume-ack-ticket` ou `text-login-fallback-ack-ticket` sem carregar
  segredo, mascara, comprimento, snapshots internos ou ponteiros de callback.
- O ack plan exige completion plan seguro, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, completion
  ticket/target selecionados, CPU/GPU sync de completion desabilitado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `ack_plan_safe`.
- Submit grafico, completion plan ausente e completion plan inseguro permanecem
  fail-closed em `text-login-fallback-ack-ticket`, com `ack_submitted=0`,
  `ack_cpu_gpu_sync_allowed=0`, `ack_cpu_gpu_sync_submitted=0`,
  `completion_reported=0`, `completion_acknowledged=0`,
  `completion_cpu_gpu_sync_allowed=0`, `completion_cpu_gpu_sync_submitted=0`,
  `deadline_armed=0`, `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `ack_allowed`, `ack_ticket_selected` e `ack_target_selected` preservam
  planejamento futuro de acknowledge sem submeter ack real, reportar completion,
  acknowledge, sincronizar CPU/GPU, armar deadline, submeter output/display,
  acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.174+20260512`

- `struct login_window_credential_screen_retire_plan` consome o ack plan seguro
  da tela de credenciais e publica somente um ticket declarativo de retire para
  futura integracao GUI/compositor/display.
- `login_window_credential_screen_retire_plan_build()` seleciona tickets
  `credential-screen-retire-ticket`, `text-recovery-retire-ticket`,
  `text-login-resume-retire-ticket` ou `text-login-fallback-retire-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O retire plan exige ack plan seguro, ack exigido e permitido mas nao
  submetido, ack ticket/target selecionados, CPU/GPU sync de ack desabilitado,
  completion permitido mas nao reportado, acknowledge de completion exigido mas
  nao executado, deadline permitido mas nao armado, timer de deadline exigido
  mas nao armado, deadline nao expirado, completion de deadline nao reportado,
  sync/timeline/fence/barrier/flush/framebuffer/blit/output/display/scanout/
  vsync/schedule/present/damage/compositor nao submetidos, nenhum valor de
  timeline publicado, nenhum framebuffer mapeado ou escrito, nenhum pixel
  copiado, DMA desabilitado, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `retire_plan_safe`.
- Submit grafico, ack plan ausente e ack plan inseguro permanecem fail-closed em
  `text-login-fallback-retire-ticket`, com `retire_submitted=0`,
  `retire_resource_release_allowed=0`, `retire_resource_released=0`,
  `retire_cpu_gpu_sync_allowed=0`, `retire_cpu_gpu_sync_submitted=0`,
  `ack_submitted=0`, `ack_cpu_gpu_sync_allowed=0`,
  `ack_cpu_gpu_sync_submitted=0`, `completion_reported=0`,
  `completion_acknowledged=0`, `completion_cpu_gpu_sync_allowed=0`,
  `completion_cpu_gpu_sync_submitted=0`, `deadline_armed=0`,
  `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `retire_allowed`, `retire_ticket_selected` e `retire_target_selected`
  preservam planejamento futuro de retire sem liberar recursos reais, submeter
  retire, submeter ack, reportar completion, acknowledge, sincronizar CPU/GPU,
  armar deadline, submeter output/display, acordar compositor ou executar flip
  neste patch.


## Incremento `0.8.0-alpha.175+20260512`

- `struct login_window_credential_screen_cleanup_plan` consome o retire plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  cleanup para futura integracao GUI/compositor/display.
- `login_window_credential_screen_cleanup_plan_build()` seleciona tickets
  `credential-screen-cleanup-ticket`, `text-recovery-cleanup-ticket`,
  `text-login-resume-cleanup-ticket` ou `text-login-fallback-cleanup-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O cleanup plan exige retire plan seguro, retire exigido e permitido mas nao
  submetido, retire ticket/target selecionados, sem release de recursos de
  retire, CPU/GPU sync de retire desabilitado, ack permitido mas nao submetido,
  completion permitido mas nao reportado, acknowledge de completion exigido mas
  nao executado, deadline permitido mas nao armado, timer de deadline exigido
  mas nao armado, deadline nao expirado, completion de deadline nao reportado,
  sync/timeline/fence/barrier/flush/framebuffer/blit/output/display/scanout/
  vsync/schedule/present/damage/compositor nao submetidos, nenhum valor de
  timeline publicado, nenhum framebuffer mapeado ou escrito, nenhum pixel
  copiado, DMA desabilitado, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `cleanup_plan_safe`.
- Submit grafico, retire plan ausente e retire plan inseguro permanecem
  fail-closed em `text-login-fallback-cleanup-ticket`, com
  `cleanup_submitted=0`, `cleanup_resource_release_allowed=0`,
  `cleanup_resource_released=0`, `cleanup_cpu_gpu_sync_allowed=0`,
  `cleanup_cpu_gpu_sync_submitted=0`, `retire_submitted=0`,
  `retire_resource_release_allowed=0`, `retire_resource_released=0`,
  `retire_cpu_gpu_sync_allowed=0`, `retire_cpu_gpu_sync_submitted=0`,
  `ack_submitted=0`, `ack_cpu_gpu_sync_allowed=0`,
  `ack_cpu_gpu_sync_submitted=0`, `completion_reported=0`,
  `completion_acknowledged=0`, `completion_cpu_gpu_sync_allowed=0`,
  `completion_cpu_gpu_sync_submitted=0`, `deadline_armed=0`,
  `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `cleanup_allowed`, `cleanup_ticket_selected` e `cleanup_target_selected`
  preservam planejamento futuro de cleanup sem limpar/liberar recursos reais,
  submeter cleanup, submeter retire, submeter ack, reportar completion,
  acknowledge, sincronizar CPU/GPU, armar deadline, submeter output/display,
  acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.176+20260512`

- `struct login_window_credential_screen_seal_plan` consome o cleanup plan
  seguro da tela de credenciais e publica somente um ticket declarativo de seal
  para futura integracao GUI/compositor/display.
- `login_window_credential_screen_seal_plan_build()` seleciona tickets
  `credential-screen-seal-ticket`, `text-recovery-seal-ticket`,
  `text-login-resume-seal-ticket` ou `text-login-fallback-seal-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O seal plan exige cleanup plan seguro, cleanup exigido e permitido mas nao
  submetido, cleanup ticket/target selecionados, sem release de recursos de
  cleanup, CPU/GPU sync de cleanup desabilitado, retire permitido mas nao
  submetido, retire ticket/target selecionados, sem release de recursos de
  retire, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `seal_plan_safe`.
- Submit grafico, cleanup plan ausente e cleanup plan inseguro permanecem
  fail-closed em `text-login-fallback-seal-ticket`, com `seal_submitted=0`,
  `seal_state_write_allowed=0`, `seal_state_written=0`,
  `seal_cpu_gpu_sync_allowed=0`, `seal_cpu_gpu_sync_submitted=0`,
  `cleanup_submitted=0`, `cleanup_resource_release_allowed=0`,
  `cleanup_resource_released=0`, `cleanup_cpu_gpu_sync_allowed=0`,
  `cleanup_cpu_gpu_sync_submitted=0`, `retire_submitted=0`,
  `retire_resource_release_allowed=0`, `retire_resource_released=0`,
  `retire_cpu_gpu_sync_allowed=0`, `retire_cpu_gpu_sync_submitted=0`,
  `ack_submitted=0`, `ack_cpu_gpu_sync_allowed=0`,
  `ack_cpu_gpu_sync_submitted=0`, `completion_reported=0`,
  `completion_acknowledged=0`, `completion_cpu_gpu_sync_allowed=0`,
  `completion_cpu_gpu_sync_submitted=0`, `deadline_armed=0`,
  `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `seal_allowed`, `seal_ticket_selected` e `seal_target_selected` preservam
  planejamento futuro de seal sem escrever estado real, limpar/liberar recursos,
  submeter seal, submeter cleanup, submeter retire, submeter ack, reportar
  completion, acknowledge, sincronizar CPU/GPU, armar deadline, submeter
  output/display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.177+20260512`

- `struct login_window_credential_screen_audit_plan` consome o seal plan seguro
  da tela de credenciais e publica somente um ticket declarativo de auditoria
  para futura integracao GUI/compositor/display.
- `login_window_credential_screen_audit_plan_build()` seleciona tickets
  `credential-screen-audit-ticket`, `text-recovery-audit-ticket`,
  `text-login-resume-audit-ticket` ou `text-login-fallback-audit-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O audit plan exige seal plan seguro, seal exigido e permitido mas nao
  submetido, seal ticket/target selecionados, escrita de estado de seal
  desabilitada, CPU/GPU sync de seal desabilitado, cleanup permitido mas nao
  submetido, cleanup ticket/target selecionados, sem release de recursos de
  cleanup, retire permitido mas nao submetido, ack permitido mas nao submetido,
  completion permitido mas nao reportado, acknowledge de completion exigido mas
  nao executado, deadline permitido mas nao armado, timer de deadline exigido
  mas nao armado, deadline nao expirado, completion de deadline nao reportado,
  sync/timeline/fence/barrier/flush/framebuffer/blit/output/display/scanout/
  vsync/schedule/present/damage/compositor nao submetidos, nenhum valor de
  timeline publicado, nenhum framebuffer mapeado ou escrito, nenhum pixel
  copiado, DMA desabilitado, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `audit_plan_safe`.
- Submit grafico, seal plan ausente e seal plan inseguro permanecem fail-closed
  em `text-login-fallback-audit-ticket`, com `audit_submitted=0`,
  `audit_log_append_allowed=0`, `audit_log_appended=0`,
  `audit_cpu_gpu_sync_allowed=0`, `audit_cpu_gpu_sync_submitted=0`,
  `seal_submitted=0`, `seal_state_write_allowed=0`, `seal_state_written=0`,
  `seal_cpu_gpu_sync_allowed=0`, `seal_cpu_gpu_sync_submitted=0`,
  `cleanup_submitted=0`, `cleanup_resource_release_allowed=0`,
  `cleanup_resource_released=0`, `cleanup_cpu_gpu_sync_allowed=0`,
  `cleanup_cpu_gpu_sync_submitted=0`, `retire_submitted=0`,
  `retire_resource_release_allowed=0`, `retire_resource_released=0`,
  `retire_cpu_gpu_sync_allowed=0`, `retire_cpu_gpu_sync_submitted=0`,
  `ack_submitted=0`, `ack_cpu_gpu_sync_allowed=0`,
  `ack_cpu_gpu_sync_submitted=0`, `completion_reported=0`,
  `completion_acknowledged=0`, `completion_cpu_gpu_sync_allowed=0`,
  `completion_cpu_gpu_sync_submitted=0`, `deadline_armed=0`,
  `deadline_timer_armed=0`, `deadline_expired=0`,
  `deadline_completion_reported=0`, `deadline_cpu_gpu_sync_allowed=0`,
  `deadline_cpu_gpu_sync_submitted=0`, `sync_submitted=0`,
  `sync_wait_allowed=0`, `sync_wait_submitted=0`, `sync_signal_allowed=0`,
  `sync_signal_submitted=0`, `sync_deadline_armed=0`,
  `sync_completion_reported=0`, `sync_cpu_gpu_sync_allowed=0`,
  `sync_cpu_gpu_sync_submitted=0`, `timeline_submitted=0`,
  `timeline_wait_allowed=0`, `timeline_wait_submitted=0`,
  `timeline_signal_allowed=0`, `timeline_signal_submitted=0`,
  `timeline_semaphore_allowed=0`, `timeline_semaphore_submitted=0`,
  `timeline_value_allocated=0`, `timeline_value_published=0`,
  `timeline_cpu_gpu_sync_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `barrier_memory_visibility_established=0`,
  `barrier_cache_visibility_established=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `framebuffer_flushed=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `audit_allowed`, `audit_ticket_selected` e `audit_target_selected` preservam
  planejamento futuro de auditoria sem anexar log real, escrever estado real,
  limpar/liberar recursos, submeter audit, seal, cleanup, retire, ack, reportar
  completion, acknowledge, sincronizar CPU/GPU, armar deadline, submeter
  output/display, acordar compositor ou executar flip neste patch.


## Incremento `0.8.0-alpha.178+20260512`

- `struct login_window_credential_screen_record_plan` consome o audit plan seguro
  da tela de credenciais e publica somente um ticket declarativo de registro para
  futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_record_plan_build()` seleciona tickets
  `credential-screen-record-ticket`, `text-recovery-record-ticket`,
  `text-login-resume-record-ticket` ou `text-login-fallback-record-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O record plan exige audit plan seguro, audit exigido e permitido mas nao
  submetido, audit ticket/target selecionados, append de log de auditoria
  desabilitado, CPU/GPU sync de auditoria desabilitado, seal permitido mas nao
  submetido, escrita de estado de seal desabilitada, cleanup permitido mas nao
  submetido, cleanup ticket/target selecionados, sem release de recursos de
  cleanup, retire permitido mas nao submetido, ack permitido mas nao submetido,
  completion permitido mas nao reportado, acknowledge de completion exigido mas
  nao executado, deadline permitido mas nao armado, timer de deadline exigido
  mas nao armado, deadline nao expirado, completion de deadline nao reportado,
  sync/timeline/fence/barrier/flush/framebuffer/blit/output/display/scanout/
  vsync/schedule/present/damage/compositor nao submetidos, nenhum valor de
  timeline publicado, nenhum framebuffer mapeado ou escrito, nenhum pixel
  copiado, DMA desabilitado, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `record_plan_safe`.
- Submit grafico, audit plan ausente e audit plan inseguro permanecem fail-closed
  em `text-login-fallback-record-ticket`, com `record_submitted=0`,
  `record_persist_allowed=0`, `record_persisted=0`,
  `record_cpu_gpu_sync_allowed=0`, `record_cpu_gpu_sync_submitted=0`,
  `audit_submitted=0`, `audit_log_append_allowed=0`, `audit_log_appended=0`,
  `audit_cpu_gpu_sync_allowed=0`, `audit_cpu_gpu_sync_submitted=0`,
  `seal_submitted=0`, `seal_state_write_allowed=0`, `seal_state_written=0`,
  `seal_cpu_gpu_sync_allowed=0`, `seal_cpu_gpu_sync_submitted=0`,
  `cleanup_submitted=0`, `cleanup_resource_release_allowed=0`,
  `cleanup_resource_released=0`, `cleanup_cpu_gpu_sync_allowed=0`,
  `cleanup_cpu_gpu_sync_submitted=0`, `retire_submitted=0`,
  `ack_submitted=0`, `completion_reported=0`, `completion_acknowledged=0`,
  `deadline_armed=0`, `deadline_timer_armed=0`, `deadline_expired=0`,
  `sync_submitted=0`, `timeline_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `flush_submitted=0`, `framebuffer_written=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `scanout_submitted=0`, `vsync_submitted=0`, `schedule_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `page_flip_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `record_allowed`, `record_ticket_selected` e `record_target_selected` preservam
  planejamento futuro de registro sem persistir registro real, anexar log real,
  escrever estado real, limpar/liberar recursos, submeter record, audit, seal,
  cleanup, retire, ack, reportar completion, acknowledge, sincronizar CPU/GPU,
  armar deadline, submeter output/display, acordar compositor ou executar flip
  neste patch.


## Incremento `0.8.0-alpha.179+20260512`

- `struct login_window_credential_screen_receipt_plan` consome o record plan seguro
  da tela de credenciais e publica somente um ticket declarativo de recibo para
  futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_receipt_plan_build()` seleciona tickets
  `credential-screen-receipt-ticket`, `text-recovery-receipt-ticket`,
  `text-login-resume-receipt-ticket` ou `text-login-fallback-receipt-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O receipt plan exige record plan seguro, record exigido e permitido mas nao
  submetido, record ticket/target selecionados, persistencia de record
  desabilitada, CPU/GPU sync de record desabilitado, audit permitido mas nao
  submetido, append de log de auditoria desabilitado, seal permitido mas nao
  submetido, cleanup permitido mas nao submetido, retire permitido mas nao
  submetido, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `receipt_plan_safe`.
- Submit grafico, record plan ausente e record plan inseguro permanecem
  fail-closed em `text-login-fallback-receipt-ticket`, com
  `receipt_submitted=0`, `receipt_persist_allowed=0`, `receipt_persisted=0`,
  `receipt_cpu_gpu_sync_allowed=0`, `receipt_cpu_gpu_sync_submitted=0`,
  `record_submitted=0`, `record_persist_allowed=0`, `record_persisted=0`,
  `record_cpu_gpu_sync_allowed=0`, `record_cpu_gpu_sync_submitted=0`,
  `audit_submitted=0`, `audit_log_append_allowed=0`, `audit_log_appended=0`,
  `seal_submitted=0`, `seal_state_write_allowed=0`, `seal_state_written=0`,
  `cleanup_submitted=0`, `cleanup_resource_release_allowed=0`,
  `cleanup_resource_released=0`, `retire_submitted=0`, `ack_submitted=0`,
  `completion_reported=0`, `completion_acknowledged=0`, `deadline_armed=0`,
  `deadline_timer_armed=0`, `deadline_expired=0`, `sync_submitted=0`,
  `timeline_submitted=0`, `fence_submitted=0`, `barrier_submitted=0`,
  `flush_submitted=0`, `framebuffer_written=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `scanout_submitted=0`,
  `vsync_submitted=0`, `schedule_submitted=0`, `present_submitted=0`,
  `damage_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `receipt_allowed`, `receipt_ticket_selected` e `receipt_target_selected`
  preservam planejamento futuro de recibo sem persistir recibo real, persistir
  registro real, anexar log real, escrever estado real, limpar/liberar recursos,
  submeter receipt, record, audit, seal, cleanup, retire, ack, reportar
  completion, acknowledge, sincronizar CPU/GPU, armar deadline, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.180+20260512`

- `struct login_window_credential_screen_ledger_plan` consome o receipt plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  ledger para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_ledger_plan_build()` seleciona tickets
  `credential-screen-ledger-ticket`, `text-recovery-ledger-ticket`,
  `text-login-resume-ledger-ticket` ou `text-login-fallback-ledger-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O ledger plan exige receipt plan seguro, receipt exigido e permitido mas nao
  submetido, receipt ticket/target selecionados, persistencia de receipt
  desabilitada, CPU/GPU sync de receipt desabilitado, record permitido mas nao
  submetido, persistencia de record desabilitada, audit permitido mas nao
  submetido, append de log de auditoria desabilitado, seal permitido mas nao
  submetido, cleanup permitido mas nao submetido, retire permitido mas nao
  submetido, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `ledger_plan_safe`.
- Submit grafico, receipt plan ausente e receipt plan inseguro permanecem
  fail-closed em `text-login-fallback-ledger-ticket`, com
  `ledger_submitted=0`, `ledger_persist_allowed=0`, `ledger_persisted=0`,
  `ledger_cpu_gpu_sync_allowed=0`, `ledger_cpu_gpu_sync_submitted=0`,
  `receipt_submitted=0`, `receipt_persist_allowed=0`, `receipt_persisted=0`,
  `receipt_cpu_gpu_sync_allowed=0`, `receipt_cpu_gpu_sync_submitted=0`,
  `record_submitted=0`, `record_persist_allowed=0`, `record_persisted=0`,
  `audit_submitted=0`, `audit_log_append_allowed=0`, `audit_log_appended=0`,
  `seal_submitted=0`, `cleanup_submitted=0`, `retire_submitted=0`,
  `ack_submitted=0`, `completion_reported=0`, `deadline_armed=0`,
  `sync_submitted=0`, `timeline_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `flush_submitted=0`, `framebuffer_written=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `page_flip_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `ledger_allowed`, `ledger_ticket_selected` e `ledger_target_selected`
  preservam planejamento futuro de ledger sem persistir ledger real, recibo real,
  registro real, anexar log real, escrever estado real, limpar/liberar recursos,
  submeter ledger, receipt, record, audit, seal, cleanup, retire, ack, reportar
  completion, acknowledge, sincronizar CPU/GPU, armar deadline, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.181+20260512`

- `struct login_window_credential_screen_journal_plan` consome o ledger plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  journal para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_journal_plan_build()` seleciona tickets
  `credential-screen-journal-ticket`, `text-recovery-journal-ticket`,
  `text-login-resume-journal-ticket` ou `text-login-fallback-journal-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O journal plan exige ledger plan seguro, ledger exigido e permitido mas nao
  submetido, ledger ticket/target selecionados, persistencia de ledger
  desabilitada, CPU/GPU sync de ledger desabilitado, receipt permitido mas nao
  submetido, persistencia de receipt desabilitada, record permitido mas nao
  submetido, persistencia de record desabilitada, audit permitido mas nao
  submetido, append de log de auditoria desabilitado, seal permitido mas nao
  submetido, cleanup permitido mas nao submetido, retire permitido mas nao
  submetido, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `journal_plan_safe`.
- Submit grafico, ledger plan ausente e ledger plan inseguro permanecem
  fail-closed em `text-login-fallback-journal-ticket`, com
  `journal_submitted=0`, `journal_persist_allowed=0`, `journal_persisted=0`,
  `journal_cpu_gpu_sync_allowed=0`, `journal_cpu_gpu_sync_submitted=0`,
  `ledger_submitted=0`, `ledger_persist_allowed=0`, `ledger_persisted=0`,
  `receipt_submitted=0`, `receipt_persist_allowed=0`, `receipt_persisted=0`,
  `record_submitted=0`, `record_persist_allowed=0`, `record_persisted=0`,
  `audit_submitted=0`, `audit_log_append_allowed=0`, `audit_log_appended=0`,
  `seal_submitted=0`, `cleanup_submitted=0`, `retire_submitted=0`,
  `ack_submitted=0`, `completion_reported=0`, `deadline_armed=0`,
  `sync_submitted=0`, `timeline_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `flush_submitted=0`, `framebuffer_written=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `page_flip_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `journal_allowed`, `journal_ticket_selected` e `journal_target_selected`
  preservam planejamento futuro de journal sem persistir journal real, ledger
  real, recibo real, registro real, anexar log real, escrever estado real,
  limpar/liberar recursos, submeter journal, ledger, receipt, record, audit,
  seal, cleanup, retire, ack, reportar completion, acknowledge, sincronizar
  CPU/GPU, armar deadline, submeter output/display, acordar compositor ou
  executar flip neste patch.

## Incremento `0.8.0-alpha.182+20260512`

- `struct login_window_credential_screen_archive_plan` consome o journal plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  archive para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_archive_plan_build()` seleciona tickets
  `credential-screen-archive-ticket`, `text-recovery-archive-ticket`,
  `text-login-resume-archive-ticket` ou `text-login-fallback-archive-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O archive plan exige journal plan seguro, journal exigido e permitido mas nao
  submetido, journal ticket/target selecionados, persistencia de journal
  desabilitada, CPU/GPU sync de journal desabilitado, ledger permitido mas nao
  submetido, persistencia de ledger desabilitada, receipt permitido mas nao
  submetido, persistencia de receipt desabilitada, record permitido mas nao
  submetido, persistencia de record desabilitada, audit permitido mas nao
  submetido, append de log de auditoria desabilitado, seal permitido mas nao
  submetido, cleanup permitido mas nao submetido, retire permitido mas nao
  submetido, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `archive_plan_safe`.
- Submit grafico, journal plan ausente e journal plan inseguro permanecem
  fail-closed em `text-login-fallback-archive-ticket`, com
  `archive_submitted=0`, `archive_persist_allowed=0`, `archive_persisted=0`,
  `archive_cpu_gpu_sync_allowed=0`, `archive_cpu_gpu_sync_submitted=0`,
  `journal_submitted=0`, `journal_persist_allowed=0`, `journal_persisted=0`,
  `ledger_submitted=0`, `ledger_persist_allowed=0`, `ledger_persisted=0`,
  `receipt_submitted=0`, `receipt_persist_allowed=0`, `receipt_persisted=0`,
  `record_submitted=0`, `record_persist_allowed=0`, `record_persisted=0`,
  `audit_submitted=0`, `audit_log_append_allowed=0`, `audit_log_appended=0`,
  `seal_submitted=0`, `cleanup_submitted=0`, `retire_submitted=0`,
  `ack_submitted=0`, `completion_reported=0`, `deadline_armed=0`,
  `sync_submitted=0`, `timeline_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `flush_submitted=0`, `framebuffer_written=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `page_flip_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `archive_allowed`, `archive_ticket_selected` e `archive_target_selected`
  preservam planejamento futuro de archive sem persistir archive real, journal
  real, ledger real, recibo real, registro real, anexar log real, escrever
  estado real, limpar/liberar recursos, submeter archive, journal, ledger,
  receipt, record, audit, seal, cleanup, retire, ack, reportar completion,
  acknowledge, sincronizar CPU/GPU, armar deadline, submeter output/display,
  acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.183+20260512`

- `struct login_window_credential_screen_retention_plan` consome o archive plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  retention para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_retention_plan_build()` seleciona tickets
  `credential-screen-retention-ticket`, `text-recovery-retention-ticket`,
  `text-login-resume-retention-ticket` ou
  `text-login-fallback-retention-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O retention plan exige archive plan seguro, archive exigido e permitido mas
  nao submetido, archive ticket/target selecionados, persistencia de archive
  desabilitada, CPU/GPU sync de archive desabilitado, journal permitido mas nao
  submetido, persistencia de journal desabilitada, ledger permitido mas nao
  submetido, persistencia de ledger desabilitada, receipt permitido mas nao
  submetido, persistencia de receipt desabilitada, record permitido mas nao
  submetido, persistencia de record desabilitada, audit permitido mas nao
  submetido, append de log de auditoria desabilitado, seal permitido mas nao
  submetido, cleanup permitido mas nao submetido, retire permitido mas nao
  submetido, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `retention_plan_safe`.
- Submit grafico, archive plan ausente e archive plan inseguro permanecem
  fail-closed em `text-login-fallback-retention-ticket`, com
  `retention_submitted=0`, `retention_persist_allowed=0`,
  `retention_persisted=0`, `retention_cpu_gpu_sync_allowed=0`,
  `retention_cpu_gpu_sync_submitted=0`, `archive_submitted=0`,
  `archive_persist_allowed=0`, `archive_persisted=0`, `journal_submitted=0`,
  `journal_persist_allowed=0`, `journal_persisted=0`, `ledger_submitted=0`,
  `ledger_persist_allowed=0`, `ledger_persisted=0`, `receipt_submitted=0`,
  `receipt_persist_allowed=0`, `receipt_persisted=0`, `record_submitted=0`,
  `record_persist_allowed=0`, `record_persisted=0`, `audit_submitted=0`,
  `audit_log_append_allowed=0`, `audit_log_appended=0`, `seal_submitted=0`,
  `cleanup_submitted=0`, `retire_submitted=0`, `ack_submitted=0`,
  `completion_reported=0`, `deadline_armed=0`, `sync_submitted=0`,
  `timeline_submitted=0`, `fence_submitted=0`, `barrier_submitted=0`,
  `flush_submitted=0`, `framebuffer_written=0`, `blit_pixels_copied=0`,
  `output_submitted=0`, `display_submitted=0`, `page_flip_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `retention_allowed`, `retention_ticket_selected` e
  `retention_target_selected` preservam planejamento futuro de retention sem
  persistir retention real, archive real, journal real, ledger real, recibo real,
  registro real, anexar log real, escrever estado real, limpar/liberar recursos,
  submeter retention, archive, journal, ledger, receipt, record, audit, seal,
  cleanup, retire, ack, reportar completion, acknowledge, sincronizar CPU/GPU,
  armar deadline, submeter output/display, acordar compositor ou executar flip
  neste patch.

## Incremento `0.8.0-alpha.184+20260512`

- `struct login_window_credential_screen_expiry_plan` consome o retention plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  expiry para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_expiry_plan_build()` seleciona tickets
  `credential-screen-expiry-ticket`, `text-recovery-expiry-ticket`,
  `text-login-resume-expiry-ticket` ou `text-login-fallback-expiry-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O expiry plan exige retention plan seguro, retention exigido e permitido mas
  nao submetido, retention ticket/target selecionados, persistencia de retention
  desabilitada, CPU/GPU sync de retention desabilitado, archive permitido mas nao
  submetido, persistencia de archive desabilitada, journal permitido mas nao
  submetido, persistencia de journal desabilitada, ledger permitido mas nao
  submetido, persistencia de ledger desabilitada, receipt permitido mas nao
  submetido, persistencia de receipt desabilitada, record permitido mas nao
  submetido, persistencia de record desabilitada, audit permitido mas nao
  submetido, append de log de auditoria desabilitado, seal permitido mas nao
  submetido, cleanup permitido mas nao submetido, retire permitido mas nao
  submetido, ack permitido mas nao submetido, completion permitido mas nao
  reportado, acknowledge de completion exigido mas nao executado, deadline
  permitido mas nao armado, timer de deadline exigido mas nao armado, deadline
  nao expirado, completion de deadline nao reportado, sync/timeline/fence/
  barrier/flush/framebuffer/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor nao submetidos, nenhum valor de timeline publicado, nenhum
  framebuffer mapeado ou escrito, nenhum pixel copiado, DMA desabilitado,
  timer/fence/wake/flip desabilitados, rota selecionada, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo antes
  de marcar `expiry_plan_safe`.
- Submit grafico, retention plan ausente e retention plan inseguro permanecem
  fail-closed em `text-login-fallback-expiry-ticket`, com
  `expiry_submitted=0`, `expiry_persist_allowed=0`, `expiry_persisted=0`,
  `expiry_cpu_gpu_sync_allowed=0`, `expiry_cpu_gpu_sync_submitted=0`,
  `expiry_timer_allowed=0`, `expiry_timer_armed=0`, `expiry_delete_allowed=0`,
  `expiry_deleted=0`, `retention_submitted=0`, `retention_persist_allowed=0`,
  `retention_persisted=0`, `archive_submitted=0`, `archive_persist_allowed=0`,
  `archive_persisted=0`, `journal_submitted=0`, `journal_persist_allowed=0`,
  `journal_persisted=0`, `ledger_submitted=0`, `ledger_persist_allowed=0`,
  `ledger_persisted=0`, `receipt_submitted=0`, `receipt_persist_allowed=0`,
  `receipt_persisted=0`, `record_submitted=0`, `record_persist_allowed=0`,
  `record_persisted=0`, `audit_submitted=0`, `audit_log_append_allowed=0`,
  `audit_log_appended=0`, `seal_submitted=0`, `cleanup_submitted=0`,
  `retire_submitted=0`, `ack_submitted=0`, `completion_reported=0`,
  `deadline_armed=0`, `sync_submitted=0`, `timeline_submitted=0`,
  `fence_submitted=0`, `barrier_submitted=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `blit_pixels_copied=0`, `output_submitted=0`,
  `display_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `expiry_allowed`, `expiry_ticket_selected` e `expiry_target_selected`
  preservam planejamento futuro de expiry sem persistir expiry real, retention
  real, archive real, journal real, ledger real, recibo real, registro real,
  armar timer real, apagar estado real, anexar log real, escrever estado real,
  limpar/liberar recursos, submeter expiry, retention, archive, journal, ledger,
  receipt, record, audit, seal, cleanup, retire, ack, reportar completion,
  acknowledge, sincronizar CPU/GPU, armar deadline, submeter output/display,
  acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.185+20260512`

- `struct login_window_credential_screen_purge_plan` consome o expiry plan
  seguro da tela de credenciais e publica somente um ticket declarativo de purge
  para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_purge_plan_build()` seleciona tickets
  `credential-screen-purge-ticket`, `text-recovery-purge-ticket`,
  `text-login-resume-purge-ticket` ou `text-login-fallback-purge-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O purge plan exige expiry plan seguro, expiry exigido e permitido mas nao
  submetido, expiry ticket/target selecionados, persistencia de expiry
  desabilitada, CPU/GPU sync de expiry desabilitado, timer e delete de expiry
  desabilitados, retention permitido mas nao submetido, persistencia de
  retention desabilitada, archive permitido mas nao submetido, persistencia de
  archive desabilitada, journal permitido mas nao submetido, persistencia de
  journal desabilitada, ledger permitido mas nao submetido, persistencia de
  ledger desabilitada, receipt permitido mas nao submetido, persistencia de
  receipt desabilitada, record permitido mas nao submetido, persistencia de
  record desabilitada, audit permitido mas nao submetido, append de log de
  auditoria desabilitado, seal permitido mas nao submetido, cleanup permitido
  mas nao submetido, retire permitido mas nao submetido, ack permitido mas nao
  submetido, completion permitido mas nao reportado, acknowledge de completion
  exigido mas nao executado, deadline permitido mas nao armado, timer de deadline
  exigido mas nao armado, deadline nao expirado, completion de deadline nao
  reportado, sync/timeline/fence/barrier/flush/framebuffer/blit/output/display/
  scanout/vsync/schedule/present/damage/compositor nao submetidos, nenhum valor
  de timeline publicado, nenhum framebuffer mapeado ou escrito, nenhum pixel
  copiado, DMA desabilitado, timer/fence/wake/flip desabilitados, rota
  selecionada, credenciais limpas e redigidas, callbacks de submit/auth zerados
  e login textual autoritativo antes de marcar `purge_plan_safe`.
- Submit grafico, expiry plan ausente e expiry plan inseguro permanecem
  fail-closed em `text-login-fallback-purge-ticket`, com
  `purge_submitted=0`, `purge_persist_allowed=0`, `purge_persisted=0`,
  `purge_cpu_gpu_sync_allowed=0`, `purge_cpu_gpu_sync_submitted=0`,
  `purge_delete_allowed=0`, `purge_deleted=0`, `expiry_submitted=0`,
  `expiry_persist_allowed=0`, `expiry_persisted=0`,
  `expiry_cpu_gpu_sync_allowed=0`, `expiry_cpu_gpu_sync_submitted=0`,
  `expiry_delete_allowed=0`, `expiry_deleted=0`, `retention_submitted=0`,
  `retention_persist_allowed=0`, `retention_persisted=0`,
  `archive_submitted=0`, `archive_persist_allowed=0`, `archive_persisted=0`,
  `journal_submitted=0`, `journal_persist_allowed=0`, `journal_persisted=0`,
  `ledger_submitted=0`, `ledger_persist_allowed=0`, `ledger_persisted=0`,
  `receipt_submitted=0`, `receipt_persist_allowed=0`, `receipt_persisted=0`,
  `record_submitted=0`, `record_persist_allowed=0`, `record_persisted=0`,
  `audit_submitted=0`, `audit_log_append_allowed=0`,
  `audit_log_appended=0`, `seal_submitted=0`, `cleanup_submitted=0`,
  `retire_submitted=0`, `ack_submitted=0`, `completion_reported=0`,
  `deadline_armed=0`, `sync_submitted=0`, `timeline_submitted=0`,
  `fence_submitted=0`, `barrier_submitted=0`, `flush_submitted=0`,
  `framebuffer_written=0`, `blit_pixels_copied=0`, `output_submitted=0`,
  `display_submitted=0`, `page_flip_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `purge_allowed`, `purge_ticket_selected` e `purge_target_selected` preservam
  planejamento futuro de purge sem persistir purge real, expiry real, retention
  real, archive real, journal real, ledger real, recibo real, registro real,
  apagar purge real, armar timer real, apagar expiry real, anexar log real,
  escrever estado real, limpar/liberar recursos, submeter purge, expiry,
  retention, archive, journal, ledger, receipt, record, audit, seal, cleanup,
  retire, ack, reportar completion, acknowledge, sincronizar CPU/GPU, armar
  deadline, submeter output/display, acordar compositor ou executar flip neste
  patch.

## Incremento `0.8.0-alpha.186+20260512`

- `struct login_window_credential_screen_tombstone_plan` consome o purge plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  tombstone para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_tombstone_plan_build()` seleciona tickets
  `credential-screen-tombstone-ticket`, `text-recovery-tombstone-ticket`,
  `text-login-resume-tombstone-ticket` ou `text-login-fallback-tombstone-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O tombstone plan exige purge plan seguro, purge exigido e permitido mas nao
  submetido, purge ticket/target selecionados, persistencia de purge
  desabilitada, CPU/GPU sync de purge desabilitado, delete de purge desabilitado,
  expiry/retention/archive/journal/ledger/receipt/record/audit/seal/cleanup/
  retire/ack/completion/deadline preservados apenas como estados declarativos
  nao submetidos, sync/timeline/fence/barrier/flush/framebuffer/blit/output/
  display/scanout/vsync/schedule/present/damage/compositor/page flip nao
  submetidos, credenciais limpas e redigidas, callbacks de submit/auth zerados e
  login textual autoritativo antes de marcar `tombstone_plan_safe`.
- Submit grafico, purge plan ausente e purge plan inseguro permanecem
  fail-closed em `text-login-fallback-tombstone-ticket`, com
  `tombstone_submitted=0`, `tombstone_persist_allowed=0`,
  `tombstone_persisted=0`, `tombstone_cpu_gpu_sync_allowed=0`,
  `tombstone_cpu_gpu_sync_submitted=0`, `purge_submitted=0`,
  `purge_persist_allowed=0`, `purge_persisted=0`, `purge_deleted=0`,
  `expiry_submitted=0`, `retention_submitted=0`, `archive_submitted=0`,
  `journal_submitted=0`, `ledger_submitted=0`, `receipt_submitted=0`,
  `record_submitted=0`, `audit_submitted=0`, `audit_log_appended=0`,
  `seal_submitted=0`, `cleanup_submitted=0`, `retire_submitted=0`,
  `ack_submitted=0`, `completion_reported=0`, `deadline_armed=0`,
  `sync_submitted=0`, `timeline_submitted=0`, `fence_submitted=0`,
  `barrier_submitted=0`, `flush_submitted=0`, `framebuffer_written=0`,
  `blit_pixels_copied=0`, `output_submitted=0`, `display_submitted=0`,
  `page_flip_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `tombstone_allowed`, `tombstone_ticket_selected` e
  `tombstone_target_selected` preservam planejamento futuro de tombstone sem
  persistir tombstone real, purge real, expiry real, retention real, archive
  real, journal real, ledger real, recibo real, registro real, apagar purge real,
  armar timer real, apagar expiry real, anexar log real, escrever estado real,
  limpar/liberar recursos, submeter tombstone, purge, expiry, retention,
  archive, journal, ledger, receipt, record, audit, seal, cleanup, retire, ack,
  reportar completion, acknowledge, sincronizar CPU/GPU, armar deadline,
  submeter output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.187+20260512`

- `struct login_window_credential_screen_compaction_plan` consome o tombstone
  plan seguro da tela de credenciais e publica somente um ticket declarativo de
  compaction para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_compaction_plan_build()` seleciona tickets
  `credential-screen-compaction-ticket`, `text-recovery-compaction-ticket`,
  `text-login-resume-compaction-ticket` ou
  `text-login-fallback-compaction-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O compaction plan exige tombstone plan seguro, tombstone exigido e permitido
  mas nao submetido, tombstone ticket/target selecionados, persistencia de
  tombstone desabilitada, CPU/GPU sync de tombstone desabilitado, purge/expiry/
  retention/archive/journal/ledger/receipt/record/audit/seal/cleanup/retire/ack/
  completion/deadline preservados apenas como estados declarativos nao
  submetidos, sync/timeline/fence/barrier/flush/framebuffer/blit/output/display/
  scanout/vsync/schedule/present/damage/compositor/page flip nao submetidos,
  credenciais limpas e redigidas, callbacks de submit/auth zerados e login
  textual autoritativo antes de marcar `compaction_plan_safe`.
- Submit grafico, tombstone plan ausente e tombstone plan inseguro permanecem
  fail-closed em `text-login-fallback-compaction-ticket`, com
  `compaction_submitted=0`, `compaction_storage_write_allowed=0`,
  `compaction_storage_written=0`, `compaction_resource_release_allowed=0`,
  `compaction_resource_released=0`, `compaction_cpu_gpu_sync_allowed=0`,
  `compaction_cpu_gpu_sync_submitted=0`, `tombstone_submitted=0`,
  `tombstone_persist_allowed=0`, `tombstone_persisted=0`,
  `tombstone_cpu_gpu_sync_allowed=0`, `tombstone_cpu_gpu_sync_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `compaction_allowed`, `compaction_ticket_selected` e
  `compaction_target_selected` preservam planejamento futuro de compaction sem
  compactar storage real, liberar recursos reais, persistir compaction real,
  persistir tombstone real, purge real, expiry real, retention real, archive
  real, journal real, ledger real, recibo real, registro real, apagar purge real,
  armar timer real, apagar expiry real, anexar log real, escrever estado real,
  submeter compaction, tombstone, purge, expiry, retention, archive, journal,
  ledger, receipt, record, audit, seal, cleanup, retire, ack, reportar
  completion, acknowledge, sincronizar CPU/GPU, armar deadline, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.188+20260512`

- `struct login_window_credential_screen_reclaim_plan` consome o compaction
  plan seguro da tela de credenciais e publica somente um ticket declarativo de
  reclaim para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_reclaim_plan_build()` seleciona tickets
  `credential-screen-reclaim-ticket`, `text-recovery-reclaim-ticket`,
  `text-login-resume-reclaim-ticket` ou `text-login-fallback-reclaim-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O reclaim plan exige compaction plan seguro, compaction exigida e permitida
  mas nao submetida, compaction ticket/target selecionados, compactacao de
  storage desabilitada, liberacao de recursos por compaction desabilitada,
  CPU/GPU sync de compaction desabilitado, tombstone/purge/expiry/retention/
  archive/journal/ledger/receipt/record/audit/seal/cleanup/retire/ack/
  completion/deadline preservados apenas como estados declarativos nao
  submetidos, credenciais limpas e redigidas, callbacks de submit/auth zerados e
  login textual autoritativo antes de marcar `reclaim_plan_safe`.
- Submit grafico, compaction plan ausente e compaction plan inseguro permanecem
  fail-closed em `text-login-fallback-reclaim-ticket`, com
  `reclaim_submitted=0`, `reclaim_storage_prune_allowed=0`,
  `reclaim_storage_pruned=0`, `reclaim_resource_release_allowed=0`,
  `reclaim_resource_released=0`, `reclaim_cpu_gpu_sync_allowed=0`,
  `reclaim_cpu_gpu_sync_submitted=0`, `compaction_submitted=0`,
  `compaction_storage_write_allowed=0`, `compaction_storage_written=0`,
  `compaction_resource_release_allowed=0`, `compaction_resource_released=0`,
  `compaction_cpu_gpu_sync_allowed=0`, `compaction_cpu_gpu_sync_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `reclaim_allowed`, `reclaim_ticket_selected` e `reclaim_target_selected`
  preservam planejamento futuro de reclaim sem podar storage real, liberar
  recursos reais, persistir reclaim real, persistir compaction real, persistir
  tombstone real, purge real, expiry real, retention real, archive real, journal
  real, ledger real, recibo real, registro real, apagar purge real, armar timer
  real, apagar expiry real, anexar log real, escrever estado real, submeter
  reclaim, compaction, tombstone, purge, expiry, retention, archive, journal,
  ledger, receipt, record, audit, seal, cleanup, retire, ack, reportar
  completion, acknowledge, sincronizar CPU/GPU, armar deadline, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.189+20260512`

- `struct login_window_credential_screen_release_plan` consome o reclaim plan
  seguro da tela de credenciais e publica somente um ticket declarativo de
  release para futura integracao GUI/compositor/display/auditoria persistente.
- `login_window_credential_screen_release_plan_build()` seleciona tickets
  `credential-screen-release-ticket`, `text-recovery-release-ticket`,
  `text-login-resume-release-ticket` ou `text-login-fallback-release-ticket`
  sem carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O release plan exige reclaim plan seguro, compaction exigida e permitida mas
  nao submetida, compaction ticket/target selecionados, compactacao de storage
  desabilitada, reclaim exigido e permitido mas nao submetido, prune/liberacao/
  sync de reclaim desabilitados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `release_plan_safe`.
- Submit grafico, reclaim plan ausente e reclaim plan inseguro permanecem
  fail-closed em `text-login-fallback-release-ticket`, com
  `release_submitted=0`, `release_storage_prune_allowed=0`,
  `release_storage_pruned=0`, `release_resource_release_allowed=0`,
  `release_resource_released=0`, `release_cpu_gpu_sync_allowed=0`,
  `release_cpu_gpu_sync_submitted=0`, `reclaim_submitted=0`,
  `reclaim_storage_prune_allowed=0`, `reclaim_storage_pruned=0`,
  `reclaim_resource_release_allowed=0`, `reclaim_resource_released=0`,
  `reclaim_cpu_gpu_sync_allowed=0`, `reclaim_cpu_gpu_sync_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `release_allowed`, `release_ticket_selected` e `release_target_selected`
  preservam planejamento futuro de release sem podar storage real, liberar
  recursos reais, submeter release real, persistir release real, persistir
  reclaim real, persistir compaction real, persistir tombstone real, purge real,
  expiry real, retention real, archive real, journal real, ledger real, recibo
  real, registro real, anexar log real, escrever estado real, sincronizar
  CPU/GPU, submeter output/display, acordar compositor ou executar flip neste
  patch.

## Incremento `0.8.0-alpha.190+20260512`

- `struct login_window_credential_screen_gui_plan` consome o release plan seguro
  da tela de credenciais e publica somente um ticket declarativo de GUI para
  futura integracao com compositor/display/auditoria persistente.
- `login_window_credential_screen_gui_plan_build()` seleciona tickets
  `credential-screen-gui-ticket`, `text-recovery-gui-ticket`,
  `text-login-resume-gui-ticket` ou `text-login-fallback-gui-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O GUI plan exige release plan seguro, compaction/reclaim/release exigidos e
  permitidos mas nao submetidos, tickets/targets selecionados, prune/liberacao/
  sync e escrita de storage desabilitados, credenciais limpas e redigidas,
  callbacks de submit/auth zerados e login textual autoritativo antes de marcar
  `gui_plan_safe`.
- Submit grafico, release plan ausente e release plan inseguro permanecem
  fail-closed em `text-login-fallback-gui-ticket`, com `gui_submitted=0`,
  `gui_pixels_write_allowed=0`, `gui_pixels_written=0`,
  `gui_auth_submit_allowed=0`, `gui_auth_attempt_allowed=0`,
  `release_submitted=0`, `release_storage_prune_allowed=0`,
  `release_storage_pruned=0`, `release_resource_release_allowed=0`,
  `release_resource_released=0`, `release_cpu_gpu_sync_allowed=0`,
  `release_cpu_gpu_sync_submitted=0`, `reclaim_submitted=0`,
  `reclaim_storage_prune_allowed=0`, `reclaim_storage_pruned=0`,
  `reclaim_resource_release_allowed=0`, `reclaim_resource_released=0`,
  `reclaim_cpu_gpu_sync_allowed=0`, `reclaim_cpu_gpu_sync_submitted=0`,
  `compaction_submitted=0`, `compaction_storage_write_allowed=0`,
  `compaction_storage_written=0`, `compaction_resource_release_allowed=0`,
  `compaction_resource_released=0`, `compaction_cpu_gpu_sync_allowed=0`,
  `compaction_cpu_gpu_sync_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `gui_allowed`, `gui_ticket_selected` e `gui_target_selected` preservam
  planejamento futuro de GUI sem escrever pixels reais, submeter GUI real,
  autenticar pela GUI, submeter release real, podar storage real, liberar
  recursos reais, persistir estado real, sincronizar CPU/GPU, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.191+20260512`

- `struct login_window_credential_screen_window_plan` consome o GUI plan seguro
  da tela de credenciais e publica somente um ticket declarativo de janela para
  futura integracao com compositor/window manager/display.
- `login_window_credential_screen_window_plan_build()` seleciona tickets
  `credential-screen-window-ticket`, `text-recovery-window-ticket`,
  `text-login-resume-window-ticket` ou `text-login-fallback-window-ticket` sem
  carregar segredo, mascara, comprimento, snapshots internos ou ponteiros de
  callback.
- O window plan exige GUI plan seguro, compaction/reclaim/release/GUI exigidos e
  permitidos mas nao submetidos, tickets/targets selecionados, escrita de pixels,
  submit GUI, prune/liberacao/sync e escrita de storage desabilitados,
  credenciais limpas e redigidas, callbacks de submit/auth zerados e login
  textual autoritativo antes de marcar `window_plan_safe`.
- Submit grafico, GUI plan ausente, GUI plan inseguro e GUI plan com efeitos
  reais permanecem fail-closed em `text-login-fallback-window-ticket`, com
  `window_created=0`, `window_surface_bound=0`, `window_input_bound=0`,
  `window_auth_submit_allowed=0`, `window_auth_attempt_allowed=0`,
  `gui_submitted=0`, `gui_pixels_write_allowed=0`, `gui_pixels_written=0`,
  `gui_auth_submit_allowed=0`, `gui_auth_attempt_allowed=0`,
  `release_submitted=0`, `release_storage_prune_allowed=0`,
  `release_storage_pruned=0`, `reclaim_submitted=0`, `compaction_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `window_allowed`, `window_ticket_selected` e `window_target_selected` preservam
  planejamento futuro de janela sem criar janela real, vincular surface real,
  vincular input real, submeter window real, escrever pixels reais, autenticar
  pela GUI, submeter release real, podar storage real, liberar recursos reais,
  persistir estado real, sincronizar CPU/GPU, submeter output/display, acordar
  compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.192+20260512`

- `struct login_window_credential_screen_window_surface_plan` consome o window
  plan seguro da tela de credenciais e publica somente um ticket declarativo de
  surface de janela para futura integracao com compositor/window manager/display.
- `login_window_credential_screen_window_surface_plan_build()` seleciona tickets
  `credential-screen-window-surface-ticket`,
  `text-recovery-window-surface-ticket`,
  `text-login-resume-window-surface-ticket` ou
  `text-login-fallback-window-surface-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window surface plan exige window plan seguro com origem GUI disponivel e
  segura, compaction/reclaim/release/GUI/window exigidos e permitidos mas nao
  submetidos, tickets/targets selecionados, janela nao criada, surface/input nao
  vinculados, escrita de pixels, submit GUI/window, prune/liberacao/sync e
  escrita de storage desabilitados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `window_surface_plan_safe`.
- Submit grafico, window plan ausente, window plan inseguro e window plan com
  efeitos reais permanecem fail-closed em
  `text-login-fallback-window-surface-ticket`, com `surface_bound=0`,
  `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `surface_compositor_submit_allowed=0`, `surface_compositor_submitted=0`,
  `surface_auth_submit_allowed=0`, `surface_auth_attempt_allowed=0`,
  `window_created=0`, `window_surface_bound=0`, `window_input_bound=0`,
  `window_auth_submit_allowed=0`, `window_auth_attempt_allowed=0`,
  `gui_submitted=0`, `gui_pixels_write_allowed=0`, `gui_pixels_written=0`,
  `release_submitted=0`, `reclaim_submitted=0`, `compaction_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `surface_allowed`, `surface_ticket_selected` e `surface_target_selected`
  preservam planejamento futuro de surface sem vincular surface real, mapear
  memoria real, escrever pixels reais, submeter compositor real, submeter window
  real, autenticar pela GUI, submeter release real, podar storage real, liberar
  recursos reais, persistir estado real, sincronizar CPU/GPU, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.193+20260513`

- `struct login_window_credential_screen_window_compositor_plan` consome o
  window surface plan seguro da tela de credenciais e publica somente um ticket
  declarativo de compositor de janela para futura integracao com compositor,
  damage tracking e display.
- `login_window_credential_screen_window_compositor_plan_build()` seleciona
  tickets `credential-screen-window-compositor-ticket`,
  `text-recovery-window-compositor-ticket`,
  `text-login-resume-window-compositor-ticket` ou
  `text-login-fallback-window-compositor-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window compositor plan exige window surface plan seguro com origem window
  disponivel e segura, compaction/reclaim/release/GUI/window/surface exigidos e
  permitidos mas nao submetidos, tickets/targets selecionados, janela nao
  criada, surface nao vinculada, memoria nao mapeada, pixels nao escritos,
  submit de compositor/surface/window/GUI, prune/liberacao/sync e escrita de
  storage desabilitados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `window_compositor_plan_safe`.
- Submit grafico, window surface plan ausente, window surface plan inseguro e
  window surface plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-compositor-ticket`, com
  `compositor_submitted=0`, `compositor_surface_submitted=0`,
  `compositor_damage_submitted=0`, `compositor_auth_submit_allowed=0`,
  `compositor_auth_attempt_allowed=0`, `surface_bound=0`,
  `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `surface_compositor_submit_allowed=0`, `surface_compositor_submitted=0`,
  `window_created=0`, `window_surface_bound=0`, `window_input_bound=0`,
  `gui_submitted=0`, `release_submitted=0`, `reclaim_submitted=0`,
  `compaction_submitted=0`, `submit_enabled=0` e `auth_attempt_allowed=0`.
- `compositor_allowed`, `compositor_ticket_selected`,
  `compositor_target_selected`, `compositor_surface_allowed` e
  `compositor_damage_planned` preservam planejamento futuro de compositor sem
  submeter compositor real, surface real, damage real, window real, GUI real,
  autenticar pela GUI, submeter release real, podar storage real, liberar
  recursos reais, persistir estado real, sincronizar CPU/GPU, submeter
  output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.194+20260513`

- `struct login_window_credential_screen_window_present_plan` consome o window
  damage plan seguro da tela de credenciais e publica somente um ticket
  declarativo de apresentacao de janela para futura integracao com compositor,
  present e display.
- `login_window_credential_screen_window_present_plan_build()` seleciona tickets
  `credential-screen-window-present-ticket`,
  `text-recovery-window-present-ticket`,
  `text-login-resume-window-present-ticket` ou
  `text-login-fallback-window-present-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window present plan exige window damage plan seguro com origem window
  compositor/window surface disponivel e segura, damage autorizado mas nao
  submetido, cache hit real ausente, tickets/targets selecionados, credenciais
  limpas e redigidas, callbacks de submit/auth zerados e login textual
  autoritativo antes de marcar `window_present_plan_safe`.
- Submit grafico, window damage plan ausente, window damage plan inseguro e
  window damage plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-present-ticket`, com `present_submitted=0`,
  `damage_submitted=0`, `damage_cache_hit=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `window_created=0`, `gui_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `present_allowed`, `present_ticket_selected`, `present_target_selected`,
  `present_incremental_allowed`, `present_cache_allowed` e
  `present_reuse_allowed` preservam planejamento futuro de apresentacao sem
  apresentar frame real, submeter present real, enviar damage real, submeter
  compositor real, surface real, window real, GUI real, autenticar pela GUI,
  submeter output/display, acordar compositor ou executar flip neste patch.

## Incremento `0.8.0-alpha.195+20260513`

- `struct login_window_credential_screen_window_schedule_plan` consome o window
  present plan seguro da tela de credenciais e publica somente um ticket
  declarativo de agendamento de janela para futura integracao com frame pacing,
  compositor e display.
- `login_window_credential_screen_window_schedule_plan_build()` seleciona
  tickets `credential-screen-window-schedule-ticket`,
  `text-recovery-window-schedule-ticket`,
  `text-login-resume-window-schedule-ticket` ou
  `text-login-fallback-window-schedule-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window schedule plan exige window present plan seguro com origem window
  damage/compositor/surface disponivel e segura, present autorizado mas nao
  submetido, cache hit real ausente, tickets/targets selecionados, credenciais
  limpas e redigidas, callbacks de submit/auth zerados e login textual
  autoritativo antes de marcar `window_schedule_plan_safe`.
- Submit grafico, window present plan ausente, window present plan inseguro e
  window present plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-schedule-ticket`, com `schedule_submitted=0`,
  `frame_timer_armed=0`, `compositor_wake_submitted=0`,
  `page_flip_submitted=0`, `present_submitted=0`, `damage_submitted=0`,
  `compositor_submitted=0`, `surface_bound=0`, `surface_memory_mapped=0`,
  `surface_pixels_written=0`, `window_created=0`, `gui_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `schedule_allowed`, `schedule_ticket_selected`, `schedule_target_selected`,
  `schedule_incremental_allowed`, `schedule_cache_allowed` e
  `schedule_reuse_allowed` preservam planejamento futuro de agendamento sem
  agendar frame real, armar timer real, acordar compositor, executar page flip,
  submeter schedule real, present real, damage real, compositor real, surface
  real, window real, GUI real, autenticar pela GUI ou submeter output/display
  neste patch.

## Incremento `0.8.0-alpha.196+20260513`

- `struct login_window_credential_screen_window_vsync_plan` consome o window
  schedule plan seguro da tela de credenciais e publica somente um ticket
  declarativo de sincronizacao de janela para futura integracao com frame
  pacing, fences, compositor e display.
- `login_window_credential_screen_window_vsync_plan_build()` seleciona tickets
  `credential-screen-window-vsync-ticket`,
  `text-recovery-window-vsync-ticket`,
  `text-login-resume-window-vsync-ticket` ou
  `text-login-fallback-window-vsync-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window vsync plan exige window schedule plan seguro com origem window
  present/damage/compositor/surface disponivel e segura, schedule autorizado mas
  nao submetido, timers e page flip reais ausentes, tickets/targets selecionados,
  credenciais limpas e redigidas, callbacks de submit/auth zerados e login
  textual autoritativo antes de marcar `window_vsync_plan_safe`.
- Submit grafico, window schedule plan ausente, window schedule plan inseguro e
  window schedule plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-vsync-ticket`, com `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`, `schedule_submitted=0`,
  `frame_timer_armed=0`, `compositor_wake_submitted=0`,
  `page_flip_submitted=0`, `present_submitted=0`, `damage_submitted=0`,
  `compositor_submitted=0`, `surface_bound=0`, `surface_memory_mapped=0`,
  `surface_pixels_written=0`, `window_created=0`, `gui_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `vsync_allowed`, `vsync_ticket_selected`, `vsync_target_selected`,
  `vsync_fence_required` e `frame_pacing_allowed` preservam planejamento futuro
  de sincronizacao sem aguardar vsync real, armar fence real, submeter vsync
  real, submeter wait real, agendar frame real, armar timer real, acordar
  compositor, executar page flip, submeter schedule real, present real, damage
  real, compositor real, surface real, window real, GUI real, autenticar pela GUI
  ou submeter output/display neste patch.

## Incremento `0.8.0-alpha.197+20260513`

- `struct login_window_credential_screen_window_scanout_plan` consome o window
  vsync plan seguro da tela de credenciais e publica somente um ticket
  declarativo de scanout de janela para futura integracao com o display
  controller, scanout engine e flip queue.
- `login_window_credential_screen_window_scanout_plan_build()` seleciona
  tickets `credential-screen-window-scanout-ticket`,
  `text-recovery-window-scanout-ticket`,
  `text-login-resume-window-scanout-ticket` ou
  `text-login-fallback-window-scanout-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window scanout plan exige window vsync plan seguro com origem window
  schedule/present/damage/compositor/surface disponivel e segura, vsync
  autorizado mas nao submetido, fence/wait/timer/page flip reais ausentes,
  tickets/targets selecionados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `window_scanout_plan_safe`.
- Submit grafico, window vsync plan ausente, window vsync plan inseguro e
  window vsync plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-scanout-ticket`, com `scanout_submitted=0`,
  `scanout_buffer_attached=0`, `scanout_buffer_submitted=0`,
  `scanout_display_flip_allowed=0`, `scanout_display_flip_submitted=0`,
  `vsync_submitted=0`, `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `window_created=0`, `gui_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `scanout_allowed`, `scanout_ticket_selected` e `scanout_target_selected`
  preservam planejamento futuro de scanout sem anexar buffer real, submeter
  scanout real, executar display flip real, aguardar vsync real, armar fence
  real, submeter wait real, agendar frame real, armar timer real, acordar
  compositor, executar page flip, submeter schedule real, present real, damage
  real, compositor real, surface real, window real, GUI real, autenticar pela
  GUI ou submeter output/display neste patch.

## Incremento `0.8.0-alpha.198+20260513`

- `struct login_window_credential_screen_window_display_plan` consome o window
  scanout plan seguro da tela de credenciais e publica somente um ticket
  declarativo de display de janela para futura integracao com o display
  controller, output e pipeline.
- `login_window_credential_screen_window_display_plan_build()` seleciona
  tickets `credential-screen-window-display-ticket`,
  `text-recovery-window-display-ticket`,
  `text-login-resume-window-display-ticket` ou
  `text-login-fallback-window-display-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window display plan exige window scanout plan seguro com origem window
  vsync/schedule/present/damage/compositor/surface disponivel e segura, scanout
  autorizado mas nao submetido, buffer/flip/wait/timer/page flip reais
  ausentes, tickets/targets selecionados, credenciais limpas e redigidas,
  callbacks de submit/auth zerados e login textual autoritativo antes de
  marcar `window_display_plan_safe`.
- Submit grafico, window scanout plan ausente, window scanout plan inseguro e
  window scanout plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-display-ticket`, com `display_submitted=0`,
  `display_controller_attached=0`, `display_output_submitted=0`,
  `display_pipeline_submitted=0`, `scanout_submitted=0`,
  `scanout_buffer_attached=0`, `scanout_buffer_submitted=0`,
  `scanout_display_flip_allowed=0`, `scanout_display_flip_submitted=0`,
  `vsync_submitted=0`, `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `window_created=0`, `gui_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `display_allowed`, `display_ticket_selected` e `display_target_selected`
  preservam planejamento futuro de display sem anexar controlador real,
  submeter display real, executar output real, submeter pipeline real, anexar
  buffer real, submeter scanout real, executar display flip real, aguardar
  vsync real, armar fence real, submeter wait real, agendar frame real, armar
  timer real, acordar compositor, executar page flip, submeter schedule real,
  present real, damage real, compositor real, surface real, window real, GUI
  real ou autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.199+20260513`

- `struct login_window_credential_screen_window_output_plan` consome o window
  display plan seguro da tela de credenciais e publica somente um ticket
  declarativo de saida visual de janela para futura integracao com conector,
  modo, sinal de output e sincronizacao com a placa.
- `login_window_credential_screen_window_output_plan_build()` seleciona
  tickets `credential-screen-window-output-ticket`,
  `text-recovery-window-output-ticket`,
  `text-login-resume-window-output-ticket` ou
  `text-login-fallback-window-output-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window output plan exige window display plan seguro com origem window
  scanout/vsync/schedule/present/damage/compositor/surface disponivel e
  segura, display autorizado mas nao submetido, controlador/output/pipeline
  reais ausentes, buffer/flip/wait/timer/page flip reais ausentes,
  tickets/targets selecionados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `window_output_plan_safe`.
- Submit grafico, window display plan ausente, window display plan inseguro e
  window display plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-output-ticket`, com `output_submitted=0`,
  `output_connector_attached=0`, `output_connector_submitted=0`,
  `output_mode_attached=0`, `output_mode_submitted=0`,
  `output_signal_armed=0`, `output_signal_submitted=0`,
  `display_submitted=0`, `display_controller_attached=0`,
  `display_output_submitted=0`, `display_pipeline_submitted=0`,
  `scanout_submitted=0`, `scanout_buffer_attached=0`,
  `scanout_buffer_submitted=0`, `scanout_display_flip_allowed=0`,
  `scanout_display_flip_submitted=0`, `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `window_created=0`, `gui_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `output_allowed`, `output_ticket_selected` e `output_target_selected`
  preservam planejamento futuro de output sem anexar conector real, armar modo
  real, armar sinal real, submeter output real, submeter sinal real, anexar
  controlador real, submeter display real, executar output real, submeter
  pipeline real, anexar buffer real, submeter scanout real, executar display
  flip real, aguardar vsync real, armar fence real, submeter wait real,
  agendar frame real, armar timer real, acordar compositor, executar page
  flip, submeter schedule real, present real, damage real, compositor real,
  surface real, window real, GUI real ou autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.200+20260513`

- `struct login_window_credential_screen_window_blit_plan` consome o window
  output plan seguro da tela de credenciais e publica somente um ticket
  declarativo de blit de janela para futura integracao com mapeamento de
  framebuffer, copia de pixels, DMA e sincronizacao com scanout.
- `login_window_credential_screen_window_blit_plan_build()` seleciona tickets
  `credential-screen-window-blit-ticket`,
  `text-recovery-window-blit-ticket`,
  `text-login-resume-window-blit-ticket` ou
  `text-login-fallback-window-blit-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window blit plan exige window output plan seguro com origem window
  display/scanout/vsync/schedule/present/damage/compositor/surface disponivel
  e segura, output autorizado mas nao submetido, conector/modo/sinal reais
  ausentes, controlador/output/pipeline reais ausentes, buffer/flip/wait/timer/
  page flip reais ausentes, tickets/targets selecionados, credenciais limpas e
  redigidas, callbacks de submit/auth zerados e login textual autoritativo
  antes de marcar `window_blit_plan_safe`.
- Submit grafico, window output plan ausente, window output plan inseguro e
  window output plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-blit-ticket`, com `blit_submitted=0`,
  `blit_source_buffer_mapped=0`, `blit_destination_buffer_mapped=0`,
  `blit_pixels_copied=0`, `blit_dma_allowed=0`, `blit_dma_submitted=0`,
  `output_submitted=0`, `output_connector_attached=0`,
  `output_connector_submitted=0`, `output_mode_attached=0`,
  `output_mode_submitted=0`, `output_signal_armed=0`,
  `output_signal_submitted=0`, `display_submitted=0`,
  `display_controller_attached=0`, `display_output_submitted=0`,
  `display_pipeline_submitted=0`, `scanout_submitted=0`,
  `scanout_buffer_attached=0`, `scanout_buffer_submitted=0`,
  `scanout_display_flip_allowed=0`, `scanout_display_flip_submitted=0`,
  `vsync_submitted=0`, `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`, `surface_pixels_written=0`,
  `window_created=0`, `gui_submitted=0`, `submit_enabled=0` e
  `auth_attempt_allowed=0`.
- `blit_allowed`, `blit_ticket_selected` e `blit_target_selected` preservam
  planejamento futuro de blit sem mapear buffer real, copiar pixels reais,
  armar DMA real, submeter blit real, anexar conector real, armar modo real,
  armar sinal real, submeter output real, submeter sinal real, anexar
  controlador real, submeter display real, executar output real, submeter
  pipeline real, anexar buffer real, submeter scanout real, executar display
  flip real, aguardar vsync real, armar fence real, submeter wait real,
  agendar frame real, armar timer real, acordar compositor, executar page
  flip, submeter schedule real, present real, damage real, compositor real,
  surface real, window real, GUI real ou autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.201+20260513`

- `struct login_window_credential_screen_window_commit_plan` consome o window
  blit plan seguro da tela de credenciais e publica somente um ticket
  declarativo de commit atomico de janela para futura integracao com atomic
  modeset, page flips coordenados, callbacks de frame e fila de KMS.
- `login_window_credential_screen_window_commit_plan_build()` seleciona
  tickets `credential-screen-window-commit-ticket`,
  `text-recovery-window-commit-ticket`,
  `text-login-resume-window-commit-ticket` ou
  `text-login-fallback-window-commit-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window commit plan exige window blit plan seguro com origem window
  output/display/scanout/vsync/schedule/present/damage/compositor/surface
  disponivel e segura, blit autorizado mas nao submetido, buffer/DMA reais
  ausentes, conector/modo/sinal reais ausentes, controlador/output/pipeline
  reais ausentes, buffer/flip/wait/timer/page flip reais ausentes,
  tickets/targets selecionados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `window_commit_plan_safe`.
- Submit grafico, window blit plan ausente, window blit plan inseguro e
  window blit plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-commit-ticket`, com `commit_submitted=0`,
  `commit_state_attached=0`, `commit_state_submitted=0`,
  `commit_atomic_allowed=0`, `commit_atomic_submitted=0`,
  `commit_callback_armed=0`, `commit_callback_submitted=0`,
  `blit_submitted=0`, `blit_source_buffer_mapped=0`,
  `blit_destination_buffer_mapped=0`, `blit_pixels_copied=0`,
  `blit_dma_allowed=0`, `blit_dma_submitted=0`, `output_submitted=0`,
  `output_signal_submitted=0`, `display_submitted=0`,
  `display_controller_attached=0`, `display_pipeline_submitted=0`,
  `scanout_submitted=0`, `scanout_buffer_attached=0`,
  `scanout_display_flip_submitted=0`, `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`, `schedule_submitted=0`,
  `frame_timer_armed=0`, `compositor_wake_submitted=0`,
  `page_flip_submitted=0`, `present_submitted=0`, `damage_submitted=0`,
  `compositor_submitted=0`, `surface_bound=0`, `surface_memory_mapped=0`,
  `surface_pixels_written=0`, `window_created=0`, `gui_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `commit_allowed`, `commit_ticket_selected` e `commit_target_selected`
  preservam planejamento futuro de commit atomico sem anexar estado real,
  armar atomic commit real, armar callback de frame real, submeter callback
  real, mapear buffer real, copiar pixels reais, armar DMA real, submeter
  blit real, anexar conector real, armar modo real, armar sinal real,
  submeter output real, submeter sinal real, anexar controlador real,
  submeter display real, executar output real, submeter pipeline real,
  anexar buffer real, submeter scanout real, executar display flip real,
  aguardar vsync real, armar fence real, submeter wait real, agendar frame
  real, armar timer real, acordar compositor, executar page flip, submeter
  schedule real, present real, damage real, compositor real, surface real,
  window real, GUI real ou autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.202+20260513`

- `struct login_window_credential_screen_window_flip_plan` consome o window
  commit plan seguro da tela de credenciais e publica somente um ticket
  declarativo de page flip de janela para futura integracao com DRM/KMS, fila
  de vblank, eventos de flip e scanout coordenado.
- `login_window_credential_screen_window_flip_plan_build()` seleciona tickets
  `credential-screen-window-flip-ticket`,
  `text-recovery-window-flip-ticket`,
  `text-login-resume-window-flip-ticket` ou
  `text-login-fallback-window-flip-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window flip plan exige window commit plan seguro com origem window
  blit/output/display/scanout/vsync/schedule/present/damage/compositor/surface
  disponivel e segura, commit autorizado mas nao submetido, estado/atomic
  commit/callback reais ausentes, buffer/DMA reais ausentes, conector/modo/
  sinal reais ausentes, controlador/output/pipeline reais ausentes,
  buffer/flip/wait/timer/page flip reais ausentes, tickets/targets
  selecionados, credenciais limpas e redigidas, callbacks de submit/auth
  zerados e login textual autoritativo antes de marcar
  `window_flip_plan_safe`.
- Submit grafico, window commit plan ausente, window commit plan inseguro e
  window commit plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-flip-ticket`, com `flip_submitted=0`,
  `flip_buffer_attached=0`, `flip_buffer_submitted=0`,
  `flip_vblank_armed=0`, `flip_vblank_submitted=0`,
  `flip_event_armed=0`, `flip_event_submitted=0`,
  `flip_async_allowed=0`, `flip_async_submitted=0`,
  `commit_submitted=0`, `commit_state_attached=0`,
  `commit_state_submitted=0`, `commit_atomic_allowed=0`,
  `commit_atomic_submitted=0`, `commit_callback_armed=0`,
  `commit_callback_submitted=0`, `blit_submitted=0`,
  `blit_dma_submitted=0`, `output_submitted=0`,
  `output_signal_submitted=0`, `display_submitted=0`,
  `display_pipeline_submitted=0`, `scanout_submitted=0`,
  `scanout_display_flip_submitted=0`, `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`,
  `surface_pixels_written=0`, `window_created=0`, `gui_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `flip_allowed`, `flip_ticket_selected` e `flip_target_selected` preservam
  planejamento futuro de page flip sem anexar buffer real, armar vblank real,
  armar evento real, submeter flip async, anexar estado real, armar atomic
  commit real, armar callback de frame real, submeter callback real, mapear
  buffer real, copiar pixels reais, armar DMA real, submeter blit real, anexar
  conector real, armar modo real, armar sinal real, submeter output real,
  submeter sinal real, anexar controlador real, submeter display real,
  executar output real, submeter pipeline real, anexar buffer real, submeter
  scanout real, executar display flip real, aguardar vsync real, armar fence
  real, submeter wait real, agendar frame real, armar timer real, acordar
  compositor, executar page flip, submeter schedule real, present real,
  damage real, compositor real, surface real, window real, GUI real ou
  autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.203+20260513`

- `struct login_window_credential_screen_window_vblank_plan` consome o window
  flip plan seguro da tela de credenciais e publica somente um ticket
  declarativo de sincronizacao de vblank de janela para futura integracao com
  DRM/KMS, vblank events, compositor atomic e display sem execucao real.
- `login_window_credential_screen_window_vblank_plan_build()` seleciona
  tickets `credential-screen-window-vblank-ticket`,
  `text-recovery-window-vblank-ticket`,
  `text-login-resume-window-vblank-ticket` ou
  `text-login-fallback-window-vblank-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window vblank plan exige window flip plan seguro com origem window
  commit/blit/output/display/scanout/vsync/schedule/present/damage/compositor/
  surface disponivel e segura, flip autorizado mas nao submetido, buffer/
  vblank/evento/async reais ausentes, commit/atomic/callback reais ausentes,
  DMA real ausente, conector/modo/sinal reais ausentes, controlador/pipeline
  reais ausentes, wait/timer/page flip reais ausentes, tickets/targets
  selecionados, credenciais limpas e redigidas, callbacks de submit/auth
  zerados e login textual autoritativo antes de marcar
  `window_vblank_plan_safe`.
- Submit grafico, window flip plan ausente, window flip plan inseguro e
  window flip plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-vblank-ticket`, com `vblank_submitted=0`,
  `vblank_event_armed=0`, `vblank_event_submitted=0`,
  `vblank_callback_armed=0`, `vblank_callback_submitted=0`,
  `vblank_timestamp_captured=0`, `vblank_timestamp_submitted=0`,
  `vblank_frame_completed=0`, `vblank_frame_submitted=0`,
  `flip_submitted=0`, `flip_buffer_attached=0`,
  `flip_buffer_submitted=0`, `flip_vblank_armed=0`,
  `flip_vblank_submitted=0`, `flip_event_armed=0`,
  `flip_event_submitted=0`, `flip_async_allowed=0`,
  `flip_async_submitted=0`, `commit_submitted=0`,
  `commit_state_attached=0`, `commit_state_submitted=0`,
  `commit_atomic_allowed=0`, `commit_atomic_submitted=0`,
  `commit_callback_armed=0`, `commit_callback_submitted=0`,
  `blit_submitted=0`, `blit_dma_submitted=0`, `output_submitted=0`,
  `output_signal_submitted=0`, `display_submitted=0`,
  `display_pipeline_submitted=0`, `scanout_submitted=0`,
  `scanout_display_flip_submitted=0`, `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`,
  `surface_pixels_written=0`, `window_created=0`, `gui_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `vblank_allowed`, `vblank_ticket_selected` e `vblank_target_selected`
  preservam planejamento futuro de sincronizacao de vblank sem armar evento
  de vblank real, armar callback de vblank real, submeter callback, capturar
  timestamp real, submeter timestamp, completar frame real, submeter frame,
  anexar buffer real, armar vblank real, armar evento real, submeter flip
  async, anexar estado real, armar atomic commit real, armar callback de
  frame real, submeter callback real, mapear buffer real, copiar pixels reais,
  armar DMA real, submeter blit real, anexar conector real, armar modo real,
  armar sinal real, submeter output real, submeter sinal real, anexar
  controlador real, submeter display real, executar output real, submeter
  pipeline real, anexar buffer real, submeter scanout real, executar display
  flip real, aguardar vsync real, armar fence real, submeter wait real,
  agendar frame real, armar timer real, acordar compositor, executar page
  flip, submeter schedule real, present real, damage real, compositor real,
  surface real, window real, GUI real ou autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.204+20260513`

- `struct login_window_credential_screen_window_event_plan` consome o window
  vblank plan seguro da tela de credenciais e publica somente um ticket
  declarativo de eventos de janela para futura integracao com handlers,
  filas, dispatchers, callbacks, timestamping, frame completion, DRM/KMS,
  vblank events, compositor atomic e display sem execucao real.
- `login_window_credential_screen_window_event_plan_build()` seleciona
  tickets `credential-screen-window-event-ticket`,
  `text-recovery-window-event-ticket`,
  `text-login-resume-window-event-ticket` ou
  `text-login-fallback-window-event-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window event plan exige window vblank plan seguro com origem window
  flip/commit/blit/output/display/scanout/vsync/schedule/present/damage/
  compositor/surface disponivel e segura, vblank autorizado mas nao
  submetido, event/callback/timestamp/frame completion reais ausentes,
  buffer/vblank/evento/async reais ausentes, commit/atomic/callback reais
  ausentes, DMA real ausente, conector/modo/sinal reais ausentes,
  controlador/pipeline reais ausentes, wait/timer/page flip reais ausentes,
  tickets/targets selecionados, credenciais limpas e redigidas, callbacks de
  submit/auth zerados e login textual autoritativo antes de marcar
  `window_event_plan_safe`.
- Submit grafico, window vblank plan ausente, window vblank plan inseguro e
  window vblank plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-event-ticket`, com `event_submitted=0`,
  `event_handler_armed=0`, `event_handler_submitted=0`,
  `event_queue_armed=0`, `event_queue_submitted=0`,
  `event_dispatch_allowed=0`, `event_dispatch_submitted=0`,
  `event_callback_armed=0`, `event_callback_submitted=0`,
  `event_timestamp_captured=0`, `event_timestamp_submitted=0`,
  `event_frame_completed=0`, `event_frame_submitted=0`,
  `vblank_submitted=0`, `vblank_event_armed=0`,
  `vblank_event_submitted=0`, `vblank_callback_armed=0`,
  `vblank_callback_submitted=0`, `vblank_timestamp_captured=0`,
  `vblank_timestamp_submitted=0`, `vblank_frame_completed=0`,
  `vblank_frame_submitted=0`, `flip_submitted=0`, `flip_buffer_attached=0`,
  `flip_buffer_submitted=0`, `flip_vblank_armed=0`,
  `flip_vblank_submitted=0`, `flip_event_armed=0`,
  `flip_event_submitted=0`, `flip_async_allowed=0`,
  `flip_async_submitted=0`, `commit_submitted=0`,
  `commit_state_attached=0`, `commit_state_submitted=0`,
  `commit_atomic_allowed=0`, `commit_atomic_submitted=0`,
  `commit_callback_armed=0`, `commit_callback_submitted=0`,
  `blit_submitted=0`, `blit_dma_submitted=0`, `output_submitted=0`,
  `output_signal_submitted=0`, `display_submitted=0`,
  `display_pipeline_submitted=0`, `scanout_submitted=0`,
  `scanout_display_flip_submitted=0`, `vsync_submitted=0`,
  `vsync_wait_submitted=0`, `vsync_fence_armed=0`,
  `schedule_submitted=0`, `frame_timer_armed=0`,
  `compositor_wake_submitted=0`, `page_flip_submitted=0`,
  `present_submitted=0`, `damage_submitted=0`, `compositor_submitted=0`,
  `surface_bound=0`, `surface_memory_mapped=0`,
  `surface_pixels_written=0`, `window_created=0`, `gui_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `event_allowed`, `event_ticket_selected` e `event_target_selected`
  preservam planejamento futuro de eventos de janela sem armar handler real,
  armar fila real, despachar evento real, armar callback real, submeter
  callback, capturar timestamp real, submeter timestamp, completar frame
  real, submeter frame, armar evento de vblank real, armar callback de
  vblank real, submeter callback, capturar timestamp real, submeter
  timestamp, completar frame real, submeter frame, anexar buffer real, armar
  vblank real, armar evento real, submeter flip async, anexar estado real,
  armar atomic commit real, armar callback de frame real, submeter callback
  real, mapear buffer real, copiar pixels reais, armar DMA real, submeter
  blit real, anexar conector real, armar modo real, armar sinal real,
  submeter output real, submeter sinal real, anexar controlador real,
  submeter display real, executar output real, submeter pipeline real,
  anexar buffer real, submeter scanout real, executar display flip real,
  aguardar vsync real, armar fence real, submeter wait real, agendar frame
  real, armar timer real, acordar compositor, executar page flip, submeter
  schedule real, present real, damage real, compositor real, surface real,
  window real, GUI real ou autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.205+20260513`

- `struct login_window_credential_screen_window_input_plan` consome o window
  event plan seguro da tela de credenciais e publica somente um ticket
  declarativo de input de janela para futura integracao com drivers de
  teclado, pointer, foco, keymap, decoder, roteador, callbacks e grab sem
  execucao real.
- `login_window_credential_screen_window_input_plan_build()` seleciona
  tickets `credential-screen-window-input-ticket`,
  `text-recovery-window-input-ticket`,
  `text-login-resume-window-input-ticket` ou
  `text-login-fallback-window-input-ticket` sem carregar segredo, mascara,
  comprimento, snapshots internos ou ponteiros de callback.
- O window input plan exige window event plan seguro com origem window
  vblank/flip/commit/blit/output/display/scanout/vsync/schedule/present/
  damage/compositor/surface disponivel e segura, event/handler/queue/
  dispatch/callback/timestamp/frame reais ausentes, vblank reais ausentes,
  flip async real ausente, commit/atomic/callback reais ausentes, DMA real
  ausente, output/display/scanout reais ausentes, vsync/fence/wait reais
  ausentes, schedule/timer/page flip reais ausentes, present/damage/
  compositor/surface/window/GUI reais ausentes, tickets/targets
  selecionados, credenciais limpas e redigidas, callbacks de submit/auth
  zerados e login textual autoritativo antes de marcar
  `window_input_plan_safe`.
- Submit grafico, window event plan ausente, window event plan inseguro e
  window event plan com efeitos reais permanecem fail-closed em
  `text-login-fallback-window-input-ticket`, com `input_submitted=0`,
  `input_keyboard_armed=0`, `input_keyboard_submitted=0`,
  `input_pointer_armed=0`, `input_pointer_submitted=0`,
  `input_focus_armed=0`, `input_focus_submitted=0`,
  `input_keymap_loaded=0`, `input_keymap_submitted=0`,
  `input_decode_submitted=0`, `input_route_submitted=0`,
  `input_callback_armed=0`, `input_callback_submitted=0`,
  `input_grab_allowed=0`, `input_grab_submitted=0`,
  `event_submitted=0`, `event_handler_armed=0`,
  `event_handler_submitted=0`, `event_queue_armed=0`,
  `event_queue_submitted=0`, `event_dispatch_submitted=0`,
  `event_callback_armed=0`, `event_callback_submitted=0`,
  `event_timestamp_captured=0`, `event_timestamp_submitted=0`,
  `event_frame_completed=0`, `event_frame_submitted=0`,
  `submit_enabled=0` e `auth_attempt_allowed=0`.
- `input_allowed`, `input_ticket_selected` e `input_target_selected`
  preservam planejamento futuro de input de janela sem armar teclado real,
  submeter teclado, armar pointer real, submeter pointer, armar foco real,
  submeter foco, carregar keymap real, submeter keymap, decodificar input
  real, rotear input real, armar callback de input, submeter callback de
  input, permitir grab real, submeter grab, armar handler real, armar fila
  real, despachar evento real, armar callback real, submeter callback,
  capturar timestamp real, submeter timestamp, completar frame real,
  submeter frame, armar evento de vblank real, anexar buffer real, armar
  vblank real, armar evento real, submeter flip async, anexar estado real,
  armar atomic commit real, mapear buffer real, copiar pixels reais, armar
  DMA real, submeter blit real, anexar conector real, armar modo real, armar
  sinal real, submeter output real, anexar controlador real, submeter
  display real, anexar buffer real, submeter scanout real, executar display
  flip real, aguardar vsync real, armar fence real, submeter vsync real,
  submeter wait real, agendar frame real, armar timer real, acordar
  compositor, executar page flip, submeter schedule real, present real,
  damage real, compositor real, surface real, window real, GUI real ou
  autenticar pela GUI neste patch.

## Incremento `0.8.0-alpha.206+20260513`

- Fora da pipeline declarativa: este incremento endurece o **caminho real**
  de autenticacao por senha que a loginwindow GUI futura ira consumir, sem
  introduzir efeitos graficos, IO bloqueante, alteracoes de ABI ou mudancas
  no formato de `/etc/passwd`.
- `userdb_authenticate()` (`src/auth/user.c`) passa a comparar o hash
  PBKDF2-SHA256 da senha digitada com o hash armazenado via
  `crypt_constant_time_compare` (`src/security/crypt.c`), eliminando o
  timing side-channel do laco byte-a-byte com saida antecipada que ja
  existia. A funcao agora zera os buffers locais `hash[USER_HASH_SIZE]` e
  `struct user_record rec` antes de qualquer retorno, reduzindo a janela de
  remanencia de material sensivel em stack.
- `config_log_user_record_state()`
  (`src/config/first_boot/storage_users.c`) deixa de imprimir o salt e o
  hash PBKDF2 do administrador no log de bootstrap. O log emite agora
  apenas `salt=[redacted size=N present=0|1]` e
  `hash=[redacted size=N present=0|1]`, fechando o vetor offline em que um
  atacante com leitura do log de boot poderia recuperar
  `(salt, hash)` e tentar brute force contra senhas fracas. O helper
  `bytes_to_hex_str`, antes usado somente nesse log, foi substituido por
  `bytes_have_nonzero`, que confirma presenca de material sem revelar
  qualquer byte.
- `tests/test_crypt_vectors.c` ganhou
  `test_constant_time_compare_semantics`, registrado em
  `run_crypt_vector_tests`, cobrindo buffers iguais, mismatch no primeiro
  byte, mismatch no ultimo byte e comprimento zero. A garantia subjacente
  da qual `userdb_authenticate` depende agora tem cobertura estatica
  dedicada.
- Invariantes preservadas: o formato e o conteudo de `/etc/passwd`
  continuam inalterados; releases anteriores/posteriores autenticam contra
  esta release; PBKDF2 com 64000 iteracoes continua sendo a primitiva de
  derivacao; o login textual continua autoritativo enquanto o loginwindow
  GUI real nao for entregue.
- Este patch nao altera o estado da pipeline declarativa fail-closed
  (`window_surface_plan` ... `window_input_plan`) e nao destrava nenhum
  dos entregaveis grafico/smoke pendentes da Etapa 2; ele apenas eleva o
  piso de seguranca do **backend** que a GUI futura ira invocar.

## Incremento `0.8.0-alpha.207+20260513`

- Fora da pipeline declarativa: complementa `alpha.206` integrando o
  sistema de lockout (`auth_policy`) ao caminho real de autenticacao
  por senha. Antes, `auth_policy_check_allowed/record_*` existiam mas
  eram invocadas manualmente pelo caller (`system_setup.c::login`), o
  que deixava aberto o risco de uma futura porta de entrada (por
  exemplo o submit GUI da loginwindow) esquecer um dos passos e burlar
  o lockout.
- Novo `userdb_authenticate_with_policy()` (`src/auth/user.c`) compoe
  em ordem: `auth_policy_check_allowed(username)` (refusa cedo se a
  conta esta bloqueada, sem queimar PBKDF2); `userdb_authenticate(
  username, password, out)` (caminho timing-equalizado contra user
  enumeration por `k_userdb_dummy_salt`);
  `auth_policy_record_success(username)` em sucesso ou
  `auth_policy_record_failure(username)` em falha. Retorna
  `USERDB_AUTH_OK`, `USERDB_AUTH_LOCKED` ou `USERDB_AUTH_FAILED`,
  declarados em `include/auth/user.h` como ABI publica estavel.
- `include/auth/user.h` ganha as constantes `USERDB_AUTH_OK`,
  `USERDB_AUTH_FAILED`, `USERDB_AUTH_LOCKED` (`0/-1/-2`) e o prototipo
  `userdb_authenticate_with_policy`, fechando a ABI publica que estava
  incompleta — `user.c` ja definia o wrapper, mas o header nao o
  expunha, o que tornava o build inconsistente para qualquer caller
  externo que tentasse usa-lo.
- `src/config/system_setup.c::login` migra para
  `userdb_authenticate_with_policy` em uma unica linha, com `switch`
  sobre o codigo de retorno. A UI preserva as tres mensagens
  distintas (credenciais ausentes, conta bloqueada, credencial
  invalida) sem repetir manualmente o trio
  `check_allowed`/`authenticate`/`record_*`. O wipe da senha agora
  ocorre em um unico ponto, imediatamente apos a chamada, reduzindo
  caminhos onde o buffer poderia escapar esquecido.
- `Makefile` linha 1109 (`TEST_SRCS`) tinha o segmento corrupto
  `tests/test_net_probe.c src/driverslogin_window_gui_layout.c
  src/auth//net/net_probe.c src/drivers/net/netvsc.c` (path mesclado
  sem `/` e duplo `/` antes de `net`) que quebrava o build host-side
  dos testes. O patch restaura `tests/test_net_probe.c
  src/drivers/net/net_probe.c src/drivers/net/netvsc.c`, desbloqueando
  o build.
- `tests/test_auth_policy.c` ganha um bloco contratual fixando
  `USERDB_AUTH_OK == 0`, `USERDB_AUTH_FAILED == -1`,
  `USERDB_AUTH_LOCKED == -2` e checagens de unicidade — drift desses
  valores faria a UX confundir "conta bloqueada" com "credencial
  invalida" silenciosamente.
- Invariantes preservadas: ABI de `userdb_authenticate` inalterada
  (mesmos codigos 0/-1 e mesma semantica); formato de `/etc/passwd`
  inalterado; callers system-internal de admin verify
  (`kernel_shell_runtime.c`, `config/first_boot/program.c`) continuam
  chamando `userdb_authenticate` diretamente (nao devem trigger
  lockout); login textual segue autoritativo.
- Este patch nao altera a pipeline declarativa fail-closed
  (`window_surface_plan` ... `window_input_plan`) e nao destrava
  nenhum dos entregaveis grafico/smoke pendentes; ele endurece o
  caminho real de autenticacao que o submit da loginwindow GUI futura
  vai consumir e desbloqueia o build de testes do host.

## Incremento `0.8.0-alpha.208+20260513`

- Fora da pipeline declarativa: consolida o hardening criptografico em
  quatro frentes paralelas, todas no caminho real que a loginwindow
  GUI futura ira consumir. Continua a trilha iniciada em `alpha.206`
  e `alpha.207`, agora no nivel das primitivas (CSPRNG, SHA-256,
  tabela do `auth_policy`) e do wipe hygiene do `userdb_set_password`.
- **CSPRNG snapshot leak fechado.** `src/security/csprng.c::
  csprng_get_bytes` agora chama `sha256_clear(&temp_ctx)` ao final de
  cada iteracao do laco de emissao. Antes, `temp_ctx` (copia do pool
  de entropia com o digest finalizado dentro) permanecia vivo na
  stack apos `sha256_final`: o campo `state[]` continha o proprio
  digest emitido (ou seja, os bytes que foram entregues ao caller) e
  `data[]` continha o ultimo bloco padded. Qualquer info-leak de
  stack subsequente (dump de panic, use-after-pop, leitura de stack
  adjacente) poderia recuperar esses bytes. O wipe explicito do
  `digest[32]` ja existia; o do `temp_ctx` faltava.
- **`sha256_clear` vira API publica.** `include/security/sha256.h`
  declara `void sha256_clear(struct sha256_ctx *ctx)` com semantica
  volatile-safe (loop com `volatile uint8_t *`), documentada como
  "zera todo o contexto, seguro contra dead-store elimination,
  no-op em NULL". A implementacao em `src/security/sha256.c` ja
  existia internamente, mas o header nao a expunha; agora todos os
  modulos podem chamar.
- **`auth_policy` reads non-allocating.** `src/auth/auth_policy.c`
  ganha `find_existing(username)` (read-only, retorna NULL se
  username nao esta rastreado). Os caminhos
  `auth_policy_check_allowed`, `auth_policy_is_locked`,
  `auth_policy_record_success` e `auth_policy_unlock` migram para
  `find_existing` — antes todos usavam `find_or_alloc`, que **criava
  uma entrada nova** mesmo para usuarios desconhecidos. Isso abria um
  ataque onde probing read-only com `AUTH_MAX_TRACKED_USERS+1`
  usernames forjados exauria `g_attempts[32]`, e a partir dai
  `find_or_alloc` retornava NULL para usuarios legitimos →
  `record_failure` sem efeito → lockout **silenciosamente desabilitado**
  para esses usuarios. Com `find_existing`, probing read-only e
  zero-impact na tabela.
- **`auth_policy` LRU eviction.** `find_or_alloc` (usado apenas por
  `record_failure`) ganha logica de eviction quando a tabela esta
  cheia: reclama lockouts naturalmente expirados, depois evicta a
  entrada **nao-bloqueada** com menor `last_fail_tick`. Entradas
  bloqueadas sao **stickys** — evictar um lockout ativo permitiria
  ao atacante resetar seu proprio bloqueio fazendo spray de
  usernames novos. Se todos os 32 slots estao locked simultaneamente
  (cenario de ataque massivo), retorna NULL, e a tabela se recupera
  naturalmente quando os lockouts expiram pelo tempo configurado em
  `lockout_duration_ticks` (padrao 300s). Audit log de
  `record_success` foi movido para antes da busca, preservando o
  log "Login success" para primeiros logins de usuarios nunca
  rastreados.
- **`userdb_set_password` wipe completo.** `src/auth/user.c::
  userdb_set_password` ganha wipes sistematicos em todas as paths
  de retorno: `!out` (`memory_zero(source, source_len)` antes de
  kfree), serialize-fail (`memory_zero` para `line`, `&rec`, `out`,
  `source`), por-iteracao apos copia (`memory_zero(line)`,
  `memory_zero(&rec)` ao inves do parcial `rec.salt`/`rec.hash`),
  `!updated` (`memory_zero(source)` ao lado do `memory_zero(out)`),
  e sucesso (`memory_zero(source)`). `source` carrega o
  `/etc/passwd` inteiro com salt+hash hex de todos os usuarios em
  ASCII; sem wipe, o heap freed retinha esses dados ate ser reusado
  pelo proximo kalloc, deixando o material de credenciais exposto
  via heap-info-leak. `line[]` carrega o registro serializado com
  salt_hex+hash_hex do usuario corrente; igualmente sensivel.
- **`memory_zero` volatile-safe.** `src/auth/user.c::memory_zero`
  passa a usar `volatile uint8_t *p`. Sem o `volatile`, o compilador
  pode provar que os stores sao dead (ninguem le o buffer depois,
  ele e kfreed ou sai de escopo) e eliminar a chamada inteira — o
  padrao classico que silenciosamente otimiza wipes de seguranca
  em C.
- **Cobertura estatica nova.**
  `tests/test_crypt_vectors.c::test_sha256_clear_semantics` valida
  que o contexto fica zerado byte-a-byte apos `sha256_clear` e que
  `sha256_clear(NULL)` e no-op seguro.
  `tests/test_auth_policy.c` ganha dois blocos contratuais: read
  paths non-allocating (probing 37 usernames distintos via
  `check_allowed`/`is_locked` deixa a tabela vazia) e LRU eviction
  (32 falhas distintas + 1 newcomer evicta o LRU `fill-00`).
- Invariantes preservadas: ABI publica de
  `csprng_get_bytes`/`auth_policy_*`/`userdb_set_password` inalterada;
  formato de `/etc/passwd` inalterado; comportamento observavel para
  o usuario final identico (login, lockout, set_password, audit log);
  PBKDF2 com 64000 iteracoes continua a primitiva de derivacao;
  login textual continua autoritativo.
- Este patch nao altera a pipeline declarativa fail-closed
  (`window_surface_plan` ... `window_input_plan`) e nao destrava
  nenhum dos entregaveis grafico/smoke pendentes; ele endurece as
  primitivas e o gerenciamento de estado sensivel que sustentam o
  caminho real de autenticacao por senha que o submit da loginwindow
  GUI futura ira consumir.

## Incremento `0.8.0-alpha.209+20260513`

- Fora da pipeline declarativa: propaga o uso de `sha256_clear`
  (API publica desde `alpha.208`) para **todos** os consumidores reais
  do kernel/userland que processam segredos via contextos SHA-256
  transitorios. Continua a trilha iniciada em `alpha.206` (constant-time
  compare), `alpha.207` (lockout integrado), `alpha.208` (CSPRNG wipe +
  sha256_clear publico + auth_policy reads non-allocating +
  userdb_set_password wipe completo) — agora cobre a "longa cauda" de
  sites que ainda deixavam o contexto SHA-256 vivo na stack apos
  `sha256_final`.
- **`src/security/crypt.c::hmac_sha256` (static, usado por PBKDF2).**
  Caminho quente de cada login: PBKDF2-SHA256 com 64000 iteracoes
  invoca esta funcao uma vez por iteracao, e cada chamada faz
  `sha256_final` duas vezes (inner HMAC + outer HMAC). Antes, o `ctx`
  reutilizado pelas duas camadas e o `key_ctx` (criado quando key >
  `SHA256_BLOCK_SIZE`) ficavam vivos na stack apos cada chamada.
  Agora: `key_ctx` foi declarada fora do bloco condicional com flag
  `key_ctx_used` e e zerada via `sha256_clear(&key_ctx)` quando usada;
  `ctx` e zerada via `sha256_clear(&ctx)` antes dos `secure_clear`
  finais.
- **`src/security/crypt.c::crypt_hmac_sha256` (API publica de HMAC).**
  Mesmo padrao do HMAC static, mas exposto via header. `ctx` e
  reutilizada tres vezes (key hash opcional, inner HMAC, outer HMAC)
  e ganha wipe via `sha256_clear(&ctx)` apos a ultima fase. Apos o
  `sha256_final` da camada outer, `state[]` continha o MAC produzido
  (que IS `out`) e `data[]` continha o ultimo bloco padded.
- **`src/security/sha256.c::sha256_hash` (convenience wrapper).**
  O wrapper init → update → final cria `ctx` no stack frame do caller,
  processa o input, finaliza em `hash`, e antes saia sem wipe. Agora
  chama `sha256_clear(&ctx)` apos `sha256_final`. Este wrapper e
  usado em varios lugares do codigo (key storage, signing helpers,
  shell utilities, debug fingerprints); todos os call sites passam a
  ter wipe automatico sem mudanca local de codigo.
- **`src/arch/x86_64/kernel_volume_runtime/key_storage_probe.c::
  compute_volume_key_hash` (trilha de cripto de disco).** A senha
  normalizada do volume cifrado e hasheada para gerar `out_hash` (o
  digest e o segredo que gate a derivacao XTS das chaves do volume).
  Antes, `ctx` ficava na stack do caller (caminho de boot) com o
  digest em `state[]` e o bloco final padded em `data[]` (derivado
  diretamente da senha do volume). Agora chama `sha256_clear(&ctx)`
  antes de retornar 0. Caminho extremamente sensivel porque ocorre
  durante boot e o `state[]` resultante revela o digest da senha.
- **Limites deste slice.** Nao toca `src/security/ed25519.c` (que e
  um stub simplificado/aproximado de Ed25519 documentado como "SHA-512
  approximation using double SHA-256"), porque o wipe defensivo nele
  requer um slice dedicado de correctness review. Nao introduz
  `secure_clear` como API publica geral — mantem o padrao
  static-helper em `crypt.c`, com `sha256_clear` como a API
  especifica para wipe de SHA-256 contexts.
- **Invariantes preservadas.** ABI publica de `crypt_pbkdf2_sha256`,
  `crypt_hmac_sha256`, `sha256_hash` e `compute_volume_key_hash`
  inalterada. Saida funcional identica (mesmos bytes para mesmos
  inputs). Vetores oficiais (NIST/RFC) continuam passando — o wipe
  acontece DEPOIS do `sha256_final`, sem interferir no resultado.
  Volume cifrado existente continua decifrando com a mesma senha.
  `/etc/passwd` continua sendo aceito com a mesma derivacao PBKDF2.
- **Desempenho.** `sha256_clear` e um loop de 104 stores volatile
  (uma cache line + alguns bytes). Em PBKDF2 × 64000 iteracoes,
  custa ~64000 × 2 × 104 = 13 MB de stores volatile no total. Para
  um login completo (~50–200 ms de PBKDF2 em CPU tipica), o overhead
  e submilisegundo. Custo desprezivel.
- Este patch nao altera a pipeline declarativa fail-closed e nao
  destrava entregaveis grafico/smoke pendentes; e puramente defensivo
  sobre as primitivas de cripto.

## Incremento `0.8.0-alpha.210+20260513`

- Fora da pipeline declarativa: fecha um vetor critico de
  comprometimento no canal de atualizacoes do sistema.
  `src/security/ed25519.c::ed25519_verify` em versoes <= alpha.209
  era matematicamente quebrado e era o unico gate criptografico
  para aceitar manifests de update.
- **A equacao quebrada.** A "verificacao" anterior reduzia-se a
  checar `signature[32+i] == signature[i] + (hram[i] XOR
  ed25519_hash(public_key)[i])`. Qualquer atacante satisfaz essa
  equacao sem private key: (1) pegue qualquer
  `signature[0..31]`, (2) compute `hram = SHA-256(signature[0..31]
  || public_key || message)` — todos os inputs sao publicos, (3)
  compute `hash = ed25519_hash(public_key)`, (4) defina
  `signature[32+i] = signature[i] + (hram[i] XOR hash[i])`. Nenhuma
  multiplicacao escalar na curva, nenhum private key, nenhuma
  propriedade de RFC 8032.
- **O ponto de exploracao.**
  `src/services/update_agent.c::manifest_signature_ed25519_valid`
  usava `ed25519_verify` como o UNICO gate em 7 caminhos:
  poll/staged/imported manifests, dois caminhos pre-install, status
  reporting do `signature_ready`, validacao do payload cache contra
  manifest. Qualquer atacante com escrita no canal de update (MitM,
  repositorio comprometido, escrita local) podia forjar um manifest
  com payload arbitrario e instalar codigo como "update oficial".
- **A decisao: fail-closed.** Implementar Ed25519 real (RFC 8032) e
  ~2000 linhas de aritmetica de campo em tempo constante,
  compressao/descompressao de pontos, multiplicacao escalar e
  SHA-512 verdadeiro — fora do escopo deste slice. Sistema em
  alpha pre-release sem manifests produtivos. Fail-closed e a
  unica posicao segura.
- **`src/security/ed25519.c::ed25519_verify`** retorna -1
  incondicionalmente com comentario SECURITY de ~30 linhas
  detalhando o ataque trivial e o impacto no update_agent.
- **`src/security/ed25519.c::ed25519_sign`** zera o buffer
  `signature` via `volatile uint8_t *` (resistente a dead-store
  elimination). Inputs (`message`, `public_key`, `private_key`)
  intocados; NULL signature tolerado.
- **`src/security/ed25519.c::ed25519_create_keypair`** zera ambos
  `public_key` (32 bytes) e `private_key` (64 bytes) via
  `volatile uint8_t *`. Seed intocado; NULL output buffers
  tolerados.
- **`src/security/ed25519.c::ed25519_hash`** (helper interno,
  double SHA-256 com pad 0x36 — nao e SHA-512 real) marcado
  `__attribute__((unused))` com comentario explicando que esta
  dormente ate a implementacao real de RFC 8032 e que deve ser
  substituido por SHA-512 verdadeiro antes de reativacao.
- **`include/security/ed25519.h`** ganha SECURITY WARNING explicito
  no topo do header documentando o estado placeholder, com
  `/* Placeholder: ... */` em cada declaracao de funcao.
- **`src/services/update_agent.c::manifest_signature_ed25519_valid`**
  ganha comentario explicando que o gate e intencionalmente
  fail-closed em producao, e que o path UNIT_TEST via
  `g_update_manifest_verifier` continua sendo a unica forma
  suportada de aceitar manifests em testes. Sem mudanca de codigo
  no callsite — `ed25519_verify` agora sempre retorna -1, entao
  `== 0` sempre falha, manifest rejeitado.
- **`tests/test_crypt_vectors.c::test_ed25519_failclosed_contract`**
  teste contratual novo cobrindo: `ed25519_verify` retorna -1 para
  inputs nao-zero, zero, e mensagem vazia (NULL pointer); `ed25519_sign`
  zera os 64 bytes do signature mas nao escreve alem (sentinel 0x77 em
  8 bytes posteriores preservado); `ed25519_create_keypair` zera ambos
  32+64 bytes; NULLs sao tolerados sem crash. Adicionado a
  `run_crypt_vector_tests`.
- **Tests existentes preservados.** `tests/test_update_agent.c`,
  `tests/test_update_transact.c`, `tests/test_audit_events.c`
  usam o hook `g_update_manifest_verifier` (verificado via grep) e
  nao chamam `ed25519_verify` diretamente. Continuam funcionais
  sem mudanca.
- **Invariantes preservadas.** ABI publica de `ed25519_verify`,
  `ed25519_sign`, `ed25519_create_keypair` inalterada (mesmas
  assinaturas, mesmos tipos). Manifests produtivos nao existem
  (alpha pre-release), entao a rejeicao de 100% nao quebra
  usuarios reais. Esqueleto de aritmetica de curva (`fe25519`,
  `ge25519`, `fe_mul`, `fe_pow2523`, etc.) preservado como ponto
  de partida para implementacao futura de RFC 8032 (cada helper
  ja estava marcado `__attribute__((unused))` antes desta entrega).
- **Desempenho.** Caminho de update rejeita manifests em O(1)
  (`return -1` imediato). Menor que a versao anterior que
  executava multiplos `sha256_init/update/final` antes de retornar
  "valid".
- Este patch nao altera a pipeline declarativa fail-closed e nao
  destrava entregaveis grafico/smoke pendentes; e uma correcao
  estrutural na cadeia de atualizacoes.

## Incremento `0.8.0-alpha.211+20260513`

- Fora da pipeline declarativa: fecha duas fugas de privacidade reais
  na trilha de autenticacao por senha. Descoberta durante uma
  auditoria de paths que tocam nomes de usuario.
- **Fuga A: `auth_policy_status` listava usernames.**
  `src/auth/auth_policy.c::auth_policy_status` e o backend do comando
  shell `auth-status` (em
  `src/shell/commands/extended.c::cmd_auth_status`), que nao tem
  nenhum check de privilegio. A funcao iterava todo o array
  `g_attempts[]` e imprimia, para cada slot ocupado, o username + a
  contagem de falhas + o estado de lockout. Qualquer sessao local —
  incluindo user comum, recuperacao e guest — podia rodar
  `auth-status` e enumerar quem usa o sistema, ler quais contas tem
  falhas pendentes e identificar contas atualmente bloqueadas.
- **Tres vetores praticos.** (1) Enumeracao passiva de usuarios; (2)
  sinal de strategy (atacante sabe quais alvos estao no limiar do
  lockout); (3) lockout escape (atacante sabe quando o janelamento
  expira para retomar credential stuffing contra alvo especifico).
- **Fuga B: `priv_log_emit` anexava username em `[priv]` logs.**
  `src/auth/privilege.c::priv_log_emit` (chamado por
  `privilege_log_denied` e `privilege_log_granted`) adicionava
  `actor=<username>` em todo log emitido para `klog`. Cada denial de
  UI/shell — `apps/settings.c:151::settings-add-user`,
  `shell/commands/user_manage.c:174::denial generico`,
  `shell/commands/user_manage.c:375::set-pass:other` — chamava
  `privilege_log_denied(action, session_active()->user)`, levando o
  username completo do principal para o klog ring.
- **Escopo do log.** `klog` e consumido por qualquer ferramenta com
  acesso ao log pipe; o ring inteiro e serializado em panic buffers
  e crash dumps. O username escapava bem alem do subsistema de auth.
- **`src/auth/auth_policy.c::auth_policy_status`.** Colapsa o loop
  por username em duas linhas agregadas `Tracked accounts: <N>
  (locked: <M>)`. Comentario PRIVACY de ~25 linhas no corpo
  documenta o threat model (enumeracao, sinal de strategy, lockout
  escape) e por que counts agregados preservam o valor de operacao
  sem PII. A configuracao publica (`max_attempts`,
  `min_password_length`, `audit`) continua sendo emitida — sao
  parametros, nao PII.
- **`src/auth/auth_policy.c::auth_policy_tracked_count`,
  `auth_policy_locked_count`.** Novas funcoes publicas que retornam
  o numero de slots ocupados / bloqueados em `g_attempts[]`. Usadas
  pelo proprio `auth_policy_status` (privacy-preserving) e por
  diagnosticos/dashboards que precisam de uma visao agregada nao
  identificante.
- **`src/auth/auth_policy.c::auth_policy_is_tracked`.** Exposta
  apenas sob `#if defined(UNIT_TEST)` (no `.c` e no header). Retorna
  1 se o username tem slot alocado, 0 senao. Permite que tests
  validem rastreamento de username especifico sem expor essa
  capacidade em producao (onde poderia ser vetor de enumeracao
  similar ao do `auth_policy_status` original).
- **`src/auth/privilege.c::priv_log_emit`.** Substitui o trecho que
  anexava `actor=<username>` pelo equivalente `actor_role=<role>`. O
  role e um identificador de classe (admin, user, ...) ligado a
  politica de privilegio do sistema, nao um identificador unico do
  principal. Comentario PRIVACY de ~20 linhas no corpo detalha os
  callsites afetados e o trade-off (sinal de auditoria preservado:
  "denial spike from non-admin role" continua acionavel; PII
  removido). Defensive: quando `actor->role[0]` e vazio, omite a
  suffix; quando `actor == NULL`, omite a suffix sem crash.
- **`include/auth/auth_policy.h`.** Declara
  `auth_policy_tracked_count`/`auth_policy_locked_count` na API
  publica e `auth_policy_is_tracked` sob `#if defined(UNIT_TEST)`.
  Documenta no header o contrato privacy-preserving do
  `auth_policy_status`.
- **`tests/test_auth_policy.c`.** Asserts que grepavam o status
  output por usernames (`"newcomer"`, `"fill-00"`) migrados para
  `auth_policy_is_tracked(...)`. Asserts negativos novos validam o
  contrato (`strstr(status, "<username>") == NULL`). Asserts
  agregados validam `auth_policy_tracked_count`/`locked_count` apos
  saturacao da tabela.
- **`tests/test_privilege.c::test_privilege_log_omits_username`**.
  Teste contratual novo que captura o `klog` ring via
  `capture_klog` (incluindo `kernel/log/klog.h`) e valida:
  - `denied/granted` nao contem nenhum username (`alice`, `carol`);
  - `denied/granted` contem `actor_role=admin` quando role esta
    populado;
  - `denied` com `actor->role[0] == 0` omite `actor_role=` mas
    mantem `verdict: action`;
  - `denied` com `actor == NULL` nao crasha e emite verdict/action.
- **Invariantes preservadas.** ABI publica de
  `auth_policy_init/set_config/check_allowed/record_success/
  record_failure/is_locked/unlock/validate_password` inalterada. ABI
  de `auth_policy_status`/`privilege_log_denied`/`granted` inalterada
  nas assinaturas; output textual do callback `print` e do `klog`
  muda. Consumidores que parseavam `auth-status` ou `[priv]` lines
  precisam adaptar — esses formatos eram canais de log, nao
  interfaces estaveis.
- **Desempenho.** `auth_policy_status` mais barato (O(N) com 2
  prints em vez de O(N) com 4 prints por slot). `tracked_count` e
  `locked_count` cada uma e O(AUTH_MAX_TRACKED_USERS = 32). Custo
  desprezivel. `priv_log_emit`: mesma O(line length) que antes.
- **Limites.** Nao adiciona controle de acesso ao comando shell
  `auth-status` — o comando continua disponivel para qualquer
  sessao, agora apenas com payload sem PII. Adicionar restricao
  admin em `cmd_auth_status` seria estritamente mais conservador,
  mas mudaria a UX de diagnostico para usuarios legitimos sem
  aumentar a postura de seguranca (o output ja nao tem PII). Nao
  remove o uso de `klog` para eventos `[priv]`; um audit log
  separado, com controle de acesso proprio e politica de retencao
  distinta, e um slice futuro. Nao altera o tracking interno em
  `g_attempts[]` — a tabela continua guardando username em memoria
  para o algoritmo de lockout funcionar; a mudanca e apenas no que
  sai pelos canais de observabilidade.
- Este patch nao altera a pipeline declarativa fail-closed e nao
  destrava entregaveis grafico/smoke pendentes; e uma correcao
  estrutural de privacidade na cadeia de auditoria.

## Incremento `0.8.0-alpha.212+20260513`

- Fora da pipeline declarativa: fecha um timing side-channel
  critico em `src/auth/user.c::userdb_authenticate_with_policy`
  (a API publica de login). Sequencia direta da composicao alpha.206
  + alpha.211: ambas removeram informacao sobre lockouts e
  existencia de accounts dos canais explicitos (status, logs), mas
  o wall-clock da resposta da API ainda diferenciava as classes.
- **Antes.** O wrapper ordenava as operacoes assim:
  1. `auth_policy_check_allowed(username)` (O(1) lookup,
     microssegundos).
  2. Se 0, retornar `USERDB_AUTH_LOCKED` IMEDIATAMENTE.
  3. Caso contrario, `userdb_authenticate(username, password, out)`
     (~50 ms de PBKDF2 com 64000 iteracoes).
  4. Branch para `record_success`/`record_failure` e retornar.
  Resultado: account locked = ~1 us; account not-locked = ~50 ms.
  Diferenca de 50000x facilmente observavel via RTT mesmo em rede
  com jitter significativo.
- **Tres consequencias praticas.** (1) Enumeracao remota de accounts
  locked: um probe por username candidato, classifica via latencia
  com confianca quase perfeita. (2) Lockout escape: monitorar
  timing de uma conta especifica ate ela passar a responder
  lentamente — janelamento expirou, retomar credential stuffing.
  (3) Triangulacao com alpha.211: mesmo apos `auth-status` parar de
  listar lockouts e `[priv]` logs pararem de vazar username, o
  conjunto de contas locked era reconstruivel via timing.
- **Mitigacao.** Inversao da ordem:
  1. Capturar `allowed = auth_policy_check_allowed(username)` (nao
     branchar ainda).
  2. Sempre executar `userdb_authenticate(username, password, out)`
     (~50 ms invariante, herdando a equalizacao
     existent-vs-non-existent que alpha.206 instalou via
     `k_userdb_dummy_salt`).
  3. Se `!allowed`, descartar `rc`, zerar `*out` via
     `user_record_clear` e retornar `USERDB_AUTH_LOCKED`.
  4. Caso contrario, branch normal para record success/failure.
- **`user_record_clear(out)` defensivo no caminho locked.** Cobre o
  caso em que o atacante adivinhou a senha correta de uma conta
  locked (legitima vazada por outro canal, ex. backup
  comprometido). Nesse caso `userdb_authenticate` populou `*out`
  com o registro real. O wrapper agora apaga isso antes de retornar
  LOCKED, garantindo que o registro nao escapa para o caller
  enquanto a conta esta no janelamento.
- **`auth_policy_record_failure` NAO e chamado para accounts
  locked.** O `lockout_until_tick` ja esta ancorado no momento em
  que `failed_count >= max_attempts` foi atingido; incrementar
  ainda mais o contador nao estende o janelamento (que e
  determinado por `last_fail_tick + lockout_duration_ticks` capturado
  no momento do lock). Incrementar o contador apenas poluiria a
  metrica para o operador que olha as estatisticas — atacante
  gerando volume de tentativas durante o lockout faria parecer que
  o usuario legitimo errou muito.
- **Comentario SECURITY de ~40 linhas.** Documenta no proprio
  `src/auth/user.c::userdb_authenticate_with_policy` o threat
  model, a composicao com alpha.206 e alpha.211, o rationale do
  rate-limiting inerente, o rationale de nao chamar
  `record_failure` quando ja locked, e o wipe defensivo de `out`.
- **`tests/test_auth_policy.c`.** Novo bloco alpha.212 que valida o
  lado policy do contrato: `auth_policy_check_allowed` continua
  retornando 0 para account locked e 1 para account fresh. O
  contrato de timing equalization em si fica em revisao de codigo
  porque `src/auth/user.c` nao esta no host-side test binary
  (depende de VFS/kmem); o sinal estatico — chamada de
  `userdb_authenticate(username, password, out)` ABOVE the
  `if (!allowed)` branch — e o comentario SECURITY juntos travam o
  contrato.
- **Invariantes preservadas.** ABI publica de
  `userdb_authenticate_with_policy` inalterada (mesma assinatura,
  mesmos codigos `USERDB_AUTH_OK/FAILED/LOCKED`, locked por
  contrato em `test_auth_policy.c`). `system_setup.c` continua
  branching identicamente em rc. Callers GUI futuros nao precisam
  saber da mudanca.
- **Desempenho.** Custo de probe contra account locked sobe de
  ~1 us para ~50 ms. Para usuarios reais que tentam login com
  conta propria locked, a UI ve a mesma mensagem "conta
  bloqueada" apos ~50 ms em vez de instantaneamente —
  imperceptivel. Para atacante, o custo de enumeracao subiu
  proporcionalmente: 32 accounts × 50 ms = 1.6 s para varrer a
  tabela inteira de tracking, e ainda assim sem distincao
  locked-vs-not-locked.
- **Limites.** Nao fecha o canal de tempo dentro de `find_existing`
  (que tem early-exit no primeiro match). Mas esse residuo de
  timing e dominado pelos ~50 ms de PBKDF2 que vem depois — para
  um atacante remoto a janela e indistinguivel. Constant-time
  string compare em usernames seria estritamente mais conservador
  mas e diminishing returns dado que usernames sao publicos por
  natureza (ja sao mostrados na UI de login). Nao remove o uso de
  `klog` para `[auth] Login failure recorded.` (sem PII desde
  inicio, OK). Nao altera a tabela `g_attempts[]` (continua
  guardando username em memoria para o algoritmo de lockout
  funcionar; a privacy desse dado e tratada pelo alpha.211).
- Este patch nao altera a pipeline declarativa fail-closed e nao
  destrava entregaveis grafico/smoke pendentes; e uma correcao
  estrutural de canal de tempo na API publica de auth.

## Incremento `0.8.0-alpha.213+20260513`

- Fora da pipeline declarativa: introduz **HKDF-SHA256 (RFC
  5869)** em `src/security/crypt.c` como primitiva criptografica
  fundacional. Sequencia natural apos os hardenings de auth
  (alpha.206-212) e a wipe hygiene de SHA-256 (alpha.208-209):
  agora que as primitivas existentes estao corretas, o proximo
  passo de "valor real para slices futuros" e adicionar a
  primitiva de derivacao de chave canonica que sera consumida
  por TLS userland (Etapa 5), key wrapping para AES-XTS, secure
  boot e update_agent signing.
- **Antes.** O sistema cripto exposto era:
  - SHA-256 (init/update/final/clear, `crypt_hmac_sha256`)
  - PBKDF2-SHA256 com 64000 iteracoes (somente para senhas)
  - AES-XTS 256 (somente para volume cifrado)
  - Constant-time compare
  Faltava: derivacao de chave context-aware a partir de um
  segredo ja uniforme. Sem isso, qualquer slice futuro que
  precisasse derivar varias chaves de um master secret (TLS
  derivando handshake keys do master secret, AES-XTS isolando
  dominio entre dados/metadata, secure boot versionando keys)
  teria que reinventar a primitiva ad-hoc. Reinventar KDF e o
  unico caminho garantido para vulnerabilidade silenciosa.
- **Agora.** Pipeline RFC 5869 completo:
  - `crypt_hkdf_sha256_extract(salt, ikm) -> PRK`: comprime IKM
    em 32 bytes pseudoaleatorios via HMAC-SHA256(salt, IKM).
    Salt opcional (NULL/zero-length substituido por HashLen
    zero octets per §2.2, mandatorio).
  - `crypt_hkdf_sha256_expand(PRK, info, L) -> OKM`: produz ate
    255*32 = 8160 bytes de output via T(i) = HMAC(PRK, T(i-1)
    || info || byte(i)) iterado ceil(L/HashLen) vezes.
  - `crypt_hkdf_sha256(salt, ikm, info, L) -> OKM`: wrapper que
    compoe extract+expand.
- **Streaming HMAC interno (`hkdf_hmac_begin/update/end`).** A
  funcao `crypt_hmac_sha256` publica espera todos os dados em um
  buffer contiguo. HKDF expand precisa de `HMAC(PRK, T(i-1) ||
  info || byte(i))` onde `info` pode ser arbitrariamente longo.
  Buffer-ar tudo em um array contiguo exigiria stack frame
  ilimitado ou kalloc — ambos indesejaveis num primitivo cripto
  kernel-side. Solucao: tres helpers `static` privados que
  envolvem a construcao HMAC padrao em torno da API streaming
  `sha256_init/update/final`. `begin` instala kipad no SHA-256
  interno e guarda kopad para finalizacao. `update` aceita
  pedacos arbitrarios e roteia para o SHA-256 interno. `end`
  finaliza inner, faz outer SHA256(kopad || inner_digest), zera
  todo o estado sensivel.
- **Fail-closed em todos os bounds (RFC 5869 §2.2/§2.3).**
  - `prk == NULL` em extract: retorna -1.
  - `prk == NULL` em expand: retorna -1.
  - `out == NULL` em expand: retorna -1.
  - `L > 8160` em expand: retorna -1. Sem isso, o counter byte
    enrolaria de 255 para 0 ou seria cast incorretamente para
    256 e produziria output silenciosamente truncado ou
    duplicado.
  - `prk_len < HashLen` em expand: retorna -1. RFC obriga PRK
    >= HashLen para preservar o limite de seguranca.
  - `L == 0`: no-op success.
  - `salt == NULL` ou `salt_len == 0` em extract: substituicao
    obrigatoria por HashLen bytes de zero. Sem isso, HMAC
    degenera para chave vazia e perde a propriedade
    universal-hash que HKDF assume.
- **Wipe hygiene.** Em todo caminho de saida (sucesso E erro):
  - `crypt_hkdf_sha256_extract`: `zero_salt` zerado.
  - `crypt_hkdf_sha256_expand`: `t_prev` zerado (em sucesso e
    em todos os erros intermediarios, ex. counter > 255).
  - `hkdf_hmac_begin`: `kipad`, `key_hash` (quando usado),
    contexto SHA-256 de hash de chave (`key_ctx`) zerados via
    `secure_clear`/`sha256_clear`.
  - `hkdf_hmac_end`: contexto `outer`, contexto `ctx->inner`,
    `ctx->kopad` zerados.
  - `crypt_hkdf_sha256` (wrapper): `prk` zerado antes do
    retorno em sucesso e erro.
  Composicao com a wipe hygiene volatile-safe instalada em
  alpha.208 (CSPRNG pool snapshot, `sha256_clear` publico) e
  alpha.209 (HMAC publico/PBKDF2 interno/sha256_hash/volume key
  hash) preservada — cada `sha256_clear` chama o helper que o
  compilador nao pode eliminar como dead-store.
- **Threat model documentado inline.** Comentario no header e na
  implementacao explicita: HKDF NAO substitui PBKDF2 para
  senhas. HKDF assume IKM uniformemente pseudoaleatorio (ou
  proximo). Para raw passphrases, o pipeline correto e
  `password -> PBKDF2(64000 iter) -> PRK -> HKDF expand ->
  {subkey_disk, subkey_network, subkey_session, ...}`. O output
  pos-PBKDF2 e exatamente o tipo de IKM para o qual HKDF foi
  projetado. Sem essa nota, callers futuros poderiam cair na
  armadilha classica (HKDF e rapido demais para resistir a
  brute-force de senha fraca; PBKDF2 com 64000 iteracoes existe
  exatamente para introduzir esse custo).
- **Testes (RFC 5869 Appendix A).**
  `tests/test_crypt_vectors.c::test_hkdf_sha256_vectors` cobre:
  - **TC1**: IKM=22, salt=13, info=10, L=42 (small inputs).
    Valida extract isolado, expand isolado e o wrapper
    combinado.
  - **TC2**: IKM=80, salt=80, info=80, L=82 (long inputs). L=82
    spans 3 iteracoes do expand (L > 2*HashLen=64). Valida o
    loop counter, a concatenacao streaming de info (sem buffer
    fixo limitando), e a truncation correta do ultimo bloco.
  - **TC3**: salt empty, info empty, L=42. Valida a
    substituicao zero-octet de §2.2 com NULL pointer E com
    zero-length array — ambos devem produzir o mesmo PRK.
  - **6 contract checks fail-closed**: NULL prk em extract,
    NULL prk em expand, NULL out em expand, L > 255 * HashLen
    em expand, PRK_LEN < HashLen em expand, L=0 no-op success
    em expand.
  - `expect_hex` ampliado de 64 para 256 bytes de buffer
    interno para acomodar OKM de 82 bytes do TC2. Todos os
    calls existentes (que testam <= 64 bytes) continuam
    funcionando identicamente.
- **Desempenho.**
  - Extract: 1 HMAC-SHA256 (~2 microssegundos em x86_64,
    dominado pelo SHA-256 fixo).
  - Expand para L bytes: ceil(L / HashLen) HMACs. Para L=32
    (uso comum, derivar uma chave AES-256): 1 HMAC. Para L=42
    (TC1): 2 HMACs. Para L=8160 (max teorico): 255 HMACs (~510
    microssegundos).
  - Streaming HMAC evita alocacao e copia adicional de `info`.
- **Composicao com slices anteriores.**
  - alpha.206-212 (auth hardening): ortogonal — HKDF e
    primitiva, nao substitui PBKDF2 para senhas.
  - alpha.208-209 (SHA-256 wipe hygiene): HKDF consome essas
    APIs. Cada contexto SHA-256 reutilizado dentro do HKDF
    inner/outer e zerado.
  - alpha.210 (Ed25519 fail-closed): ortogonal. Quando Ed25519
    real entrar, HKDF pode ser usado para domain-separar
    hashes de mensagens versionadas.
- **Limites.**
  - Nao implementa HKDF-SHA512 (instanciacao trivial, sem
    callers planejados).
  - Nao implementa derivacao para AEAD (ChaCha20-Poly1305) —
    slice futuro.
  - Nao adiciona callers reais ainda (TLS userland, key
    wrapping, secure boot estao em etapas posteriores). Esta
    entrega e a fundacao.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
- Este patch nao altera a pipeline declarativa fail-closed; e
  uma adicao de primitiva criptografica fundacional para slices
  futuros.

## Incremento `0.8.0-alpha.217+20260513`

- Fora da pipeline declarativa: entrega **Ed25519 (RFC 8032)
  implementacao REAL** em `src/security/ed25519.c` substituindo
  o esqueleto fail-closed que vinha desde alpha.210. **Update
  verifier (`src/services/update_agent.c`) oficialmente
  OPERACIONAL pela primeira vez** — manifests assinados com
  chave canonica em producao agora aceitos quando
  criptograficamente validos.
- **Refatoracao 1: fe25519 compartilhado.** Field arithmetic
  GF(2^255-19) extraido de `x25519.c` para
  `include/security/fe25519.h` + `src/security/fe25519.c`, agora
  consumido por X25519 + Ed25519 (elimina duplicacao). Adiciona
  `fe_pow22523` (cadeia para sqrt em Ed25519 decode), `fe_neg`,
  `fe_cmov`, `fe_isnegative`, `fe_iszero`, `fe_notequal`.
  `x25519.c` reduzida para Montgomery ladder + APIs publicas
  consumindo `fe25519`.
- **Refatoracao 2: Ed25519 real.** `src/security/ed25519.c`
  (~1500 LOC):
  - Twisted Edwards group ops em coordenadas extended (X:Y:Z:T)
    com a = -1: `ge_dbl` (dbl-2008-hwcd: 4sq + 4mul), `ge_add`
    (add-2008-hwcd-3: 9mul + 7add com `T1 * 2d * T2`),
    `ge_neg_p` (-X, Y, Z, -T), `ge_cmov` constant-time.
  - Scalar multiplication double-and-add constant-time (256
    doubles + 256 cond-adds, cmov mascarado sem branch sobre
    bit secreto): `ge_scalarmult`, `ge_scalarmult_base` usando
    `ED_B` fixo, `ge_double_scalarmult` para verify (k*A+S*B).
  - Encoding/decoding compressed (32 bytes Y || sign(x)):
    `ge_encode` (Y/Z em LE + sign bit), `ge_decode` (parse y +
    canonicality check via re-encode comparison, candidate
    `x = u*v^3*(uv^7)^((p-5)/8)` via `fe_pow22523`, verify
    `v*x^2 == ±u`, multiply por `ED_SQRTM1` se -u, set sign
    correto, rejeita `x==0 && x_0==1` per RFC §5.1.3 step 5).
  - Scalar arithmetic mod L (L = 2^252 +
    27742317777372353535851937790883648493): `sc_reduce64`
    (porte ref10 signed 21-bit limbs com cascading
    multiply-and-add), `sc_muladd` (a*b+c mod L),
    `sc_is_canonical` (S<L constant-time).
  - Constants verificadas contra dalek-cryptography (5x51-bit
    limbs): `ED_D = -121665/121666`, `ED_D2 = 2*ED_D`,
    `ED_SQRTM1 = 2^((p-1)/4)`, `ED_B_X/ED_B_Y/ED_B_T`, `ED_L_BYTES`.
- **APIs publicas em `include/security/ed25519.h`.** Tres
  entradas com fail-closed em NULL e wipe hygiene volatile-safe:
  - `ed25519_create_keypair(pk, sk, seed)`: `h = SHA-512(seed)`,
    `s = clamp(h[0..32])`, `prefix = h[32..64]`, `A = s*B`,
    encode A para pk. `sk = seed || pk`.
  - `ed25519_sign(sig, M, len, pk, sk)`: RFC §5.1.6
    PureEd25519 deterministico. `r = SHA-512(prefix || M)
    mod L`, `R = r*B`, `sig[0..32] = R`, `k = SHA-512(R || A
    || M) mod L`, `sig[32..64] = (r + k*s) mod L`.
  - `ed25519_verify(sig, M, len, pk)`: RFC §5.1.7. Check
    `S < L`, decode R + pk, `k = SHA-512(R || A || M) mod L`,
    compute `check = S*B - k*A`, multiply both R e check por
    cofator 8, compare via projective equality (`X1*Z2 ==
    X2*Z1 && Y1*Z2 == Y2*Z1`). Cofator 8 mandatory per RFC
    elimina componentes torsao.
- **Update agent operacional.**
  `src/services/update_agent.c::manifest_signature_ed25519_valid`
  substitui comentario fail-closed por documentacao do gate
  criptografico ativo. Manifests com assinatura forjada/corrompida,
  `S >= L`, R/pk decodificando para ponto invalido, ou
  `[8]SB != [8]R + [8](kA)` sao todos rejeitados fail-closed.
- **Threat model documentado inline.**
  - Cofator 8 em verify per RFC §5.1.7 step 4 (mandatory):
    elimina componentes torsao, resistente a strongbinding
    attacks.
  - `S < L` gate previne signature malleability.
  - Canonicality check em `ge_decode` (re-encode + compare)
    previne non-canonical encodings.
  - `x == 0 && x_0 == 1` rejeita o ponto ambiguo per RFC
    §5.1.3 step 5.
  - Wipe volatile-safe em todos os intermediarios.
- **Tests reformulados** em `tests/test_crypt_vectors.c`
  (`test_ed25519_failclosed_contract` substituida, nao
  deletada — contrato mudou de fail-closed para implementation
  real):
  - 3 vetores oficiais RFC 8032 §7.1 (empty / 1-byte / 2-byte
    messages): valida pk derivation, signature derivation,
    verify acceptance.
  - Tampering rejection: flip de `sig[0]` (R half) e `sig[32]`
    (S half) devem falhar.
  - Wrong-pk rejection: flip de `pk[0]` deve falhar.
  - Non-canonical S rejection: `S = L` e `S > L` devem falhar.
  - NULL inputs: signature/pk NULL retornam -1.
  - Tolera NULL output: `sign(NULL, ...)` e
    `create_keypair(NULL, NULL, ...)` nao crasham.
  - Round-trip: chave fresca assina + verifica mensagem 64-byte;
    tamper-message deve falhar.
  - Determinismo: re-assinar produz mesma sig.
- **Build.** `fe25519.o` adicionado a `CAPYOS64_OBJS` (kernel).
  `fe25519.c` + `sha512.c` adicionados a `TEST_SRCS` (host).
- **ABI publica preservada.** Mesmas 3 funcoes Ed25519 que antes
  retornavam fail-closed agora retornam resultados reais.
- **Limites.**
  - Performance: `ge_scalarmult` usa double-and-add simples
    (sem precomputed table). ~5-10 ms por scalar mult em x86_64.
    Speedups via fixed-base comb tables podem vir em slices
    futuros.
  - Apenas 3 vetores RFC 8032 §7.1 nos tests (nao 1024-byte ou
    SHA-abc). Cobertura adequada para sanity.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
  - TLS 1.3 userland ainda nao implementado (Etapa 5).
    Primitivas todas prontas.
- Este patch nao altera a pipeline declarativa fail-closed; e o
  fechamento do ultimo gap de primitivas criptograficas modernas
  na fundacao cripto canonica.

## Incremento `0.8.0-alpha.220+20260514`

- Fora da pipeline declarativa: entrega **implicit re-hash on
  successful auth + Argon2id volume-key derivation primitive** —
  fecha os dois ultimos limites residuais documentados em
  `alpha.218` (volume key derivation continua PBKDF2) e
  `alpha.219` (timing leak transicional PBKDF2 vs Argon2id).
- **Problema A: timing leak transicional.** Toda conta criada ou
  que trocou senha desde `alpha.219` ja saia com Argon2id
  memory-hard. Contas legadas hashed com PBKDF2-SHA256 antes do
  rollout continuavam autenticando via PBKDF2 — legitimo
  (`userdb_authenticate` dispatcha por `algo_id`), mas o caminho
  legacy completa em ~50ms (PBKDF2 64000 iter) enquanto o
  caminho moderno completa em ~200ms (Argon2id `t=3, m=8192`).
  Atacante observando latencia distinguia "conta predates
  `alpha.219` deployment e nunca trocou senha" vs "conta
  Argon2id". Vazamento minimo (nao revela senha, nao revela
  existencia — `alpha.206` ja timing-equalizou
  existent-vs-non-existent — apenas idade aproximada).
- **Problema B: volume key derivation continua PBKDF2.**
  `crypt_derive_xts_keys` em `src/security/crypt.c:149` chamada
  por `installer_main.c:464`, `key_storage_probe.c:75`,
  `kernel.c:392`. Salt fixo `g_disk_salt` ("NoirOS-FS-Salt") + 16000
  iter PBKDF2-SHA256. Atacante com disco roubado e binario do
  kernel (em `/boot` nao cifrado) brute-forcava o volume password
  offline com speedup GPU/ASIC tipico >10000x sobre CPU.
- **Solucao A: implicit re-hash on successful auth.**
  `src/auth/user.c` refatorado:
  - **`userdb_replace_password_hash` (helper privado, novo).**
    Toda a logica de read-modify-write do `/etc/users.db`
    extraida de `userdb_set_password`. Le DB inteiro -> parse
    linha-a-linha -> identifica registro alvo por username ->
    gera salt fresco via
    `csprng_get_bytes(USER_SALT_SIZE=16)` per RFC 9106 §3.1 ->
    re-deriva com `user_password_hash_derive`
    (`USER_ARGON2ID_T_COST=3, USER_ARGON2ID_M_COST=8192` KiB) ->
    `serialize_user_record_line` escolhe 10-field schema
    automaticamente por `algo_id` -> grava via
    `userdb_write_blob` atomicamente. Wipe `volatile` em todos
    os buffers de credencial em todos os exit paths.
  - **`userdb_set_password` (entry publico, refatorado).** Aplica
    `auth_policy_validate_password` (mantem politica de senha do
    `alpha.211/.212`) e delega para
    `userdb_replace_password_hash`. Mesma semantica que antes
    para callers existentes.
  - **`userdb_authenticate` (entry publico, hook novo).** Depois
    de `auth_ok=1` com `user_found=1` e `rec.algo_id !=
    USER_PASSWORD_ALGO_ARGON2ID`, executa
    `(void)userdb_replace_password_hash(username, password)`.
    Fail-silent: allocation/FS error nao bloqueia auth ja
    bem-sucedida — record stays on PBKDF2 e retry no proximo
    login. Comentario inline documenta threat model: leak
    self-heals; population de PBKDF2 records shrinks
    monotonically toward zero conforme contas autenticam;
    residual leak restrito a "contas que nunca logaram desde
    `alpha.220` deployment".
  - **Por que o policy check fica em `userdb_set_password` e
    nao no helper.** O implicit re-hash nao pode falhar por
    causa de uma policy que apertou depois do ultimo password
    change — o usuario seria locked out da propria conta. Ja o
    set explicito tem que validar antes de aceitar o novo
    password. Separar a logica no helper deixa as duas
    responsabilidades em pontos distintos do codigo, mesma
    derivacao Argon2id em ambos.
- **Timing apos `alpha.220`:**
  - Conta legacy PBKDF2 (primeiro login pos-upgrade): ~250ms
    (50ms verify + 200ms rehash + ~5ms FS rewrite).
  - Conta legacy PBKDF2 (segundo login pos-upgrade): ~200ms
    (record agora e Argon2id).
  - Conta Argon2id: ~200ms (inalterado).
- **Solucao B: Argon2id volume-key derivation primitive.**
  - **`include/security/crypt.h:30-31`** adiciona constantes:
    ```c
    #define CRYPT_VOLUME_ARGON2ID_T_COST 3u
    #define CRYPT_VOLUME_ARGON2ID_M_COST 8192u
    ```
    Mesma tuning que userdb (`USER_ARGON2ID_T_COST`,
    `USER_ARGON2ID_M_COST`) — reaproveita o budget de 8 MiB do
    kernel heap, evita ter dois work memory pools dimensionados
    independentemente.
  - **`include/security/crypt.h:82-86`** adiciona signature:
    ```c
    int crypt_derive_xts_keys_argon2id(const char *password,
                                       const uint8_t *salt,
                                       size_t salt_len,
                                       uint32_t t_cost,
                                       uint32_t m_cost,
                                       uint8_t key1[CRYPT_KEY_SIZE],
                                       uint8_t key2[CRYPT_KEY_SIZE]);
    ```
  - **`src/security/crypt.c:174-253`** implementa:
    1. **Fail-closed first.** Wipe `key1` e `key2` a zero NO
       INICIO do dispatcher (antes de qualquer parameter check)
       — caller que esqueca de checar return code recebe
       sentinela "no key here" inequivoco em vez de stack
       residue do frame anterior.
    2. Rejeita NULL `password`/`salt`/`key1`/`key2`, `t_cost <
       ARGON2_MIN_T_COST=1`, `m_cost < ARGON2_MIN_M_COST=8`,
       `salt_len < 8` per RFC 9106 §3.1.
    3. `kalloc(m_cost * 1024)` — 8 MiB para a tuning default.
       Return -1 imediato se allocation falhar.
    4. `argon2id_hash(password, pass_len, salt, salt_len,
       t_cost, m_cost, memory, memory_bytes, derived, 64)`.
    5. Wipe `memory` volatile-safe **antes** de `kfree` — impede
       que freed heap region retenha matriz Argon2id walk (que
       carrega password-derived state).
    6. Split `derived[0..32]` -> `key1`, `derived[32..64]` ->
       `key2` (mesma split semantics que
       `crypt_derive_xts_keys` PBKDF2 preserva, garante drop-in
       compat em layout de key schedule AES-XTS).
    7. Wipe scratch `derived[64]` antes de retornar.
- **Callers em producao NAO trocados nesta release.**
  `installer_main.c:464`, `key_storage_probe.c:75`,
  `kernel.c:392` continuam em PBKDF2. A primitiva esta entregue
  mas o consumo aguarda design do header de volume com algorithm
  marker — sem ele, upgrade do binario do kernel quebra volumes
  encrypted com PBKDF2 (chaves derivadas diferentes). Slice
  futuro definira o header de volume + tools de re-keying para
  migracao incremental.
- **Memoria.** Implicit re-hash: 8 MiB por re-hash event (mesma
  alocacao que o caminho de criar/trocar senha em `alpha.219`).
  Acontece apenas no primeiro login pos-`alpha.220` de uma conta
  legacy. Allocation failure nao bloqueia auth. Volume key
  Argon2id: 8 MiB por unlock event quando a primitiva for
  consumida. Callers atuais em PBKDF2, entao nao ha custo runtime
  nesta release — apenas a presenca do simbolo no binario do
  kernel.
- **Testes novos** em
  `tests/test_crypt_vectors.c::test_crypt_derive_xts_keys_argon2id`
  (11 assertions):
  - Determinismo (chamadas identicas produzem `key1` e `key2`
    byte-a-byte iguais).
  - `key1 != key2` (anti-bug de split do output de 64 bytes).
  - Salt sensitivity (mudar salt muda AMBAS as chaves porque
    Argon2id mixa salt em H0 que alimenta toda a matriz).
  - Fail-closed em NULL `password`/`salt`/`key1`/`key2`,
    `t_cost=0`, `m_cost=7`, `salt_len=7`.
  - Wipe forensics em failure path (sentinela `0xA5` deve virar
    `0x00` apos return -1).
  - Non-collision com `crypt_derive_xts_keys` (4 iter PBKDF2 vs
    1/8 Argon2id produzem chaves diferentes).
- **ABI publica preservada.** Nenhuma quebra. `struct
  user_record` format preservado integral.
  `userdb_authenticate`/`userdb_set_password` signatures
  identicas. `crypt_derive_xts_keys` signature identical. Novos
  symbols: `crypt_derive_xts_keys_argon2id` (publico),
  `userdb_replace_password_hash` (privado static).
- **Composicao com slices anteriores integral.**
  - `alpha.219` (Argon2id em userdb): integral —
    `userdb_replace_password_hash` usa o mesmo dispatcher
    `user_password_hash_derive` que `alpha.219` instalou.
  - `alpha.218` (Argon2id + BLAKE2b primitivas): integral —
    `crypt_derive_xts_keys_argon2id` consome `argon2id_hash`
    direto.
  - `alpha.214` (CSPRNG): preservada — salt fresco do rehash
    via `csprng_get_bytes`.
  - `alpha.212` (timing-equalised lockout): preservada —
    `userdb_authenticate_with_policy` continua chamando
    `userdb_authenticate` ANTES do `allowed` check; o rehash
    inline acontece DENTRO de `userdb_authenticate` mas DEPOIS
    de `auth_ok=1`, entao o wrapper observa um auth ja
    completo.
  - `alpha.207` (`userdb_authenticate_with_policy` wrapper):
    preservada.
  - `alpha.206` (timing equalization existent-vs-non-existent):
    preservada — `k_userdb_dummy_salt` continua sendo usado
    para usuarios desconhecidos.
- **Limites residuais novos.**
  - **Implicit re-hash exige login** para migrar. Contas
    dormentes (service accounts, contas que nao logam ha tempos)
    ficam PBKDF2 indefinidamente. Slice futuro tera ferramenta
    administrativa de batch re-hash (mas precisa do password de
    cada conta — operacional via fluxo "admin re-cadastra").
  - **Volume key primitive sem callers em producao.**
    Instalacoes feitas com binarios anteriores continuam PBKDF2.
    Slice futuro precisa: definir on-disk volume header com
    algorithm marker + parametros (`algo_id, t_cost, m_cost,
    salt_len, salt`), atualizar `installer_main.c` para escrever
    o header, atualizar `key_storage_probe.c` para ler o header
    no boot e despachar para a KDF correta, ferramenta de
    re-keying para upgrade in-place de volumes legacy.
  - **Disk salt continua hardcoded** (`g_disk_salt` em
    `installer_main.c:39-41`). Mitigacao: Argon2id ainda paga 8
    MiB por candidate mesmo com salt fixo (memory wall
    preservado), mas idealmente seria per-install random salt
    armazenado no header de volume. Endereçado no mesmo slice
    futuro do header de volume.
- **Impacto user-final.** Contas legadas migram para memory-hard
  hashing transparentemente sem acao do usuario nem do
  administrador. Ataque offline contra `/etc/users.db` agora paga
  8 MiB por candidate (speedup GPU/ASIC sobre CPU cai de
  >10000x para <10x per RFC 9106 §1.2), empurra crack time de
  "horas" para "anos" para senhas de qualidade media.
- **Impacto estrutural.** Codigo do userdb fica mais limpo
  (`set_password` e wrapper fino de `replace_password_hash` +
  policy check, mesma derivacao Argon2id em ambos os caminhos
  sem duplicacao). Primitive de volume key Argon2id disponivel
  para installer/volume-creation tools futuros sem rebuild do
  kernel. `alpha.218` e `alpha.219` Limites residuais agora
  ambos endereçados.
- Este patch nao altera a pipeline declarativa fail-closed; e
  hardening cripto-economico do caminho de login local (implicit
  re-hash) + entrega de primitiva fundacional (Argon2id volume
  key) pronta para consumo futuro. Nao destrava entregaveis
  pendentes da Etapa 2 (loginwindow GUI real, smokes).

## Incremento `0.8.0-alpha.219+20260514`

- Fora da pipeline declarativa: entrega **Argon2id em producao
  no userdb** — primeira caller real da primitiva memory-hard
  introduzida em `alpha.218`. Login local CapyOS agora usa
  Argon2id por default em toda conta criada e em toda troca de
  senha. Contas legacy hashed com PBKDF2-SHA256 continuam
  autenticando sem migracao de DB.
- **Problema fechado.** `alpha.218` entregou Argon2id como
  primitiva auditavel mas com limite explicito: *"Sem callers
  reais ainda em userdb"*. Toda a fundacao memory-hard ficava
  dormente — atacante com acesso offline a `/etc/users.db`
  continuava crackando senhas fracas em horas via GPU/ASIC.
- **Dispatcher novo: `src/auth/user_password_hash.{c,h}`**
  (~190 LOC + 105 LOC). Decoupla `src/auth/user.c` das primitivas
  crypto e isola toda a logica de escolha de algoritmo, alocacao
  de Argon2id work memory e wipe hygiene em modulo testavel
  host-side sem dependencia de VFS/userdb (`kalloc` do host stub
  fornece a memoria).
- **APIs publicas em `include/auth/user_password_hash.h`.**
  - `user_password_hash_algo_to_string(algo_id)` -> canonical
    token (`"pbkdf2"` ou `"argon2id"`).
  - `user_password_hash_algo_from_string(text, len, &out)` ->
    parser exato (rejeita prefix collision como `"pbkdf2x"` ou
    truncamento como `"argon2i"`).
  - `user_password_hash_derive(password, len, salt, salt_len,
    algo_id, t_cost, m_cost, hash_out, hash_out_len)` ->
    dispatcha PBKDF2-SHA256 ou Argon2id. PBKDF2 path: `t_cost==0`
    mapeia para `USER_ITERATIONS` (64000) — bridge para registros
    legacy sem campo de iteracoes serializado. Argon2id path:
    aloca `m_cost*1024` bytes via `kalloc`, chama
    `argon2id_hash`, wipa buffer com `volatile`-typed pointer
    antes de `kfree`. Rejeita `t_cost<1` ou `m_cost<8` per RFC
    9106 §3.1. Fail-closed: `hash_out` wipeado a zero em todo
    path de erro.
  - `user_password_hash_verify(password, len, salt, salt_len,
    algo_id, t_cost, m_cost, stored_hash, stored_len)` -> deriva
    em scratch local 64-byte capped, compara via
    `crypt_constant_time_compare`, wipa scratch antes de
    retornar.
- **`include/auth/user.h` ganha:**
  - `USER_PASSWORD_ALGO_PBKDF2_SHA256 = 0` (legacy).
  - `USER_PASSWORD_ALGO_ARGON2ID = 1` (default desde alpha.219).
  - `USER_ARGON2ID_T_COST = 3`, `USER_ARGON2ID_M_COST = 8192`
    (8 MiB — OWASP minimo defensavel para constrained device).
  - `struct user_record` cresce 9 bytes no **final**: `uint8_t
    algo_id`, `uint32_t algo_t_cost`, `uint32_t algo_m_cost`.
    Append-only — **27 callsites existentes** (`apps/settings`,
    `gui/desktop`, `apps/file_manager`, `shell/...`, `fs/vfs`,
    `auth/privilege`, `auth/login_runtime`, `auth/session`)
    compilam unchanged porque so leem
    `username/uid/gid/home/role`.
- **`src/auth/user.c` refatorado.**
  - `user_record_init` sempre emite Argon2id com
    `USER_ARGON2ID_T_COST/M_COST`.
  - `userdb_set_password` sempre re-hashea com Argon2id — primeira
    troca de senha apos upgrade **promove automaticamente conta
    legacy para Argon2id** sem migracao explicita de DB.
  - `userdb_authenticate` dispatcha conforme `rec.algo_id`:
    usuario encontrado chama `user_password_hash_verify` com o
    algoritmo do record (PBKDF2 legacy continua aceito); usuario
    desconhecido roda **Argon2id** com `k_userdb_dummy_salt` +
    `USER_ARGON2ID_T_COST/M_COST` — equaliza com a baseline nova
    (~200ms) e preserva mitigacao de user enumeration timing
    herdada de `alpha.206`.
  - `parse_user_line` aceita **7 campos** (legacy PBKDF2,
    `algo_id=0/t_cost=0/m_cost=0`) ou **10 campos** (Argon2id
    trailer `:argon2id:t_cost:m_cost`); algoritmo desconhecido na
    posicao 7 rejeita a linha inteira fail-closed (atacante nao
    downgrade conta forjando algoritmo).
  - `serialize_user_record_line` escolhe 7 ou 10 campos por
    `algo_id` — PBKDF2 legacy preservado no formato antigo
    permite downgrade transparente para binarios pre-alpha.219
    sem migracao reversa do DB.
  - Buffer da linha aumentado de `+64` para `+128` bytes de
    headroom para acomodar trailer Argon2id com margem (worst
    case ~30 bytes extras).
- **Schema `/etc/users.db`:**
  ```
  # Legacy (escrito por binarios pre-alpha.219):
  username:uid:gid:home:salt_hex:hash_hex:role

  # Argon2id (escrito por alpha.219+):
  username:uid:gid:home:salt_hex:hash_hex:role:argon2id:t_cost:m_cost
  ```
- **Memoria.** 8 MiB por auth (50% do kernel heap 16 MiB)
  alocado via `kalloc`, wipeado volatile-safe, liberado via
  `kfree`. Auth e serializado (sem concorrencia no kernel
  atual) — 16 MiB suporta uma derivacao por vez. `kfree`
  coalesce, memoria volta integral ao pool.
- **Threat model atualizado em comentario inline de
  `userdb_authenticate` (~25 linhas).** Explica timing leak
  transicional: registros PBKDF2 legacy autenticam em ~50ms;
  registros Argon2id novos em ~200ms. Atacante observando
  latencia distingue *"conta predates alpha.219 deployment e
  nunca trocou senha"* vs *"conta Argon2id"*. Vazamento minimo —
  nao revela senha, nao revela existencia, apenas idade
  aproximada da ultima troca de senha. Plano de eliminacao:
  slice futuro de **implicit re-hash on successful auth**.
- **Composicao com slices anteriores integral.**
  - `alpha.218` (Argon2id primitiva): consumida diretamente —
    sem duplicacao de codigo.
  - `alpha.214` (CSPRNG): salt continua gerado via
    `csprng_get_bytes(salt, 16)` per RFC 9106 §3.1.
  - `alpha.213` (HKDF-SHA256): ortogonal — sem interacao.
  - `alpha.212` (timing-equalised lockout): wrapper continua
    executando `userdb_authenticate` antes do check policy.
  - `alpha.207` (`userdb_authenticate_with_policy`): wrapper
    publico continua funcional, agora Argon2id-aware via
    dispatcher.
  - `alpha.206` (dummy salt para non-existent users):
    `k_userdb_dummy_salt` continua valido, agora alimentando
    Argon2id ao inves de PBKDF2.
  - `alpha.208/209` (wipe hygiene SHA-256): dispatcher herda
    `volatile`-typed wipe em todo intermediario.
- **Testes novos** em `tests/test_user_password_hash.c`
  (`run_user_password_hash_tests`). 30 assertions em 6 functions:
  - `test_algo_string_roundtrip`: canonicalizacao
    `pbkdf2`/`argon2id`; rejeicao de prefix collision e
    truncamento; NULL guards (8 assertions).
  - `test_pbkdf2_legacy_roundtrip`: `t_cost=0` mapeia para
    `USER_ITERATIONS`; equivalencia com `t_cost` explicito;
    verify aceita correto e rejeita errado (4 assertions).
  - `test_argon2id_roundtrip`: determinismo; nao-colisao com
    PBKDF2; verify aceita/rejeita (5 assertions).
  - `test_argon2id_sensitivity`: sensibilidade a
    `salt`/`t_cost`/`m_cost` — anti-regressao de parameter
    threading pelo dispatcher (3 assertions).
  - `test_derive_fail_closed`: NULL password com `len>0`, NULL
    salt com `salt_len>0`, `t_cost=0`, `m_cost<8`, algoritmo
    desconhecido, NULL `hash_out`, zero-len `hash_out` — todos
    rejeitam e zeram `hash_out` (7 assertions).
  - `test_verify_fail_closed`: NULL stored, zero-len stored,
    oversize stored > scratch 64-byte (3 assertions).
- **Build.** `src/auth/user_password_hash.c` em `CAPYOS64_OBJS`
  (kernel). `tests/test_user_password_hash.c
  src/auth/user_password_hash.c` em `TEST_SRCS`.
  `tests/test_runner.c` chama `run_user_password_hash_tests`
  apos `run_crypt_vector_tests`.
- **Limites residuais.**
  - **Timing leak transicional** (descrito acima) — sera
    eliminado por slice futuro de implicit re-hash on successful
    auth.
  - **Volume key derivation** (`crypt_derive_xts_keys` para
    AES-XTS) continua PBKDF2-SHA256. Threat model diferente
    (chave separada da senha de login) — migracao para Argon2id
    e slice futuro de menor prioridade.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
- Este patch nao altera a pipeline declarativa fail-closed; e a
  promocao da fundacao cripto canonica (alpha.218) a caller real
  no caminho de login local.

## Incremento `0.8.0-alpha.218+20260514`

- Fora da pipeline declarativa: entrega **Argon2id (RFC 9106) +
  BLAKE2b (RFC 7693)** memory-hard password hashing nativo do
  CapyOS em `src/security/argon2.c` (~600 LOC) e
  `src/security/blake2b.c` (~270 LOC). **Fundacao cripto canonica
  CapyOS completa (11 primitivas modernas em `src/security/`).**
- **Problema.** PBKDF2-SHA256 (default historico em
  `src/security/crypt.c` para password hashing em `userdb`) nao
  tem memory-hardness. Atacante com GPU/ASIC dedicado avalia
  1000-10000x mais candidates por segundo do que CPU comum.
  Passwords fracas crackam em horas. PBKDF2-SHA256 atingiu o fim
  da vida util cripto-economica.
- **Argon2id e vencedor do Password Hashing Competition (2015).**
  RFC 9106 padronizou em 2021. Recomendado por OWASP, NIST SP
  800-63B (rascunho 2024), e a maioria das auditorias cripto
  modernas. Argon2id (hibrido: primeira metade da pass 0
  data-independent, depois data-dependent) e a variante
  recomendada para password hashing.
- **APIs publicas BLAKE2b.** `blake2b_init/update/final/wipe`
  streaming + one-shot `blake2b()` com keyed mode HMAC-like ate
  64-byte key, lazy compression para streaming correto, param
  block per RFC §2.5 codificado em `h[0]` inicial, IV/sigma/
  rotations identicas a SHA-512, fail-closed em NULL/comprimento
  invalido, wipe volatile-safe em `m`/`v`/state/buf.
- **API publica Argon2id.** `argon2id_hash(password,
  password_len, salt, salt_len, t_cost, m_cost, memory,
  memory_len, out, out_len)` com **caller-provided memory
  buffer** (sem `malloc` no kernel), `parallelism = 1` fixo, sem
  secret/AD. Limites: `salt_len >= 8`, `t_cost >= 1`, `m_cost >=
  8` (KiB), `out_len >= 4`, `memory_len >= m_cost * 1024`.
- **Algoritmo (RFC 9106 §3).** Pre-hash H0 inicial via BLAKE2b
  (§3.2); variable-length H' via BLAKE2b iterado para outputs
  > 64 bytes (§3.3); G compression function 1024-byte com
  matrix 8x8 de registers 16-byte, apply P row-wise + column-
  wise, com **fBlaMka** `a = a + b + 2 * (a_lo * b_lo)`
  substituindo soma simples (acrescenta multiplicacao 32x32->64
  que aumenta cost-per-op em ASIC) (§3.6); address block
  generation per §3.4.1.1 com input_block `{pass, lane=0, slice,
  m', t_cost, type=2, counter, zero(968)}`, `address_block =
  G(zero_block, G(zero_block, input_block))`; modo Argon2id
  seleciona data-indep para `pass=0 && slice<2`, data-dep para
  resto (§3.4); reference index alpha com `J1^2` mapping
  nao-uniforme + start_pos exclude segment corrente em pass > 0;
  block computation pass 0 `B[i] = G(B[i-1], B[ref])`, pass > 0
  `B[i] ^= G(B[i-1], B[ref])` (v1.3 overwrite-XOR); finalize com
  `tag = H'(out_len, B[lane_length-1])` para p=1.
- **Wipe hygiene volatile-safe em todos os intermediarios.** H0,
  V chain do H', blocos `prev_block`/`ref_block`/`new_block`/
  `existing` da compressao, `input_block`, `address_block`,
  `zero_block`, `final_block`. Wipeados em sucesso e erro.
  Memory buffer caller-provided NAO e wipeado automaticamente
  (caller decide).
- **Threat model documentado inline em ambos os headers.**
  - **BLAKE2b**: resistencia a colisoes 2^256, preimage 2^512,
    indistinguibilidade de PRF quando chaveado, resistencia a
    length-extension via flag `f[0]` distinto entre blocos
    intermediarios (0) e ultimo bloco (0xFF..FF).
  - **Argon2id**: brute-force massivo em GPU/ASIC defendido via
    memory wall (`m_cost * 1024` bytes por candidate forca <10x
    speedup em ASIC dedicado vs >10000x para PBKDF2); TMTO
    resistance (reducao memoria por fator k aumenta tempo por
    k^2); side-channel timing hibrido (data-independent slice
    0-1 pass 0 contra cache-timing, data-dependent depois
    contra ranking-TMTO).
- **Parametros recomendados (OWASP 2024, RFC 9106 §4).**
  - `t_cost >= 2` (RFC §4 recomenda 3 para high-security; usar
    1 se `m_cost >= 2 GiB`).
  - `m_cost`: 65536 (64 MiB) para servidor potente; 19456
    (19 MiB) para servidor moderado; 8192 (8 MiB) minimo
    defensavel para constrained device (login local CapyOS).
  - `parallelism = 1` (fixado nesta implementacao).
- **Composicao com slices anteriores.** alpha.214 (CSPRNG):
  `csprng_get_bytes(salt, 16)` para gerar salt aleatorio
  recomendado (RFC 9106 §3.1). alpha.215-alpha.217 (cripto
  canonica complete): preservadas integralmente — esta entrega
  e aditiva. PBKDF2-SHA256 em `crypt.c` continua disponivel —
  userdb existente nao quebra. Migracao incremental para
  Argon2id sera slice futuro com algorithm prefix
  `$argon2id$v=19$m=...,t=...,p=1$salt$hash` + validacao
  automatica do PBKDF2 antigo + re-hash em proximo login
  bem-sucedido.
- **Testes contratuais novos** em `tests/test_crypt_vectors.c`:
  - `test_blake2b_rfc7693_abc`: vetor canonico RFC 7693
    Appendix A `BLAKE2b("abc")`.
  - `test_blake2b_empty`: vetor Python hashlib `BLAKE2b("")`.
  - `test_blake2b_multiblock`: vetor fox.
  - `test_blake2b_streaming_equals_oneshot`: update em chunks
    cruzando boundary 128 + 256 + tail produz o mesmo digest
    que one-shot. Valida lazy compression.
  - `test_blake2b_variable_output`: outlen 16/32/64 produzem
    outputs distintos.
  - `test_blake2b_keyed`: HMAC-like, keys diferentes -> outputs
    diferentes.
  - `test_blake2b_fail_closed`: NULL out, outlen=0/65, keylen=65,
    NULL key com keylen>0.
  - `test_argon2id_smoke`: m_cost=8 KiB cobrindo determinismo,
    sensibilidade a password/salt/t_cost/m_cost/out_len, empty
    password OK, fail-closed em todos os parametros invalidos.
- **KAT cross-checked vs argon2-cffi reference impl (Python).**
  Validacao manual confirmou MATCH byte-a-byte em 5 variantes
  (t=1/m=8/len=32, t=2/m=8/len=32, t=1/m=16/len=32, t=1/m=8/
  len=16, empty password/t=1/m=8/len=32). KAT embarcado em
  `test_argon2id_smoke` para regressao automatica. RFC 9106 §A.3
  KAT canonico usa p=4 + secret + AD (fora do escopo).
- **Build.** `blake2b.o` + `argon2.o` adicionados a
  `CAPYOS64_OBJS`; `blake2b.c` + `argon2.c` em `TEST_SRCS`.
- **Limites.**
  - Sem callers reais ainda em userdb. PBKDF2-SHA256 continua
    sendo o caminho de password hashing em producao.
  - `parallelism = 1` fixo (multi-lane fora de escopo).
  - Sem secret K nem associated data X.
  - Memory buffer caller-provided NAO e wipeado
    automaticamente.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
- Este patch nao altera a pipeline declarativa fail-closed; e o
  fechamento do gap fundamental contra brute-force massivo em
  GPU/ASIC para password hashing, completando a fundacao cripto
  canonica CapyOS (11 primitivas modernas auditaveis).

## Incremento `0.8.0-alpha.216+20260513`

- Fora da pipeline declarativa: entrega **a primeira primitiva de
  key exchange nativa do CapyOS — X25519 (RFC 7748)** em
  `src/security/x25519.c`. Implementacao do zero auditavel,
  independente do TLS stack BearSSL (que tem X25519 mas preso ao
  SSL engine). Esta entrega completa o **triplet canonico
  ECDH→HKDF→AEAD** — ate alpha.215 o sistema tinha HKDF
  (alpha.213) e AEAD ChaCha20-Poly1305 (alpha.215), mas faltava o
  "E" do ECDH.
- **APIs publicas em `include/security/x25519.h`.** Duas entradas
  com fail-closed em NULL e wipe hygiene volatile-safe:
  - `x25519(scalar, u_coord, shared)` — RFC 7748 §5. Clamping
    interno do scalar (zera bits 0,1,2,255; seta bit 254 —
    cofator 8 absorvido). Top-bit masking do u-coord. **Small-
    subgroup detection per §6.1:** rejeita `shared == 0` fail-
    closed (atacante poderia forcar shared=0 enviando small-order
    point).
  - `x25519_base(scalar, public_key)` — RFC 7748 §4.1. Computa
    public key usando u=9 (base point). Sem small-subgroup gate
    (base point tem ordem prima).
- **Field arithmetic em GF(p), p = 2^255 - 19.** Representacao em
  5 limbs de 51 bits cada (radix 2^51):
  - `fe_mul`/`fe_sq`: schoolbook com `__uint128_t`, reducao mod p
    via `*19` nos termos `i+j >= 5`.
  - `fe_sub`: soma `2*p` para evitar underflow.
  - `fe_carry`: propaga carries com reducao mod p.
  - `fe_invert`: cadeia ref10 (255 sq + 11 mul) para
    `a^(p-2) = a^(2^255-21)`.
  - `fe_tobytes`: canonicalizacao para `[0, p)` via subtracao
    condicional constant-time.
  - `fe_cswap`: conditional swap constant-time via
    `mask = -(uint64_t)swap`.
- **Montgomery ladder per RFC 7748 §5.** 255 iteracoes (bit 254
  down to bit 0), sem branches sobre bits secretos do scalar.
  Cada iteracao: `fe_cswap` condicional + ladder step (9 mul +
  2 sq + 4 add + 4 sub) computando doubling + addition
  simultaneamente. `a24 = 121665`.
- **Composicao com slices anteriores.**
  - alpha.214 (CSPRNG): fonte do scalar 32-byte uniforme.
  - alpha.213 (HKDF): KDF natural para `session_key` a partir de
    `shared + context label`.
  - alpha.215 (ChaCha20-Poly1305): consome `session_key` para
    canal autenticado com forward secrecy.
- **Threat model documentado inline.** Confidencialidade sob CDH,
  fail-closed em small-order via shared=0 gate, indistinguibilidade
  modulo top bit, limites (nao autentica public keys — caller
  responsibility).
- **Tests novos** em `tests/test_crypt_vectors.c` (6 funcoes, 25+
  assertions):
  - `test_x25519_rfc7748_scalarmult`: 2 vetores §5.2.
  - `test_x25519_rfc7748_dh`: §6.1 Alice/Bob com convergence.
  - `test_x25519_small_order_rejection`: u=0 e u=1.
  - `test_x25519_fail_closed`: 5 NULL combinations.
  - `test_x25519_high_bit_masked`: flip do bit 255 nao altera.
  - `test_x25519_scalar_clamping`: flip dos bits clamped nao altera.
- **Build.** `x25519.o` em `CAPYOS64_OBJS` (kernel) e
  `x25519.c` em `TEST_SRCS` (host).
- **ABI publica nova, aditiva.** Nao quebra callers existentes;
  header e auto-contido.
- **Limites.**
  - Sem callers reais ainda — esta entrega e a primitiva.
    Callers chegarao em slices futuros (TLS 1.3 userland Etapa 5,
    secure messaging local com forward secrecy, future
    WireGuard-like channel).
  - Ed25519 continua fail-closed (esta entrega NAO destrava o
    update verifier real). Ed25519 e proximo slice natural.
  - Nao substitui BearSSL para TLS handshake.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
  - Field arithmetic duplicada com `src/security/ed25519.c`
    (esqueleto dormante). Refatoracao adiada para quando Ed25519
    real for implementado.
- Este patch nao altera a pipeline declarativa fail-closed; e uma
  adicao de primitiva criptografica fundacional para slices
  futuros.

## Incremento `0.8.0-alpha.215+20260513`

- Fora da pipeline declarativa: entrega **a primeira AEAD nativa do
  CapyOS — ChaCha20-Poly1305 (RFC 8439)** em
  `src/security/chacha20_poly1305.c`. Implementacao do zero, audit-
  friendly, independente do TLS stack BearSSL (que tem ChaCha20-
  Poly1305 mas SO dentro do contexto TLS handshake). Esta entrega
  fecha um gap critico da fundacao cripto: ate alpha.214 o sistema
  tinha SHA-256, HMAC-SHA256, PBKDF2-SHA256, HKDF-SHA256, AES-128-
  XTS (confidencialidade sem MAC), CSPRNG hardened, Ed25519 fail-
  closed, e TLS via BearSSL. Faltava AEAD canonica para usos fora
  de TLS.
- **APIs publicas em `include/security/chacha20_poly1305.h`.**
  Quatro entradas com fail-closed em NULLs e wipe hygiene
  volatile-safe:
  - `chacha20_block(key, counter, nonce, out)` — RFC §2.3, gera
    64 bytes de keystream por counter. Macro `QR` para
    quarter-round inline em hot path. State + state_inicial back-
    add per spec.
  - `chacha20_encrypt(key, counter, nonce, in, out, len)` — RFC
    §2.4, stream cipher in-place. Counter overflow detection
    fail-closed: rejeita `initial_counter + ceil(len/64) > 2^32`
    para prevenir reuso de keystream blocks.
  - `poly1305_mac(otk, msg, msg_len, tag)` — RFC §2.5, one-time
    MAC em radix-26 (5 limbs de 26 bits). Clamping correto.
    Multiplicacao `h * r mod (2^130-5)` reduzida via `*5`.
    Reducao final via dual representation em tempo constante.
  - `chacha20_poly1305_encrypt`/`decrypt` — RFC §2.8, AEAD com
    OTK derivada do bloco 0, tag em tempo constante via
    `crypt_constant_time_compare` ANTES do decrypt (nao revela
    plaintext parcial se tag invalido).
- **Composicao com slices anteriores.**
  - alpha.208/209 (SHA-256 wipe): padrao volatile-safe seguido em
    Poly1305 internal state.
  - alpha.210 (Ed25519 fail-closed): padrao de fail-closed
    seguido.
  - alpha.213 (HKDF-SHA256): KDF para derivar chaves ChaCha20 de
    master secret + context label.
  - alpha.214 (CSPRNG hardened): fonte de key (256 bits) e nonce
    (96 bits) uniformes.
- **Threat model documentado inline.** Confidencialidade,
  integridade autenticada, indistinguibilidade polynomial, replay
  como caller-responsibility, limites (256 GiB por key/nonce, nao
  constant-time em tamanho de input).
- **Tests novos** em `tests/test_crypt_vectors.c` (4 funcoes, 30+
  assertions):
  - `test_chacha20_block_vectors`: RFC 8439 §A.1 TC1/TC2.
  - `test_chacha20_encrypt_round_trip`: round-trip, in-place,
    counter overflow rejection.
  - `test_poly1305_vectors`: §A.3 TC1 (zero key/msg → zero tag),
    TC2 (r=0 → tag=s), avalanche.
  - `test_chacha20_poly1305_aead`: round-trip + 5 categorias de
    tampering (ct/AAD/tag/key/nonce flips), empty pt/AAD, fail-
    closed.
- **Build.** `chacha20_poly1305.o` em `CAPYOS64_OBJS` (kernel) e
  `chacha20_poly1305.c` em `TEST_SRCS` (host).
- **ABI publica nova, aditiva.** Nao quebra callers existentes;
  header e auto-contido.
- **Limites.**
  - Sem callers reais ainda — esta entrega e a primitiva. Callers
    chegarao em slices futuros (IPC autenticado kernel-userland,
    container cifrado userland, secure messaging local, future TLS
    1.3 userland).
  - Nao implementa Ed25519 real ou X25519 — slices separados.
  - Nao substitui BearSSL para TLS handshake.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
- Este patch nao altera a pipeline declarativa fail-closed; e uma
  adicao de primitiva criptografica fundacional para slices futuros.

## Incremento `0.8.0-alpha.214+20260513`

- Fora da pipeline declarativa: realiza **hardening profundo do
  CSPRNG** (`src/security/csprng.c`) — coracao criptografico do
  sistema consumido por TODA primitiva que precisa de
  aleatoriedade. Sequencia natural depois de instalar HKDF
  (alpha.213): primitiva de derivacao de chave pronta, agora
  garantir que a primitiva de geracao de chave esta solida.
- **Bug 1 corrigido — `rdtsc` constraint `"=A"` mal-formed em
  x86_64.** A constraint `"=A"` em 64-bit significa "union de
  RAX e RDX" (compilador escolhe um); `rdtsc` deposita EDX:EAX
  como em 32-bit independente do modo, entao a constraint
  poderia gerar codigo errado. Mitigacao:
  ```c
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
  ```
- **Bug 2 corrigido — boot-time entropy fragil em VM hostil.**
  Em CPU velha sem RDRAND ou VM com TSC virtualizado para zero,
  o pool inicial degenerava para `sha256(salt fixo +
  boot-marker)` conhecido a partir do binario. Mitigacao em duas
  partes:
  - **TSC jitter loop** (16 rondas): intercala operacoes
    triviais com leituras de TSC. Mesmo em VM com TSC
    deterministico, deltas variam por efeitos de cache miss,
    branch predictor state e scheduler preemption —
    intrinsecamente nao-deterministicos no nivel de hardware.
  - **Pool address mixing**: `(uintptr_t)&entropy_pool` entra
    no pool. Com KASLR ativo, isso randomiza o pool entre
    boots; sem KASLR, ainda contribui via versionamento binario.
- **Bug 3 corrigido — sem reseed proativo.** Contador
  `bytes_since_reseed` incrementado em `csprng_get_bytes`. Ao
  cruzar `CSPRNG_RESEED_INTERVAL_BYTES` (64 KiB), chama
  `mix_hardware_entropy()` antes de emitir o proximo bloco.
  Limita janela de comprometimento caso atacante consiga
  inferir parte do pool: apos reseed, o pool inclui RDRAND/TSC
  frescos que nao estavam no estado inferido. Custo: ~80
  ciclos a cada 64 KiB. Comparativo: Linux 4 MiB, FreeBSD 1
  MiB, OpenBSD por tempo (5 min). 64 KiB e conservador.
- **Bug 4 corrigido — `csprng_feed_entropy` so 32-bit.** Nova
  API `csprng_feed_entropy_buffer(const void *data, size_t
  len)` aceita buffer arbitrario com `NULL`/zero graceful. Para
  fontes naturais de 64+ bits (TSC completo, network packet
  contents, disk sector contents, audio frames).
- **Bug 5 corrigido — RDRAND sem retry-loop.** Intel SDM
  recomenda 10 tentativas para tolerar falhas transitorias.
  Probabilidade de sucesso por chamada sobe de ~10^-2 (worst
  case) para ~1 - 2^-2000.
- **Helper centralizado `mix_hardware_entropy()`.** Encapsula
  RDRAND + TSC + reseed_counter. Reutilizado em tres caminhos:
  `csprng_init` (boot seed), loop de `csprng_get_bytes` (reseed
  automatico), `csprng_reseed` (reseed manual). Garante mesma
  qualidade de entropia em todos os caminhos.
- **Nova API publica `csprng_reseed()`.** Para callers criticos
  antes de operacoes longas (key generation, TLS handshake,
  master key derivation). Disponivel para slices futuros.
- **Caller real em mouse PS/2.**
  `src/drivers/input/mouse.c::mouse_ps2_irq_handler` alimenta o
  CSPRNG com cada byte de pacote (timing humano residual). Custo
  desprezivel (ISR ja estava no caminho).
- **Threat model documentado inline no header.**
  - Forward secrecy via output feedback no pool.
  - Backward secrecy via SHA-256 one-way.
  - Indistinguibilidade polynomial sob assumption de SHA-256.
  - Limites: VM hostil sem fontes hardware reais.
- **Testes contratuais novos** em `tests/test_csprng.c`:
  - `test_csprng_feed_buffer`: NULL/zero graceful + buffer muda
    output.
  - `test_csprng_reseed`: idempotencia comportamental — nao
    reseta pool.
  - `test_csprng_auto_reseed_after_interval`: emite 256 KiB (4x
    o intervalo) e valida que reseed automatico nao corrompe
    stream nem destroi continuidade.
- **Wipe hygiene preservada (alpha.208).** `csprng_get_bytes`
  continua zerando `temp_ctx` (via `sha256_clear`) e `digest`
  (via loop volatile) em cada iteracao. Compositionalmente:
  reseed automatico nao adiciona novo material sensivel ao
  stack frame da funcao; o `mix_hardware_entropy` so escreve no
  pool global.
- **ABI publica preservada.** Todas as funcoes antigas mantem
  assinatura exata. APIs novas (`csprng_feed_entropy_buffer`,
  `csprng_reseed`, constante `CSPRNG_RESEED_INTERVAL_BYTES`) sao
  aditivas.
- **Limites.**
  - Nao implementa reseed por tempo (so por bytes). Reseed por
    tempo exigiria timer/scheduler dependency que nao queremos
    no CSPRNG core.
  - Nao adiciona callers em network drivers ainda — slice
    futuro quando network stack tiver paths estaveis.
  - Nao destrava entregaveis pendentes da Etapa 2 (loginwindow
    GUI real, smokes).
- Este patch nao altera a pipeline declarativa fail-closed; e
  uma melhoria estrutural da fundacao criptografica que
  beneficia todos os consumidores de aleatoriedade do sistema
  (passados, presentes e futuros) sem mudar a forma como sao
  chamados.

## Sumário consolidado `alpha.221`-`alpha.237` — fechamento técnico da Etapa 2

Os 17 incrementos abaixo encerraram o escopo técnico da Etapa 2.
Para diminuir drift e manter o histórico denso compacto, cada
incremento é resumido em uma linha aqui. O detalhe completo de cada
um vive em `docs/plans/STATUS.md` e
`docs/plans/active/capyos-master-plan.md`.

**Storage/security — header on-disk + motor de migração transacional (alphas 221-232)**

- `alpha.221+20260514` — On-disk volume header module
  (`include/security/volume_header.h` + `src/security/volume_header.c`,
  ~290+~620 LOC): magic CAPYVHDR, version, kdf_algo_id, salt_len,
  data_offset_lba, reserved_lba_count, kdf_check_tag HMAC-SHA256,
  CRC32 IEEE 802.3 reflected, threat-model two-tier inline.
- `alpha.222+20260514` — Header-managed encrypted volumes em produção
  (`include/security/volume_provider.h` + `src/security/volume_provider.c`):
  `volume_provider_install` (fresh boot pós-install, Argon2id),
  `volume_provider_open` (boots subsequentes, header autoritário sem
  fallback para downgrade), 9 funções de teste host-side.
- `alpha.223+20260514` — Preflight seguro read-only de re-key /
  migração legacy: gates de shrink, scratch ausente e ranges
  source/target determinísticos.
- `alpha.224+20260514` — Planner transacional read-only que só marca
  migração como `READY` quando há scratch após o range alvo, cópia
  reversa segura e ranges determinísticos.
- `alpha.225+20260514` — Executor transacional como contrato
  guardado / dry-run: valida plano, reporta fases
  checkpoint/cópia/commit/verify e recusa writes reais com
  `WRITES_DISABLED`.
- `alpha.226+20260514` — Checkpoint persistente da migração: record
  little-endian de 128 bytes, CRC32, reserved-zero e validação
  semântica de progresso para resume/rollback/abort.
- `alpha.227+20260514` — Primeira escrita guardada do executor:
  grava só o checkpoint no bloco scratch com flag explícita,
  verifica por read/parse, bloqueia copy/re-encrypt destrutivo e
  commit de header.
- `alpha.228+20260514` — Executor prepara identidade criptográfica
  do destino no scratch: checkpoint + header Argon2id com salt
  CSPRNG + manifest com CRCs, verificados por read-back antes de
  qualquer cópia destrutiva.
- `alpha.229+20260514` — Executor aplica primeiro passo real de
  copy/re-encrypt reverso: copia um bloco legacy para o domínio
  header-managed Argon2id, verifica plaintext no destino, atualiza
  checkpoint+manifest no scratch.
- `alpha.230+20260514` — Executor comita LBA0 por último: exige
  cópia completa, grava header staged, verifica read-back, abre
  pelo caminho header-managed e valida o superbloco CAPYFS antes
  de marcar checkpoint como `COMPLETED`.
- `alpha.231+20260514` — Recovery operacional do executor:
  rollback/abort antes do commit restaura um bloco por chamada e
  zera o scratch ao completar; cleanup pós-commit abre
  header-managed, valida CAPYFS, localiza o scratch por geometria,
  rejeita estado estranho.
- `alpha.232+20260514` — Orquestrador automático de passo único da
  migração cifrada: detecta legacy / header-managed, lê scratch
  checkpoint+manifest, delega
  stage/copy/commit/cleanup/rollback ao próximo passo seguro.

**Loginwindow GUI real — submit, recovery e handoff (alphas 233-235)**

- `alpha.233+20260514` — Submit/autenticação real do loginwindow:
  política explícita habilita submit só com runtime pronto, a ponte
  `login_window_credential_auth_submit_userdb_consume()` chama
  `userdb_authenticate_with_policy`, preserva `OK`/`FAILED`/`LOCKED`,
  zera o buffer de credencial em todos os caminhos.
- `alpha.234+20260514` — Decisão segura de recuperação:
  `login_window_credential_recovery_decision_build()` consolida
  controller GUI + submit autenticado, aceita apenas rotas
  `stay`/`recovery`/`resume`/`text-login`, bloqueia bypass de
  lockout, bloqueia recovery após autenticação, exige reset+rerender
  no resume.
- `alpha.235+20260514` — Handoff loginwindow → sessão gráfica:
  `login_window_credential_session_handoff_build()` aceita apenas
  submit GUI autenticado, recovery decision segura, usuário desktop
  elegível e auditoria redigida antes de liberar `session_begin`,
  ativação de sessão, init do shell context e desktop autostart.

**Gates externos determinísticos — `gui-session` e `mouse-events` (alphas 236-237)**

- `alpha.236+20260514` — Gate determinístico `gui-session`:
  `desktop_gui_session_smoke_gate_from_readiness()` separa prontidão
  de sessão gráfica de `mouse-events`; runtime emite uma única vez
  `[smoke] gui-session ready` quando framebuffer, dimensões,
  taskbar, dispatcher essencial, fila saudável e ausência de
  overlays/drag estão prontos; alvo `smoke-x64-vmware-gui-session`
  adicionado.
- `alpha.237+20260514` — Gate externo final `mouse-events`:
  `desktop_mouse_events_smoke_gate_from_readiness()` exige
  `gui-session` + mouse + cursor + rotas de mouse prontas; runtime
  emite uma única vez `[smoke] mouse-events ready`; alvo
  `smoke-x64-vmware-mouse-events` adicionado; esteira de release
  exige DHCP + `gui-session` + `mouse-events`.

**Observabilidade defensiva (sem bump de alpha, 2026-05-16):** Após
`alpha.237`, o desktop runtime ganhou dois emissores simétricos de
diagnóstico complementares aos markers de `ready`. Cada gate emite
**no máximo uma vez** uma linha
`[smoke-diag] <gate> blocked: <first_blocker_name>` no serial log
quando passou 1000 iterações do main loop (~1-10s reais, variando
entre o caminho ativo de ~1ms/iter e o idle de ~10ms/iter sob PIT
a 100 Hz) e o gate continua bloqueado. O prefixo `[smoke-diag]` é
deliberadamente distinto de `[smoke]` para evitar colisão de
substring com o parser oficial. Bounded: no máximo 2 linhas
adicionais por execução do runtime; em boot saudável (gates prontos
antes do delay), zero linhas. Detalhes operacionais no playbook
[`../operations/etapa-2-external-validation-playbook.md`](../operations/etapa-2-external-validation-playbook.md)
seção "Marcadores de diagnóstico (não-obrigatórios)".

## Limites

- Não implementa CapyDisplay 2D, drivers gráficos avançados,
  Wayland, Mesa/Vulkan ou CapyLX — fora do escopo da Etapa 2.
- Não substitui execução real dos smokes oficiais: a Etapa 2
  permanece pendente da execução externa final dos gates
  documentada em
  [`../operations/etapa-2-external-validation-playbook.md`](../operations/etapa-2-external-validation-playbook.md).
- Não altera ABI pública de userland fora do escopo dos contratos
  declarativos descritos acima.

## Próximos slices

- **Execução externa final dos gates da Etapa 2.** O playbook
  consolidado em
  [`../operations/etapa-2-external-validation-playbook.md`](../operations/etapa-2-external-validation-playbook.md)
  orquestra fases A-E (build gates, provisionamento, smoke real
  `mouse-events`, evidência, aceitação e promoção). O aceite
  operacional da Etapa 2 fica condicionado a essa execução em
  VMware + UEFI + E1000 fora desta máquina.
- **Etapa 3** (Driver framework + entrada USB HID + storage estável,
  reorganizada por ROI em 2026-05-15) fica desbloqueada após o
  aceite operacional da Etapa 2 e segue a regra sequencial estrita
  (`../plans/active/capyos-master-plan.md:18-31`).
