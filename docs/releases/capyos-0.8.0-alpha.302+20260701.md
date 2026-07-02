# CapyOS 0.8.0-alpha.302+20260701

**Data:** 2026-07-01
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.302+20260701`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — smoke de RUNTIME do multi-fetch, provado em QEMU)

## Resumo executivo

alpha.302+20260701: Etapa 7 / Slice 7.5 (smoke de runtime browser-multifetch,
provado em QEMU). Transforma a prova de link ring-3 do `alpha.301`
(`capymultifetch-elf`) numa **prova de runtime completa**: o app ring-3
`/bin/capymultifetch` agora bota de verdade sob QEMU+OVMF, busca uma URL
cacheável DUAS vezes através do mesmo `browser_fetch_ctx` persistente sobre o
transporte TLS/socket real da Etapa 5, e o kernel confirma via
`process_exit` que a 2ª visita foi servida do cache **sem** uma 2ª chamada de
transporte de rede — o critério de aceite "cache acelera a 2ª visita" da
Etapa 7, agora provado em **runtime**, não só em host.

Segue o **smoke-marker-pattern** já usado por `tls_smoke`/`capybrowse-text`/
`capygfx`: um latch puro (`capymultifetch_smoke.{c,h}`, host-testado) +
um TU de I/O que emite o marcador COM1 + wiring gated
(`CAPYOS_MULTIFETCH_SMOKE`) em `embedded_progs.c` (blob) +
`user_init.c`/`kernel_main.c` (boot hook) + `process.c` (latch no
`process_exit`). O smoke QEMU hermético (`tools/scripts/
smoke_x64_qemu_browser_multifetch.py`) serve uma página **cacheável**
(`Cache-Control: max-age=86400`) num servidor local via o gateway SLIRP
(`http://10.0.2.2:18081/`), e cruza a validação por DOIS lados independentes:
o marcador do kernel (contagem interna do guest) **e** a contagem de
requisições HTTP observada pelo próprio servidor do host (exatamente 1).

**Resultado do smoke real:**
```
[user_init] CAPYOS_MULTIFETCH_SMOKE; spawning capymultifetch.
[smoke] browser-multifetch ready
[ok]   + host observed exactly 1 HTTP request (2nd visit served from the
guest's cache, as expected)
[ok] qemu-browser-multifetch smoke passed
```

**Descoberta arquitetural (investigada em profundidade, receita de desbloqueio
registrada para Slice C runtime / Slice B):** `capygfx` **não linka
`capylibc-net`** deliberadamente. Rastreado até a causa raiz exata: o core
GRÁFICO do CapyBrowser é compilado via `CAPYBROWSER_PIPELINE_CFLAGS`
(`src/url/%.c`, `src/text/%.c`, `src/html/%.c`, `src/css/%.c`,
`src/layout/%.c`, `src/displaylist/%.c` — 6 diretórios, mais
`browser_pipeline.c`/`browser_render_pixel.c`/`capygfx/main.c`), que **não**
inclui nenhum rename de símbolo — diferente do core de TEXTO, que usa
`CAPYBROWSER_TEXT_RENAME := -Dcapy_url_parse=capybrowse_core_url_parse`
aplicado só aos seus 2 diretórios (`src/url`, `src/text`). Ou seja, o
`url_parse.c` do core gráfico exporta o símbolo **real, não-renomeado**
`capy_url_parse` — o mesmo nome que `capylibc-net/capy_net_url.c` (e
`browser_fetch.c`) definem/usam. Linkar os dois no mesmo ELF → **símbolo
duplicado** no link (não um erro de compilação; um erro do `ld`).

**Receita de desbloqueio (verificada estaticamente, não aplicada nesta
sessão por prudência — ver "por que não apliquei" abaixo):**
1. Adicionar `-Dcapy_url_parse=capybrowse_core_url_parse` a
   `CAPYBROWSER_PIPELINE_CFLAGS` (não a `CAPYBROWSER_CORE_CFLAGS`, que também
   alimenta `HOST_CFLAGS`/`make test` — misturar os dois quebraria os testes
   de host). Confirmado por grep que `browser_pipeline.c`,
   `browser_render_pixel.c` e `capygfx/main.c` **não referenciam
   `capy_url_parse` diretamente** — o rename é inofensivo para eles.
2. Compilar `browser_fetch.c`/`page_budget.c`/os 5 módulos de fetch-policy
   para `capygfx` pela MESMA regra sem-rename já criada para
   `capymultifetch` (`$(CAPYLIBC_BUILD_DIR)/capymultifetch/%.o`), OU uma nova
   pasta de objetos dedicada — nunca via `CAPYBROWSER_PIPELINE_CFLAGS`.
3. Adicionar `$(CAPYLIBC_NET_OBJS)` + esses objetos a `CAPYGFX_OBJS`.

**Por que não apliquei agora:** o mesmo `CAPYBROWSER_PIPELINE_CFLAGS`
alimenta `TEST_PIPELINE_INCLUDES`/`browser_pipeline_tests` — o binário de
teste SEPARADO que existe precisamente porque o core gráfico usa o símbolo
real `capy_url_parse` sem colisão ali (não há capylibc-net nesse binário de
teste). Antes de aplicar o rename globalmente preciso verificar se
`test_browser_pipeline.c` referencia `capy_url_parse` por conta própria (o
que exigiria ajuste espelhado nesse teste). Essa verificação + a cirurgia de
Makefile + a re-validação completa do pipeline gráfico já provado (o
decode-de-imagem-embutida do alpha.294) é trabalho real, com risco de
regressão num subsistema já validado — não uma correção de uma linha.
Prefiro entregar a receita precisa a arriscar quebrar o que já funciona no
fim de uma sessão já muito longa.

## Mudancas

### Smoke-marker-pattern para o browser-multifetch
- **`include/kernel/capymultifetch_smoke.h` + `src/kernel/capymultifetch_smoke.c` (novos):** latch puro (mirror de `capybrowse_text_smoke`), host-testado.
- **`src/kernel/capymultifetch_smoke_io.c` (novo) + `tests/stubs/stub_capymultifetch_smoke_io.c` (novo):** emissor real do marcador COM1 + stub de host test.
- **`tests/kernel/test_capymultifetch_smoke_gate.c` (novo):** 8 casos (reset, predicado do gate, exits não-zero ignorados, latch dispara uma vez, NULL-safe, string do marcador canônica, latch global single-emission).
- **`src/kernel/process.c`:** hook em `process_exit` sob `CAPYOS_MULTIFETCH_SMOKE`.
- **`src/kernel/embedded_progs.c`:** registro do blob `/bin/capymultifetch` sob o gate.
- **`src/kernel/user_init.c` + `include/kernel/user_init.h`:** `kernel_boot_run_capymultifetch()`.
- **`src/arch/x86_64/kernel_main.c`:** branch de boot sob o gate.
- **`Makefile`:** `CAPYMULTIFETCH_BLOB_OBJ` (objcopy do ELF) embutido no kernel sob `CAPYOS_MULTIFETCH_SMOKE`; alvos `smoke-x64-vmware-browser-multifetch` (oficial) e `smoke-x64-qemu-browser-multifetch` (dev, hermético).
- **`tools/scripts/smoke_x64_qemu_browser_multifetch.py` (novo):** servidor HTTP local cacheável + boot QEMU + asswinatura do marcador + contagem cruzada de requisições.
- **`userland/bin/capymultifetch/main.c`:** ajustado para buscar `CAPYMULTIFETCH_URL` diretamente (sem passar por `browser_fetch_plan`/HTTPS-first — política já exaustivamente host-testada; o smoke hermético usa HTTP simples via o gateway SLIRP, igual ao `capybrowse-text`) e corrigido um bug de contagem: `transport_calls` cresce a cada tentativa de retry (inclusive falhas durante a janela de DHCP), então o sucesso é comparado por snapshot **antes/depois** da 2ª visita, não por `==1` absoluto.

## Validacao

- `make test` — **verde**, incl. `[browser-multifetch-smoke] all gate tests passed` (novo) + `[browser_fetch] all multi-fetch runtime tests passed` (inalterado) + zero regressões.
- `make layout-audit` — **limpo** (Warnings: none).
- `make all64` **default** (sem flags de smoke) — **verde**, binário idêntico em forma (sem o blob do multifetch, confirmando o gate).
- `make all64 PROFILE=full CAPYOS_MULTIFETCH_SMOKE=1 ...` + `make iso-uefi` + `make manifest64` — **verdes**.
- **`make smoke-x64-qemu-browser-multifetch` (via `smoke_x64_qemu_browser_multifetch.py`) — PASSOU em QEMU+OVMF real**: marcador `[smoke] browser-multifetch ready` observado no COM1 + confirmação independente do servidor host (exatamente 1 requisição HTTP para as 2 visitas).
- `make version-audit` — verde (current=`0.8.0-alpha.302`).
- **Sem mudança de ABI** nem de contrato cross-repo; 6 repos irmãos inalterados.
- **VMware oficial:** `smoke-x64-vmware-browser-multifetch` está definido e pronto, mas **não foi executado** nesta sessão (passo do operador — a plataforma VMware+UEFI+E1000 é o aceite oficial; QEMU aqui é feedback de dev, embora com prova de runtime real).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.301+20260626` | `0.8.0-alpha.302+20260701` | Slice 7.5: smoke de runtime do browser-multifetch provado em QEMU real (kernel boot hook + servidor cacheável hermético). **Sem mudança de ABI.** |

Sem mudança de ABI nem de contrato cross-repo. Os 6 repos irmãos permanecem
inalterados.

_Build: `0.8.0-alpha.302+20260701`_
