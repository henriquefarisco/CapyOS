# CapyOS 0.8.0-alpha.288+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.288+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.1 — abertura da Etapa 7)

## Resumo executivo

alpha.288+20260617: ABRE a Etapa 7 (browser usavel com web estatica moderna) com o Slice 7.1 -- o primeiro render backend CapyOS-side para o display-list desacoplado do capy-browser-core. Contexto: o CapyBrowser (sister repo, v0.6.5) ja entrega, puro e host-testado, todo o core estatico (DOM M1, CSS parse+cascade M2, block layout M3a, e o display-list versionado capy_dl M3b: nos TEXT/RECT/IMAGE/LINK com geometria em celulas de layout); o lado CapyOS e que faltava consumir esse display-list. Slice 7.1 entrega o consumidor: novo modulo userland/bin/capybrowse/browser_render.{c,h} -- capyos_browser_render_text() rasteriza o capy_dl num grid de caracteres limitado (celula (x,y) -> coluna x / linha y, fiel porque a geometria ja e em celulas), produzindo uma view de texto deterministica (runs de TEXT posicionados por coluna, placeholders [img:alt] para IMAGE, lista numerada Links: para LINK, RECT/background contados e omitidos no modo texto). Puro, allocation-free, deterministico, fail-closed e freestanding (host-testavel E linkavel no ring-3). Seguranca: bytes de controle (<0x20 ou 0x7F) do conteudo remoto nao-confiavel sao sanitizados para '?' (bloqueia injecao de escape de terminal); UTF-8 (>=0x20) passa intacto; ranges de payload fora da arena de strings sao fail-closed; o content extent e limitado (MAX_COLS=200/MAX_ROWS=2000) contra documentos remotos gigantes. Satisfaz o criterio 6 da Etapa 7 (parser/layout/display-list substituiveis sem acoplar o core puro ao compositor): o display-list desacoplado agora tem um render backend CapyOS, testado. Novo host test tests/userland/test_browser_render.c (placement por coluna no grid, determinismo, placeholder de imagem, sanitizacao de controle, fail-closed em NULL/versao/payload-fora-da-arena, clipping off-grid). Deteccao de sibling CAPYBROWSER_CORE_AVAILABLE (display_list.h presente) -> CAPYBROWSER_CORE_CFLAGS (-DCAPYOS_HAVE_CAPYBROWSER_CORE + includes displaylist/css/html/layout) em HOST_CFLAGS + TEST_SRCS; no-op em checkout CapyOS-only. Validado: make test verde; layout-audit limpo; version-audit verde (com a guarda de pin do alpha.287). Sem mudanca de runtime/ABI do kernel; CapyBrowser v0.6.5 consumido como-esta (so headers; nenhum TU extra linkado ainda); 6 irmaos sem bump. Proximas fatias da Etapa 7 (sequenciadas): 7.2 superficie grafica ring-3 (syscalls de janela/blit/present seguros, copy-in validado) + render do display-list em pixels; 7.3 decode de imagem inline (adapter CapyCodecs capy-codec-image v2, injecao de allocator+inflater); 7.4 pipeline real no app; 7.5 cache HTTP; 7.6 cookies/forms; sempre mantendo o modo texto como fallback.

## Mudancas

### Etapa 7 aberta (plano)
- **`docs/plans/active/capyos-master-plan.md`**: tabela sequencial Etapa 7 -> **Em andamento (alpha.288)**; bloco "ATIVA desde alpha.288" + **Plano de slices** (7.1 concluido; 7.2 superficie grafica ring-3; 7.3 imagens; 7.4 pipeline real; 7.5 cache/cookies/forms; 7.6 streaming/limites/cert) na secao da Etapa 7.
- **`docs/plans/STATUS.md`**: rotulo de etapa atual -> Etapa 7 em andamento; nota `alpha.288` com o conteudo do Slice 7.1.

### Slice 7.1 — render backend do display-list
- **`userland/bin/capybrowse/browser_render.h` / `browser_render.c`** (novos): `capyos_browser_render_text(const struct capy_dl *, char *out, size_t cap, stats*)` -- consumidor CapyOS-side do display-list `capy_dl` (capy-browser-core M3b). Rasteriza num grid de caracteres limitado; TEXT por coluna, IMAGE como `[img:alt]`, LINK como lista numerada `Links:`. Puro/allocation-free/deterministico/fail-closed/freestanding. Sanitiza bytes de controle do conteudo remoto; payload fora da arena e fail-closed; extent limitado.
- **`tests/userland/test_browser_render.c`** (novo) + registro em **`tests/test_runner.c`**: grid/coluna, determinismo, `[img:...]`, sanitizacao de controle, fail-closed (NULL/versao/payload-fora-da-arena), clipping off-grid.
- **`Makefile`**: deteccao `CAPYBROWSER_CORE_AVAILABLE` (sibling `src/displaylist/display_list.h`) -> `CAPYBROWSER_CORE_CFLAGS` (`-DCAPYOS_HAVE_CAPYBROWSER_CORE` + includes `displaylist`/`css`/`html`/`layout`) em `HOST_CFLAGS`; `TEST_SRCS += test_browser_render.c` (sempre, no-op sem o flag) e `+= browser_render.c` (quando o core esta presente). Mesma disciplina de desacoplamento do text core (headers nunca entram em src/ ou include/ do kernel).

### Versao
- Bump `0.8.0-alpha.287` -> `0.8.0-alpha.288` (via `make bump-alpha`).

## Validacao

- `make test` -- **verde** ("Todos os testes passaram"), incluindo o novo `test_browser_render`.
- `make layout-audit` -- **limpo**. `make version-audit` -- **verde** (guarda de pin de modules-index do alpha.287 ativa; nenhum pin tocado).
- Sem mudanca de C de runtime no kernel; ISO/all64 inalterados (a CI de release reconstroi e revalida). O render backend e pura logica host-testavel; a integracao grafica/ring-3 e o wiring do app no fluxo real sao as fatias 7.2/7.4.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.287+20260617` | `0.8.0-alpha.288+20260617` | Abre a Etapa 7 (Slice 7.1); sem mudanca de ABI. |

Sem mudanca de ABI nem de contrato cross-repo. **CapyBrowser** `v0.6.5` e consumido
como-esta (apenas os headers do display-list; nenhum TU extra linkado ainda) atraves
do contrato `browser-core-integration-contract.md`. Os 6 repos irmaos permanecem
inalterados.

_Build: `0.8.0-alpha.288+20260617`_
