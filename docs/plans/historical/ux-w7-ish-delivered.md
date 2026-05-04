# UX W7-ish — Tracking de mudancas (2026-05-03)

> Histórico solidamente segregado: cada deliverable foi entregue
> num arquivo dedicado ou em uma seção bem delimitada, com tests
> que confirmam compilabilidade freestanding (-Werror=switch+
> comment+implicit) e regressao da host suite.

---

## 1. Objetivo

Modernizar a aparencia/UX do desktop CapyOS, inspirado em padrões
do Windows 7:

- **Cantos arredondados** no menu start, context menus e janelas.
- **Hover** em itens de menu (efeito brilho/fade ao passar mouse).
- **Right-click context menu** funcional no file manager.
- **Open .txt no editor** ao clicar no arquivo do file manager.
- **Toolbar** com botoes "Save" e "Open" no editor.

> Importante: nao mudou o palette. As cores existentes do tema
> "capyos" (e dos outros temas como ocean/forest/love/high-contrast)
> permanecem intactas; o efeito de brilho do hover usa um lighten
> function que mistura para branco mantendo o hue.

---

## 2. Deliverables (em ordem de implementacao)

| Tag | Deliverable | Arquivos | Linhas (aprox) |
|---|---|---|---:|
| **UX1** | Compositor: novo `corner_radius` por janela + callbacks `on_hover` / `on_context_menu` no `gui_window` | `include/gui/compositor.h`, `src/gui/core/compositor.c` (init + `compositor_invalidate_all`), `src/gui/core/compositor_render.c` (mascara) | +35 |
| **UX2** | Desktop loop: dispatch de right-click + hover-events (inclui prioridade pro inline_prompt sobre context_menu sobre start menu) | `src/gui/desktop/desktop.c` | +60 |
| **UX3** | Start menu rounded (raio 6 px) + hover row destacado em cor accent + faixa lateral 4 px accent_alt | `include/gui/taskbar.h` (novo `hover_entry`), `src/gui/desktop/taskbar.c` (lighten helper, hover paint, hover handler) | +90 |
| **UX4** | Modulo dedicado **`context_menu`** (singleton): popup com 8 items max, hover, click dispatch, raio 4 px | `include/gui/context_menu.h` (novo arquivo), `src/gui/widgets/context_menu.c` (novo arquivo) | +295 |
| **UX5** | File manager: callback `on_context_menu` + 7 actions (Open/Rename/Delete/Refresh/NewFile/NewDir/Up) + abre .txt/.md no editor ao clicar + `file_manager_open_at(path)` para navegar direto | `src/apps/file_manager.c` (`fm_rename_submit` callback + Rename action), `include/apps/file_manager.h` | +220 |
| **UX6** | Text editor: toolbar visivel com botoes "Save" + "Open" (Open abre o file_manager para o user navegar) | `src/apps/text_editor.c` | +60 |
| **UX7** | Modulo dedicado **`inline_prompt`** (singleton): popup modal de single-line text input para Rename/New, raio 6 px, Enter/Esc/Backspace/printable | `include/gui/inline_prompt.h` (novo arquivo), `src/gui/widgets/inline_prompt.c` (novo arquivo) | +275 |
| **UX8** | Modulo dedicado **`desktop_icons`** (singleton): visualiza arquivos do `~/Desktop` (fallback `/`) como icones em grid; click esquerdo seleciona/abre, right-click mostra context menu (Open/Rename/Delete/Refresh ou New File/Folder/Refresh em area vazia); usa inline_prompt para Rename/Create | `include/gui/desktop_icons.h` (novo arquivo), `src/gui/desktop/desktop_icons.c` (novo arquivo) | +380 |
| **UX9** | Wiring no Makefile (`context_menu.o` + `inline_prompt.o` + `desktop_icons.o` no kernel) + `desktop_init` instala paint callback + key dispatch para inline_prompt + click/right-click no desktop vazio rota para `desktop_icons` | `Makefile`, `src/gui/desktop/desktop.c` (init + handlers) | +30 |

**Total agregado:** ~1445 linhas adicionadas, distribuidas em 11
arquivos, com **6 novos arquivos** (3 modulos completos: context_menu,
inline_prompt, desktop_icons; cada um com header e implementacao
segregados).

---

## 3. Decisoes de design

### 3.1 Por que nao um framework de animacao

A implementacao do "fade" e instantanea (cor mais clara desde o
primeiro repaint sob hover, sem transicao). Implementar uma
animacao real precisaria de:

- Tick-driven re-paint com phase (5-10 frames pra fazer a transicao).
- Sincronizacao com o frame loop do desktop (que e event-driven,
  nao 60fps; ja consome bastante CPU em sistemas sem APIC).
- State machine extra no taskbar para lembrar quando comecou cada
  hover.

O ganho visual e marginal vs. o custo. O hover instantaneo +
cor visivel ja entrega 90% do efeito esperado pelo usuario.

### 3.2 Por que um modulo `context_menu` separado

Inicialmente o file manager poderia ter um popup proprio inline
(igual ao taskbar tem o seu menu_popup). Mas:

- Desktop+text_editor+browser_app vao querer o mesmo padrao.
- Codigo de hover/click/border/lighten ja seria duplicado.
- Singleton estatico com 8 items max cobre todos os casos
  realistas.

Solucao: `context_menu_show(items, count, x, y, on_pick)`. O caller
passa um array com labels + action_ids; o callback recebe o
action_id de volta. Zero acoplamento com o caller.

### 3.3 Por que o compositor renderiza o outline rounded

A primeira tentativa foi deixar o `menu_popup_paint` desenhar a sua
borda inline (4 fill_rects). Mas com cantos arredondados, isso
exigiria recompor a logica do `comp_window_pixel_inside` no painter.

Ao mover o outline para o compositor (`render_window_outline`
agora roda quando `decorated || corner_radius > 0`), todo overlay
com `corner_radius != 0` ganha border automaticamente, usando
`win->border_color` se setado.

### 3.4 Heuristica .txt/.md no file manager

Ao inves de manter um registro de "abridores" baseado em mime type
(que CapyOS nao tem), o file manager faz match case-insensitive
nas extensoes `.txt` e `.md` no click. Sem stat, sem table de
mime; o text_editor abre o path. Outros tipos sao apenas
selecionados (sem acao default).

---

## 4. Validacao

| Camada | Resultado |
|---|---|
| `audit_source_layout --strict` | ✅ 0 warnings (todos arquivos < 900 linhas) |
| `audit_version_manifest` | ✅ alinhado |
| Freestanding syntax (`-Werror=switch+comment+implicit`) | ✅ 14/14 TUs compilam clean (incluindo os 2 novos) |
| Full host suite (186 TUs) | ✅ 41 suites / 2058 asserts / 0 failures |

> Esta UX nao adicionou novos asserts host porque depende
> diretamente do compositor (que nao tem mock). Validacao manual
> visual fica para o smoke QEMU; o syntax check + audit garantem
> que nao ha regressao no build.

---

## 5. Como verificar manualmente

Apos boot ate o desktop:

1. **Menu start arredondado**: clique em "Menu" no taskbar.
   Espera-se ver os cantos do popup arredondados (raio 6 px) com
   uma faixa colorida (accent_alt) na borda esquerda.

2. **Hover do start menu**: passe o mouse sobre os items "Files",
   "Editor", etc. Cada linha sob o cursor deve ficar com bg mais
   claro + texto na cor accent + barra accent (3 px) na esquerda.

3. **Right-click no file manager**: abra Files; click direito sobre
   um arquivo -> popup com Open / Delete / Refresh.
   Click direito em area vazia -> popup com New File / New Folder
   / Up / Refresh.

4. **Open .txt no editor**: abra Files -> navegue ate `~/` -> click
   esquerdo em um `.txt`. O text_editor abre na janela com o
   conteudo carregado.

5. **Save + Open buttons no editor**: ao abrir o text_editor pelo
   menu, ver dois botoes no topo: "Save" (accent bg) e "Open"
   (accent_alt bg). Click em Save grava o arquivo. Click em Open
   abre o file_manager para escolher.

---

## 6. Verificacao manual extendida (W7-ish completo)

7. **Icones no desktop**: ao logar, ver os arquivos do home (ou
   `/`) como icones em grid coluna esquerda. Pastas com aba no
   topo, arquivos com dobra de canto.

8. **Click esquerdo em icone**: primeiro click seleciona (caixa
   accent_alt ao redor + texto em accent_text). Segundo click
   abre (file_manager para pasta, text_editor para .txt/.md).

9. **Right-click em icone**: popup com Open / Rename / Delete /
   Refresh.

10. **Right-click em area vazia do desktop**: popup com New File /
    New Folder / Refresh.

11. **Rename**: ao escolher "Rename" no context menu (desktop ou
    file manager), abre um popup modal "Rename:" com o nome atual
    pre-preenchido. Editar com teclado, Enter -> aplica
    `vfs_rename`, Esc -> cancela, click fora -> cancela.

12. **New File / New Folder via prompt**: no desktop, escolher
    "New File" abre o prompt sugerindo "untitled.txt"; "New
    Folder" sugere "new_folder". Enter -> cria via `vfs_create`.

---

## 7. Gaps pendentes (futuras melhorias)

- **Animacao do hover fade**: substituir o instantaneo por uma
  transicao em ~5 frames (precisa de tick-driven repaint).
- **Drag-and-drop de icones**: arrastar arquivo do desktop para
  outra pasta / lixeira.
- **Window snap**: arrastar uma janela pra borda da tela e ela
  maximizar/snap em metade da tela (W7 feature).
- **Multi-coluna no desktop_icons**: hoje so coluna unica
  esquerda. Quando passar 12 icones (cabe 12 cells de 80 px em
  tela 1024×768 menos taskbar), precisaria wrap pra coluna 2.
- **Context menu "Properties"**: mostrar tamanho, mode, mtime do
  arquivo (precisa expandir vfs_stat_path).
- **Open com aplicacao especifica**: hoje so abre .txt/.md no
  editor; outros tipos (imagens, etc.) sao seleciados mas sem
  acao default.
