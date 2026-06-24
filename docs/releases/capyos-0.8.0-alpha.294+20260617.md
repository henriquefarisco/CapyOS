# CapyOS 0.8.0-alpha.294+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.294+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.4.2 — prova de runtime em QEMU do decode de imagem inline em ring-3; fecha a Slice 7.4)

## Resumo executivo

alpha.294+20260617: Etapa 7 / Slice 7.4.2 (prova de runtime) -- decode de imagem inline EM RING-3 (QEMU). Estende o app ring-3 gated /bin/capygfx para, quando o core grafico do CapyBrowser E o core de codecs do CapyCodecs estao presentes no build, embutir uma pagina HTML com <img>, decodar a imagem (um PNG 2x2 real) EM RING-3 via o adapter browser_image (CapyCodecs capy-codec-image v2 + inflater tinf in-tree) e blita-la (escalada) no no IMAGE do display-list -- em vez do placeholder -- contra o compositor REAL do kernel. Fecha a Slice 7.4 (cujo nucleo foi host-provado na alpha.293), provando em runtime e em ring-3 a cadeia inteira bytes-de-imagem -> ARGB32 -> blit -> present. Makefile: o bloco da Slice 7.4b compila o adapter browser_image.c + as 8 TUs de codec do CapyCodecs (image/detect/metadata/bmp/png/jpeg/qoi/ico) + as 3 TUs de tinf (tinflate/tinfzlib/adler32) como objetos ring-3 no dir capybrowser-gfx/ (CC64 freestanding, -DCAPYOS_HAVE_CAPYCODECS_IMAGE) e os liga no capygfx quando ambos os cores estao presentes (aninhado sob CAPYBROWSER_CORE_AVAILABLE + CAPYCODECS_IMAGE_AVAILABLE); sem colisao de simbolo (codecs/tinf nao tem capy_url_parse, e o capygfx nao linka capylibc-net). main.c: sob #if CAPYBROWSER_CORE && CAPYCODECS_IMAGE, a pagina embutida ganha um <img src=logo.png>, o resolver cb_resolve_image decoda o PNG embutido via capyos_image_decode, e o rasterizador recebe esse resolver -> o no IMAGE e desenhado a partir dos pixels decodificados; o smoke FALHA FECHADO se images_decoded < 1 (o marker so dispara se o decode em ring-3 realmente aconteceu). Sem o core de codecs, o <img> cai no placeholder (graceful). O binario ring-3 tem ~1.83 MiB de .bss (arenas do pipeline ~1.09 MiB + a surface 320x240 ~0.30 MiB + a arena de decode de imagem 512 KiB); o smoke confirma EMPIRICAMENTE que o ELF loader mapeia/zera esse .bss e que o decode + rasterizacao executam em ring-3 sem falha. Validado: make capygfx-elf linka limpo (codecs+tinf+adapter, sem colisao; .bss 1.83 MiB / text 65 KiB); make test VERDE (agregado unit_tests + test-browser-pipeline 19/19, inalterados); layout-audit limpo; e make smoke-x64-qemu-capygfx PASSOU em QEMU+OVMF (marker [smoke] capygfx ready observado no COM1, agora gatado pelo decode em ring-3). Sem mudanca de ABI nem de TU do kernel (so o app ring-3 capygfx + regras de Makefile mudaram; o kernel e byte-identico ao alpha.293). Gate VMware oficial smoke-x64-vmware-browser-graphical (mesma imagem) continua mapeado e PULADO neste ciclo (sustentado por QEMU); ao reabilitar, confirma o render visual ao vivo da imagem decodificada quando a integracao com o desktop interativo (CapyUI) ligar o loop do compositor. CapyBrowser v0.6.6 e CapyCodecs capy-codec-image v2 consumidos como-esta (TUs puros so no binario ring-3 e no teste focado, nunca no kernel); 5 repos irmaos inalterados. Proximas fatias: fetch HTTPS->grafico com fallback de texto (criterio 5), HTTP cache/cookies/forms, e a janela interativa no desktop (CapyUI).

## Mudancas

### App ring-3 capygfx decoda + desenha uma imagem inline (ring-3)
- **`userland/bin/capygfx/main.c`**: sob `#if defined(CAPYOS_HAVE_CAPYBROWSER_CORE) && defined(CAPYOS_HAVE_CAPYCODECS_IMAGE)`, a pagina HTML embutida ganha um `<img src="logo.png">`; um **PNG 2×2 real** fica embutido no binario; o resolver `cb_resolve_image` decoda-o **em ring-3** via `capyos_image_decode` (CapyCodecs `capy-codec-image` v2 + inflater `tinf`); o rasterizador recebe esse resolver, entao o no IMAGE e desenhado a partir dos **pixels decodificados** (blit escalado) em vez do placeholder. O smoke **falha fechado** se `images_decoded < 1` — o marker so dispara se o decode em ring-3 realmente aconteceu. Sem o core de codecs, o `<img>` cai no placeholder (graceful).

### Makefile (linkagem do decode no ring-3, sem colisao)
- Bloco Slice 7.4b: compila `browser_image.c` + as **8 TUs de codec** do CapyCodecs + as **3 TUs de tinf** como objetos ring-3 em `capybrowser-gfx/` (`CC64` freestanding, `-DCAPYOS_HAVE_CAPYCODECS_IMAGE`) e os liga no `capygfx` quando **ambos** os cores estao presentes (aninhado sob `CAPYBROWSER_CORE_AVAILABLE` + `CAPYCODECS_IMAGE_AVAILABLE`). Sem colisao de simbolo (codecs/tinf nao tem `capy_url_parse`; o capygfx nao linka `capylibc-net`). O binario ring-3 tem **~1.83 MiB de `.bss`** (arenas do pipeline ~1.09 MiB + a surface ~0.30 MiB + a arena de decode 512 KiB).

### Plano
- **`docs/plans/active/capyos-master-plan.md`**: Slice 7.4 sub-fatia **7.4.2 (prova de runtime) CONCLUÍDA em `alpha.294`**. `docs/plans/STATUS.md`: nota `alpha.294`.

## Validacao

- `make capygfx-elf` -- **linka limpo** (adapter + 8 codecs + 3 tinf TUs, sem colisao; `.bss` 1.83 MiB / text ~65 KiB).
- `make test` -- **verde**: agregado `unit_tests` ("Todos os testes passaram") + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` -- **limpo** (Warnings: none). `make version-audit` -- **verde** (current=0.8.0-alpha.294).
- `make smoke-x64-qemu-capygfx` -- **PASSOU** em QEMU+OVMF: clean build sob `CAPYOS_GFX_SMOKE`, boot direto no `/bin/capygfx` que agora **decoda a imagem embutida em ring-3** (CapyCodecs + tinf) → blita no no IMAGE → present; marker `[smoke] capygfx ready` observado no COM1 (gatado por `images_decoded >= 1`). **Confirma empiricamente** que o ELF loader carrega ~1.83 MiB de `.bss` e que o decode + rasterizacao executam em ring-3 — prova de runtime de **imagem decodificada → janela** no CapyOS.
- Sem mudanca de TU do kernel (so o app ring-3 + regras de Makefile); o kernel e byte-identico ao alpha.293.
- **VMware (oficial):** `make smoke-x64-vmware-browser-graphical` mapeado (mesma imagem) e **pulado** neste ciclo conforme instruido (sustentado por QEMU); ao reabilitar, confirma o render visual ao vivo da imagem decodificada.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.293+20260617` | `0.8.0-alpha.294+20260617` | Slice 7.4.2: prova de runtime (QEMU) do decode de imagem inline em ring-3. **Sem mudança de ABI** (kernel byte-idêntico). |

Sem mudanca de ABI nem de contrato cross-repo. **CapyBrowser** `v0.6.6` e
**CapyCodecs** `capy-codec-image` v2 consumidos como-esta: os TUs puros (core
grafico do CapyBrowser + decoders do CapyCodecs + tinf) sao compilados no binario
ring-3 capygfx (e no de teste focado), **nunca** no kernel — mesma disciplina de
desacoplamento. Os 5 repos irmaos restantes permanecem inalterados.

_Build: `0.8.0-alpha.294+20260617`_
