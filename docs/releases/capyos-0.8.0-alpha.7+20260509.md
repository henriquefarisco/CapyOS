# CapyOS 0.8.0-alpha.7+20260509

**Data:** 2026-05-09
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch incremental de **CapyUI/F6** focado em
usabilidade, segurança de prompts sensíveis e isolamento de overlays no
desktop. O objetivo foi transformar menus, prompts e taskbar em barreiras
mais previsíveis de interação, evitando vazamento de cliques, scroll, hover,
cursor hints e `Esc` para janelas ou ícones por trás.

A versão alinhada é `0.8.0-alpha.7+20260509`.

## Principais entregas

### Inline prompt seguro e editável

- `inline_prompt_show_secret()` adiciona modo de entrada sensível com máscara.
- Buffers internos e snapshot submetido são higienizados após uso.
- O fluxo de criação de usuário do Settings usa prompt secreto para senha.
- O prompt ganhou navegação por `Left`, `Right`, `Home`, `End`, `Delete` e
  click-to-caret dentro do campo.
- Textos longos mantêm viewport horizontal e caret dentro da área visível.

### Menus resilientes

- `context_menu_show()` passa a limitar o popup ao viewport do compositor via
  `compositor_screen_size()`.
- Labels do context menu usam fitting com ellipsis.
- O menu da taskbar também calcula posição com clamp de viewport e reposiciona
  o popup reutilizado a cada abertura.
- Labels de taskbar, janelas e menu popup respeitam largura disponível.
- Hit-test da taskbar foi alinhado ao layout desenhado para evitar foco em itens
  visualmente cortados.

### Isolamento modal de overlays

- Right-click com `inline_prompt` aberto é absorvido pelo prompt e não abre
  menus por trás.
- Hover/cursor hints de janelas encobertas são bloqueados quando prompts ou
  menus estão ativos.
- Mouse wheel é consumido quando `inline_prompt`, context menu ou menu da
  taskbar está aberto, sem rolar a janela focada/terminal por trás.
- `Esc` fecha context menu/menu da taskbar antes de alcançar a fila GUI ou a
  janela focada.
- `desktop_overlay_active()` centraliza o estado de overlays para cursor e
  scroll, reduzindo duplicação e risco de divergência.

## Arquivos de maior impacto

- `src/gui/widgets/inline_prompt.c`
- `include/gui/inline_prompt.h`
- `src/apps/settings.c`
- `src/gui/widgets/context_menu.c`
- `include/gui/context_menu.h`
- `src/gui/desktop/taskbar.c`
- `include/gui/taskbar.h`
- `src/gui/desktop/desktop.c`
- `src/gui/core/compositor.c`
- `include/gui/compositor.h`

## Compatibilidade

- Mantém compatibilidade com `inline_prompt_show()` existente.
- Não altera ownership do framebuffer nem do compositor; `compositor_screen_size()`
  é um accessor somente leitura.
- Não muda o contrato de ações de menus; apenas posicionamento, fitting visual e
  isolamento de input foram ajustados.
- Screenshots oficiais continuam apontando para a coleção CapyUI existente até
  nova captura visual validada.

## Validação

Nesta sessão de fechamento, a validação foi feita por revisão estática de código
e documentação. Não foram executados `make`, `git`, scripts de build, scripts de
teste ou automação Python.

Pontos revisados estaticamente:

- `VERSION.yaml` declara `current=0.8.0-alpha.7` e
  `extended=0.8.0-alpha.7+20260509`.
- `include/core/version.h` declara `CAPYOS_VERSION_FULL` alinhado.
- `README.md` declara `Versao de referencia: 0.8.0-alpha.7`.
- Esta release note existe em `docs/releases/` e declara o extended version no
  cabeçalho.
- `docs/releases/README.md`, `docs/plans/STATUS.md` e o master plan foram
  atualizados para refletir o fechamento do patch.

## Status do pacote

- ✅ Versão e documentação alinhadas para fechamento manual do patch.
- ✅ CapyUI/F6 ganhou isolamento modal consistente para prompts e menus.
- ✅ O patch pode ser revisado e registrado manualmente pelo operador.

## Próximos passos

- Operador pode executar os comandos `git` manualmente para registrar o patch.
- A validação executável (`make version-audit`, build e smokes) fica para o
  operador/CI, conforme as restrições desta sessão.
