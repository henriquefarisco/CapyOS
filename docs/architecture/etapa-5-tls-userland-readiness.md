# Etapa 5 — TLS userland real: readiness audit + plano do 1º slice

> **ESTADO: BLOQUEADA.** A Etapa 5 só pode **iniciar** quando a Etapa 4
> fechar (Fase F externa em VMware). Este documento é **preparação
> read-only** — auditoria do estado atual e desenho do primeiro slice.
> **Não autoriza implementação** e não conta como início da etapa
> (regra sequencial: `docs/plans/active/capyos-master-plan.md` §1).

**Data:** 2026-05-29 · **Versão base:** `0.8.0-alpha.261+20260529`
**Definição autoritativa:** `capyos-master-plan.md` §8.

---

## 1. Resultado da auditoria (estado real do TLS hoje)

Existem **duas superfícies TLS distintas** no tree, e a Etapa 5 trata
apenas da segunda:

### 1.1 Kernel-side `src/security/tls.c` — BearSSL **real e em produção**

`src/security/tls.c` (695 LOC) é um **cliente TLS 1.2 BearSSL completo**,
não um stub:

- `tls_connect()` (`src/security/tls.c:514`) aloca contexto + `iobuf`
  (`BR_SSL_BUFSIZE_BIDI`), aplica `SO_RCVTIMEO`/`SO_SNDTIMEO`, carrega os
  trust anchors reais via `capyos_tls_trust_anchors()`
  (`src/security/tls_trust_anchors.c`), suporta CA custom
  (`tls_load_custom_anchor`), chama `br_ssl_client_init_full`, fixa
  `BR_TLS12`, ALPN `http/1.1`, semeia o engine (`tls_seed_engine`),
  `br_ssl_client_reset(hostname, ...)` (SNI + verificação) e dirige o
  handshake por `br_sslio_flush` (`tls_handshake`, `:489`).
- `tls_send`/`tls_recv`/`tls_close`/`tls_free` são reais (`br_sslio_*`).
- Zeroização (`tls_memzero`) e liberação de anchor custom presentes.
- **Build:** o `Makefile` compila **todo** o BearSSL vendorizado
  (`BEARSSL_OBJS`, `Makefile:19-20`) e linka no kernel (`Makefile:436`);
  `CFLAGS64` inclui `-Ithird_party/bearssl/inc` (`Makefile:53`).
- **Em produção:** é o que move `http_get` (capypkg baixou módulos do
  GitHub Release por HTTPS em alpha.244; `net-fetch` reporta TLS state).

> Conclusão: **HTTPS kernel-side via BearSSL já é real.** Não é alvo da
> Etapa 5 (a não ser como implementação de referência).

### 1.2 Userland `userland/lib/capylibc-tls/` — **metadata-only / fail-closed**

A `libcapy-tls` userland (2693 LOC) é um scaffold elaborado mas **sem
engine real**:

- `capy_tls_is_supported()` retorna `0` (`userland/lib/capylibc-tls/capy_tls.c:30`).
- `capy_tls_connect_tcp()` valida args/hostname/config, adquire/prepara
  contexto, chama `capy_tls_backend_connect()` e retorna `NULL`
  (`capy_tls.c:34-66`).
- `capy_tls_backend_connect()` (`capy_tls_backend.c:175`) monta um
  **preflight fail-closed**: cada gate (trust anchors, backend plan,
  bearssl reserved state, bearssl adapter) exige explicitamente
  `handshake_allowed == 0` e `engine_initialized == 0`, e ao fim retorna
  **`CAPY_TLS_EUNSUPPORTED`** (`:185`). Nenhum byte TLS flui.
- `capy_tls_send/recv/close` retornam `EUNSUPPORTED`.
- Trust anchors userland são **metadata-only** (catálogo de
  contagem/fingerprint RSA/EC em `capy_tls_trust.c` + `capy_tls_trust_bundle.c`),
  **sem os bytes `br_x509_trust_anchor[]` reais**.
- **`grep` por símbolos `br_*` em `userland/` = 0**: o userland **não
  chama BearSSL**.
- **Build:** o BearSSL **não** é linkado no userland; o CFLAGS do
  capylibc (`Makefile:~802`) tem apenas `-Iinclude -Iuserland/include`,
  **sem** `-Ithird_party/bearssl/inc`.

### 1.3 Caminho HTTPS userland

`capy_net_internal_https_fail_closed()`
(`userland/lib/capylibc-net/capy_net_tls.c:19`) falha fechado enquanto
`!capy_tls_is_supported()` — exatamente o critério de aceite
"HTTPS em libcapy-net deixa de retornar unsupported para caso válido".

---

## 2. Gaps confirmados para um handshake userland real

| # | Gap | Estado hoje | Evidência |
|---|---|---|---|
| G1 | **Entropia userland** p/ semear o DRBG do BearSSL | **AUSENTE** — não há syscall `getrandom`; tabela termina em `SYS_DNS_RESOLVE=41` | `include/kernel/syscall_numbers.h:24-79` |
| G2 | BearSSL linkado no userland | Ausente (só kernel) | `Makefile:802` sem `-Ithird_party/bearssl/inc` |
| G3 | Trust anchors reais no userland | Só metadata (contagem/fingerprint) | `capy_tls_trust.c`, `capy_tls_backend.c:35-89` |
| G4 | Engine real (`br_ssl_client`+`br_sslio`) no seam | `EUNSUPPORTED` fixo | `capy_tls_backend.c:175-186` |
| G5 | Fonte de tempo p/ validade X.509 | **OK** — `SYS_TIME=39` existe | `syscall_numbers.h:63` |
| G6 | I/O de socket p/ callbacks `br_sslio` | **OK** — `SYS_SEND/RECV=33/34` | `syscall_numbers.h:57-58` |
| G7 | Zeroização/ownership do contexto userland | A implementar (espelhar kernel) | `tls.c:530,661-671` (referência) |
| G8 | Integração HTTPS (`is_supported→1`) | Gated em `capy_tls_is_supported()` | `capy_net_tls.c:24` |

**Já decoplado e reusável:** a política de hostname
`tls_hostname_policy_valid()` (`include/security/tls_hostname_policy.h`)
já é compartilhada por kernel e userland.

---

## 3. Implementação de referência (espelhar do kernel)

O kernel `src/security/tls.c` é o gabarito 1:1 para o userland:

| Função userland a criar | Referência kernel |
|---|---|
| seed do DRBG | `tls_seed_engine` (`tls.c:150`) — trocar CSPRNG kernel por syscall `getrandom` |
| tempo de validação | `tls_set_validation_time` (`tls.c:157`) — trocar RTC por `SYS_TIME` |
| callbacks I/O | `tls_socket_read`/`tls_socket_write` (`tls.c:323/333`) — trocar `socket_recv/send` por `SYS_RECV/SEND` |
| connect/handshake | `tls_connect`/`tls_handshake` (`tls.c:514/489`) |
| send/recv/close/free | `tls_send/recv/close/free` (`tls.c:593/620/647/661`) |

---

## 4. Sequência de slices proposta (rumo aos critérios da §8)

| Slice | Entrega | Fail-closed mantido? | Gate |
|---|---|---|---|
| **5.1 (1º, recomendado)** | **Syscall de entropia userland** (`SYS_GETRANDOM`) backed pela CSPRNG do kernel + stub capylibc + ABI assert | Sim (sem TLS ainda) | `make test` |
| 5.2 | Wiring de build do BearSSL no userland (subset curado + `-Ithird_party/bearssl/inc`); compila/linka em ring-3 | Sim | `make all64` + build capylibc |
| 5.3 | Trust anchors **reais** no userland; host-test conta/fingerprint contra o catálogo metadata existente | Sim | `make test` |
| 5.4 | Plugar `capy_tls_backend_connect` no `br_ssl_client`+`br_sslio` real (I/O via SYS_SEND/RECV, tempo via SYS_TIME, seed via getrandom); flip dos gates + `is_supported()→1`; zeroização | Não (handshake liga) | `make test` + smoke local |
| 5.5 | `capy_net` HTTPS real; smoke local `tls-handshake` contra servidor controlado; cert inválido falha fechado | — | smoke local |
| 5.6 | Gate externo `make smoke-x64-vmware-tls-handshake` + `release-check` | — | VMware (Fase externa) |

---

## 5. Primeiro slice recomendado — **5.1: syscall de entropia userland**

**Por que primeiro:** é o gap **mais fundamental, de segurança e
host-testável**, e está **ausente hoje**. Sem entropia em ring-3, o DRBG
do BearSSL (`br_ssl_client_reset`) não pode ser semeado — todo o resto da
Etapa 5 depende disso. Entregá-lo primeiro evita um gap estrutural
descoberto tarde, alinhado à disciplina "estruturante antes de feature".

**Escopo (quando a Etapa 5 abrir):**

- `include/kernel/syscall_numbers.h`: `SYS_GETRANDOM 42`, `SYSCALL_COUNT 43`
  (seguir os passos 1-4 documentados no próprio header).
- Handler em `src/kernel/syscall.c` (`syscall_init`) reusando a CSPRNG
  canônica (`src/security/csprng.c`) com cópia segura p/ buffer userland
  e limite de tamanho por chamada; fail-closed se a CSPRNG não estiver pronta.
- Stub em `userland/lib/capylibc/syscall_stubs.S` + wrapper
  `capy_getrandom()` em capylibc.
- **Sem tocar TLS:** `capy_tls_is_supported()` continua `0`. Nenhum
  handshake. Garantia fail-closed preservada.

**Host tests:**

- Estender `tests/test_capylibc_abi.c` (já assegura paridade de números
  de syscall kernel↔userland) p/ o novo `SYS_GETRANDOM`/`SYSCALL_COUNT`.
- Teste do handler: lógica pura de cópia/limite/erro (sem MMIO).

**Gate:** `make test` (host). Sem gate VMware nesta fatia.

**Cross-repo:** zero — tudo CapyOS core; ABI nova é aditiva.

---

## 6. Mapa para os critérios de aceite (master-plan §8)

| Critério §8 | Slice que fecha |
|---|---|
| Erro em qualquer gate mantém fail-closed | 5.1-5.4 (preservado por construção) |
| HTTPS em `libcapy-net` deixa de retornar unsupported p/ caso válido | 5.4 + 5.5 |
| Certificado inválido falha fechado | 5.4 + 5.5 |

---

## 7. Decisão de arquitetura a confirmar antes de 5.4

**Userland linka o próprio BearSSL (recomendado)** vs. userland chama o
TLS kernel-side por um novo syscall-proxy. O intent do roadmap
(`libcapy-tls-userland-contract.md`: "tirar HTTPS do contexto kernel")
é o **BearSSL próprio em ring-3**. O proxy-syscall é mais simples mas
recoplaria userland↔kernel e não entregaria o desacoplamento da etapa.
Registrar a decisão antes do slice 5.4.

---

## 8. Notas de drift de documentação

- `system-overview.md` §7.1 descreve o TLS BearSSL real — **correto para
  o kernel-side**; não menciona que o userland ainda é stub. Adicionar a
  distinção quando a Etapa 5 abrir (não é erro, é omissão).
- `libcapy-tls-userland-contract.md` e `libcapy-tls-bearssl-adapter.md`
  descrevem o estado alpha.64-93 (metadata-only) — ainda **diretamente
  válido**, apenas antigo. Atualizar no fechamento de cada slice da Etapa 5.

---

## 9. Referências

- `docs/plans/active/capyos-master-plan.md` §8 — definição da Etapa 5.
- `docs/plans/active/etapa-4-closure-tracker.md` — bloqueio atual (Fase F).
- `src/security/tls.c` — implementação de referência kernel-side.
- `userland/lib/capylibc-tls/` — alvo userland (stub atual).
- `docs/architecture/libcapy-tls-userland-contract.md` — contrato userland.
- `include/kernel/syscall_numbers.h` — superfície de syscalls (gap G1).
- `.windsurf/skills/capyos-network-stack` — mapa do stack de rede/TLS.
