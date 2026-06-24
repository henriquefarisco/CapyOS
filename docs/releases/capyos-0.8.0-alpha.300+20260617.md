# CapyOS 0.8.0-alpha.300+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.300+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 / Slice 7.6 — HSTS (RFC 6797), núcleo host-provado)

## Resumo executivo

alpha.300+20260617: Etapa 7 / Slice 7.6 (HSTS -- nucleo host-provado). Continua o endurecimento da 7.6 com o store de HTTP Strict-Transport-Security (subconjunto do RFC 6797), CapyOS-side (irmao do http_cache/http_cookies/http_session/http_fetch_policy). Novo include/net/http_hsts.h + src/net/services/http/http_hsts.c: puro, determinístico, fail-closed, freestanding, com relogio INJETADO (now em segundos) e store FORNECIDO PELO CHAMADOR (struct http_hsts_store, sem .bss no kernel ate alguem alocar). HSTS endurece a navegacao HTTPS-first da alpha.299: uma vez que um host envia um header Strict-Transport-Security valido SOBRE conexao segura, o cliente DEVE usar https para aquele host (e, com includeSubDomains, seus subdominios) ate a politica expirar -- derrotando SSL-stripping/downgrade onde um atacante ativo reescreve links https para http. Onde o http_fetch_policy torna uma navegacao http um UPGRADE suave, uma entrada HSTS correspondente torna o upgrade OBRIGATORIO. Conservador (RFC 6797): o header so e honrado SOBRE conexao segura (ignorado sobre http, 8.1); max-age e obrigatorio (header sem ele e ignorado); max-age=0 REMOVE a politica do host; host dado como IP-literal e rejeitado (8.1.1); includeSubDomains estende aos subdominios. API: http_hsts_init / http_hsts_process_header(store, host, value, secure, now) / http_hsts_should_upgrade(store, host, now) / http_hsts_gc. Provado em host por tests/net/test_http_hsts.c (run_http_hsts_tests, 10 grupos: store basico exato vs subdominio; header sobre http ignorado; max-age ausente ignorado; max-age=0 remove + no-op em host desconhecido; includeSubDomains apex/sub/deep-sub/suffix-sem-dot/prefix-host; IP-literal rejeitado; expiry + gc; max-age citado + whitespace; evicao em capacidade+3; caminhos NULL fail-closed). Wiring: http_hsts.o nos objetos de net service do kernel (linka no all64; dead code ate o fetch path ligar) + par de teste em TEST_SRCS + run_http_hsts_tests no test_runner. Validado: make test VERDE (unit_tests 'Todos os testes passaram', incl. marker [http_hsts] all HSTS tests passed; test-browser-pipeline 19/19 inalterado); make layout-audit limpo (http_hsts.c ~229 LOC, < 900); make all64 VERDE -> build/capyos64.bin (http_hsts.o linkado). Sem mudanca de ABI (modulo interno novo; nenhum syscall novo nem contrato cross-repo); 6 repos irmaos inalterados. Compoe com http_fetch_policy no call-site (HSTS consultado primeiro -> forca https; depois a politica de fetch decide ALLOW/BLOCK); entra em uso quando o fetch de navegacao ligar ao runtime. Gate VMware nao se aplica a este nucleo puro host-provado; COMO TESTAR (quando ligar): smoke confirmando que apos receber um Strict-Transport-Security valido a navegacao http subsequente ao host e forcada a https.

## Mudancas

### Novo: store HSTS (RFC 6797 — Strict-Transport-Security)
- **`include/net/http_hsts.h` + `src/net/services/http/http_hsts.c` (novos):** endurece a navegação HTTPS-first da `alpha.299` — uma vez que um host envia um `Strict-Transport-Security` válido **sobre conexão segura**, o cliente DEVE usar https para o host (e, com `includeSubDomains`, seus subdomínios) até a política expirar, derrotando SSL-stripping/downgrade. Onde `http_fetch_policy` faz da navegação http um UPGRADE **suave**, uma entrada HSTS torna-o **obrigatório**. CapyOS-side, irmão de cache/cookies/session/fetch-policy. **Puro, determinístico, fail-closed**, relógio **injetado**, store **fornecido pelo chamador**.
- **Conservador (RFC 6797):** header honrado só sobre conexão segura (ignorado sobre http, §8.1); `max-age` obrigatório; `max-age=0` **remove** a política; host IP-literal rejeitado (§8.1.1); `includeSubDomains` estende a subdomínios.
- API: `http_hsts_init`/`process_header(store, host, value, secure, now)`/`should_upgrade(store, host, now)`/`gc`.

### Testes + wiring
- **`tests/net/test_http_hsts.c` (novo, `run_http_hsts_tests`, 10 grupos):** store exato vs subdomínio; header sobre http ignorado; max-age ausente ignorado; `max-age=0` remove (+ no-op em host desconhecido); `includeSubDomains` (apex/sub/deep/suffix-sem-dot/prefix-host); IP rejeitado; expiry + gc; max-age citado/espaçado; evicção; NULL fail-closed.
- **`Makefile`:** `http_hsts.o` nos objetos de net service do kernel (linka no `all64`; **dead code até o fetch path ligar**) + o par de teste em `TEST_SRCS`. **`tests/test_runner.c`:** declara/invoca `run_http_hsts_tests`.
- **`docs/plans/active/capyos-master-plan.md`** + **`docs/plans/STATUS.md`**: nota `alpha.300`.

## Validacao

- `make test` — **verde**: `unit_tests` ("Todos os testes passaram", incl. `[http_hsts] all HSTS tests passed`) + `test-browser-pipeline` (`19/19`, inalterado).
- `make layout-audit` — **limpo** (Warnings: none; `http_hsts.c` ~229 LOC). `make version-audit` — **verde** (current=0.8.0-alpha.300).
- `make all64` — **verde** → `build/capyos64.bin` (`http_hsts.o` linkado).
- **Sem mudança de ABI** (módulo interno novo). Compõe com `http_fetch_policy` no call-site (HSTS consultado primeiro → força https; depois a política de fetch decide ALLOW/BLOCK); entra em uso quando o fetch de navegação ligar ao runtime.
- **VMware/QEMU:** não se aplica a este núcleo puro host-provado. **COMO TESTAR (quando ligar):** smoke confirmando que após receber um `Strict-Transport-Security` válido, a navegação http subsequente ao host é forçada a https.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.299+20260617` | `0.8.0-alpha.300+20260617` | Slice 7.6: store HSTS (RFC 6797), host-provado. **Sem mudança de ABI** (módulo de net service interno, dead code até o fetch path ligar). |

Sem mudança de ABI nem de contrato cross-repo. As políticas de fetch (cache,
cookies, sessão, segurança, HSTS) são adapters do CapyOS. Os 6 repos irmãos
permanecem inalterados.

_Build: `0.8.0-alpha.300+20260617`_
