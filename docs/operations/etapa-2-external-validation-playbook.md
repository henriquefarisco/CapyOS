# Playbook de validação externa — Etapa 2 (sessão gráfica operacional)

**Versão alvo:** `0.8.0-alpha.237+20260514`
**Plataforma oficial:** `VMware + UEFI + E1000`
**Status técnico:** `100% entregue` conforme `docs/plans/STATUS.md` em
`Etapa em validação externa`.
**Pendência única:** execução externa final dos gates registrada em
`docs/plans/STATUS.md`.
**Audiência:** operador humano externo ou CI privada — esta máquina é apenas review/edit (`MEMORY[05-local-execution-policy.md]`).

## 1. Por que este playbook existe

A Etapa 2 está tecnicamente fechada por código/documentação em
`alpha.237`. A única coisa que ainda separa o aceite operacional é a
**execução fora desta máquina** dos gates abaixo, na plataforma
oficial. Os comandos já existem no Makefile, os scripts já estão em
`tools/scripts/`, os contratos já estão no código. Falta um runbook
único e linear que diga ao operador exatamente o que rodar, em que
ordem, com que entradas e como reconhecer pass/fail por etapa.

Os cinco documentos detalhados em `docs/operations/release-ci-smoke-*`
e `docs/testing/vmware-e1000-smoke.md` continuam autoritativos sobre
cada gate individual. **Este playbook não substitui aqueles documentos
— ele orquestra a execução completa, referenciando-os onde apropriado
e adicionando os build-gates que a `STATUS.md` exige e que não
aparecem em nenhum dos cinco.**

## 2. Critério oficial de aceite da Etapa 2

Reproduzido de `docs/plans/STATUS.md` em `Visão executiva da Etapa 2`
para evitar drift:

> Critério de aceite operacional: a Etapa 2 só libera a Etapa 3 após
> `make test`, `make layout-audit`, `make all64`, `make release-check`
> e o smoke `mouse-events` oficial serem executados fora desta máquina
> com evidência anexada. Novas falhas encontradas durante esses gates
> entram como bugs dentro do bloco acima, não como novos entregáveis
> de roadmap, salvo mudança explícita deste plano.

Em resumo: **a Etapa 2 é aceita quando todas as 5 fases abaixo passam
e a evidência é encadeada via os manifestos públicos de release-CI.**

## 3. Visão geral das fases

| Fase | Tema | Onde roda | Requer VM? |
|---|---|---|---|
| A | Build gates (`make test`, `make layout-audit`, `make all64`, `make release-check`) | host com toolchain x86_64 | não |
| B | Provisionamento + handoff oficial | host CI privada com chave pública oficial | não |
| C | Smoke real `mouse-events` em VMware | host com `vmrun` ou `govc` + VM template | sim |
| D | Evidência + aceitação + promoção pós-smoke | host CI privada | não |
| E | Promoção pública final (encerra Etapa 2) | host CI privada | não |

Cada fase precisa **passar antes** de iniciar a próxima. A fase E
encerra formalmente a Etapa 2 e desbloqueia a Etapa 3.

## 4. Pré-requisitos globais

Antes de iniciar qualquer fase, garantir:

- **Repositório limpo** na tag oficial `0.8.0-alpha.237+20260514`
  (sem mudanças locais não-commitadas).
- **Toolchain x86_64** disponível para `make all64` (ELF cross
  toolchain ou `prepare-x64-toolchain` automático).
- **Chave pública oficial de release** (`ed25519.pub.pem`) com SHA-256
  fingerprint conhecido para `RELEASE_PUBLIC_KEY_SHA256`.
  A chave privada permanece **offline e fora da CI pública**.
- **VMware Workstation/Fusion/ESXi** acessível, com `vmrun` no `PATH`
  para Workstation/Fusion ou `govc` configurado para ESXi
  (`GOVC_URL`, `GOVC_USERNAME`, `GOVC_PASSWORD`, `GOVC_DATACENTER`).
- **VM template oficial** `CapyOS-Release-Smoke`:
  - x86_64, UEFI firmware.
  - NIC E1000 em rede bridged ou portgroup equivalente que entregue
    lease DHCP a guests novos.
  - Console serial configurada para arquivo legível pelo host/runner.
- **Variáveis de ambiente proibidas:** `RELEASE_PRIVATE_KEY` e
  `CAPYOS_RELEASE_PRIVATE_KEY` **NÃO** podem estar definidas no
  ambiente da CI pública — todos os gates rejeitam.

Convenções de path usadas neste playbook (ajustar conforme infra
local):

```bash
export RELEASE_TAG="0.8.0-alpha.237+20260514"
export RELEASE_PUBLIC_KEY="/caminho/oficial/ed25519.pub.pem"
export RELEASE_PUBLIC_KEY_SHA256="<fingerprint sha256 hex da pub key>"

# Escolher um dos provedores VMware:
# vmrun (Workstation/Fusion):
export SMOKE_X64_VMWARE_ARGS="--provider vmrun \
  --vmx /caminho/CapyOS-Release-Smoke.vmx \
  --serial-log build/ci/smoke_x64_vmware.serial.log \
  --summary-log build/ci/smoke_x64_vmware.summary.log"

# OU govc (ESXi):
export SMOKE_X64_VMWARE_ARGS="--provider govc \
  --vm-name CapyOS-Release-Smoke \
  --govc-serial-log '[datastore1] CapyOS/serial.log' \
  --serial-log build/ci/smoke_x64_vmware.serial.log \
  --summary-log build/ci/smoke_x64_vmware.summary.log"
```

O arquivo `.vmx` (vmrun) deve apontar o dispositivo serial para o
mesmo caminho usado em `--serial-log`.

Self-tests leves da política de markers, sem VM:

```bash
make smoke-marker-policy-selftest
```

## 5. Fase A — Build gates (host, sem VM)

Quatro gates obrigatórios pela `STATUS.md`. Podem rodar em qualquer
host com toolchain — não dependem de infraestrutura VMware.

### A1. `make test`

```bash
make test
```

**O que faz.** Compila e executa a suite de testes host-side em
`tests/` (auth, gui, security, kernel, userland, etc.). Inclui as 25
funções de teste do smoke readiness em
`tests/gui/test_desktop_smoke_readiness.c`.

**Pass.** Última linha imprime contagem `N/N passed` em todos os
módulos, exit code 0.

**Fail.** Qualquer FAIL em qualquer função. Capturar o nome da função
que falhou, anexar à evidência, NÃO prosseguir.

### A2. `make layout-audit`

```bash
make layout-audit
```

**O que faz.** Executa `tools/scripts/audit_source_layout.py --strict`
verificando o limite de 900 LOC por arquivo C/Python (com as exceções
documentadas no próprio script) e a organização dos diretórios.

**Pass.** Exit code 0 sem mensagens `[fail]`.

**Fail.** Arquivo C/Python excedeu 900 LOC sem entrada na lista de
exceções. Isso é regressão de organização — voltar para edição.

### A3. `make all64`

```bash
make all64
```

**O que faz.** Build do kernel x86_64 completo (`build/capyos64.bin`),
incluindo todos os módulos da sessão gráfica entregues até
`alpha.237`. Se a toolchain x86_64 não está pronta, dispara
`prepare-x64-toolchain` automaticamente.

**Pass.** `build/capyos64.bin` produzido, exit code 0.

**Fail.** Erro de link/compilação. Provavelmente regressão recente —
voltar para edição.

### A4. `make release-check`

```bash
make release-check
```

**O que faz.** Gate composto que encadeia: `check-toolchain` →
`test` → `layout-audit` → `version-audit` →
`boot-perf-baseline-selftest` → `verify-release-signature-selftest` →
`smoke-marker-policy-selftest` → `all64` → `iso-uefi` →
`verify-release-checksums`. Substitui A1+A2+A3 e adiciona checks
adicionais de release.

**Pass.** Última linha: `[ok] Gates de release robusta passaram.`

**Fail.** Qualquer subgate falha. O output indica qual.

**Nota.** A `STATUS.md` exige `make test`, `make layout-audit`,
`make all64` E `make release-check`. Como `release-check` já inclui
os outros três, na prática rodar **apenas `make release-check`**
satisfaz o critério da `STATUS.md`. Os três comandos individuais ficam
documentados aqui para diagnóstico mais rápido quando o composto
falha.

## 6. Fase B — Provisionamento + handoff oficial (host CI, sem VM)

Quatro gates que preparam e amarram a tag oficial à chave pública e ao
manifesto de handoff. Nenhum liga VM, chama `make`, chama `git`,
chama OpenSSL ou acessa chave privada.

### B1. `release-ci-official-provisioning-contract`

```bash
make release-ci-official-provisioning-contract \
  RELEASE_TAG="$RELEASE_TAG" \
  RELEASE_PUBLIC_KEY="$RELEASE_PUBLIC_KEY" \
  RELEASE_PUBLIC_KEY_SHA256="$RELEASE_PUBLIC_KEY_SHA256"
```

**O que faz.** Valida que a infra de provisionamento bate com o
contrato oficial (tag, chave pública, logs em `build/ci/*.log`).

**Documento autoritativo.** `docs/operations/release-ci-official-provisioning-contract.md`.

### B2. `release-ci-tag-gate`

```bash
make release-ci-tag-gate \
  RELEASE_TAG="$RELEASE_TAG" \
  RELEASE_PUBLIC_KEY="$RELEASE_PUBLIC_KEY" \
  RELEASE_PUBLIC_KEY_SHA256="$RELEASE_PUBLIC_KEY_SHA256"
```

**O que faz.** Confere `RELEASE_TAG` contra `VERSION.yaml`,
`include/core/version.h`, `README.md` e a release note pública.

**Documento autoritativo.** `docs/operations/release-ci-tag-gate.md`.

### B3. `release-official-handoff-manifest`

```bash
make release-official-handoff-manifest \
  RELEASE_TAG="$RELEASE_TAG" \
  RELEASE_PUBLIC_KEY="$RELEASE_PUBLIC_KEY" \
  RELEASE_PUBLIC_KEY_SHA256="$RELEASE_PUBLIC_KEY_SHA256"
```

**O que faz.** Gera o `build/release-official-handoff.manifest` com
`format=capyos-release-official-handoff-manifest-v1`,
`private_key_included=no`, gates obrigatórios e algoritmos
(`Ed25519`/`SHA-256`).

**Documento autoritativo.** `docs/operations/release-official-handoff-manifest.md`.

### B4. `verify-release-official-handoff-manifest`

```bash
make verify-release-official-handoff-manifest \
  RELEASE_TAG="$RELEASE_TAG" \
  RELEASE_PUBLIC_KEY="$RELEASE_PUBLIC_KEY" \
  RELEASE_PUBLIC_KEY_SHA256="$RELEASE_PUBLIC_KEY_SHA256"
```

**O que faz.** Re-verifica o manifesto gerado em B3 contra a chave
pública e a infraestrutura local.

**Pass conjunto.** Os 4 comandos retornam exit 0 sem mensagens
`[fail]`. Arquivo `build/release-official-handoff.manifest` existe e
contém o block esperado.

## 7. Fase C — Smoke real `mouse-events` em VMware (com VM ligada)

### C1. Readiness pré-smoke (sem ligar VM)

```bash
make release-ci-smoke-readiness \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Verifica que o manifesto de handoff + os argumentos do
smoke são coerentes com a infraestrutura local, **sem ligar a VM**.

**Documento autoritativo.** `docs/operations/release-ci-smoke-readiness.md`.

**Pass.** Exit 0. Caso falhe, ajustar `SMOKE_X64_VMWARE_ARGS` ou a
configuração da VM antes de C2 — **NÃO ligar a VM com readiness
falhada**.

### C2. Smoke `mouse-events` oficial (liga a VM)

```bash
make smoke-x64-vmware-mouse-events \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Constrói `all64 + iso-uefi + manifest64`, liga a VM
template via `vmrun` ou `govc`, observa o log serial e exige **três
markers obrigatórios** em ordem:

1. `[net] DHCP: lease acquired.` — confirma que a NIC E1000 obteve
   lease.
2. `[smoke] gui-session ready` — emitido pelo runtime gráfico apenas
   quando `desktop_gui_session_smoke_gate_from_readiness()` aprova
   framebuffer, dimensões, taskbar, dispatcher essencial, fila
   saudável e ausência de overlays/drag.
3. `[smoke] mouse-events ready` — emitido apenas quando
   `desktop_mouse_events_smoke_gate_from_readiness()` aprova também
   mouse, cursor e rotas de mouse (scroll/hover/left-button/right-context/capture-opt-in).

**Documento autoritativo.** `docs/testing/vmware-e1000-smoke.md`.

**Pass.** Os três markers aparecem em ordem, em comparação case-insensitive no
serial log, e nenhum marker grave de falha aparece; exit code 0.

**Fail comum.**
- Serial vazio → revisar configuração serial da VM **antes** de
  investigar DHCP.
- DHCP nunca aparece → confirmar NIC E1000 + bridge.
- DHCP OK mas `gui-session` nunca aparece → bug do runtime gráfico,
  bloqueia Etapa 2.
- `gui-session` OK mas `mouse-events` nunca aparece → bug no
  dispatcher de mouse ou no cursor, bloqueia Etapa 2.
- Aparece `kernel panic`/`panic:`/`triple fault`/`general protection fault`
  → falha catastrófica, capturar serial completo, abrir bug.

Falhas em C2 são bugs **dentro do bloco da Etapa 2**, não novos
entregáveis (per critério de aceite operacional em `STATUS.md`).

## 8. Fase D — Evidência + aceitação pós-smoke (sem religar a VM)

### D1. Gerar evidência pública

```bash
make release-ci-smoke-evidence \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Lê o serial e o summary produzidos por C2 e gera
`build/release-smoke-evidence.manifest` com hashes SHA-256, tamanhos
e a lista dos três markers obrigatórios.

**Documento autoritativo.** `docs/operations/release-ci-smoke-evidence.md`.

### D2. Verificar evidência publicada

```bash
make verify-release-ci-smoke-evidence \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Reabre o manifesto de evidência e confere consistência
com o handoff oficial e com os logs atuais.

### D3. Gerar aceitação pública

```bash
make release-ci-smoke-acceptance \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Gera `build/release-smoke-acceptance.manifest`
amarrando a evidência publicada ao handoff oficial. Registra
`public_release_smoke_accepted=yes`.

**Documento autoritativo.** `docs/operations/release-ci-smoke-acceptance.md`.

### D4. Verificar aceitação publicada

```bash
make verify-release-ci-smoke-acceptance \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Recalcula a expectativa do manifesto de evidência antes
de aceitar como pronto para promoção.

**Pass conjunto.** D1-D4 retornam exit 0. Arquivos
`build/release-smoke-evidence.manifest` e
`build/release-smoke-acceptance.manifest` existem.

## 9. Fase E — Promoção pública final (encerra Etapa 2)

### E1. Gerar promoção pública

```bash
make release-ci-smoke-promotion \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Gera `build/release-smoke-promotion.manifest`
amarrando publicação + handoff + aceitação + evidência. Registra
`public_release_promotable=yes`.

**Documento autoritativo.** `docs/operations/release-ci-smoke-promotion.md`.

### E2. Verificar promoção publicada

```bash
make verify-release-ci-smoke-promotion \
  RELEASE_TAG="$RELEASE_TAG" \
  SMOKE_X64_VMWARE_ARGS="$SMOKE_X64_VMWARE_ARGS"
```

**O que faz.** Confere a coerência final entre os quatro manifestos
encadeados.

**Pass conjunto.** E1 e E2 retornam exit 0.

## 10. Aceite operacional da Etapa 2

**A Etapa 2 só é considerada operacionalmente fechada quando:**

1. Fases A, B, C, D, E acima passaram fora desta máquina.
2. Os artefatos `build/release-official-handoff.manifest`,
   `build/release-smoke-evidence.manifest`,
   `build/release-smoke-acceptance.manifest` e
   `build/release-smoke-promotion.manifest` foram anexados como
   evidência ao registro oficial da release.
3. Em `docs/plans/STATUS.md`, na lista de entregáveis concluídos e pendência
   externa da Etapa 2, o item é alterado de
   `- [ ] Execução externa final dos gates.` para
   `- [x] Execução externa final dos gates.` (com referência ao
   identificador da CI run).
4. Em `docs/plans/STATUS.md`, o progresso global muda
   `1/16 etapas oficialmente fechadas` para
   `2/16 etapas oficialmente fechadas` (Etapa 2 sai de "Em validação"
   para "Concluída").
5. Em `docs/plans/active/capyos-master-plan.md`, o resumo executivo muda
   `Etapa 2 tecnicamente fechada por código/docs; 1/16 etapas
   oficialmente fechadas` para refletir o aceite.
6. A linha da Etapa 3 em `STATUS.md` muda de `Bloqueada` para
   `Pronta para iniciar` ou equivalente.

**Após o aceite, Etapa 3 (Driver framework + entrada USB HID +
storage estável) fica desbloqueada** e segue a regra sequencial
estrita descrita em `docs/plans/active/capyos-master-plan.md`.

## 11. Markers obrigatórios — referência rápida

Exatamente três strings precisam aparecer em ordem no serial log do smoke
oficial, em comparação case-insensitive:

| Marker | Emitido por | Significado |
|---|---|---|
| `[net] DHCP: lease acquired.` | net stack após DHCP success | NIC E1000 obteve lease |
| `[smoke] gui-session ready` | `desktop_gui_session_smoke_gate_from_readiness()` | sessão gráfica pronta (framebuffer + dimensões + taskbar + dispatcher + fila + sem overlays/drag) |
| `[smoke] mouse-events ready` | `desktop_mouse_events_smoke_gate_from_readiness()` | mouse-events pronto (gui-session + mouse + cursor + 5 rotas de mouse) |

Os contratos C que emitem cada marker estão em
`src/gui/desktop/desktop_smoke_readiness.c` e o emissor único
está em `src/gui/desktop/desktop_runtime.c`.

### Marcadores de diagnóstico (não-obrigatórios)

Quando um gate fica bloqueado por mais que ~1-10 segundos após o
desktop runtime começar (1000 iterações do main loop; o tempo real
varia conforme atividade vs idle no frame pacing, PIT a 100 Hz no
caminho idle), o kernel emite uma única linha de diagnóstico no
serial log com o primeiro blocker observado. Essas linhas são
**informacionais apenas** — não bloqueiam nem aprovam o smoke. Use-as
para acelerar o diagnóstico quando o smoke falha.

| Diagnóstico | Significado quando aparece |
|---|---|
| `[smoke-diag] gui-session blocked: <reason>` | `gui-session` continua bloqueado por `<reason>` (ex: `framebuffer`, `taskbar`, `dispatcher`) após o boot estabilizar. |
| `[smoke-diag] mouse-events blocked: <reason>` | `mouse-events` continua bloqueado por `<reason>` (ex: `mouse`, `cursor`, `dispatcher-routes`) após o boot estabilizar. |

`<reason>` é o `first_blocker_name` do contrato
`desktop_smoke_blocker_summary()`. Valores conhecidos:
`inactive`, `framebuffer`, `dimensions`, `mouse`, `cursor`,
`taskbar`, `dispatcher`, `dispatcher-routes`, `queue`, `overlay`,
`window-drag`, `unknown`.

Cada diagnóstico é emitido **no máximo uma vez** por gate por execução
do desktop runtime — o serial log nunca é inundado. Se o gate fica
inacessível e depois recupera, o diagnóstico foi emitido para o
primeiro blocker observado e o marker `ready` correspondente aparecerá
quando o gate liberar.

O prefixo `[smoke-diag]` é deliberadamente distinto de `[smoke]` para
que a comparação por substring no parser oficial dos markers
obrigatórios não tenha colisão.

Mapeamento rápido entre `<reason>` e ação do operador:

- `framebuffer` / `dimensions` → revisar configuração de tela da VM
  (UEFI firmware framebuffer ativo, modo gráfico não-VGA).
- `mouse` / `cursor` → revisar PS/2 mouse (driver ativo,
  ponteiro inicializado pelo runtime).
- `taskbar` / `dispatcher` → bug do runtime gráfico; capturar serial
  completo e abrir bug dentro da Etapa 2.
- `overlay` / `window-drag` → estado transitório improvável de
  persistir; se persistir, capturar serial e abrir bug.
- `dispatcher-routes` → rotas de input do dispatcher central não
  registradas; bug do runtime, abrir bug.
- `queue` → fila do dispatcher está com backlog/drops; aumentar a
  janela de espera do smoke ou capturar serial para diagnóstico.

## 12. Troubleshooting transversal

Se qualquer fase falhar, **NÃO** mude escopo. Aplique a regra do
critério de aceite: trate como bug dentro da Etapa 2, corrija o
código/teste/doc afetado, refaça da fase falhada em diante.

Falhas típicas mapeadas:

- **Falha em A** → regressão recente do code base. Reverter mudança
  suspeita, refazer fase A.
- **Falha em B** → drift de tag/chave/manifesto. Conferir
  `VERSION.yaml`, `include/core/version.h`, release note pública e
  fingerprint da chave pública.
- **Falha em C1** (`release-ci-smoke-readiness`) → problema de
  configuração do operador (logs path, VMware args). NÃO ligar a VM.
- **Falha em C2** (smoke real) → bug no kernel/runtime gráfico.
  Capturar serial completo + summary, abrir bug com link para esta
  run, NÃO mudar escopo.
- **Falha em D ou E** → drift entre a evidência e o handoff. Conferir
  hashes/markers; provavelmente C2 produziu evidência incompleta.

## 13. Política operacional desta máquina

Esta máquina **NÃO** executa nenhuma das fases acima
(`MEMORY[05-local-execution-policy.md]`). Tudo aqui é:

- review de código relacionado à sessão gráfica;
- ajustes documentais (incluindo este playbook);
- preparação de planos de validação;
- catch silent drift entre código e doc.

Quando bugs são reportados pelo operador externo, esta máquina entra
para corrigir o código fonte. A re-execução das fases volta a ser
externa.

## 14. Manutenção deste playbook

Atualizar este documento quando:

- a versão alvo mudar (atualizar `0.8.0-alpha.237+20260514`);
- um gate Makefile mudar de nome;
- um marker obrigatório for adicionado/removido;
- uma fase nova for adicionada (improvável até Etapa 3 fechar).

NÃO duplicar conteúdo dos cinco documentos autoritativos referenciados
em cada fase — este playbook é orquestração, não substituição.
