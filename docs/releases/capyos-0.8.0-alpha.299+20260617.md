# CapyOS 0.8.0-alpha.299+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.299+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.6 — política de segurança de fetch, núcleo host-provado; abre o endurecimento da 7.6)

## Resumo executivo

alpha.299+20260617: Etapa 7 / Slice 7.6 (politica de seguranca de fetch -- nucleo host-provado). Abre o endurecimento da 7.6 com a politica que um fetcher aplica ANTES de abrir conexao: allow-list de scheme + navegacao HTTPS-first + bloqueio de mixed-content. Novo include/net/http_fetch_policy.h + src/net/services/http/http_fetch_policy.c, CapyOS-side (o core do navegador delega HTTPS-first/mixed-content ao adapter do host; irmao do http_cache/http_cookies/http_session). Puro, determinístico, fail-closed, freestanding, SEM estado: cada decisao e funcao apenas do(s) scheme(s) classificado(s). Conservador (nega por padrao): scheme desconhecido -> BLOCK; sub-recurso http (inseguro) de pagina https (segura) -> BLOCK (mixed content, inclusive passivo); file:/ftp:/javascript: nunca buscaveis por conteudo web. API: http_fetch_classify_scheme (case-insensitive, sem ':'; OTHER se desconhecido/NULL/vazio) -> enum {https,http,wss,ws,data,blob,about,file,ftp,javascript,other}; http_fetch_scheme_is_secure (https/wss); http_fetch_policy_navigation (https->ALLOW, http->UPGRADE [HTTPS-first], data/about/blob->ALLOW, file/ftp/ws/wss/javascript/other->BLOCK); http_fetch_policy_subresource(page_secure, sub) (pagina segura so puxa sub-recurso seguro -> bloqueia mixed; data/blob/about inline ALLOW; file/ftp/javascript/other sempre BLOCK; pagina insegura puxa http/https) + nomes (decision/scheme) para diagnostico. Provado em host por tests/net/test_http_fetch_policy.c (run_http_fetch_policy_tests: classificacao incl. case-insensitive/near-miss/NULL/vazio; is_secure; navegacao; matriz de sub-recurso pagina-segura x insegura; o caso critico 'pagina https bloqueia imagem http (mixed content)'). Wiring: http_fetch_policy.o nos objetos de net service do kernel (linka no all64; dead code ate o fetch path ligar) + par de teste em TEST_SRCS + run_http_fetch_policy_tests no test_runner. Validado: make test VERDE (unit_tests 'Todos os testes passaram', incl. marker [http_fetch_policy] all fetch-policy tests passed; test-browser-pipeline 19/19 inalterado); make layout-audit limpo (http_fetch_policy.c ~114 LOC, < 900); make all64 VERDE -> build/capyos64.bin (http_fetch_policy.o linkado, +~400 B). Sem mudanca de ABI (modulo interno novo; nenhum syscall novo nem contrato cross-repo); 6 repos irmaos inalterados. E o gate que a busca de sub-recurso de rede (ex. as imagens da 7.4 quando vierem da rede) e a navegacao top-level devem aplicar; entra em uso quando o fetch de sub-recurso/navegacao ligar ao runtime. Gate VMware nao se aplica a este nucleo puro host-provado; COMO TESTAR (quando ligar): smoke confirmando que uma pagina https recusa um sub-recurso http (mixed content) e que a navegacao http e elevada (UPGRADE) ou recusada conforme a politica.

## Mudancas

### Novo: política de segurança de fetch (scheme + HTTPS-first + mixed-content)
- **`include/net/http_fetch_policy.h` + `src/net/services/http/http_fetch_policy.c` (novos):** a política que um fetcher aplica **antes de abrir conexão**, CapyOS-side (o core do navegador delega HTTPS-first/mixed-content ao adapter do host; irmão de `http_cache`/`http_cookies`/`http_session`). **Puro, determinístico, fail-closed, sem estado** — cada decisão é função apenas do(s) scheme(s) classificado(s).
- **Conservador (nega por padrão):** scheme desconhecido → BLOCK; sub-recurso **http** de página **https** → BLOCK (mixed content, inclusive passivo); `file:`/`ftp:`/`javascript:` nunca buscáveis por conteúdo web.
- API: `http_fetch_classify_scheme` (ci, sem `:`; OTHER se desconhecido/NULL) → `enum {https,http,wss,ws,data,blob,about,file,ftp,javascript,other}`; `http_fetch_scheme_is_secure` (https/wss); `http_fetch_policy_navigation` (https→ALLOW, http→**UPGRADE** [HTTPS-first], data/about/blob→ALLOW, resto→BLOCK); `http_fetch_policy_subresource(page_secure, sub)` (página segura só puxa sub-recurso seguro; data/blob/about ALLOW; file/ftp/javascript/other BLOCK) + nomes para diagnóstico.

### Testes + wiring
- **`tests/net/test_http_fetch_policy.c` (novo, `run_http_fetch_policy_tests`):** classificação (ci/near-miss/NULL/vazio); `is_secure`; navegação; **matriz de sub-recurso** página-segura × insegura; e o caso crítico **"página https bloqueia imagem http (mixed content)"**.
- **`Makefile`:** `http_fetch_policy.o` nos objetos de net service do kernel (linka no `all64`; **dead code até o fetch path ligar**) + o par de teste em `TEST_SRCS`. **`tests/test_runner.c`:** declara/invoca `run_http_fetch_policy_tests`.
- **`docs/plans/active/capyos-master-plan.md`** + **`docs/plans/STATUS.md`**: nota `alpha.299` (Slice 7.6 aberta; política de fetch host-provada).

## Validacao

- `make test` — **verde**: `unit_tests` ("Todos os testes passaram", incl. `[http_fetch_policy] all fetch-policy tests passed`) + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` — **limpo** (Warnings: none; `http_fetch_policy.c` ~114 LOC). `make version-audit` — **verde** (current=0.8.0-alpha.299).
- `make all64` — **verde** → `build/capyos64.bin` (`http_fetch_policy.o` linkado, +~400 B).
- **Sem mudança de ABI** (módulo interno novo). É o gate que a busca de sub-recurso de rede (ex.: as imagens da 7.4 quando vierem da rede) e a navegação top-level devem aplicar; entra em uso quando o fetch de sub-recurso/navegação ligar ao runtime.
- **VMware/QEMU:** não se aplica a este núcleo puro host-provado. **COMO TESTAR (quando ligar):** smoke confirmando que uma página https recusa um sub-recurso http (mixed content) e que a navegação http é elevada (UPGRADE) ou recusada conforme a política.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.298+20260617` | `0.8.0-alpha.299+20260617` | Slice 7.6: política de segurança de fetch (scheme/HTTPS-first/mixed-content), host-provada. **Sem mudança de ABI** (módulo de net service interno, dead code até o fetch path ligar). |

Sem mudança de ABI nem de contrato cross-repo. As políticas de fetch (cache,
cookies, sessão, segurança) são adapters do CapyOS. Os 6 repos irmãos permanecem
inalterados.

_Build: `0.8.0-alpha.299+20260617`_
