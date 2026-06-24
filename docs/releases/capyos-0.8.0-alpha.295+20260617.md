# CapyOS 0.8.0-alpha.295+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.295+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — cache HTTP, núcleo host-provado; abre a fatia cache/cookies/forms)

## Resumo executivo

alpha.295+20260617: Etapa 7 / Slice 7.5 (cache HTTP -- nucleo host-provado). Abre a fatia 7.5 (cache/cookies/forms) com o primeiro componente: um cache de respostas HTTP limitado (subconjunto do RFC 7234), CapyOS-side (cache e adapter do CapyOS, nao do core desacoplado do CapyBrowser; irmao do dns_cache do kernel). Novo modulo src/net/services/http/http_cache.c + include/net/http_cache.h: puro, determinístico, fail-closed, com RELOGIO INJETADO (now em segundos, nao lido de timer) e STORE FORNECIDO PELO CHAMADOR (struct http_cache, sem .bss estatico no kernel ate alguem alocar), operando sobre o modelo HTTP do kernel (struct http_request/http_response de net/http.h). Decisoes conservadoras (nunca serve stale incorretamente): so GET; status cacheavel 200/203/301/308/404; honra Cache-Control no-store (req+resp) e no-cache; respostas com Vary NAO sao cacheadas (evita servir a variante errada); frescor por Cache-Control max-age (prioridade) ou Expires-Date; idade por RFC 7234 4.2.3 (apparent age do Date + Age + tempo residente); validadores ETag/Last-Modified guiam revalidacao condicional (If-None-Match/If-Modified-Since) e refresh em 304; store limitado (entradas fixas, cap de corpo por entrada, evicao LRU; corpo acima do cap simplesmente nao e cacheado, fetch normal). Inclui um parser de IMF-fixdate (Sun, 06 Nov 1994 08:49:37 GMT -> epoch UTC) puro e host-testado. API publica: http_cache_init / parse_date / is_cacheable / freshness_lifetime / entry_age / store / lookup (MISS/FRESH/STALE) / add_conditional_headers / refresh_on_304. Provado em host por tests/net/test_http_cache.c (run_http_cache_tests, ~30 checagens em 10 grupos: parser de data canonico 784111777 + sem weekday + epoch zero + malformado/NULL/mes invalido; cacheabilidade GET/POST/no-store/Vary/500/req-no-store/sem-sinal; frescor max-age / Expires-Date / nenhum / max-age-vence-Expires; store+lookup FRESH dentro do max-age e STALE depois + corpo intacto + MISS para chave desconhecida; no-cache forca revalidacao; matematica de idade RFC + clamp de skew de relogio; headers condicionais If-None-Match/If-Modified-Since; refresh em 304 volta a ficar fresh; evicao LRU -- capacidade+1 evicta o mais antigo e retem o mais novo; corpo grande nao armazenado + caminhos NULL fail-closed). Wiring: http_cache.o adicionado aos objetos de net service do kernel (compila freestanding + linka no all64; dead code ate o fetch path ligar) + o par de teste em TEST_SRCS do unit_tests + run_http_cache_tests no test_runner. Validado: make test VERDE (unit_tests 'Todos os testes passaram', incl. marker [http_cache] all cache tests passed; test-browser-pipeline 19/19 inalterado); make layout-audit limpo (http_cache.c ~450 LOC, < 900); make all64 VERDE -> build/capyos64.bin (http_cache.o linkado, +~4.7 KiB). Sem mudanca de ABI (modulo interno novo; nenhum syscall novo nem contrato cross-repo) -- 6 repos irmaos inalterados. A ligacao do cache ao fetch path (kernel http_get e/ou capylibc-net) + um smoke QEMU de 2a-visita-servida-do-cache, mais cookies por dominio e formularios simples, seguem nas proximas sub-fatias da 7.5. Gate VMware: nao se aplica a este nucleo puro host-provado; o smoke de 2a visita sera mapeado quando o cache ligar ao runtime.

## Mudancas

### Novo: cache de respostas HTTP (RFC 7234 subset), CapyOS-side
- **`include/net/http_cache.h` + `src/net/services/http/http_cache.c` (novos):** cache de respostas HTTP limitado, irmão do `dns_cache` do kernel (cache/cookies são adapters do CapyOS, não do core desacoplado do CapyBrowser). **Puro, determinístico, fail-closed**, com **relógio injetado** (`now` em segundos, não lido de timer) e **store fornecido pelo chamador** (`struct http_cache` — sem `.bss` estático no kernel até alguém alocar), operando sobre `struct http_request`/`struct http_response` (`net/http.h`).
- **Decisões conservadoras (nunca serve stale incorretamente):** só GET; status cacheável 200/203/301/308/404; honra `Cache-Control: no-store` (req+resp) e `no-cache`; respostas com `Vary` **não** são cacheadas; frescor por `Cache-Control: max-age` (prioridade) ou `Expires`-`Date`; idade por RFC 7234 §4.2.3 (apparent age do `Date` + `Age` + tempo residente); validadores `ETag`/`Last-Modified` guiam revalidação condicional (`If-None-Match`/`If-Modified-Since`) e refresh em `304`; store limitado (entradas fixas, cap de corpo por entrada, evicção LRU; corpo acima do cap não é cacheado → fetch normal). Inclui um parser de **IMF-fixdate** (`Sun, 06 Nov 1994 08:49:37 GMT` → epoch UTC) puro/host-testado.
- API: `http_cache_init`/`parse_date`/`is_cacheable`/`freshness_lifetime`/`entry_age`/`store`/`lookup` (MISS/FRESH/STALE)/`add_conditional_headers`/`refresh_on_304`.

### Testes + wiring
- **`tests/net/test_http_cache.c` (novo, `run_http_cache_tests`):** ~30 checagens em 10 grupos (parser de data canônico `784111777` + sem weekday + epoch zero + malformado/NULL/mês inválido; cacheabilidade GET/POST/no-store/Vary/500/req-no-store/sem-sinal; frescor max-age/Expires-Date/nenhum/max-age-vence-Expires; store+lookup FRESH dentro do max-age e STALE depois + corpo intacto + MISS; no-cache força revalidação; idade RFC + clamp de skew; headers condicionais; refresh em 304; evicção LRU capacidade+1; corpo grande não armazenado + caminhos NULL fail-closed).
- **`Makefile`:** `http_cache.o` nos objetos de net service do kernel (compila freestanding + linka no `all64`; **dead code até o fetch path ligar**) + o par de teste em `TEST_SRCS`. **`tests/test_runner.c`:** declara/invoca `run_http_cache_tests`.
- **`docs/plans/active/capyos-master-plan.md`**: Slice 7.5 — núcleo do cache **host-provado em `alpha.295`**. `docs/plans/STATUS.md`: nota `alpha.295`.

## Validacao

- `make test` — **verde**: `unit_tests` ("Todos os testes passaram", incl. marker `[http_cache] all cache tests passed`) + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` — **limpo** (Warnings: none; `http_cache.c` ~450 LOC < 900). `make version-audit` — **verde** (current=0.8.0-alpha.295).
- `make all64` — **verde** → `build/capyos64.bin` (`http_cache.o` linkado, +~4.7 KiB).
- **Sem mudança de ABI** (módulo interno novo; nenhum syscall novo nem contrato cross-repo).
- **VMware:** não se aplica a este núcleo puro host-provado. O smoke QEMU de **2ª-visita-servida-do-cache** será mapeado quando o cache ligar ao fetch path (próxima sub-fatia).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.294+20260617` | `0.8.0-alpha.295+20260617` | Slice 7.5: núcleo do cache HTTP (RFC 7234 subset), host-provado. **Sem mudança de ABI** (módulo de net service interno, dead code até o fetch path ligar). |

Sem mudança de ABI nem de contrato cross-repo. Cache/cookies são adapters do
CapyOS (não do core desacoplado do CapyBrowser). Os 6 repos irmãos permanecem
inalterados.

_Build: `0.8.0-alpha.295+20260617`_
