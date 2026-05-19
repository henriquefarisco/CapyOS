# CapyUI widget model — contrato de integração com CapyOS

**Status:** referência para desenvolvimento apartado.
**Integração planejada:** Etapas 4 e 6, com o compositor/input permanecendo no sistema base.
**Repositório externo atual:** `CapyUI`.

## Migração inicial

O modelo de widget tree/event routing foi extraído para `CapyUI/src/widget/`
sem compositor, tema dinâmico, fonte ou renderização. `src/gui/widgets/widget.c`
e `include/gui/widget.h` continuam no CapyOS como adaptador ativo acoplado ao
compositor até a etapa de integração correta. `context_menu` e `inline_prompt`
também permanecem no sistema base porque dependem de janelas, input e routing
do desktop ativo.

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
- widgets atuais, context menu e inline prompt enquanto não houver adaptador
  versionado.

## Entrada e saída

Entrada:

- árvore de widgets;
- constraints de layout;
- theme tokens;
- eventos abstratos;
- estado de foco.

Saída:

- display list 2D;
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
- display-list snapshot tests.

## Gate de integração CapyOS

Quando as etapas correspondentes estiverem ativas, recomendar execução externa de:

- `make smoke-x64-vmware-compositor-damage-track`;
- `make smoke-x64-vmware-apps-basic-roundtrip`.
