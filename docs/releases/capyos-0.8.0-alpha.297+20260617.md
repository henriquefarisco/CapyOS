# CapyOS 0.8.0-alpha.297+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.297+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.5 — cookies por domínio, núcleo host-provado)

## Resumo executivo

alpha.297+20260617: Etapa 7 / Slice 7.5 (cookies por dominio -- nucleo host-provado). Continua a fatia 7.5 com o jar de cookies por dominio (subconjunto do RFC 6265), CapyOS-side (cookies sao adapters do CapyOS; irmao do http_cache). Novo include/net/http_cookies.h + src/net/services/http/http_cookies.c: puro, determinístico, fail-closed, com relogio INJETADO (now em segundos) e jar FORNECIDO PELO CHAMADOR (struct http_cookie_jar, sem .bss no kernel ate alguem alocar), operando sobre o modelo HTTP do kernel (Set-Cookie em struct http_response + host/path da request). Conservador (nunca manda cookie pro site errado): parseia name=value + atributos Domain/Path/Max-Age/Expires/Secure/HttpOnly (SameSite parseado-e-ignorado); Max-Age vence Expires; expiry ausente/inparseavel -> session cookie; um Set-Cookie cujo Domain NAO domain-matcheia o host da request e REJEITADO (anti-injecao, RFC 6265 5.3); sem Domain -> host-only (match exato do host); default-path derivado do path da request (5.1.4); cookie com expiry no passado (ou Max-Age<=0) DELETA um match; no envio: domain-match + path-match + (Secure -> so https) + nao-expirado, ordenado por path mais longo primeiro (5.4); jar limitado com evicao do mais antigo. Reusa o parser IMF-fixdate compartilhado (http_cache_parse_date) para Expires; o formato Netscape com tracos e tratado como inparseavel -> session (mapeado como melhoria). API: http_cookie_jar_init / parse_set_cookie / domain_match / path_match / set_from_response / header / gc. Provado em host por tests/net/test_http_cookies.c (run_http_cookies_tests, ~40 checagens em 10 grupos: parse basico name/value/default-domain+path/session + valor com '=' + sem '=' rejeitado; atributos completos + Max-Age-vence-Expires + Expires IMF 784111777 + dot-domain stripado; REJEICAO cross-domain + suffix-sem-dot; regras de match (domain exact/subdomain/diff/ip, path prefix/exact/boundary/root); envio + ordem por path mais longo; gating Secure (http omite, https inclui); envio a subdominio vs host-only nao-enviado a outro host; expiry + delete por Max-Age=0; update por re-set + evicao em capacidade+4; caminhos NULL/cap-zero fail-closed). Wiring: http_cookies.o nos objetos de net service do kernel (linka no all64, resolvendo http_cache_parse_date; dead code ate o fetch path ligar) + o par de teste em TEST_SRCS + run_http_cookies_tests no test_runner. Validado: make test VERDE (unit_tests 'Todos os testes passaram', incl. markers [http_cookies] all cookie tests passed E [http_cache] all cache tests passed; test-browser-pipeline 19/19 inalterado); make layout-audit limpo (http_cookies.c ~460 LOC, < 900); make all64 VERDE -> build/capyos64.bin (http_cookies.o linkado, +~8.7 KiB). Sem mudanca de ABI (modulo interno novo; nenhum syscall novo nem contrato cross-repo); 6 repos irmaos inalterados. Com o cache (alpha.295/296) e os cookies (alpha.297) host-provados, a proxima sub-fatia liga ambos ao fetch path real + um smoke QEMU; depois, formularios simples fecham a 7.5. Gate VMware nao se aplica a este nucleo puro host-provado; o smoke de runtime sera mapeado quando os adapters ligarem ao fetch path.

## Mudancas

### Novo: jar de cookies por domínio (RFC 6265 subset), CapyOS-side
- **`include/net/http_cookies.h` + `src/net/services/http/http_cookies.c` (novos):** jar de cookies por domínio limitado, irmão do `http_cache` (cookies são adapters do CapyOS, não do core desacoplado do CapyBrowser). **Puro, determinístico, fail-closed**, com **relógio injetado** e **jar fornecido pelo chamador** (`struct http_cookie_jar` — sem `.bss` no kernel até alguém alocar), sobre o modelo HTTP do kernel (`Set-Cookie` em `struct http_response` + host/path da request).
- **Conservador (nunca manda cookie pro site errado):** parseia `name=value` + `Domain`/`Path`/`Max-Age`/`Expires`/`Secure`/`HttpOnly` (`SameSite` parseado-e-ignorado); `Max-Age` vence `Expires`; expiry ausente/inparseável → session; um `Set-Cookie` cujo `Domain` **não** domain-matcheia o host da request é **rejeitado** (anti-injeção, RFC 6265 §5.3); sem `Domain` → host-only; default-path da request (§5.1.4); expiry no passado (ou `Max-Age<=0`) **deleta** um match; envio = domain-match + path-match + (`Secure`→só https) + não-expirado, ordenado por path mais longo primeiro (§5.4); jar limitado com evicção do mais antigo. Reusa o parser IMF-fixdate compartilhado (`http_cache_parse_date`) para `Expires`.
- API: `http_cookie_jar_init`/`parse_set_cookie`/`domain_match`/`path_match`/`set_from_response`/`header`/`gc`.

### Testes + wiring
- **`tests/net/test_http_cookies.c` (novo, `run_http_cookies_tests`):** ~40 checagens em 10 grupos (parse básico/valor-com-`=`/sem-`=`; atributos + Max-Age-vence-Expires + Expires IMF + dot-domain; **rejeição cross-domain** + suffix-sem-dot; match domain/path; envio + ordem por path; gating `Secure`; subdomínio vs host-only; expiry + delete por `Max-Age=0`; update por re-set + evicção; NULL/cap-zero fail-closed).
- **`Makefile`:** `http_cookies.o` nos objetos de net service do kernel (linka no `all64`, resolvendo `http_cache_parse_date`; **dead code até o fetch path ligar**) + o par de teste em `TEST_SRCS`. **`tests/test_runner.c`:** declara/invoca `run_http_cookies_tests`.
- **`docs/plans/active/capyos-master-plan.md`** + **`docs/plans/STATUS.md`**: nota `alpha.297` (cookies host-provados).

## Validacao

- `make test` — **verde**: `unit_tests` ("Todos os testes passaram", incl. `[http_cookies] all cookie tests passed` **e** `[http_cache] all cache tests passed`) + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` — **limpo** (Warnings: none; `http_cookies.c` ~460 LOC < 900). `make version-audit` — **verde** (current=0.8.0-alpha.297).
- `make all64` — **verde** → `build/capyos64.bin` (`http_cookies.o` linkado, +~8.7 KiB).
- **Sem mudança de ABI** (módulo interno novo). Com o cache (`alpha.295/296`) e os cookies (`alpha.297`) host-provados, a próxima sub-fatia liga ambos ao fetch path real + um smoke QEMU; depois, formulários simples fecham a 7.5.
- **VMware/QEMU:** não se aplica a este núcleo puro host-provado; o smoke de runtime será mapeado quando os adapters ligarem ao fetch path.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.296+20260617` | `0.8.0-alpha.297+20260617` | Slice 7.5: jar de cookies por domínio (RFC 6265 subset), host-provado. **Sem mudança de ABI** (módulo de net service interno, dead code até o fetch path ligar). |

Sem mudança de ABI nem de contrato cross-repo. Cache/cookies são adapters do
CapyOS. Os 6 repos irmãos permanecem inalterados.

_Build: `0.8.0-alpha.297+20260617`_
