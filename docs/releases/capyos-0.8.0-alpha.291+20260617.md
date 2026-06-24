# CapyOS 0.8.0-alpha.291+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.291+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.3 — pipeline real HTML→display-list, nucleo host-provado)

## Resumo executivo

alpha.291+20260617: Etapa 7 / Slice 7.3 (nucleo) -- liga o PIPELINE REAL HTML->display-list, provando HTML->pixels ponta-a-ponta em host. Novo adapter CapyOS-side userland/bin/capybrowse/browser_pipeline.{c,h} (capyos_browser_build_display_list) que dirige os CINCO estagios puros do core desacoplado do CapyBrowser v0.6.5 -- capy_html_parse (HTML->DOM) -> capy_css_parse (CSS->stylesheet) -> capy_css_cascade (estilos computados) -> capy_layout (box tree, viewport em celulas) -> capy_displaylist (display-list com resolucao de href relativo->absoluto) -- produzindo o capy_dl a partir de HTML(+CSS). Nao ha facade no core, entao o adapter e o unico lugar que o CapyOS sequencia os estagios; arenas intermediarias + saida (~1 MiB) vivem no .bss do TU (single-shot/nao-reentrante, determinístico, fail-closed: qualquer estagio que falhe retorna NULL com o indice do estagio em stats). Pre-requisito do decode de imagem (os nos IMAGE vem do pipeline), por isso movido a frente da fatia de imagens no master-plan. Validacao em host por um BINARIO DE TESTE FOCADO (make test-browser-pipeline, novo): por que separado do agregado unit_tests -- o url_parse.c do irmao define capy_url_parse, que COLIDE com o simbolo de mesmo nome do capylibc-net presente no agregado; o binario focado linka o core do browser SEM capylibc-net (mesma isolacao que o text core consegue via rename), entao nao ha colisao. O teste dirige uma pagina HTML+CSS embutida pelos 5 estagios e afirma o display-list (nos TEXT, no LINK com URL resolvida absoluta a partir da base, texto extraido na arena) e entao RASTERIZA o display-list real via o backend de pixels da 7.2 (capyos_browser_render_pixels) -- provando HTML->pixels ponta-a-ponta no host (13/13 checks: pipeline ok, sem-falha-de-estagio, versao, nos, TEXT/LINK, resolucao de link, raster ok, glifos desenhados, fail-closed em html NULL, CSS de autor ok). make test agora roda o agregado E o teste do pipeline (condicional a CAPYBROWSER_CORE_AVAILABLE, no-op em checkout CapyOS-only) -- AMBOS VERDES; layout-audit limpo. Os 11 TUs do core (url_parse/url_normalize/origin, html_entities/html_tokenizer, dom/html_parse, css_parse/cascade, layout, display_list) compilam freestanding (so a string.h minima) e linkam sem libc de host alem do test harness. Sem mudanca de runtime/ABI do kernel (adapter e host-test + ring-3-linkavel; nada novo no kernel nesta fatia); o boot smoke ring-3 do pipeline (capygfx renderizando a pagina embutida via pipeline contra o compositor real, prova de runtime em QEMU) e a fatia seguinte (alpha.292). CapyBrowser v0.6.5 consumido como-esta (headers + os 11 TUs puros compilados no binario de teste/ring-3, nunca no kernel); 6 repos irmaos inalterados.

## Mudancas

### Adapter do pipeline (CapyOS-side, ring-3 + host)
- **`userland/bin/capybrowse/browser_pipeline.{h,c}`** (novos): `capyos_browser_build_display_list(html, html_len, css, css_len, base_url, viewport_width, &stats)` dirige os 5 estagios puros do core do CapyBrowser — `capy_html_parse` → `capy_css_parse` → `capy_css_cascade` → `capy_layout` → `capy_displaylist` — e retorna o `capy_dl` resultante (ponteiro p/ storage estatico interno; ~1 MiB de arenas no `.bss`). Single-shot/nao-reentrante, deterministico, fail-closed (qualquer estagio que falhe → NULL + `stats->stage_failed`).

### Teste de host focado (sem colisao de simbolo)
- **`tests/userland/test_browser_pipeline.c`** (novo) + alvo **`make test-browser-pipeline`** (Makefile): binario standalone que linka o adapter + o rasterizador da 7.2 + os 11 TUs do core do CapyBrowser (`url_parse`/`url_normalize`/`origin`, `html_entities`/`html_tokenizer`, `dom`/`html_parse`, `css_parse`/`cascade`, `layout`, `display_list`) + `font8x8_data.c`, **sem** `capylibc-net`. Por que separado do agregado `unit_tests`: o `url_parse.c` do irmao define `capy_url_parse`, que colide com o simbolo de mesmo nome do `capylibc-net` presente no agregado; o binario focado evita a colisao (mesma isolacao que o text core obtem via rename). Dirige uma pagina HTML+CSS embutida pelos 5 estagios e afirma o display-list (TEXT, LINK com URL resolvida absoluta, texto na arena) **e** rasteriza o display-list real em pixels (HTML→pixels ponta-a-ponta).
- **`Makefile`**: `make test` agora roda o agregado **e** `test-browser-pipeline` (condicional a `CAPYBROWSER_CORE_AVAILABLE`; no-op em checkout CapyOS-only).

### Plano
- **`docs/plans/active/capyos-master-plan.md`**: **7.3 = pipeline real HTML→display-list** (nucleo host-provado em `alpha.291`), **reordenado a frente** do decode de imagem (agora 7.4) porque o pipeline e quem emite os nos IMAGE. `docs/plans/STATUS.md`: nota `alpha.291`.

## Validacao

- `make test` -- **verde**: agregado `unit_tests` ("Todos os testes passaram") **e** `test-browser-pipeline` (**13/13** checks: pipeline HTML→display-list, resolucao de link relativo→absoluto, HTML→pixels via rasterizador da 7.2, fail-closed em html NULL, CSS de autor).
- `make layout-audit` -- **limpo**. `make version-audit` -- **verde** (current=0.8.0-alpha.291).
- `make all64` -- inalterado nesta fatia (nenhuma mudanca de TU do kernel; a CI de release reconstroi e revalida). O adapter e host-testavel + ring-3-linkavel.
- **Proxima fatia (alpha.292):** boot smoke ring-3 do pipeline em QEMU — `/bin/capygfx` (ou app dedicado) renderiza a pagina HTML embutida via o pipeline → rasteriza → blita pela ABI da 7.2 → present, provando HTML→pixels→janela em runtime contra o compositor real. Gate VMware (render visual ao vivo) mapeado e pulado neste ciclo conforme instruido (sustentado por QEMU).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.290+20260617` | `0.8.0-alpha.291+20260617` | Slice 7.3: pipeline real HTML→display-list (host-provado). **Sem mudanca de ABI** (kernel inalterado). |

Sem mudanca de ABI nem de contrato cross-repo. **CapyBrowser** `v0.6.5` consumido
como-esta: os 11 TUs puros do core sao compilados no binario de teste focado (e,
na proxima fatia, no binario ring-3), **nunca** no kernel — mesma disciplina de
desacoplamento do text core. Os 6 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.291+20260617`_

_Build: `0.8.0-alpha.291+20260617`_
