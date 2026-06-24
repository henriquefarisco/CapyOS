# Compatibility audit 2026-06-11

**CapyOS core:** `0.8.0-alpha.265+20260611`
**Escopo:** handoff cross-repo para desbloquear Etapa 6 / Slice 6.4.

## Versoes coordenadas

| Repositorio | Versao anterior | Versao atual | Contrato |
|---|---|---|---|
| `CapyOS` | `0.8.0-alpha.264+20260607` | `0.8.0-alpha.265+20260611` | `capyos-base` v3 + `capyos-package-apply` v1 |
| `CapyBrowser` | `0.5.0` | `0.6.0` | `capy-browser-core` v1 text subset publicado |

Sem bump nos demais repositorios: CapyUI `2.22.0`, CapyCodecs `0.0.7`,
CapyLang `0.1.8`, CapyAgent `0.0.7` e CapyBenchmark `0.0.7`.

## Handoff CapyBrowser 0.6.0

`CapyBrowser v0.6.0` e o handoff explicito para a Etapa 6 / Slice 6.4:

- `make package STAGE=text` gera o pacote `org.capyos.browser.text`;
- `depends=` fica vazio por design, porque o modo texto nao deve esperar
  codecs de imagem da Etapa 7;
- o payload publicado e o `capy-browser-core` textual: URL
  parse/normalize/origin, HTML-to-text, links e modelo deterministico de
  erros/warnings;
- CapyOS continua dono de DNS/TCP/TLS/HTTP, filesystem, sandbox, janela,
  input, render, lifecycle e gates externos.

O pacote grafico `org.capyos.browser.core` permanece Etapa 7-gated e declara
`org.capyos.codecs.image-basic` quando imagem estiver habilitada.

## Estado CapyOS alpha.265

O bloqueio anterior da Slice 6.4 era "publicacao do core externo". Esse
bloqueio foi removido. A Slice 6.4 nao esta marcada como concluida: o proximo
trabalho e implementar o adapter CapyOS-side, reusar render/scroll existente e
passar o smoke `make smoke-x64-vmware-capybrowse-text`.

## Validacao

- CapyBrowser: `make validate`, `make package STAGE=text`,
  `make package STAGE=core`.
- CapyOS: `make version-audit`, `make layout-audit`, `make test` e, antes de
  promover runtime, `make smoke-x64-vmware-capybrowse-text`.

## Arquivos sincronizados

- `CapyBrowser/VERSION`
- `CapyBrowser/Makefile`
- `CapyBrowser/docs/compatibility.md`
- `CapyBrowser/docs/roadmap.md`
- `CapyOS/VERSION.yaml`
- `CapyOS/docs/reference/integration/compatibility-matrix.md`
- `CapyOS/docs/reference/integration/browser-core-integration-contract.md`
- `CapyOS/docs/reference/integration/external-core-repositories.md`
- `CapyOS/docs/architecture/etapa-6-desktop-apps-readiness.md`
- `CapyOS/docs/plans/active/capyos-master-plan.md`

## Addendum — adapter CapyOS-side da Slice 6.4 implementado + build-validado (in-tree)

O adapter CapyOS-side que o "Estado CapyOS alpha.265" acima listava como
proximo trabalho foi **implementado e build-validado in-tree**, sem bump de
alpha (o bump fica para o fecho apos o gate externo, como na Etapa 5):

- **App ring-3 `userland/bin/capybrowse`** consome a ABI publicada
  `capy_html_to_text` / `struct capy_text_doc` do `capy-browser-core` (subset
  `STAGE=text`: url parse/normalize/origin + html entities/tokenizer/text_emit),
  **sem reinventar** HTML-to-text. Fetch HTTPS da Etapa 5 -> `capy_html_to_text`
  -> formatter puro host-testado (`capybrowse_view`) -> stdout; erro de
  transporte via o diagnostico 6.2/6.3 (`capy_net_diagnose_stage` +
  `capy_net_stage_message`). Sem JavaScript.
- **Pre-requisito entregue:** lib de string freestanding do `capylibc`
  (`<string.h>`) que o core exige, com guarda `#include_next`/`UNIT_TEST` para
  nao sombrear o `<string.h>` do host nos testes.
- **Sibling-detection no Makefile** (mesmo padrao do CapyUI) compila o subset
  textual de `../CapyBrowser` so quando o sibling existe; smoke `capybrowse`
  (latch host-testado + embedding gated `CAPYOS_CAPYBROWSE_SMOKE`) com build de
  producao byte-identico por default.

**Dois fixes de integracao cross-repo** apareceram no link `make capybrowse-elf`
(licao registrada para futuros consumos de core externo):

1. **Colisao de simbolo `capy_url_parse`** — `capy-browser-core` e
   `capylibc-net` exportam ambos `capy_url_parse` (parsers de URL distintos).
   Resolvido namespando o simbolo do **core** no lado CapyOS
   (`-Dcapy_url_parse=capybrowse_core_url_parse` so nas TUs do core + `#define`
   casado no `main.c`); fora do `HOST_CFLAGS` para nao tocar os testes do net.
2. **`time()` do BearSSL X.509** — `x509_minimal` referencia o relogio `time()`
   da libc no link freestanding ring-3; o userland nao o tinha (o kernel prove
   em `stubs.c`, mas binarios ring-3 nao linkam os stubs do kernel). Resolvido
   com stub `time()` em `capy_tls_backend.c` (ao lado do `br_prng_seeder_system`,
   guardado por `CAPYOS_TLS_USERLAND_HANDSHAKE` + `!UNIT_TEST`); o `tls_smoke`
   foi alinhado linkando tambem `CAPYLIBC_STRING_OBJS`.

**Validado:** `make capybrowse-elf` (link cross-repo) + `make test` verdes.
**Gate de fecho pendente (externo):** `make smoke-x64-vmware-capybrowse-text` +
`release-check`. A Slice 6.4 segue **nao concluida** ate esse gate; o bump de
alpha e a promocao de runtime acontecem no fecho.

## Addendum 2026-06-17 - Consolidacao de release alpha.266

Evento: bump coordenado dos 7 repositorios consolidando o trabalho aditivo
in-tree acumulado. Nenhuma mudanca de ABI base do kernel; nenhuma mudanca
quebra contrato. Cada pacote irmao validado no host (exit=0) apos o bump.

| Repo | De | Para | Mudanca aditiva |
|---|---|---|---|
| CapyOS | alpha.265+20260611 | alpha.266+20260617 | Etapa 6 in-tree (apps-roundtrip + capybrowse-text smokes, capybrowse userland, capylibc string ops) + pivot de versao |
| CapyUI | 2.22.0 | 2.22.1 | capy_widget_type_name (helper puro; ABI v2.22 inalterada) |
| CapyCodecs | 0.0.7 | 0.0.8 | capy_image_format_name + CAPY_IMAGE_FEATURE_FORMAT_NAME |
| CapyAgent | 0.0.7 | 0.0.8 | capy_manifest_emit rejeita dependencia duplicada (CAPY_MANIFEST_DEPENDS_INVALID) |
| CapyBrowser | 0.6.0 | 0.6.1 | refs numericas WHATWG NULL/surrogate/>0x10FFFF resolvem para U+FFFD (ENTITY_INVALID); <pre> preserva whitespace; marcadores de lista |
| CapyLang | 0.1.8 | 0.1.9 | opcodes de array push/pop (0x67-0x68) + insert/remove (0x69-0x6A); 43 opcodes congelados |
| CapyBenchmark | 0.0.7 | 0.0.8 | read-side parse trilogy replay/report/evaluation (round-trip + fail-closed) |

Gates executados no host (`Automation/remote-exec.sh`), exit=0:
- Irmaos: `make validate` (CapyUI, CapyAgent, CapyBrowser, CapyCodecs, CapyBenchmark) + `make rust-validate` (CapyLang).
- CapyOS: `make version-audit` (current=alpha.266 alinhado), `make layout-audit`, `make test`.

Pendente (nao alterado por este evento):
- Etapa 6 aberta: `smoke-x64-vmware-capybrowse-text` e `smoke-x64-vmware-apps-basic-roundtrip` externos.
- P0: signer Ed25519 do CapyAgent ainda nao registrado via `capypkg_set_signature_verifier`; publishing assinado segue fail-closed.

Pin de core CapyOS nos irmaos: preservado (estes bumps de irmao nao mudam a
superficie de contrato consumida pelo adapter; o pin so move quando um bump de
core afeta o contrato do irmao).


## Addendum 2026-06-17 -- verifier Ed25519 do capypkg registrado (alpha.276)

O slot de verificacao de assinatura do capypkg, antes NULL por design, passou a
ser preenchido por um verifier CapyOS-side real:
`src/services/capypkg/capypkg_signature.c::capypkg_ed25519_verify_signature`
decodifica os 128 hex da assinatura (fail-closed) e chama o `ed25519_verify`
auditado do kernel (`src/security/ed25519.c`) sobre o descritor canonico, usando
a chave publica do publisher pinada via `capypkg_set_trusted_publisher_key`. O
binder (`src/arch/x86_64/kernel_services_capypkg.c`) registra o verifier por
`capypkg_set_signature_verifier`.

**Sem mudanca de comportamento em producao:** nenhum trust anchor e pinado por
padrao, entao o verifier devolve -1 e repos `signed` continuam fail-closed com
`CAPYPKG_ERR_SIGNATURE`. A chave de TESTE do KAT (seed publica) nunca e pinada.

**Validacao:** 6 casos host-side novos em `tests/services/test_capypkg.c`
(fragmento `test_capypkg_signature.inc`) provam que a assinatura que o signer do
CapyAgent produz (KAT congelado em `CapyAgent/docs/compatibility.md`) verifica com
o `ed25519_verify` do kernel; descritor/assinatura adulterados, ausencia de chave,
e hex malformado/curto/longo sao rejeitados. `make test` verde.

**Pendente para promover repo `signed` a user-facing:** (1) pinar a chave de
release offline real (operador) via `capypkg_set_trusted_publisher_key`; (2) KAT
externo do signer do CapyAgent (`make validate` no CapyAgent). Ate la, a politica
fail-closed permanece.


## Addendum 2026-06-17 -- Slice 7.4: display-list IMAGE carrega src + decode CapyOS (alpha.293, CapyBrowser 0.6.6)

Etapa 7 / Slice 7.4 (decode de imagem inline, nucleo host-provado). Fecha a cadeia
`<img>` -> pixels no backend de render do CapyOS, em duas partes desacopladas.

**CROSS-REPO -- CapyBrowser `0.6.5` -> `0.6.6` (`capy-browser-core` v1, aditivo):** o
emissor de display-list (`src/displaylist/display_list.c`) resolve o `src` de um
`<img>` contra `base_url` pelo nucleo de URL Fase C1 (espelhando `<a href>`) e o
grava nos campos `url_off`/`url_len` do no IMAGE -- os mesmos campos que LINK ja
usava. Aditivo e retrocompativel: `CAPY_DL_VERSION` continua 1 (o campo ja existia
para LINK); um `src` que nao resolve deixa o payload vazio. **Sem bump de ABI**
(aditivo dentro de v1). CapyBrowser liberado (develop+main `a6c506e`, CI + Security
verdes, tag `v0.6.6`); golden/dump do display-list + `docs/compatibility.md`
atualizados; `make validate` verde.

**CAPYOS -- adapter de decode + rasterizador (kernel byte-identico):** novo
`userland/bin/capybrowse/browser_image.{c,h}` sobre o ABI `capy-codec-image` v2 do
CapyCodecs injeta um bump allocator de arena `.bss` (512 KiB, reset por decode, cap
192x192, fail-closed em OOM) + um inflater zlib via `tinf` in-tree (PNG/ICO),
entregando `capy_image_rgba32` (ARGB32) -- o CapyOS nunca decoda por conta propria. O
rasterizador `browser_render_pixel.{c,h}` ganha um callback opcional `resolve_image`
(image-provider; desacopla render de decode/fetch) + blit escalado: um no IMAGE com
`src` resolvido + pixels decodificados e blitado escalado a caixa do no (incrementa
`images_decoded`), com o placeholder bordado + label `alt` como fallback. Makefile
detecta o core CapyCodecs (`CAPYCODECS_IMAGE_*` + `TINF_IMAGE_SRCS`) e liga as 8 TUs
de codec + 3 de tinf + `browser_image.o` no binario de teste focado com
`-DCAPYOS_HAVE_CAPYCODECS_IMAGE` (sem colisao de simbolo; codecs/tinf nao tem
`capy_url_parse`).

**Validacao:** `make test` verde + `make test-browser-pipeline` `19/19` (decode BMP
1x1 -> ARGB `0xFF112233`, PNG 2x2 real via tinf -> red/green/blue/white, bytes
nao-imagem -> fail-closed, rasteriza pagina com `<img>` -> `images_decoded>=1`);
`make layout-audit` limpo; `make all64` (clean) verde -> `build/capyos64.bin`. Kernel
byte-identico ao alpha.292.

**Pins:** CapyBrowser `0.6.1` -> `0.6.6` na matriz/STATUS/external-core-repos;
`capy-codec-image` v2 consumido como-esta (8 TUs + tinf so no binario de teste focado,
nunca no kernel). Os 5 repos irmaos restantes inalterados.

**Pendente:** a prova de decode EM RING-3 (`capygfx` renderiza pagina com imagem
embutida, decodando em ring-3 -> blit -> present, via `make smoke-x64-qemu-capygfx`)
fica para a sub-fatia 7.4.2 / `alpha.294`. Gate VMware oficial
`smoke-x64-vmware-browser-graphical` mapeado e pulado (sustentado por QEMU).
