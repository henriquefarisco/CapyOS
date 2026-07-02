# CapyOS 0.8.0-alpha.307+20260701

**Data:** 2026-07-01
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.307+20260701`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fix (segundo travamento do shell grafico) + enriquecimento do plano mestre + atualizacao da matriz de compatibilidade

## Resumo executivo

alpha.307+20260701: corrige o SEGUNDO travamento relatado no alpha.306 (registrado la como problema separado e nao investigado) -- o freeze de help/help-any no shell grafico do desktop com pacotes instalados, em boot novo e limpo. Causa raiz (analise estatica): shell_paginate_content (src/shell/core/shell_main/output_files.c) ignorava o sink de saida instalado pelo terminal grafico do CapyUI (shell_set_output_callbacks em CapyUI/src/desktop/desktop.c) -- escrevia direto no framebuffer de boot via vga_putc/vga_newline (por tras do compositor, corrompendo a superficie na tela, resultando em tela solida/azul) E, pior, bloqueava em tty_getc() no prompt do pager (-- mais --). Numa sessao grafica o teclado e entregue pelo dispatcher da GUI ao widget de terminal, NAO pela fila do tty_getc, entao essa leitura nunca retornava -> a sessao inteira congela. So disparava com pacotes instalados porque os comandos extras de app de desktop (open-calculator/open-browser-graphical/etc.) empurram a listagem do help-any acima do limiar do pager (20 linhas); sem pacotes a listagem cabia numa pagina e nao acionava o pager. CORRECAO: quando um sink de saida redirecionado esta instalado (g_shell_output_write nao-NULL), shell_paginate_content roteia o conteudo inteiro pelo sink (shell_print) e pula o pager bloqueante -- o widget de terminal tem seu proprio scrollback. Protege tambem os outros chamadores do pager (update_status, recovery_overview). Alem do fix: plano mestre enriquecido com a secao 20 (etapas e criterios de desenvolvimento por modulo: CapyOS, CapyUI, CapyBrowser, CapyCodecs, CapyAgent, CapyLang, CapyBenchmark) mais os criterios comuns dos modulos desacoplados; matriz de compatibilidade cross-repo atualizada para o estado atual dos siblings (CapyUI 2.22.6, CapyAgent 0.0.10, CapyBrowser 0.6.6, CapyCodecs 0.0.12, CapyLang 0.1.12, CapyBenchmark 0.0.11). Validado: make test verde (suite host agregada mais browser_pipeline_tests 19/19); make layout-audit limpo; make all64 PROFILE=full CAPYOS_DESKTOP_GRAPHICAL_BROWSER=1 TOOLCHAIN64=host build limpo. A secao paginada afetada e codigo de shell sempre compilado; o fix nao depende de gate de smoke. Reteste em VMware real recomendado ao operador para confirmar o fim do freeze de help no shell grafico com pacotes instalados.

## Mudancas

- `src/shell/core/shell_main/output_files.c`: `shell_paginate_content` agora
  detecta um sink de saida redirecionado (`g_shell_output_write != NULL`, o
  terminal grafico do CapyUI) e, nesse caso, imprime o conteudo inteiro pelo
  sink (`shell_print`) sem acionar o pager de framebuffer bruto (que escrevia
  via `vga_putc`/`vga_newline` por tras do compositor e bloqueava em
  `tty_getc()`). O caminho de console cru (sem callbacks) mantem o pager
  interativo "-- mais --" original. Corrige o freeze de `help`/`help-any`
  (e protege `update_status`/`recovery_overview`, que usam o mesmo pager).
- `docs/plans/active/capyos-master-plan.md`: nova secao 20 "Modulos — etapas e
  criterios de desenvolvimento por modulo" (criterios comuns dos modulos
  desacoplados + trilha/aceite por modulo: CapyOS, CapyUI, CapyBrowser,
  CapyCodecs, CapyAgent, CapyLang, CapyBenchmark); header e nota da Etapa 7
  atualizados para `alpha.307`; secao "Proximo comando esperado" renumerada.
- `docs/reference/integration/compatibility-matrix.md`: pins dos siblings
  atualizados para o estado atual + linha do CapyOS core movida para `alpha.307`.
- `docs/plans/STATUS.md`, `README.md`, `include/core/version.h`, `VERSION.yaml`:
  bump determinístico `alpha.306` → `alpha.307` via `make bump-alpha`.

## Validacao

- `make test` -- VERDE (suite host agregada + `browser_pipeline_tests` 19/19).
- `make layout-audit` -- limpo (sem warnings). `make version-audit` -- VERDE
  (`current=0.8.0-alpha.307 extended=0.8.0-alpha.307+20260701`).
- `make all64 PROFILE=full CAPYOS_DESKTOP_GRAPHICAL_BROWSER=1 TOOLCHAIN64=host`
  -- build limpo (capyos64.bin gerado, 0 erros), exatamente a config que
  reproduz o freeze relatado.
- **Nao validado ainda em VMware real** -- o fix e baseado em root-cause estatico
  do caminho de paginacao do shell grafico; reteste do operador recomendado para
  confirmar o fim do freeze de `help` com pacotes instalados.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.306+20260701` | `0.8.0-alpha.307+20260701` | Fix do segundo travamento do shell grafico (pager) + plano mestre + matriz. Sem mudanca de ABI. |

Sem mudanca de ABI. Os 6 repos irmaos permanecem inalterados nesta release; a
matriz de compatibilidade apenas registra as versoes locais ja existentes dos
siblings (CapyUI 2.22.6, CapyAgent 0.0.10, CapyBrowser 0.6.6, CapyCodecs 0.0.12,
CapyLang 0.1.12, CapyBenchmark 0.0.11).

_Build: `0.8.0-alpha.307+20260701`_
