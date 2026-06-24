# CapyOS 0.8.0-alpha.293+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.293+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.4 — decode de imagem inline, núcleo host-provado; coordenação cross-repo com CapyBrowser 0.6.6)

## Resumo executivo

alpha.293+20260617: Etapa 7 / Slice 7.4 (decode de imagem inline -- nucleo host-provado). Fecha a cadeia <img> -> pixels no backend de render do CapyOS, em duas partes desacopladas. (1) CROSS-REPO -- CapyBrowser 0.6.6 (ja liberado: develop+main, CI+Security verdes, tag v0.6.6): o emissor de display-list passa a resolver o src de um <img> contra base_url pelo nucleo de URL Fase C1 (espelhando <a href>) e a grava-lo nos campos url_off/url_len do no IMAGE -- os MESMOS campos que LINK ja usava -- de modo que o consumidor (o backend de render do CapyOS) possa buscar/decodar a imagem em vez de so mostrar o placeholder do alt. Aditivo e retrocompativel: CAPY_DL_VERSION continua 1 (o campo ja existia para LINK); um src que nao resolve deixa o payload de URL vazio. (2) CAPYOS -- novo adapter de decode userland/bin/capybrowse/browser_image.{h,c} sobre o ABI capy-codec-image v2 do CapyCodecs: injeta um bump allocator limitado (arena estatica de 512 KiB em .bss, resetada por decode, cap de dimensao 192x192, fail-closed em OOM) + um inflater zlib via tinf in-tree (PNG/ICO), entregando o capy_image_rgba32 (ARGB32) do core de codecs desacoplado sem o CapyOS jamais decodar imagem por conta propria. O rasterizador browser_render_pixel.{c,h} ganha um callback opcional resolve_image (image-provider, desacopla o render do decode/fetch) + um blit escalado (nearest-neighbor, clipado): um no IMAGE com src resolvido e pixels decodificados e blitado escalado a caixa do no (e incrementa images_decoded) em vez do placeholder; o placeholder bordado + label alt permanece como fallback quando nao ha resolver, nao ha src, ou o decode falha. Makefile: novo bloco de deteccao do core CapyCodecs (CAPYCODECS_IMAGE_AVAILABLE/_SRCS/_CFLAGS + TINF_IMAGE_SRCS) que, quando ../CapyCodecs esta presente, liga as 8 TUs de codec (image/detect/metadata/bmp/png/jpeg/qoi/ico) + as 3 TUs de tinf (tinflate/tinfzlib/adler32) + o browser_image.o no binario de teste focado com -DCAPYOS_HAVE_CAPYCODECS_IMAGE; sem colisao de simbolo (os codecs/tinf nao tem capy_url_parse). Tudo puro, allocation-free (arena), determinístico, fail-closed e freestanding (host-testavel e ring-3 linkable); o KERNEL e byte-identico (so o adapter ring-3 + o rasterizador + regras de Makefile + testes + docs mudaram). Validado: make test VERDE (agregado unit_tests + test-browser-pipeline 19/19 -- inclui decode de um BMP 1x1 -> ARGB 0xFF112233, decode de um PNG 2x2 real via o inflater tinf -> red/green/blue/white, bytes nao-imagem -> fail-closed, e rasterizacao de uma pagina com <img> em que o resolver decoda -> images_decoded>=1); make layout-audit limpo; make all64 (clean) VERDE -> build/capyos64.bin. A prova de decode EM RING-3 (o app capygfx renderiza uma pagina com imagem embutida, decodando em ring-3 -> blit -> present, em QEMU) segue na fatia 7.4.2 / alpha.294. Gate VMware oficial smoke-x64-vmware-browser-graphical (mesma imagem) continua mapeado e PULADO neste ciclo (sustentado por QEMU). Compliance cross-repo: CapyBrowser 0.6.5 -> 0.6.6 (capy-browser-core v1, aditivo dentro de v1 -- sem bump de ABI); CapyCodecs 0.0.x / capy-codec-image v2 consumido como-esta (8 TUs + tinf so no binario de teste focado, nunca no kernel); 5 repos irmaos inalterados.

## Mudancas

### Cross-repo: CapyBrowser 0.6.6 (display-list IMAGE carrega o src resolvido)
- **`CapyBrowser` `0.6.5` → `0.6.6`** (já liberado: develop+main `a6c506e`, CI+Security verdes, tag `v0.6.6`): o emissor de display-list (`src/displaylist/display_list.c`) resolve o `src` de um `<img>` contra `base_url` pelo núcleo de URL Fase C1 (espelhando `<a href>`) e o grava em `url_off`/`url_len` do nó IMAGE — os **mesmos campos** que LINK já usava. **Aditivo:** `CAPY_DL_VERSION` continua 1 (o campo já existia); `src` que não resolve deixa o payload vazio. Golden + dump format + `docs/compatibility.md` atualizados (gate `make validate` verde).

### CapyOS: adapter de decode de imagem (`capy-codec-image` v2)
- **`userland/bin/capybrowse/browser_image.{h,c}` (novo):** adapter sobre o ABI `capy-codec-image` v2 do CapyCodecs — injeta um **bump allocator** limitado (arena estática de 512 KiB em `.bss`, resetada por decode, cap 192×192, fail-closed em OOM) + um **inflater zlib via tinf** in-tree (PNG/ICO), entregando `capy_image_rgba32` (ARGB32). O CapyOS **nunca** decoda imagem por conta própria — só injeta allocator/inflater no core desacoplado.

### CapyOS: rasterizador desenha imagem decodificada (não placeholder)
- **`userland/bin/capybrowse/browser_render_pixel.{c,h}`:** novo callback opcional `resolve_image` (image-provider — desacopla render de decode/fetch) + blit escalado nearest-neighbor clipado. Um nó IMAGE com `src` resolvido + pixels decodificados é **blitado escalado à caixa do nó** (incrementa `images_decoded`); o placeholder bordado + label `alt` permanece como fallback (sem resolver / sem src / decode falhou).

### Makefile (detecção do core CapyCodecs)
- Novo bloco que, quando `../CapyCodecs` existe, define `CAPYCODECS_IMAGE_AVAILABLE`/`_SRCS`/`_CFLAGS` + `TINF_IMAGE_SRCS` e liga as **8 TUs de codec** (image/detect/metadata/bmp/png/jpeg/qoi/ico) + **3 TUs de tinf** (tinflate/tinfzlib/adler32) + `browser_image.o` no binário de teste focado com `-DCAPYOS_HAVE_CAPYCODECS_IMAGE`. Sem colisão de símbolo (codecs/tinf não têm `capy_url_parse`).

### Testes + Plano
- **`tests/userland/test_browser_pipeline.c`:** + decode BMP 1×1 → ARGB `0xFF112233`, PNG 2×2 real via tinf → red/green/blue/white, bytes não-imagem → fail-closed, e rasterização de página com `<img>` em que o resolver decoda → `images_decoded>=1` (todos gated em `CAPYOS_HAVE_CAPYCODECS_IMAGE`). Os 3 callers do rasterizador (`test_browser_render_pixel.c`, `test_browser_pipeline.c`, `capygfx/main.c`) inicializam os novos campos de `opts`.
- **`docs/plans/active/capyos-master-plan.md`**: Slice 7.4 marcado **núcleo host-provado em `alpha.293`** (prova em ring-3 → 7.4.2/`alpha.294`). `docs/plans/STATUS.md`: nota `alpha.293`. Matriz + audit + external-core-repos: pin CapyBrowser → `0.6.6`.

## Validacao

- `make test` — **verde**: agregado `unit_tests` + `test-browser-pipeline` **19/19** (decode BMP/PNG-via-tinf + fail-closed + rasteriza-com-imagem).
- `make layout-audit` — **limpo** (Warnings: none). `make version-audit` — **verde** (current=0.8.0-alpha.293).
- `make all64` (**clean**) — **verde** → `build/capyos64.bin` (2.1 MiB). **KERNEL byte-idêntico** ao alpha.292 (só adapter ring-3 + rasterizador + Makefile + testes + docs mudaram).
- CapyBrowser `make validate` — **verde** (0 falhas; golden do display-list com `src=` confere).
- A prova de decode **em ring-3** (capygfx renderiza página com imagem embutida → blit → present, em QEMU) segue na **7.4.2 / alpha.294**.
- **VMware (oficial):** `make smoke-x64-vmware-browser-graphical` mapeado e **pulado** neste ciclo conforme instruído (sustentado por QEMU).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.292+20260617` | `0.8.0-alpha.293+20260617` | Slice 7.4 (núcleo host-provado): adapter de decode + rasterizador desenha imagem. **Sem mudança de ABI** (kernel byte-idêntico). |
| **CapyBrowser** | `0.6.5` | `0.6.6` | `capy-browser-core` v1: nó IMAGE do display-list carrega o `src` resolvido. **Aditivo dentro de v1** (sem bump de ABI; `CAPY_DL_VERSION` inalterado). |

Coordenação cross-repo com **CapyBrowser 0.6.6** (acima): aditiva dentro de
`capy-browser-core` v1 — sem bump de ABI. **CapyCodecs** `capy-codec-image` v2
consumido como-está (8 TUs de codec + 3 de tinf compiladas **somente** no binário
de teste focado, **nunca** no kernel — mesma disciplina de desacoplamento do
text/graphical core). Os 5 repos irmãos restantes (CapyUI, CapyCodecs, CapyAgent,
CapyLang, CapyBenchmark) permanecem inalterados.

_Build: `0.8.0-alpha.293+20260617`_
