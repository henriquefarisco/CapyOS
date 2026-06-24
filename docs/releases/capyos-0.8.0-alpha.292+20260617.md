# CapyOS 0.8.0-alpha.292+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.292+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.3 — prova de runtime em QEMU do pipeline HTML→pixels em ring-3)

## Resumo executivo

alpha.292+20260617: Etapa 7 / Slice 7.3 (prova de runtime) -- prova HTML->pixels->janela EM RING-3 (QEMU) do pipeline real entregue (host) na alpha.291. O app ring-3 gated /bin/capygfx (smoke da 7.2.2), quando o core grafico do CapyBrowser esta presente no build, passa a renderizar uma PAGINA HTML EMBUTIDA via o pipeline real -- capyos_browser_build_display_list (HTML->DOM->CSS->cascade->layout->display-list) -> rasteriza o display-list com o backend de pixels da 7.2 -> blita pela ABI grafica -> present -- em vez do padrao auto-composto (que vira fallback quando o core esta ausente, via #ifdef CAPYOS_HAVE_CAPYBROWSER_CORE). Isso prova, em runtime e contra o compositor REAL do kernel, a cadeia inteira HTML->pixels->janela a partir de ring-3. Makefile: novo bloco que compila os 11 TUs puros do core (url_parse/url_normalize/origin, html_entities/html_tokenizer, dom/html_parse, css_parse/cascade, layout, display_list) como objetos ring-3 num dir dedicado (capybrowser-gfx/) SEM o rename de capy_url_parse do text core -- porque o capygfx NAO linka capylibc-net, entao nao ha colisao (o rename so existe para o text core coexistir com o net stack num mesmo binario); quando CAPYBROWSER_CORE_AVAILABLE, o capygfx linka esses TUs + o adapter browser_pipeline.o + o rasterizador browser_render_pixel.o + a font compartilhada font8x8_data.o + a <string.h> freestanding, e compila o main.c com -DCAPYOS_HAVE_CAPYBROWSER_CORE + os includes do core. O binario ring-3 resultante tem ~1.33 MiB de .bss (arenas do pipeline ~1.09 MiB + a surface 320x240); o smoke confirma EMPIRICAMENTE que o ELF loader do CapyOS mapeia/zera esse .bss e que o pipeline inteiro executa em ring-3 sem falha. Validado: make capygfx-elf linka limpo (sem colisao de simbolo); make test VERDE (agregado unit_tests + o teste focado do pipeline 13/13, inalterados); layout-audit limpo; e make smoke-x64-qemu-capygfx PASSOU em QEMU+OVMF (boot direto no app que agora renderiza a pagina via pipeline, marker [smoke] capygfx ready observado no COM1). Sem mudanca de ABI nem de TU do kernel (so o app ring-3 capygfx + regras de Makefile mudaram; o kernel e byte-identico). Gate VMware oficial smoke-x64-vmware-browser-graphical (mesma imagem) continua mapeado e PULADO neste ciclo (sustentado por QEMU); ao reabilitar, confirma o render visual ao vivo quando a integracao com o desktop interativo (CapyUI) ligar o loop do compositor. Proximas fatias: 7.4 decode de imagem inline (CapyCodecs), fetch HTTPS->grafico com fallback de texto, cache/cookies/forms, e a janela interativa no desktop. CapyBrowser v0.6.5 consumido como-esta (11 TUs puros no binario ring-3, nunca no kernel); 6 repos irmaos inalterados.

## Mudancas

### App ring-3 capygfx renderiza pagina HTML via pipeline
- **`userland/bin/capygfx/main.c`**: sob `#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE`, em vez do padrao auto-composto (agora fallback quando o core esta ausente), renderiza uma **pagina HTML embutida** via o pipeline real — `capyos_browser_build_display_list` (HTML→DOM→CSS→cascade→layout→display-list) → `capyos_browser_render_pixels` (rasterizador da 7.2) → `capy_surface_blit`/`present`. Exercita a cadeia inteira HTML→pixels→janela em ring-3.

### Makefile (linkagem do pipeline no ring-3, sem colisao)
- Novo bloco que compila os **11 TUs puros do core** (`url_parse`/`url_normalize`/`origin`, `html_entities`/`html_tokenizer`, `dom`/`html_parse`, `css_parse`/`cascade`, `layout`, `display_list`) como objetos ring-3 num dir dedicado (`capybrowser-gfx/`), **sem** o rename de `capy_url_parse` do text core — porque o capygfx **nao** linka `capylibc-net`, entao nao ha colisao (o rename so existe para o text core coexistir com o net stack num binario).
- Quando `CAPYBROWSER_CORE_AVAILABLE`: `capygfx` linka esses TUs + o adapter `browser_pipeline.o` + o rasterizador `browser_render_pixel.o` + `font8x8_data.o` + a `<string.h>` freestanding, e compila `main.c` com `-DCAPYOS_HAVE_CAPYBROWSER_CORE` + os includes do core. O binario ring-3 tem **~1.33 MiB de `.bss`** (arenas do pipeline ~1.09 MiB + a surface).

### Plano
- **`docs/plans/active/capyos-master-plan.md`**: Slice 7.3 boot smoke ring-3 marcado **CONCLUÍDO em `alpha.292`**. `docs/plans/STATUS.md`: nota `alpha.292`.

## Validacao

- `make capygfx-elf` -- **linka limpo** (sem colisao de simbolo; `.bss` 1.33 MiB).
- `make test` -- **verde**: agregado `unit_tests` ("Todos os testes passaram") + o teste focado do pipeline (`13/13`, inalterado).
- `make layout-audit` -- **limpo**. `make version-audit` -- **verde** (current=0.8.0-alpha.292).
- `make smoke-x64-qemu-capygfx` -- **PASSOU** em QEMU+OVMF: boot direto no `/bin/capygfx` que agora renderiza a pagina via o pipeline, marker `[smoke] capygfx ready` observado no COM1. **Confirma empiricamente** que o ELF loader carrega ~1.33 MiB de `.bss` em ring-3 e que o pipeline inteiro executa em ring-3 contra o compositor real — primeira prova de runtime de **HTML→pixels→janela** no CapyOS.
- Sem mudanca de TU do kernel (so o app ring-3 + regras de Makefile); o kernel e byte-identico ao alpha.291.
- **VMware (oficial):** `make smoke-x64-vmware-browser-graphical` mapeado (mesma imagem) e **pulado** neste ciclo conforme instruido (sustentado por QEMU); ao reabilitar, confirma o render visual ao vivo quando a integracao com o desktop interativo (CapyUI) ligar o loop do compositor.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.291+20260617` | `0.8.0-alpha.292+20260617` | Slice 7.3: prova de runtime (QEMU) do pipeline HTML→pixels em ring-3. **Sem mudanca de ABI** (kernel inalterado). |

Sem mudanca de ABI nem de contrato cross-repo. **CapyBrowser** `v0.6.5` consumido
como-esta: os 11 TUs puros do core sao compilados no binario ring-3 capygfx (e no
binario de teste focado), **nunca** no kernel — mesma disciplina de desacoplamento
do text core. Os 6 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.292+20260617`_

_Build: `0.8.0-alpha.292+20260617`_
