# Etapa 6 — External Validation Playbook

**Etapa:** 6 — Apps básicos do desktop maduros + `CapyBrowse Text`.
**Status de abertura:** 2026-06-07 (alpha.264), após o fechamento da Etapa 5;
Slice 6.4 desbloqueado pelo handoff `CapyBrowser v0.6.0` (alpha.265).
**Plataforma oficial:** VMware Workstation/ESXi + UEFI + E1000.
**Sister repos abrindo gate:** `CapyBrowser` (`capy-browser-core` text) e
`CapyUI` (apps + desktop session).
**Documento autoritativo upstream:** `docs/plans/active/capyos-master-plan.md` §9.
**Tracker por slice (código ↔ teste ↔ gate):** `docs/architecture/etapa-6-desktop-apps-readiness.md`.

Este playbook é operator-facing. Esta workspace é review/edit only; nenhum
gate aqui é executado localmente (ver §7).

---

## 0. Atalho: fechar a Etapa 6 em um comando

```sh
make etapa-6-vmware-gates SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout ..."
```

Roda, em sequência: (1) **pré-voo QEMU local** (grátis) dos dois markers —
`smoke-x64-qemu-apps-basic-roundtrip` + `smoke-x64-qemu-capybrowse-text` — para
que um build quebrado falhe localmente **antes** de gastar tempo de VMware; (2)
os **gates VMware oficiais** `smoke-x64-vmware-apps-basic-roundtrip` +
`smoke-x64-vmware-capybrowse-text`. Aborta no primeiro que falhar; imprime
banners por etapa e um `OK` final quando ambos passam.

- Sem QEMU no host? Pule o pré-voo: `ETAPA6_SKIP_QEMU_PREFLIGHT=1`.
- O `apps-basic-roundtrip` já roda continuamente como **gate de runtime na CI**
  (job `qemu-apps-roundtrip`, QEMU+OVMF), então o build chega ao VMware
  pré-validado.
- As seções abaixo detalham cada gate isoladamente, pré-requisitos e o formato
  de report.

---

## 1. Por que este playbook existe

A Etapa 6 entrega os apps básicos do desktop maduros e o `CapyBrowse Text` — o
primeiro app que consome um **core apartado** (`capy-browser-core`, do sister
`CapyBrowser`) por contrato versionado, sobre o HTTPS userland real entregue na
Etapa 5. O que falta para fechar a etapa **não é mais código host**, e sim os
gates externos:

1. confirmar que o app ring-3 `capybrowse` **linka** contra o core do sibling
   (`make capybrowse-elf`);
2. validar o roundtrip do CapyBrowse Text contra um servidor controlado
   (smoke `capybrowse-text`);
3. validar o roundtrip dos apps básicos (smoke `apps-basic-roundtrip`, pendente
   de orquestração — ver §5.D);
4. `make release-check` continuar verde.

Todo o caminho de smoke está in-tree atrás de flags **default OFF**
(`CAPYOS_CAPYBROWSE_SMOKE`, `CAPYOS_APPS_ROUNDTRIP_SMOKE`): o build padrão é
byte-idêntico.

---

## 2. Critério oficial de aceite da Etapa 6

Reproduzido de `etapa-6-desktop-apps-readiness.md` §4 (espelha o master plan
§9). Em caso de divergência, o master plan é autoritativo.

- [ ] Cada app abre, executa função primária e fecha sem crash.
- [ ] Falha de um app não derruba o desktop.
- [ ] `CapyBrowse Text` abre páginas de texto/HTML simples, mostra erros claros
      de DNS/TLS/HTTP e **não** executa JavaScript.
- [ ] HTML-to-text e widget layout entram por **contratos versionados**, com
      adaptador CapyOS pequeno e testável.
- [ ] Apps usam o tema da Etapa 1.
- [ ] Strings de UI localizadas em PT-BR e ES com **fallback EN**.

---

## 3. Visão geral dos slices

Detalhe vivo em `etapa-6-desktop-apps-readiness.md`.

| Slice | Entrega | Estado | Gate |
|---|---|---|---|
| 6.1 | Readiness audit + plano por slice | ✅ in-tree | n/a |
| 6.2 | Diagnóstico de rede (estágio DNS/TCP/TLS/HTTP) | ✅ in-tree | `make test` |
| 6.3 | `capy_net_diagnose_stage` + TLS state | ✅ in-tree | `make test` |
| 6.4 | CapyBrowse Text sobre `capy-browser-core` text | ✅ in-tree + build-validado | `make capybrowse-elf` + smoke `capybrowse-text` |
| 6.5 | EN como fallback obrigatório (`localization_select`) | ✅ in-tree | `make test` |
| 6.6 | Maturidade dos apps (roundtrip + crash isolation) | 🟡 latch host-testado; orquestração pendente | smoke `apps-basic-roundtrip` |

Slices 6.2–6.5 estão code-complete in-tree; 6.4 também tem o link cross-repo
validado. A etapa fecha quando os smokes externos (§5.C, §5.D) + `release-check`
passarem.

---

## 4. Pré-requisitos globais

### 4.1 Toolchain
- GCC cross-toolchain pinado por `tools/scripts/check_toolchain.py` (`make check-toolchain`).
- Python 3.10+ para os harnesses (`tools/scripts/smoke_x64_*.py`).

### 4.2 Siblings presentes
- **`../CapyBrowser`** (>= `0.6.0`): o subset textual do core é compilado por
  sibling-detection do Makefile (`CAPYBROWSER_DIR ?= ../CapyBrowser`). Sem ele,
  `CAPYBROWSER_TEXT_AVAILABLE` fica unset, o app `capybrowse` não é construído e
  o smoke `capybrowse-text` falha no build do blob.
- **`../CapyUI`** (>= `2.22.0`): apps + desktop session para o smoke
  `apps-basic-roundtrip`.

### 4.3 VM template
- VMware Workstation/ESXi com firmware UEFI, NIC E1000, >= 2 GB RAM, storage
  AHCI ou NVMe, captura serial habilitada (`serial0.fileType = "file"`).
- **Rede com saída para um servidor HTTP/HTTPS controlado** (ver §4.5).
- Se a VM estiver **criptografada**, forneça a senha — `vmrun` falha com
  "Incorrect password" sem ela. Use o env var `CAPYOS_VMX_PASSWORD=<senha>`
  (preferível; não aparece nos args do `make`) **ou**
  `SMOKE_X64_VMWARE_ARGS="--vmx-password <senha> …"` (a harness repassa a
  `vmrun -vp`; a senha fica visível na lista de processos). Uma VM **sem**
  criptografia dispensa isso e é o caminho mais simples.

### 4.4 Variáveis de ambiente
- `SMOKE_X64_VMWARE_ARGS` configurando `--vmx`, `--serial-log`, `--timeout`.
- Build alpha alvo: `>= 0.8.0-alpha.265`.

### 4.5 Servidor controlado para o CapyBrowse Text
O smoke `capybrowse-text` aponta o app para uma URL controlada via
`CAPYOS_CAPYBROWSE_URL` (default `https://example.com/`). Forneça um endpoint
alcançável da VM que sirva uma página de texto/HTML simples e determinística:

- **HTTPS:** o cert deve encadear até um dos trust anchors embutidos
  (`src/security/tls_trust_anchors.c`) ou uma CA de teste cujo anchor seja
  injetado pela PKI de teste só-pública. Hostname do cert deve casar com o host
  da URL.
- A página deve ser pequena (< 32 KB por default; ver `CAPYBROWSE_FETCH_MAX` em
  `userland/bin/capybrowse/main.c`) e conter texto + alguns links, para validar
  título + corpo + lista de links `[n]`.

Documente no report (§6) a URL e a CA usadas.

### 4.6 Snapshot da VM antes de cada gate
Crie snapshot pré-gate para re-execução determinística.

---

## 5. Sequência de validação

### 5.A Gates host (rodam em qualquer máquina de build)

```bash
make check-toolchain
make layout-audit
make version-audit
make test
```

`make test` cobre, entre outros, os artefatos novos da Etapa 6:

- `tests/userland/test_capybrowse_view.c` — formatter puro do CapyBrowse Text
  (título + corpo + links `[n]` + warnings + truncamento; gated por
  `CAPYOS_HAVE_CAPYBROWSER_TEXT`, ativo quando `../CapyBrowser` está presente);
- `tests/kernel/test_capybrowse_text_smoke_gate.c` — latch da smoke 6.4
  (exit-0 emite uma vez; exit != 0 não; idempotência; reset);
- `tests/kernel/test_apps_roundtrip_smoke_gate.c` — latch da smoke 6.6
  (conta N saídas limpas → marcador único; saída não-limpa não conta/não dispara);
- `tests/lang/test_localization.c` — seleção PT-BR default + fallback EN (6.5);
- `tests/userland/test_capylibc_string.c` — `<string.h>` freestanding do `capylibc`.

### 5.B Link cross-repo do CapyBrowse Text (Slice 6.4)

```bash
make capybrowse-elf
```

Critério: **linka limpo**. Compila o subset textual de `../CapyBrowser`
(`url_parse/url_normalize/origin` + `html_entities/html_tokenizer/text_emit`)
sob `USERLAND_CFLAGS` freestanding + a string lib do `capylibc`, e linka o
binário ring-3. Dois pontos de integração cross-repo já resolvidos (se
reaparecerem, o erro de link/compile aponta):

- **colisão `capy_url_parse`** (core vs `capylibc-net`) — resolvida por
  `-Dcapy_url_parse=capybrowse_core_url_parse` só nas TUs do core + `#define`
  casado em `main.c`;
- **`time()` do BearSSL X.509** — stub em `capy_tls_backend.c` (gated
  `CAPYOS_TLS_USERLAND_HANDSHAKE` + `!UNIT_TEST`).

### 5.C Smoke VMware oficial `capybrowse-text` (Slice 6.4)

**Design do marker (por que exit-code e não stdout):** `capy_write(1,...)` em
ring-3 cai no debug-console (porta 0xE9), que o VMware **não** captura (VMware
lê COM1). Logo o sinal autoritativo é o **exit code**: `capybrowse` sai 0 só
quando o fetch + `capy_html_to_text` + render tiveram sucesso; o latch
kernel-side (`process_exit`, gated `CAPYOS_CAPYBROWSE_SMOKE`) emite
**`[smoke] capybrowse-text ready`** no COM1 nesse exit-0. Mesma mecânica do
tls-handshake smoke da Etapa 5.

**Comando alvo** (a flag de URL propaga ao sub-make `all64`):

```bash
make smoke-x64-vmware-capybrowse-text \
  EXTRA_USERLAND_CFLAGS='-DCAPYOS_CAPYBROWSE_URL=\"https://lab.internal/page\"' \
  SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 600"
```

O alvo faz `make clean` + `make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1
CAPYOS_CAPYBROWSE_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_CAPYBROWSE_SMOKE'` +
`iso-uefi` + `manifest64` + `smoke_x64_vmware.py`. O boot sobe a rede, lança
`capybrowse` no lugar do login; o app busca a URL, converte para texto, formata
e imprime, e sai 0; o latch emite o marker. Markers esperados, **em ordem**:

```
[net] DHCP: lease acquired.
[smoke] capybrowse-text ready
```

**Modos de falha:**
- **Build falha no blob** → `../CapyBrowser` ausente (ver §4.2).
- **Sem marker / timeout** → fetch falhou (DNS/TCP/TLS/HTTP). O app imprime o
  diagnóstico amigável (estágio via `capy_net_diagnose_stage`) no debug-console
  e sai 1 — confira o log 0xE9 do QEMU em dev, ou ajuste a URL/servidor (§4.5).
- **`exit 1` após render** → erro de parse/input do core (raro; só entradas
  NULL fazem o core tolerante falhar).

**Mapa para os critérios de aceite (§2):** o marker cobre "CapyBrowse Text abre
páginas simples" (#3, caso de sucesso). O caminho de erro DNS/TLS/HTTP
fail-closed é exercitado apontando a URL para um endpoint inválido (deve sair 1,
sem marker). "Não executa JavaScript" é garantido por design (o core não tem JS).

### 5.D Smoke VMware `apps-basic-roundtrip` (Slice 6.6) — pendente de orquestração

> **Status:** o latch puro `apps_roundtrip_smoke` está in-tree e host-testado
> (§5.A), mas a **orquestração de boot ainda não existe**: apps GUI do desktop
> não "saem 0" como os binários single-shot (`tls_smoke`/`capybrowse`), então
> lançar cada app, rodar sua função primária e fechá-lo limpo precisa de um
> design co-projetado com o `CapyUI` (modelo de conclusão por app, sequência de
> lançamento, definição de "função primária" por app). Enquanto isso o alvo
> `make smoke-x64-vmware-apps-basic-roundtrip` não está cabeado. Proposta de
> design da orquestração (opções + recomendação + fronteira CapyUI):
> `docs/architecture/etapa-6-apps-roundtrip-orchestration.md`.

Quando a orquestração for definida, o gate seguirá o mesmo padrão: boot lança os
apps, o latch conta N saídas limpas e emite **`[smoke] apps-basic-roundtrip ready`**
no COM1. O isolamento de crash (critério #2) já é validado pelo gate
`thread-crash-survives` da Etapa 4.

### 5.E Fechamento da Etapa 6

1. `make release-check` verde.
2. Cross-check dos acceptance criteria em §2 — todos `[x]`.
3. Atualizar `etapa-6-desktop-apps-readiness.md`, a matriz, o audit e o `STATUS.md`;
   bump de alpha + nova entrada em `VERSION.yaml`.
4. Invocar o workflow `etapa-transition` para fechar a Etapa 6 e abrir a Etapa 7.

---

## 6. Reporting format expected from the operator

1. **Build alvo:** alpha tag exato + as flags do smoke.
2. **Comando executado:** linha completa do `make`.
3. **Servidor usado:** URL + CA/anchor de confiança (para o `capybrowse-text`).
4. **Resultado:** PASS / FAIL.
5. **Tail do serial:** últimas ~50 linhas (especialmente os markers, em ordem).
6. **Tempo total:** segundos até PASS ou timeout.
7. **Anomalias:** warnings, klog ERROR, ou qualquer indício de plaintext vazando
   num peer TLS inválido.

---

## 7. Local execution policy

Esta workspace é review/edit only para os gates de build/smoke. O agente:

- Edita código, tests e documentação.
- Valida por inspeção estática e (quando autorizado) gates host (`make test`,
  `make layout-audit`, `make version-audit`).
- Recomenda os gates externos (`make capybrowse-elf`, smokes, `release-check`).
- **Não** invoca o smoke/release nem promove flags a default localmente.

VMware + UEFI + E1000 é a plataforma de aceite oficial; QEMU local é feedback
de desenvolvimento, não aceite de release.

---

## 8. Referências cruzadas

- `docs/plans/active/capyos-master-plan.md` §9 — definição autoritativa da Etapa 6.
- `docs/architecture/etapa-6-desktop-apps-readiness.md` — tracker por slice + critérios.
- `docs/reference/integration/browser-core-integration-contract.md` — contrato `capy-browser-core`.
- `docs/reference/integration/capyui-widget-integration-contract.md` — contrato CapyUI.
- `docs/architecture/smoke-marker-pattern.md` — pattern canônico dos markers.
- `docs/operations/etapa-5-external-validation-playbook.md` — template deste playbook.
- `.windsurf/workflows/etapa-transition.md` — workflow de fechamento de etapa.
