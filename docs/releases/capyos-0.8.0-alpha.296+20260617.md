# CapyOS 0.8.0-alpha.296+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.296+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — orquestração de fetch do cache HTTP, núcleo host-provado)

## Resumo executivo

alpha.296+20260617: Etapa 7 / Slice 7.5 (orquestracao de fetch do cache HTTP -- nucleo host-provado). Estende o cache de respostas HTTP da alpha.295 com a CAMADA DE ORQUESTRACAO do fluxo de fetch RFC 7234, completando o cerebro do cache, ainda puro e desacoplado do transporte. (1) A entrada de cache (struct http_cache_entry) passa a guardar os HEADERS de resposta + a Location, alem do corpo, para servir respostas FIEIS do cache (Content-Type etc. preservados). (2) Novo http_cache_fetch(cache, req, resp, now, fetch_fn, ctx): implementa o fluxo -- FRESH -> serve do cache SEM fetch de transporte (a 2a visita rapida); STALE -> adiciona headers condicionais (If-None-Match/If-Modified-Since) e faz fetch, com 304 reusando o corpo cacheado e 200 substituindo; MISS -> fetch e store se cacheavel. O transporte e INJETADO por um ponteiro de funcao (http_cache_fetch_fn), entao o fluxo e deterministicamente testavel em host SEM rede e fica desacoplado do cliente HTTP real (kernel http_request, transporte userland, ou um fake de teste). Resultado retornado como enum http_cache_result (ERROR/MISS_FETCHED/FRESH_SERVED/REVALIDATED/REFETCHED). Em served/revalidated, resp aponta para dentro da entrada do cache (valido ate a proxima mutacao do cache). Provado em host por test_fetch_orchestration (estende run_http_cache_tests): MISS->fetched (1 chamada de transporte); 2a visita FRESH servida do cache SEM 2a chamada de transporte (a prova-chave de 'cache acelera a 2a visita') + status/corpo/Content-Type cacheados restaurados; STALE->304 revalidado reusando o corpo cacheado (nao o 304 vazio) + header condicional presente na request; fresh de novo apos o refresh do 304; STALE->200 substituindo pelo novo corpo; erro de transporte e fetch_fn NULL fail-closed (ERROR). Validado: make test VERDE (unit_tests 'Todos os testes passaram', incl. marker [http_cache] all cache tests passed; test-browser-pipeline 19/19 inalterado); make layout-audit limpo (http_cache.c ~540 LOC, < 900); make all64 VERDE (http_cache.o com a orquestracao linkado no kernel). Aditivo: structs/funcao/testes novos; as primitivas da alpha.295 ficam inalteradas em comportamento (store agora tambem captura os headers da resposta, adicao benigna). Sem mudanca de ABI; nenhum syscall novo; 6 repos irmaos inalterados. O fluxo continua DEAD CODE ate uma sub-fatia de integracao ligar um fetch_fn real (kernel http_request com um cache estatico, e/ou o caminho do navegador via capylibc-net) -- ai entra o smoke QEMU de 2a-visita-servida-do-cache. Gate VMware nao se aplica a este nucleo puro host-provado. COMO TESTAR (quando a integracao ligar): smoke QEMU que faz http_cache_fetch 2x para a mesma URL servida por um servidor controlado com Cache-Control: max-age, confirmando que a 2a visita nao emite fetch de transporte (cache hit) -- deterministico com servidor de laboratorio/loopback.

## Mudancas

### Cache HTTP: serving fiel (headers + Location)
- **`include/net/http_cache.h` + `src/net/services/http/http_cache.c`:** a entrada (`struct http_cache_entry`) passa a guardar os **headers de resposta + a `Location`**, além do corpo, para servir respostas **fiéis** do cache (Content-Type, etc. preservados). `http_cache_store` captura-os; um novo `serve_entry` os restaura num `struct http_response`.

### Cache HTTP: orquestração de fetch (RFC 7234)
- **`http_cache_fetch(cache, req, resp, now, fetch_fn, ctx)` (novo):** o ponto de entrada que um fetcher usa. **FRESH** → serve do cache **sem** fetch de transporte (a 2ª visita rápida); **STALE** → adiciona headers condicionais (`If-None-Match`/`If-Modified-Since`) e faz fetch, com **304** reusando o corpo cacheado e **200** substituindo; **MISS** → fetch + store. O transporte é **injetado** por um ponteiro de função (`http_cache_fetch_fn`), então o fluxo é deterministicamente testável em host **sem rede** e fica desacoplado do cliente HTTP real (kernel `http_request`, transporte userland, ou um fake). Resultado em `enum http_cache_result` (ERROR/MISS_FETCHED/FRESH_SERVED/REVALIDATED/REFETCHED).

### Testes + plano
- **`tests/net/test_http_cache.c`:** + `test_fetch_orchestration` — MISS→fetched (1 chamada); **2ª visita FRESH servida do cache sem 2ª chamada de transporte** (prova-chave de "cache acelera a 2ª visita") + status/corpo/Content-Type restaurados; STALE→304 revalidado reusando o corpo cacheado + header condicional na request; fresh de novo após o 304; STALE→200 substituindo; erro de transporte e `fetch_fn` NULL → ERROR (fail-closed).
- **`docs/plans/active/capyos-master-plan.md`** + **`docs/plans/STATUS.md`**: nota `alpha.296` (orquestração host-provada; integração de transporte + smoke QEMU mapeados).

## Validacao

- `make test` — **verde**: `unit_tests` ("Todos os testes passaram", incl. `[http_cache] all cache tests passed`) + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` — **limpo** (Warnings: none; `http_cache.c` ~540 LOC < 900). `make version-audit` — **verde** (current=0.8.0-alpha.296).
- `make all64` — **verde** → `build/capyos64.bin` (orquestração linkada; kernel cresceu ~128 B — só o código de `http_cache_fetch`/`serve_entry`, pois o store maior é caller-owned).
- **Aditivo, sem mudança de ABI** (structs/função/testes novos; as primitivas da `alpha.295` ficam inalteradas em comportamento). O fluxo é **dead code** até a sub-fatia de integração ligar um `fetch_fn` real.
- **VMware/QEMU:** não se aplica a este núcleo puro host-provado. **COMO TESTAR (quando a integração ligar):** smoke QEMU que chama `http_cache_fetch` 2× para a mesma URL servida por um servidor controlado com `Cache-Control: max-age`, confirmando que a 2ª visita não emite fetch de transporte (cache hit) — determinístico com servidor de laboratório/loopback.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.295+20260617` | `0.8.0-alpha.296+20260617` | Slice 7.5: orquestração de fetch do cache HTTP (host-provada). **Sem mudança de ABI** (aditivo; dead code até a integração de transporte). |

Sem mudança de ABI nem de contrato cross-repo. Cache/cookies são adapters do
CapyOS. Os 6 repos irmãos permanecem inalterados.

_Build: `0.8.0-alpha.296+20260617`_
