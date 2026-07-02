# CapyOS 0.8.0-alpha.303+20260701

**Data:** 2026-07-01
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.303+20260701`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — desbloqueio arquitetural + fetch de imagem pela rede em ring-3, provado em QEMU)

## Resumo executivo

alpha.303+20260701: resolve o bloqueio arquitetural documentado no `alpha.302`
(colisão do símbolo `capy_url_parse` entre o core GRÁFICO do CapyBrowser e o
`capylibc-net`) e, com o bloqueio removido, **implementa e prova em QEMU real**
o fetch de sub-recurso (imagem) pela rede dentro do navegador gráfico
`capygfx`, através do gate de mixed-content — a Slice C runtime da Etapa 7.

**A correção (Makefile, cirúrgica e isolada):** o core GRÁFICO é compilado via
`CAPYBROWSER_PIPELINE_CFLAGS`, que passou a incluir o mesmo rename que o core
de TEXTO já usa (`-Dcapy_url_parse=capybrowse_core_url_parse`), aplicado
uniformemente aos 6 diretórios-fonte do core gráfico. Confirmado por grep que
`browser_pipeline.c`, `browser_render_pixel.c` e `capygfx/main.c` não
referenciam `capy_url_parse` diretamente (o rename é inofensivo para eles), e
que `browser_pipeline_tests` (o binário de teste de host) usa `HOST_CFLAGS` +
`TEST_PIPELINE_INCLUDES` **próprios**, completamente isolados de
`CAPYBROWSER_PIPELINE_CFLAGS` — logo o teste de host nunca viu essa mudança.
Com o core renomeado, `capygfx` agora também linka `capylibc-net` (símbolo
real, não-renomeado) + os mesmos objetos de fetch do `capymultifetch`
(`browser_fetch`, `page_budget`, os 5 módulos `http_*`), sem colisão.

**A feature (main.c do capygfx, opt-in, aditiva):** um novo resolvedor de
imagem `cb_resolve_image_net`, ativado **apenas** por um novo define
`CAPYGFX_NET_IMAGE_SMOKE` (OFF por padrão — o smoke `alpha.294` de imagem
embutida continua byte-a-byte inalterado). Quando ativo: o `<img src="...">`
da página é buscado de verdade pela rede via `browser_fetch_get`, passa pelo
gate de mixed-content (`browser_fetch_subresource_allowed`) antes do fetch, e
os bytes recebidos são decodificados pelo MESMO adaptador CapyCodecs que já
decodifica a imagem embutida — falha fechada (sem fallback) em qualquer bloqueio
ou falha, para que o smoke reflita genuinamente esse caminho.

**Resultado do smoke real (QEMU, com rede E1000/SLIRP):**
```
[user_init] CAPYOS_GFX_SMOKE; init compositor + spawning capygfx.
[smoke] capygfx ready
capygfx: window+fill+blit+present+poll ok
[ok]   + '[smoke] capygfx ready'
[ok] qemu-capygfx-net-image smoke passed
```
A imagem servida é byte-idêntica à PNG 2x2 embutida do `alpha.294`, provando
que o MESMO caminho de decode agora é alimentado por bytes vindos da rede via
o gate de mixed-content, em vez de um array embutido.

**Regressão:** o smoke `alpha.294` original (`smoke-x64-qemu-capygfx`, sem
rede) foi re-executado do zero após a correção do Makefile e **continua
passando** (`[smoke] capygfx ready`), confirmando que a cirurgia de símbolo
não afetou o caminho existente.

## Mudancas

### Desbloqueio arquitetural (Makefile)
- `CAPYBROWSER_PIPELINE_CFLAGS` (usado só pelo `capygfx` ring-3, isolado de `HOST_CFLAGS`/`browser_pipeline_tests`) passa a incluir `CAPYBROWSER_GFX_RENAME := -Dcapy_url_parse=capybrowse_core_url_parse`.
- `CAPYGFX_OBJS` passa a linkar `$(CAPYLIBC_NET_OBJS)` + os objetos de fetch do `capymultifetch` (reaproveitados, mesma regra sem-rename) quando o core gráfico está presente.
- Comentários desatualizados ("NO capylibc-net") corrigidos para refletir a nova realidade.

### Fetch de imagem pela rede (Slice C runtime)
- `userland/bin/capygfx/main.c`: novo resolvedor `cb_resolve_image_net` (gated por `CAPYOS_HAVE_CAPYGFX_NET && CAPYGFX_NET_IMAGE_SMOKE`), HTML de smoke com `<img src="CAPYGFX_IMAGE_URL">` quando o gate está ativo.
- `Makefile`: alvos `smoke-x64-vmware-capygfx-net-image` (oficial) e `smoke-x64-qemu-capygfx-net-image` (dev, hermético).
- `tools/scripts/smoke_x64_qemu_capygfx_net_image.py` (novo): servidor HTTP local servindo a mesma PNG 2x2 do smoke embutido + boot QEMU + assinatura do marcador reaproveitado `[smoke] capygfx ready`.

## Validacao

- `make test` — **verde**, incl. `browser_pipeline_tests: 19/19 checks passed` (sem regressão, confirma o isolamento do rename).
- `make layout-audit` — **limpo** (Warnings: none).
- `make capygfx-elf` (standalone) — **linka limpo** com capylibc-net + browser_fetch + os 5 módulos http_* (0 erros, 0 undefined references).
- `make all64` **default** (clean, sem flags de smoke) — **verde**, `capyos64.bin` **byte-idêntico** (2125360 bytes) ao build antes desta mudança, confirmando zero impacto no kernel de produção.
- **`make smoke-x64-qemu-capygfx` (regressão do alpha.294, sem rede) — PASSOU** após a correção do Makefile.
- **`make smoke-x64-qemu-capygfx-net-image` (NOVO) — PASSOU em QEMU+OVMF real com rede E1000/SLIRP**: fetch de imagem pela rede + gate de mixed-content + decode CapyCodecs + blit, tudo em ring-3.
- `make version-audit` — verde (current=`0.8.0-alpha.303`).
- **Sem mudança de ABI** nem de contrato cross-repo; 6 repos irmãos inalterados.
- **VMware oficial:** `smoke-x64-vmware-capygfx-net-image` está definido e pronto, mas **não foi executado** nesta sessão (passo do operador).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.302+20260701` | `0.8.0-alpha.303+20260701` | Slice 7.5: desbloqueio do símbolo `capy_url_parse` (capygfx + capylibc-net) + fetch de imagem pela rede via mixed-content gate, provado em QEMU. **Sem mudança de ABI.** |

Sem mudança de ABI nem de contrato cross-repo. Os 6 repos irmãos permanecem
inalterados.

_Build: `0.8.0-alpha.303+20260701`_
