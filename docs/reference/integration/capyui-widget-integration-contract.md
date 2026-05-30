# CapyUI widget model — contrato de integração com CapyOS

**Status:** referência para desenvolvimento apartado.
**Integração planejada:** Etapas 4 e 6, com o compositor/input permanecendo no sistema base.
**Repositório externo atual:** `CapyUI`.
**Contrato ativo:** `capy-ui-widget` v2.19, display-list schema v7 (`CapyUI/src/widget/capy_display_list.h`).

## Escopo atual

Este contrato viabiliza a integração do `CapyUI` com o core gráfico do CapyOS.
Ele ainda não é um SDK público completo para qualquer shell gráfica de terceiro
substituir a desktop session. O estado atual é semi-desacoplado:

- `CapyUI` é o owner autoritativo da desktop session, widgets de alto nível,
  event routing abstrato, layout e display-list schema;
- CapyOS base é o owner autoritativo do compositor, janelas, input real, fontes,
  surfaces, framebuffer, damage tracking, lifecycle de processo/app, package
  install e ativação final por module gate;
- o adapter CapyOS-side consome o header de display-list do sibling `../CapyUI`
  somente no ponto de integração declarado e versionado;
- o fallback in-tree existe para build/continuidade quando o sibling está
  ausente, não para receber features paralelas ao CapyUI.

Uma UI alternativa futura precisa de contrato próprio em
`docs/reference/integration/`, ABI/versionamento na compatibility matrix,
política de pacote/ativação/fallback e gate externo. Ela não deve depender de
internals do kernel ou do compositor além das APIs públicas declaradas.

## Migração inicial

O modelo de widget tree/event routing foi extraído para `CapyUI/src/widget/`
sem compositor, tema dinâmico, fonte ou renderização. `src/gui/widgets/widget.c`
e `include/gui/widget.h` continuam no CapyOS como adaptador ativo acoplado ao
compositor até a etapa de integração correta. `context_menu` e `inline_prompt`
também permanecem no sistema base porque dependem de janelas, input e routing
do desktop ativo.

A Etapa 4 consome a ABI real do sister via
`include/gui/capyui_display_adapter.h` e
`src/gui/widgets/capyui_display_adapter.c`. O adapter recebe
`struct capy_display_list` / `struct capy_dl_cmd` do header do CapyUI
quando o sibling `../CapyUI` está presente; sem sibling, o caminho
falha fechado como indisponível.

O bridge de Fase B expõe `capyui_display_adapter_render_producer_window`,
que recebe um produtor de display-list via callback, renderiza a janela pelo
adapter e mantém o widget model sem acesso direto ao compositor. Esse seam é
host-testado com `capy_widget_emit` real do sibling. Os primeiros fluxos reais
de produção conectados são Calculator, Text Editor, Settings, File Manager,
Task Manager, Taskbar, Taskbar menu/recent popups e Notification overlay do
`../CapyUI`, além de Desktop icons via callback de wallpaper com clip de damage
do compositor, Terminal gráfico, Context menu, Inline prompt e widgets
genéricos do CapyOS, que emitem `capy_display_list` schema v7 quando compilados
pelo CapyOS com `CAPYOS_HAVE_CAPYUI_WIDGET`; a migração dos demais fluxos
desktop/app continua incremental.

## Fronteira

O projeto apartado pode desenvolver:

- modelo retained-mode de widget tree;
- layout de widgets;
- tema/tokens;
- foco e navegação por teclado;
- event routing abstrato;
- accessibility labels;
- display list independente de backend.

O CapyOS base fornece:

- compositor;
- janelas;
- fontes/raster reais;
- input real;
- timers;
- clipboard;
- lifecycle de app;
- integração com launcher/taskbar;
- widgets atuais, context menu e inline prompt enquanto a integração visual
  com o adapter versionado não substituir o fluxo legado.

## Entrada e saída

Entrada:

- árvore de widgets;
- constraints de layout;
- theme tokens;
- eventos abstratos;
- estado de foco.

Saída:

- display list 2D (`capy_display_list`, schema v7);
- hit-test map;
- accessibility tree mínima;
- actions/callback ids;
- cursor/focus request.

## Segurança e robustez

- Widget model não acessa compositor diretamente.
- Callbacks são ids/eventos, não ponteiros crus.
- Layout tem limites de profundidade e nós.
- Falha de layout retorna erro controlado e não derruba desktop.

## Testes apartados obrigatórios

- layout golden tests;
- event routing tests;
- theme fallback tests;
- keyboard focus traversal tests;
- display-list snapshot tests;
- adapter host-side tests em `tests/gui/test_capyui_display_adapter.c`.

## Gate de integração CapyOS

Quando as etapas correspondentes estiverem ativas, recomendar execução externa de:

- `make smoke-x64-vmware-compositor-damage-track`;
- `make smoke-x64-vmware-apps-basic-roundtrip`.
