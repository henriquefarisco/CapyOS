# Etapa 5 — External Validation Playbook

**Etapa:** 5 — TLS userland real (`libcapy-tls` de metadata-only para handshake real).
**Status de abertura:** 2026-06-02 (alpha.262), imediatamente após o fechamento formal da Etapa 4.
**Plataforma oficial:** VMware Workstation/ESXi + UEFI + E1000.
**Sister repo abrindo gate:** nenhum — Etapa 5 é **core-only** (sem repo apartado).
**Documento autoritativo upstream:** `docs/plans/active/capyos-master-plan.md` §8.
**Tracker por slice (detalhe código ↔ teste ↔ gate):** `docs/architecture/etapa-5-tls-userland-readiness.md` §4.

Este playbook é operator-facing. Sirva-o como referência sequencial
para validar externamente o fechamento da Etapa 5. Mantenha sincronia
com o tracker por slice e com o audit técnico vigente.

---

## 1. Por que este playbook existe

A Etapa 5 torna **real** a `libcapy-tls` userland (handshake BearSSL em
ring-3), que historicamente era um stub fail-closed
(`capy_tls_is_supported()==0`). O TLS BearSSL **kernel-side** já é real e
em produção (`src/security/tls.c`); a Etapa 5 leva esse mesmo motor para
o userland, de forma que browser, update-agent e ferramentas de
diagnóstico usem HTTPS sem chamar o TLS kernel-side diretamente.

Todo o caminho está **in-tree atrás de `CAPYOS_TLS_USERLAND_HANDSHAKE`**
(default OFF → fail-closed). O build padrão é inalterado; o handshake
real só liga com a flag. Este playbook documenta os gates externos que
fecham a etapa — o que falta **não é mais código host**, e sim:

1. confirmar que o subset ring-3 (BearSSL + anchors + handshake) **linka**
   (`make all64 ... CAPYOS_TLS_USERLAND_HANDSHAKE=1`);
2. validar o handshake real contra um servidor controlado (smoke);
3. `make release-check` continuar verde.

Esta workspace é review/edit only; nenhum gate aqui é executado
localmente (ver §7).

---

## 2. Critério oficial de aceite da Etapa 5

Reproduzido de `docs/plans/active/capyos-master-plan.md` §8 para evitar
drift. Em caso de divergência, o master plan é autoritativo.

- [ ] Erro em qualquer gate mantém fail-closed.
- [ ] HTTPS em `libcapy-net` deixa de retornar unsupported para caso válido.
- [ ] Certificado inválido falha fechado.

Mapa critério → evidência:

| Critério | Evidência host (flag OFF) | Evidência externa (flag ON) |
|---|---|---|
| Fail-closed em erro de gate | `test_tls_handshake_drive` (EOF/garbage/write-fail), `test_capylibc_tls*` | smoke: handshake contra peer inválido não vaza plaintext |
| HTTPS deixa de ser unsupported | n/a (flag OFF mantém unsupported por design) | smoke: GET HTTPS válido retorna corpo (`[smoke] tls-handshake ready`) |
| Certificado inválido falha fechado | `test_tls_cert_validation` (hostname/expiry/issuer), `test_tls_trust_userland_sync` | smoke: GET HTTPS contra cert inválido → erro, sem corpo |

---

## 3. Visão geral dos slices

Detalhe vivo por slice em `docs/architecture/etapa-5-tls-userland-readiness.md` §4.

| Slice | Entrega | Estado | Gate |
|---|---|---|---|
| 5.1 | `SYS_GETRANDOM` (entropia userland p/ DRBG) | ✅ in-tree | `make test` |
| 5.2 | BearSSL no build userland (`-Ithird_party/bearssl/inc` + objetos) | 🟡 include OK; **link ring-3 pendente** | `make all64 ...=1` |
| 5.3 | Trust anchors reais + lock metadata↔bundle | ✅ in-tree | `make test` |
| 5.4 | Handshake real gated em `capy_tls_backend_connect` | ✅ in-tree (gated) | `make all64 ...=1` + smoke |
| 5.5 | Validação de cert + `capy_net` HTTPS real (seam de transporte) gated | ✅ in-tree (gated) | `make all64 ...=1` + smoke |
| 5.6 | Gate externo final + fechamento | ⏳ operador | smoke + `release-check` |

Slices 5.1–5.5 estão **code-complete in-tree**. A Etapa 5 fecha quando o
slice 5.6 (esta validação externa) passar.

---

## 4. Pré-requisitos globais

### 4.1 Toolchain

- GCC cross-toolchain `x86_64-elf-gcc` na versão pinada por `tools/scripts/check_toolchain.py` (`make check-toolchain`).
- Python 3.10+ para os harnesses (`tools/scripts/smoke_x64_*.py`).

### 4.2 VM template

- VMware Workstation/ESXi com:
  - Firmware UEFI.
  - NIC E1000 (não VMXNET3 ainda).
  - 2 GB RAM mínimo.
  - Storage AHCI **ou** NVMe.
  - Captura serial habilitada (`serial0.fileType = "file"`).
- **Rede com saída para um servidor HTTPS controlado** (ver §4.5). A
  Etapa 5 exercita um handshake TLS real; sem um peer HTTPS alcançável o
  smoke não tem o que validar.

### 4.3 A flag de build (obrigatória)

O caminho real **só existe com a flag**. Todo build de validação desta
etapa usa:

```bash
make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1
```

Sem `CAPYOS_TLS_USERLAND_HANDSHAKE=1`, `capy_tls_is_supported()==0`, o
gate HTTPS de `capy_net` falha fechado e o smoke não pode passar — isso
é por design (build padrão inalterado).

### 4.4 Variáveis de ambiente

- `SMOKE_X64_VMWARE_ARGS` configurando `--vmx`, `--serial-log`, `--timeout`.
- Build alpha alvo: `>= 0.8.0-alpha.262` (abertura da Etapa 5) ou superior.

### 4.5 Servidor HTTPS controlado

O smoke precisa de dois endpoints alcançáveis da VM:

1. **Endpoint válido** — servidor HTTPS cujo certificado encadeia até um
   dos 146 trust anchors embutidos (`src/security/tls_trust_anchors.c`,
   travados host-side por `tests/security/test_tls_trust_userland_sync.c`)
   **ou** uma CA de teste cujo anchor seja injetado conforme a PKI de
   teste só-pública usada em `tests/security/test_tls_cert_validation.c`.
   Hostname do cert deve casar com o SNI requisitado pelo smoke.
2. **Endpoint inválido** — mesmo servidor com cert expirado / hostname
   errado / issuer não-confiável, para exercitar o fail-closed.

Documente no report (§6) qual servidor/cert foi usado.

### 4.6 Snapshot da VM antes de cada gate

Crie snapshot pré-gate para re-execução determinística.

---

## 5. Sequência de validação

### 5.A Gates host (flag OFF) — fundação, rodam em qualquer máquina de build

```bash
make check-toolchain
make layout-audit
make version-audit
make test
```

`make test` (flag OFF) cobre, entre outros:

- `tests/security/test_tls_trust_anchors.c` — bundle 146 anchors bem-formado;
- `tests/security/test_tls_trust_userland_sync.c` — metadata userland travada ao bundle real (contagem/split/key-type por índice);
- `tests/security/test_tls_client_engine.c` — `br_ssl_client` real emite ClientHello válido com os 146 anchors;
- `tests/security/test_tls_handshake_drive.c` — handshake-drive + fail-closed em EOF/garbage/write-fail;
- `tests/security/test_tls_cert_validation.c` — `br_x509_minimal` aceita cadeia válida e falha fechado em hostname/expiry/issuer;
- `tests/userland/test_capylibc_tls*.c` + `tests/userland/test_capylibc_net*.c` — façade userland (inclui o caminho HTTP via seam de transporte e o fail-closed HTTPS com flag OFF).

Todos devem passar limpos. Esses gates **não** dependem da flag; validam
a lógica pura (engine, anchors, cert, seam) host-side.

### 5.B Slice 5.2 — link ring-3 com a flag (primeiro gate externo real)

```bash
make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1
```

Critério: **linka limpo**. Sob a flag o Makefile adiciona ao userland
(`CAPYLIBC_TLS_OBJS`):

- `capy_tls_handshake.o`,
- `src/security/tls_trust_anchors.c` compilado como objeto userland,
- `$(BEARSSL_OBJS)` (reusa os objetos kernel-built, mesmo `CC64`/`-mcmodel=small`),

e `-DCAPYOS_TLS_USERLAND_HANDSHAKE` em `USERLAND_CFLAGS`. Isto fecha o
"Falta: link ring-3 do subset" do slice 5.2. Se faltar símbolo
(`br_*`, `capyos_tls_trust_anchors`, `capy_tls_*`), o erro de link
aponta o objeto a adicionar.

> Nota: `capy_net_http.o`/`capy_net_tls.o` referenciam
> `capy_tls_connect_tcp/send/recv/close/free` (seam de transporte do
> slice 5.5). Esses símbolos vêm de `capy_tls.o`, já em
> `CAPYLIBC_TLS_OBJS` no build padrão, então o link resolve com **e** sem
> a flag.

### 5.C Smoke local (dev feedback, opcional)

QEMU/OVMF local é feedback de desenvolvimento, não aceite oficial:

```bash
make smoke-x64-tls-handshake TOOLCHAIN64=host   # alvo a criar (ver 5.D)
```

### 5.D Slice 5.6 — smoke VMware oficial `tls-handshake`

> **Status do scaffold (in-tree, não buildado/validado):** o gate está
> cabeado de ponta a ponta, **tudo gated por `CAPYOS_TLS_HANDSHAKE_SMOKE`**
> (build default e host tests intactos):
> - alvo `make smoke-x64-vmware-tls-handshake` (Makefile);
> - programa ring-3 `userland/bin/tls_smoke/main.c` (+ regras ELF/blob);
> - registro `/bin/tls_smoke` em `embedded_progs.c`;
> - helper `kernel_boot_run_tls_smoke()` em `user_init.c`, **lançado pós-rede**
>   em `kernel_main.c` (após o stage 8/8 de rede, no lugar do login);
> - latch puro `tls_handshake_smoke.{h,c}` + emissor COM1
>   `tls_handshake_smoke_io.c` + hook em `process.c::process_exit`;
> - host test `tests/kernel/test_tls_handshake_smoke_gate.c` (registrado no
>   runner) — **a única parte validável nesta máquina**; trava o predicado
>   do latch (exit-0 emite uma vez; exit != 0 não; idempotência; reset).

**Design do marker (por que exit-code e não stdout):** `capy_write(1,...)` em
ring-3 cai no debug-console (porta 0xE9, `src/kernel/syscall.c::sys_write`),
que o smoke QEMU captura mas o **VMware NÃO** (VMware lê COM1). Logo o sinal
autoritativo é o **exit code**: `tls_smoke` sai 0 se e somente se o GET válido
retornou corpo **e** o GET de cert inválido falhou fechado; o latch
kernel-side (`process_exit`, gated) emite **`[smoke] tls-handshake ready`** no
COM1 nesse exit-0. Mesma mecânica do thread-crash smoke da Etapa 4.

**O que o gate faz:**

1. `make clean` + `make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1
   CAPYOS_TLS_HANDSHAKE_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_TLS_HANDSHAKE_SMOKE'`
   + `iso-uefi` + `manifest64` + `smoke_x64_vmware.py`.
2. Boot normal sobe a rede (stage 8/8); `kernel_main` lança `tls_smoke` no
   lugar do login. O programa, com retry tolerante ao DHCP assíncrono:
   - GET HTTPS no **endpoint válido** (§4.5) → tem que retornar 2xx/3xx;
   - GET no **endpoint inválido** → tem que **falhar fechado** (sem corpo);
   - `capy_exit(0)` se ambos; senão `capy_exit(1)`.
3. `process_exit` (gated) vê o exit-0 e emite o marker no COM1. O harness
   valida, **em ordem**:

```
[net] DHCP: lease acquired.
[smoke] tls-handshake ready
```

**Comando alvo:**

```bash
make smoke-x64-vmware-tls-handshake \
  SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 600"
```

**Mapa para os critérios de aceite (§2):**

- `[smoke] tls-handshake ready` (emitido pelo kernel no exit-0 do tls_smoke)
  cobre os três critérios juntos: o GET válido retornou corpo ("HTTPS deixa
  de retornar unsupported para caso válido", #2) **e** o GET de cert inválido
  falhou fechado ("certificado inválido falha fechado", #3; "erro mantém
  fail-closed", #1) — o programa só sai 0 quando ambos valem.

**Regressão (antes ou depois):** os smokes da Etapa 4 devem continuar
passando com a flag ligada (o caminho default permanece fail-closed,
então a flag não deve regredir desktop/scheduler/storage):

```bash
make smoke-x64-vmware-etapa-4
```

### 5.E Fechamento da Etapa 5

1. `make release-check` deve passar limpo (com e sem a flag, conforme a
   política de promoção — ver comentário do Makefile: a flag só vira
   default depois do smoke verde).
2. Cross-check dos acceptance criteria em §2 — todos `[x]`.
3. Atualizar o tracker `etapa-5-tls-userland-readiness.md` (slice 5.6 ✅)
   e o `STATUS.md`.
4. Invocar o workflow `etapa-transition` para fechar a Etapa 5 e abrir a
   Etapa 6 (Apps básicos do desktop maduros).

---

## 6. Reporting format expected from the operator

Quando reportar resultado de gate executado externamente, envie:

1. **Build alvo:** alpha tag exato + a flag (`CAPYOS_TLS_USERLAND_HANDSHAKE=1`).
2. **Comando executado:** linha completa do `make`.
3. **Servidor HTTPS usado:** endpoint(s) válido/inválido e a CA/anchor de confiança.
4. **Resultado:** PASS / FAIL.
5. **Tail do serial:** últimas ~50 linhas (especialmente os markers).
6. **Tempo total:** segundos até PASS ou timeout.
7. **Anomalias:** qualquer warning, klog ERROR, ou — crítico para TLS —
   qualquer indício de plaintext vazando no peer inválido.

Exemplo de report PASS:

```
Build: 0.8.0-alpha.262+20260602  (CAPYOS_TLS_USERLAND_HANDSHAKE=1)
Gate: make smoke-x64-vmware-tls-handshake
Servidor: https://lab.internal (cert ISRG Root X1) + https://expired.lab.internal (cert expirado)
Resultado: PASS
Tempo: 214s
Markers no COM1 (em ordem):
  [net] DHCP: lease acquired.
  [smoke] tls-handshake ready
  [smoke] tls-handshake bad-cert refused
Anomalias: nenhuma; nenhum plaintext observado no peer inválido.
```

---

## 7. Local execution policy

Esta workspace é review/edit only para os gates de build/smoke. O CapyOS
agent neste workspace:

- Edita código, tests e documentação.
- Valida mudanças por inspeção estática e gates host quando autorizado
  (`make test`, `make layout-audit`, `make version-audit`).
- Recomenda os gates externos (`make all64 ...=1`, smoke, `release-check`).
- **Não** promove a flag a default nem invoca o smoke/release localmente.

VMware + UEFI + E1000 é a plataforma de aceite oficial; QEMU local é
feedback de desenvolvimento, não aceite de release.

---

## 8. Referências cruzadas

- `docs/plans/active/capyos-master-plan.md` §8 — definição autoritativa da Etapa 5.
- `docs/architecture/etapa-5-tls-userland-readiness.md` — tracker por slice (código ↔ testes ↔ gate); fonte única de detalhe.
- `docs/architecture/libcapy-tls-userland-contract.md` — contrato userland (caminho default fail-closed).
- `docs/architecture/libcapy-tls-bearssl-adapter.md` — adaptador BearSSL.
- `docs/architecture/smoke-marker-pattern.md` — pattern canônico para os markers novos.
- `docs/operations/etapa-4-external-validation-playbook.md` — template para este playbook.
- `.windsurf/workflows/etapa-transition.md` — workflow usado para fechar a Etapa 5 quando o smoke + release-check passarem.
