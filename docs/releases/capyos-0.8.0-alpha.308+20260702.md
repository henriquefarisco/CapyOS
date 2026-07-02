# CapyOS 0.8.0-alpha.308+20260702

**Data:** 2026-07-02
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.308+20260702`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fix triplo do shell grafico/desktop + features de launcher (release coordenado com CapyUI 2.23.0)

## Resumo executivo

alpha.308+20260702: fecha os 3 problemas de campo do shell grafico/desktop relatados pelo operador + 2 features de launcher (com CapyUI 2.23.0; a 2.22.7 publicou o input bridge ring-3 deferido do alpha.305, destravando os 4 testes gfx-owned redirect que derrubaram a CI do alpha.307). FIX 1 (open-browser-graphical nao abria janela): root cause no BUILD -- a variavel CAPYOS_DESKTOP_GRAPHICAL_BROWSER=1 so LINKAVA o blob; o define C nunca chegava aos TUs do kernel (so os smoke targets passavam EXTRA_CFLAGS64), entao embedded_progs/kernel_spawn_capygfx_desktop/comando de shell compilavam FORA e o comando respondia not built into this kernel mesmo no build do operador; e o loop interativo (CAPYGFX_DESKTOP_INTERACTIVE) era opt-in -- sem ele o capygfx sai imediatamente apos present e a janela e destruida no ato. Correcao: navegador grafico vira PADRAO no perfil full (a variavel agora liga CFLAGS64 += -DCAPYOS_DESKTOP_GRAPHICAL_BROWSER + blob linkado; desabilita com CAPYOS_DESKTOP_GRAPHICAL_BROWSER= vazio) e o loop interativo vira padrao no blob de producao, excluido nos builds de smoke boot-exclusivos (CAPYOS_GFX_SMOKE/CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE), cujos markers dependem do exit-0 imediato (alpha.290/294/303/304 inalterados). FIX 2 (exit no terminal grafico fechava o desktop inteiro): desktop_terminal_command tratava exit com desktop_stop(); agora exit fecha SO a janela do terminal (compositor_destroy_window roda desktop_terminal_on_close exatamente uma vez: solta o sink do shell, tira da taskbar); sair da sessao continua explicito via menu Sair ou bye. FIX 3 (help-any sem scroll no terminal grafico): os DOIS painters do terminal (terminal_paint raw e o renderer display-list terminal_display_list.c) ignoravam scroll_offset -- a roda do mouse ate ajustava o offset mas a tela nao mudava. Novo acessor terminal_view_cell(term,row,col) compoe a view scrollback+live (topo N linhas vem do anel de scrollback, resto das celulas vivas) e ambos painters passam por ele; TERM_SCROLLBACK 64->128; desktop_terminal_key ganha PgUp/PgDn (pagina) e setas Up/Down (linha); digitar qualquer tecla volta a view ao vivo; banner do terminal documenta o scroll. DEV (launcher CapyUI 2.23.0): registro unico desktop_menu_apps alimenta o menu iniciar; nova entrada Navegador (fixada) chama o hook estavel kernel_desktop_open_browser_graphical (definido incondicionalmente em todos os perfis; devolve -1 sem blob e o CapyUI mostra o erro no terminal -- falha nunca silenciosa); novo toggle Lista completa/Lista resumida reconstroi o menu com o catalogo inteiro em ordem alfabetica na categoria Apps (busca e scroll do menu ja existentes cobrem o catalogo; apps entregues por modulo ficam com atalho pronto -- disponibilidade sondada no clique, nao no build do menu). Docs: matriz de compatibilidade pina CapyUI 2.23.0; plano mestre atualizado. Validado: make test verde; make validate (CapyUI) verde; make all64 default limpo (blob embutido por padrao); smoke-x64-qemu-capygfx-desktop-spawn verde apos as mudancas de Makefile. Reteste VMware do operador recomendado para os 3 fixes.

## Mudancas

- `Makefile`: `CAPYOS_DESKTOP_GRAPHICAL_BROWSER` vira padrao no perfil full e
  agora liga de verdade o define do kernel (`CFLAGS64 += -D...`); o blob de
  producao do capygfx compila com `-DCAPYGFX_DESKTOP_INTERACTIVE` (loop aberto
  ate WINDOW_CLOSE, com fail-safe), excluido nos builds de smoke boot-exclusivos.
- `include/gui/desktop_runtime.h` + `src/shell/commands/extended.c`: novo hook
  estavel `kernel_desktop_open_browser_graphical()` (definido em todos os
  perfis; spawn via `kernel_spawn_capygfx_desktop` quando o blob existe).
- `include/gui/terminal.h` + `src/gui/terminal/terminal.c` +
  `src/gui/widgets/terminal_display_list.c`: acessor `terminal_view_cell`
  (view scrollback+live) usado pelos dois painters; `TERM_SCROLLBACK` 64->128;
  digitar reseta o scroll para a view ao vivo.
- **CapyUI 2.23.0** (`src/desktop/desktop.c`): `exit` fecha so a janela do
  terminal; PgUp/PgDn/Up/Down navegam o scrollback; registro unico
  `desktop_menu_apps` + entrada "Navegador" + toggle "Lista completa".
- Docs: matriz de compatibilidade (CapyUI 2.23.0), plano mestre, STATUS.

## Validacao

- `make test` -- verde (suite host agregada + browser_pipeline_tests).
- `make layout-audit` / `make version-audit` -- verdes.
- `make all64` default (TOOLCHAIN64=host) -- limpo, 0 warnings nos arquivos
  tocados; blob capygfx embutido por padrao (capyos64.bin ~2.74 MB).
- `make validate` (CapyUI) -- verde (348/348 contratos, decoupling,
  version-check 2.23.0).
- `make smoke-x64-qemu-capygfx-desktop-spawn` -- verde (mecanismo de spawn +
  marker exit-0 preservado com os novos defaults de Makefile).
- **Reteste VMware do operador recomendado** para confirmar os 3 fixes na
  plataforma oficial (janela do navegador via menu/comando, exit no terminal,
  scroll do help-any).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.307+20260701` | `0.8.0-alpha.308+20260702` | Fix triplo do desktop + navegador padrao no perfil full. Sem mudanca de ABI cross-repo (hook novo e aditivo). |
| **CapyUI** | `2.22.6` | `2.23.0` | `2.22.7`: input bridge ring-3 (gfx_owned_redirect). `2.23.0`: launcher (registro + Navegador + Lista completa) + terminal (exit fecha janela + scroll). Aditivo. |

Os demais 5 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.308+20260702`_
