# CapyOS 0.8.0-alpha.301+20260626

**Data:** 2026-06-26
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.301+20260626`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — binding de transporte + runtime de navegador multi-fetch + wiring vivo da política 7.6 + orçamento por página)

## Resumo executivo

alpha.301+20260626: Etapa 7 / Slice 7.5+7.6 (binding de transporte + runtime
multi-fetch + política viva). Fecha o maior gap que faltava na Etapa 7: a suíte
**pura** de fetch-policy (`http_cache`/`http_cookies`/`http_session`/
`http_fetch_policy`/`http_hsts`), até aqui *dead code*, passa a ser **dirigida
por um runtime de navegador real**.

1. **Binding de transporte (capylibc-net):** o cliente HTTP ring-3 `capy_http_get`
   ganhou a variante aditiva `capy_http_get_with_headers(url, req_headers, n,
   body, cap, out)` (+ `capy_http_build_get_request_ex`), que emite headers de
   request do chamador (`Cookie`, `If-None-Match`, `If-Modified-Since`) após o
   bloco padrão e antes do `Connection: close`. As funções antigas viram
   wrappers `(NULL,0)` — zero quebra de callers (capybrowse/tls_smoke/31 testes).
   Validação **fail-closed** antes de abrir socket: nome precisa ser token HTTP,
   valor sem bytes CTL (um `\r`/`\n` nunca injeta linha de header), e nomes de
   framing/roteamento reservados (`Host`/`Connection`/`Content-Length`/
   `Transfer-Encoding`) são rejeitados (anti request-smuggling).
2. **Runtime multi-fetch (`userland/bin/capybrowse/browser_fetch.{c,h}`):** liga
   o `http_session` (cache + cookie jar) ao transporte real num contexto
   **persistente** e caller-owned. Uma 2ª visita a uma URL cacheável é servida
   do cache **sem** 2ª chamada de rede (critério "cache acelera a 2ª visita"), e
   cookies setados por uma resposta acompanham requests same-site posteriores.
3. **Política viva (7.6c wiring):** `browser_fetch_plan` aplica HSTS + navegação
   HTTPS-first (http→UPGRADE; host com HSTS→https **obrigatório**); o header
   `Strict-Transport-Security` recebido sobre conexão segura é colhido para o
   store. `browser_fetch_subresource_allowed` aplica o gate de **mixed-content**
   (imagem http numa página https → BLOCK; `file:`/`javascript:` → BLOCK).
4. **Orçamento por página (`page_budget.{c,h}`, 7.6a/7.6b):** caps puros e
   fail-closed de bytes totais + tempo de parede por página (sticky-exceeded),
   o backbone de recurso do streaming render.
5. **Prova de link ring-3 (`capymultifetch-elf`):** a pilha inteira
   (browser_fetch + page_budget + os 5 módulos de fetch-policy compilados como
   objetos CC64 + capylibc-net/tls) **builda e linka freestanding** num ELF
   ring-3 — pré-requisito do smoke de runtime e do navegador gráfico.

> **Nota de ambiente:** esta release foi desenvolvida e validada num host de
> build **Windows + WSL2** (`MESTRE-WINDOWS`), via `Automation/remote-exec.sh`
> dirigindo o WSL. O toolchain Linux (gcc/ld/make/qemu) roda no WSL sobre o
> workspace montado em `/mnt/c/...`. A aceitação VMware oficial segue como passo
> do operador (ver Validação).

## Mudancas

### Binding de transporte (capylibc-net)
- **`userland/include/capylibc-net/capy_net.h` + `userland/lib/capylibc-net/capy_net_http.c`:** `capy_http_get_with_headers` + `capy_http_build_get_request_ex` (aditivos); wrappers preservam `capy_http_get`/`capy_http_build_get_request`.
- **Split de layout:** `capy_net_http.c` (946 LOC após a feature) foi dividido para respeitar o teto de 900 — novo `capy_net_http_request.c` (builder + sanitização host/path + writers) + `capy_net_http_internal.h` (predicados de safety compartilhados, `static inline`). `capy_net_http.c` → 713 LOC.

### Runtime multi-fetch + política + orçamento (CapyOS-side, userland)
- **`userland/bin/capybrowse/browser_fetch.{c,h}` (novos):** `browser_fetch_ctx` (http_session + http_hsts_store + contador de transporte + scratch de corpo); `browser_fetch_get[_with_transport]` (transporte injetável p/ host-test, real via `capy_http_get_with_headers`); `browser_fetch_plan` (HSTS + HTTPS-first); `browser_fetch_subresource_allowed` (mixed-content); colheita de `Strict-Transport-Security`.
- **`userland/bin/capybrowse/page_budget.{c,h}` (novos):** caps puros de bytes + tempo por página, fail-closed, sticky.
- **`userland/bin/capymultifetch/main.c` (novo):** app ring-3 smoke-ready que planeja a navegação, busca 2× e prova `transport_calls==1` (cache short-circuit), emitindo `[smoke] browser-multifetch ready`.

### Testes + wiring
- **`tests/userland/test_capylibc_net_url.c` + `test_capylibc_net_http.c`:** +7 casos (builder emite headers na ordem certa; rejeita Content-Length/CRLF/nome-não-token; `ex(NULL,0)`==builder simples; `get_with_headers` manda Cookie+If-None-Match no fio e rejeita header reservado **antes** de qualquer I/O).
- **`tests/net/test_browser_fetch.c` (novo, `run_browser_fetch_tests`):** build_url; fill_request (capy_url_parse); **2ª-visita-servida-do-cache**; cookie-rides-along; navegação https/http/ftp; **HSTS força https** (+ includeSubDomains); **mixed-content** (http-em-https/file/javascript bloqueados); page_budget (bytes/tempo/sticky/unlimited/NULL); fail-closed.
- **`Makefile`:** `capy_net_http_request.o` em `CAPYLIBC_NET_OBJS`; `browser_fetch.c`/`page_budget.c`/`test_browser_fetch.c` + os módulos http_* no `TEST_SRCS`; `-Iuserland/bin/capybrowse` incondicional no `HOST_CFLAGS` (browser_fetch é CapyOS-side, independe do sibling); alvo `capymultifetch-elf` (compila http_cache/cookies/session/fetch_policy/hsts como objetos CC64). **`tests/test_runner.c`:** declara/invoca `run_browser_fetch_tests`.

## Validacao

- `make test` — **verde** (`unit_tests` "Todos os testes passaram", incl. `[browser_fetch] all multi-fetch runtime tests passed`; `test-browser-pipeline` 19/19). Zero regressões.
- `make layout-audit` — **limpo** (Warnings: none; `capy_net_http.c` 713 / `_request.c` 196 / `browser_fetch.c` ~265 / `page_budget.c` ~62, todos < 900).
- `make capybrowse-elf` + `make tls-smoke-elf` — **linkam** o `capy_net_http_request.o` (compile freestanding CC64).
- `make capymultifetch-elf` — **linka** a pilha multi-fetch inteira em ring-3 (sem símbolos pendentes / multiplas definições).
- `make all64` + `make version-audit` — **verdes** (kernel inalterado por estas mudanças userland; versão alinhada current=0.8.0-alpha.301).
- **Sem mudança de ABI de kernel** (a mudança é na lib userland + adapters de net service CapyOS-side) **nem de contrato cross-repo**. Os 6 repos irmãos permanecem inalterados.

### Pendente para o FECHAMENTO oficial da Etapa 7 (runtime + operador)
- **Smoke de runtime do multi-fetch:** hook de boot no kernel (registro em `embedded_progs.c` + branch em `kernel_main` + latch em `process_exit`, como o `capybrowse_text_smoke`) + servidor controlado **cacheável** + alvo `smoke-x64-{qemu,vmware}-browser-multifetch` confirmando 2ª-visita-servida-do-cache no fio.
- **Fetch de sub-recurso (imagem) pela rede** em ring-3 passando pelo gate de mixed-content (o gate é host-provado; falta o caminho de rede em runtime).
- **Slice B — navegador gráfico com janela interativa no desktop CapyUI** (subsistema de integração com o compositor/sessão).
- **Gates de aceite VMware + UEFI + E1000** (`smoke-x64-vmware-browser-https-static`, `...-browser-text-fallback`): passo do **operador** no host (os alvos `smoke-x64-vmware-*` assumem VMware nativo Linux; o `vmrun.exe` existe no host Windows mas a adaptação WSL→vmrun não foi exercida).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.300+20260617` | `0.8.0-alpha.301+20260626` | Slice 7.5/7.6: binding de transporte (capylibc-net) + runtime multi-fetch + política viva (HSTS/mixed-content) + orçamento por página + prova de link ring-3. **Sem mudança de ABI.** |

Sem mudança de ABI nem de contrato cross-repo. Cache/cookies/sessão/política/HSTS
são adapters do CapyOS; `browser_fetch`/`page_budget` são runtime CapyOS-side. Os
6 repos irmãos permanecem inalterados.

_Build: `0.8.0-alpha.301+20260626`_
