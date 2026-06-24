# CapyOS 0.8.0-alpha.298+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.298+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — sessão de fetch: cache + cookies compostos, núcleo host-provado; completa a camada pura de fetch-policy)

## Resumo executivo

alpha.298+20260617: Etapa 7 / Slice 7.5 (sessao de fetch: cache + cookies compostos -- nucleo host-provado). Completa a camada pura de fetch-policy da 7.5 compondo os dois adapters puros (http_cache + http_cookies) na unica cadeia de fetch que uma sessao de navegador usa, SEM acopla-los entre si. Novo include/net/http_session.h + src/net/services/http/http_session.c: struct http_session { http_cache cache; http_cookie_jar jar; } + http_session_fetch(s, req, resp, now, fetch_fn, ctx) que, por fetch: (1) anexa os cookies do jar que casam (host/path/scheme) como header Cookie na request; (2) roda o fluxo de cache RFC 7234 (http_cache_fetch) -- um hit FRESH e servido SEM fetch de transporte, senao revalida/busca via a fn injetada; (3) colhe Set-Cookie de uma resposta de REDE (miss/refetch) de volta no jar (um hit servido/revalidado devolve o corpo cacheado, cujo Set-Cookie ja foi processado no store). Puro, determinístico (relogio injetado via now), fail-closed, freestanding: a sessao e FORNECIDA PELO CHAMADOR (sem .bss no kernel ate alguem alocar) e o transporte e INJETADO (http_cache_fetch_fn), entao a cadeia inteira e testavel em host SEM rede. Este e o ponto de entrada que um binding de fetch real (kernel http_request, ou um transporte userland) vai chamar; o binding em si aguarda um runtime de navegador multi-fetch/persistente (o app de texto single-shot nao se beneficia de um cache, e o capy_http_get do capylibc-net nem aceita headers de request -- ambos mapeados). Provado em host por tests/net/test_http_session.c (run_http_session_tests, 3 grupos: fluxo cache+cookie -- MISS /login seta SID + corpo cacheado com max-age, 2a /login servida do cache SEM fetch (f.calls inalterado), /app nao-cacheado e buscado E a request carrega o cookie SID armazenado (a fake fetch inspeciona o header Cookie); gating Secure -- cookie Secure omitido sobre http, enviado sobre https; caminhos NULL session/fetch-NULL/attach-NULL fail-closed). Wiring: http_session.o nos objetos de net service do kernel (linka no all64 resolvendo http_cache_fetch + http_cookie_jar_*; dead code ate o fetch path ligar) + o par de teste em TEST_SRCS + run_http_session_tests no test_runner. Validado: make test VERDE (unit_tests 'Todos os testes passaram', incl. markers [http_session] all session tests passed, [http_cookies] all cookie tests passed e [http_cache] all cache tests passed; test-browser-pipeline 19/19 inalterado); make layout-audit limpo (http_session.c ~67 LOC); make all64 VERDE -> build/capyos64.bin (http_session.o linkado, +~176 B). Sem mudanca de ABI (modulo interno novo; nenhum syscall novo nem contrato cross-repo); 6 repos irmaos inalterados. Com o cache (295/296), os cookies (297) e a sessao que os compoe (298) host-provados, a camada PURA de fetch-policy da 7.5 esta completa; os formularios simples ja estao prontos no core do CapyBrowser (capy_form_submit). Restam apenas: (a) o binding de transporte real -- precisa estender o capy_http_get do capylibc-net para aceitar headers de request (Cookie + If-None-Match) + um runtime de navegador multi-fetch -- e (b) a 7.6 (streaming/limites/mixed-content). Gate VMware nao se aplica a este nucleo puro host-provado; COMO TESTAR (quando o binding ligar): smoke QEMU num navegador multi-fetch confirmando 2a-visita-servida-do-cache + header Cookie reenviado a partir do jar.

## Mudancas

### Novo: sessão de fetch (cache + cookies compostos)
- **`include/net/http_session.h` + `src/net/services/http/http_session.c` (novos):** compõe os dois adapters puros (`http_cache` + `http_cookies`) na única cadeia de fetch que uma sessão de navegador usa, **sem acoplá-los entre si**. `struct http_session { http_cache cache; http_cookie_jar jar; }` + `http_session_fetch(s, req, resp, now, fetch_fn, ctx)`: (1) anexa os cookies do jar que casam (host/path/scheme) como header `Cookie`; (2) roda o fluxo de cache RFC 7234 (`http_cache_fetch`) — um hit **FRESH** é servido **sem** fetch de transporte; (3) colhe `Set-Cookie` de uma resposta de **rede** (miss/refetch) de volta no jar (um hit servido/revalidado devolve o corpo cacheado, cujo `Set-Cookie` já foi processado no store).
- **Puro, determinístico (relógio injetado), fail-closed, freestanding**: sessão **fornecida pelo chamador** (sem `.bss` no kernel até alguém alocar) + transporte **injetado** (`http_cache_fetch_fn`) → cadeia inteira testável em host **sem rede**. É o ponto de entrada que um binding de fetch real chamará.

### Testes + wiring
- **`tests/net/test_http_session.c` (novo, `run_http_session_tests`):** fluxo cache+cookie (MISS `/login` seta SID + cacheia; 2ª `/login` **servida do cache sem fetch**; `/app` buscado **com o cookie SID na request**); gating `Secure` (omitido em http, enviado em https); NULL/`fetch`-NULL fail-closed.
- **`Makefile`:** `http_session.o` nos objetos de net service do kernel (linka no `all64` resolvendo `http_cache_fetch` + `http_cookie_jar_*`; **dead code até o fetch path ligar**) + o par de teste em `TEST_SRCS`. **`tests/test_runner.c`:** declara/invoca `run_http_session_tests`.
- **`docs/plans/active/capyos-master-plan.md`** + **`docs/plans/STATUS.md`**: nota `alpha.298` (camada pura de fetch-policy da 7.5 completa).

## Validacao

- `make test` — **verde**: `unit_tests` ("Todos os testes passaram", incl. `[http_session]`/`[http_cookies]`/`[http_cache] all tests passed`) + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` — **limpo** (Warnings: none; `http_session.c` ~67 LOC). `make version-audit` — **verde** (current=0.8.0-alpha.298).
- `make all64` — **verde** → `build/capyos64.bin` (`http_session.o` linkado, +~176 B).
- **Sem mudança de ABI** (módulo interno novo). Com o cache (`alpha.295/296`), os cookies (`alpha.297`) e a sessão que os compõe (`alpha.298`) host-provados, a **camada pura de fetch-policy da 7.5 está completa** (forms já prontos no core do CapyBrowser, `capy_form_submit`).
- **VMware/QEMU:** não se aplica a este núcleo puro host-provado. Restam: o binding de transporte real (estender `capy_http_get` do capylibc-net p/ aceitar headers de request — Cookie + If-None-Match — + um runtime de navegador multi-fetch) e a 7.6. **COMO TESTAR (quando o binding ligar):** smoke QEMU num navegador multi-fetch confirmando 2ª-visita-servida-do-cache + header `Cookie` reenviado do jar.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.297+20260617` | `0.8.0-alpha.298+20260617` | Slice 7.5: sessão de fetch (cache + cookies compostos), host-provada. **Sem mudança de ABI** (módulo de net service interno, dead code até o fetch path ligar). |

Sem mudança de ABI nem de contrato cross-repo. Cache/cookies/sessão são adapters
do CapyOS. Os 6 repos irmãos permanecem inalterados.

_Build: `0.8.0-alpha.298+20260617`_
