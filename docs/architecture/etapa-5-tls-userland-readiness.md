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
| G1 | **Entropia userland** p/ semear o DRBG do BearSSL | ✅ **RESOLVIDO (Slice 5.1)** — `SYS_GETRANDOM=42` + `capy_getrandom` backed pela CSPRNG | `syscall_numbers.h`, `syscall.c::sys_getrandom`, `syscall_stubs.S`, `capylibc.h` |
| G2 | BearSSL linkado no userland | 🟡 **include-path ligado** (Slice 5.2: `USERLAND_CFLAGS` + `HOST_CFLAGS` com `-Ithird_party/bearssl/inc`); link ring-3 do subset pendente | `make all64` valida o link userland; host build já compila contra `bearssl.h` |
| G3 | Trust anchors reais no userland | Só metadata (contagem/fingerprint) | `capy_tls_trust.c`, `capy_tls_backend.c:35-89` |
| G4 | Engine real (`br_ssl_client`+`br_sslio`) no seam | ✅ **PLUGADO (gated)** — `capy_tls_backend_connect` faz o handshake real BearSSL (init_full + time + buffer + TLS1.2 + ALPN + seed + reset + `br_sslio_flush`); send/recv/close via `br_sslio`. Sob `CAPYOS_TLS_USERLAND_HANDSHAKE` (default OFF → fail-closed) até `make all64` + smoke. Espelha o kernel `tls.c` | `capy_tls_backend.c`, `capy_tls.c` |
| G5 | Fonte de tempo p/ validade X.509 | ✅ **RESOLVIDO** — `SYS_CLOCK_REALTIME=43` (wall-clock via RTC do kernel) + `capy_clock_realtime` + helper puro `capy_tls_unix_to_x509_time` (host-testado). **Correção:** `SYS_TIME=39` era enganoso — retorna ticks do APIC desde o boot, NÃO data, logo inútil p/ expiry de cert | `syscall_numbers.h`, `syscall.c::sys_clock_realtime`, `capy_tls_handshake.c` |
| G6 | I/O de socket p/ callbacks `br_sslio` | **OK** — `SYS_SEND/RECV=33/34` | `syscall_numbers.h:57-58` |
| G7 | Zeroização/ownership do contexto userland | ✅ **coberto (gated)** — engine + iobuf vivem no `capy_tls_context` (slot estático); `capy_tls_context_reset` faz memzero do contexto inteiro no release/free, apagando session keys + plaintext | `capy_tls_context.c:13-19` |
| G8 | Integração HTTPS (`is_supported→1`) | ✅ **gated** — `capy_tls_is_supported()` retorna 1 sob `CAPYOS_TLS_USERLAND_HANDSHAKE`; `capy_net_tls` passa a usar o caminho real automaticamente | `capy_tls.c`, `capy_net_tls.c:24` |

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
| **5.1 ✅ ENTREGUE (alpha.262+)** | **Syscall de entropia userland** (`SYS_GETRANDOM`=42) backed pela CSPRNG do kernel + stub capylibc + ABI assert | Sim (TLS intocado) | `make test` ✅ |
| **5.2 🟡 PARCIAL (in-tree)** | `-Ithird_party/bearssl/inc` ligado no `USERLAND_CFLAGS` + `HOST_CFLAGS`; host test valida os 146 trust anchors BearSSL reais (fundação da 5.3) com tipos reais do BearSSL host-side. Falta: link ring-3 do subset | `make test` ✅ (anchors) / `make all64` (link ring-3) |
| 5.3 | Trust anchors **reais** no userland; host-test conta/fingerprint contra o catálogo metadata existente | Sim | `make test` |
| **5.4 ✅ PLUGADO (gated)** | Handshake real ligado em `capy_tls_backend_connect` (I/O via `capy_send/recv`, seed via `SYS_GETRANDOM`, time via `SYS_CLOCK_REALTIME`, anchors reais) + `connect_tcp` devolve contexto vivo + send/recv/close via `br_sslio` + `is_supported()→1` + contexto zeroizado no release — tudo sob `CAPYOS_TLS_USERLAND_HANDSHAKE` (default OFF). Espelha o kernel `tls.c`. Falta: validar com build+smoke | `make test` (default OFF, intacto) / `make all64 ...=1` + smoke (flag ON) |
| **5.5 🟡 PARCIAL** | Validação de cert (`br_x509_minimal`, o mesmo engine que `br_ssl_client_init_full` arma) **host-testada** (`test_tls_cert_validation.c`, PKI de teste só-público): aceita cadeia válida p/ o host certo e **falha fechado** em hostname errado / expirado / issuer não-confiável. Falta: `capy_net` HTTPS real + smoke contra servidor | `make test` ✅ (validação) / smoke local (HTTPS real) |
| 5.6 | Gate externo `make smoke-x64-vmware-tls-handshake` + `release-check` | — | VMware (Fase externa) |

> **Engine smoke host-side entregue (de-risca a 5.4):**
> `tests/security/test_tls_client_engine.c` constrói um `br_ssl_client`
> real com os **146 trust anchors de produção**, injeta entropia de teste
> e emite um **ClientHello TLS válido** (record `0x16`, handshake `0x01`,
> SNI `example.com`) — provando engine + anchors + config host-side, sem
> rede. Linka `$(BEARSSL_SRCS)` + `tests/stubs/stub_bearssl_seeder.c` no
> binário de teste. **Não liga** o handshake em produção: o seam userland
> segue fail-closed (`capy_tls_is_supported()=0`) até a 5.4 plugar o I/O
> (`SYS_SEND/RECV`) e a 5.6 validar em VMware.

---

## 5. Primeiro slice recomendado — **5.1: syscall de entropia userland**

> **STATUS: ✅ ENTREGUE in-tree** (Etapa 5 ativa desde alpha.262).
> `SYS_GETRANDOM=42` (`SYSCALL_COUNT=43`) + handler `sys_getrandom` em
> `src/kernel/syscall.c` (usa `csprng_get_bytes`; cap 256 B/chamada;
> fail-closed em `flags!=0` ou buf NULL) + stub `capy_getrandom` em
> `userland/lib/capylibc/syscall_stubs.S` + declaração em
> `userland/include/capylibc/capylibc.h` + assert de ABI em
> `tests/userland/test_capylibc_abi.c`. Verificação: `make test` verde
> (`SYS_GETRANDOM == 42 OK`, `SYSCALL_COUNT == 43 OK`), `make layout-audit`
> sem warnings, `syscall.c` passa `gcc -fsyntax-only`. **TLS intocado**
> (`capy_tls_is_supported()` continua 0; garantia fail-closed preservada).
> Link userland completo + boot dependem de `make all64` (externo).
> **Próximo: Slice 5.2** (BearSSL no build userland).

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
