# CapyOS — Master Plan único e linear

**Data de referência:** 2026-05-10
**Versão atual:** `0.8.0-alpha.93+20260510`
**Plataforma oficial:** `VMware + UEFI + E1000`
**Branch ativa:** `main` / `develop`
**Substitui:** `capyos-robustness-master-plan.md`, `system-master-plan.md`, `system-roadmap.md`, `system-execution-plan.md`, `capyos-master-improvement-plan.md`, `browser-status-roadmap.md`, `source-organization-roadmap.md`, `m5-userland-progress.md`, `post-m5-ux-followups.md` (todos arquivados em `historical/` em 2026-05-01).

---

## 0. Convenção de nomenclatura única (2026-05-02)

A partir desta data, **toda documentação ativa** (`docs/plans/active/*.md`,
release notes em `docs/releases/`, e `STATUS.md`) usa o vocabulário
abaixo de forma consistente:

| Termo | Significado | Onde aparece |
|---|---|---|
| **Etapa N** (N = 1, 2, 3, …) | Unidade linear de trabalho com escopo finito, dependência declarada e critério de aceite verificável. Substitui `Fase F1`, `Slice 5d`, `Phase 7b`, `Sessão 3`. | Toda nova subdivisão de plano. |
| **Seção a, b, c, d, e** | Sub-divisão dentro de uma Etapa. Letra minúscula, sequencial. Substitui `E1.1`, `E1.2`, `slice 4-final`, `M5.G.4`. | Toda nova decomposição interna de uma Etapa. |
| **Marco** (M0…M8) | **Tag arqueológico** de blocos históricos já entregues (M0 governança, M4 processos, M5 userland, M6 segurança, M7 WAL, M8 browser kernel-side). Imutável. Apenas leitura. | Tabela de "Entregue" em §2.1 deste master plan. |
| **F1…F10** | **Tag arqueológico** das fases originais deste master plan. Identificadores estáveis para evitar quebra de referências cruzadas, mas todo conteúdo NOVO usa Etapa N + Seção a-e. | §4 (renomeada para "Etapas linearizadas"). |
| **Sessão** | Janela temporal de trabalho de um operador (não unidade de plano). Forma livre. Em release notes, vira "Etapa N seção L (sessão YYYY-MM-DD)". | Logs de sessão, conversation summaries. |

**Regra de ouro:** Qualquer plano novo ou refactor de plano existente
**deve** usar `Etapa N` + `Seção a/b/c…`. Identificadores legados (F3.3f,
slice 5d, M5.G.4, etc.) podem aparecer entre crases como tag arqueológico
mas nunca como cabeçalho de uma tarefa nova.

---

## 1. Propósito deste documento

Este é o **único plano vivo** do CapyOS. Ele consolida:

1. O que já foi entregue (M0–M8 do plano antigo de robustez, M4 finalization, M5 userland, W1–W3 UX).
2. O que falta — em forma de **fases lineares numeradas (F1…F10)**, cada uma com escopo finito, dependências explícitas, critério de aceite verificável e plano de execução.
3. A visão de longo prazo (pós-F10) sem prometer datas.

Regras de manutenção:

- **Toda mudança relevante atualiza este documento e o `STATUS.md`.**
- Fases só promovem para `Implementado` com **evidência** (host test + smoke + commit).
- Nenhuma feature de produto começa sem fechar a fase anterior, exceto quando explicitamente marcada como `paralelo possível`.
- Nenhum plano novo pode ser criado em `active/` sem reconciliar com este. `historical/` recebe planos finalizados ou substituídos.

---

## 2. Estado atual consolidado

### 2.1 Entregue (não volta a este plano como tarefa)

| Bloco | Status | Evidência principal |
|---|---|---|
| **M0** Governança / matriz de suporte / gates de release | ✅ 100% | `make release-check`; `docs/reference/driver-support-matrix.md` |
| **M1** Build `TOOLCHAIN64=elf` + stack-protector + checksums + version-audit | ✅ 100% | `make all64 TOOLCHAIN64=elf`; `make iso-uefi`; `make version-audit` |
| **M3** Layout estrito (≤ 900 linhas, headers `internal/`, audit cruzado) | ✅ 100% | `make layout-audit --strict` |
| **M4** Processos + scheduler preemptivo + CoW + TSS/RSP0 + ring-3 preemption (31/31 fases) | ✅ 100% | `historical/m4-finalization-progress.md`; suítes `test_vmm_anon_regions`, `test_vmm_cow`, `test_pmm_refcount`, `test_user_task_init`, `test_context_switch`; smokes `smoke-x64-preemptive-all` |
| **M5-perf** Métricas de boot, baseline, `perf-*` commands, op_budget genérico | ✅ 100% | `docs/performance/boot-baseline.json`; `make boot-perf-baseline-selftest` |
| **M5-userland** fork/exec/wait/pipe + capysh ring 3 + isolamento de crash | ✅ 95% (CI pendente) | A.1–F.4 entregues; smokes `fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh` (aguardam CI) |
| **M6.1** Política de senha + lockout | ✅ 100% | `tests/test_auth_policy.c` |
| **M6.2** Auditoria persistente (`[auth]`/`[net]`/`[update]`/`[recovery]`/`[priv]`/`[browser]`/`[capyfs-journal]`) | ✅ 100% | `tests/test_audit_events.c` |
| **M6.3** API centralizada de privilégios | ✅ 100% | `src/auth/privilege.c`; `tests/test_privilege.c` |
| **M6.5** CAPYFS journal v2 autenticado (HMAC-SHA256) | ✅ 100% | `tests/test_journal.c` |
| **M7** WAL + replay + fsck + recovery cause + update transacional | ✅ 100% | `tests/test_journal`, `test_capyfs_check`, `test_capyfs_journal_cause`, `test_update_transact` |
| **M8.1** Browser com estado formal + strict mode + isolation hooks | ✅ 100% | `tests/html_viewer/navigation_cases.inc` |
| **M8.3** Budgets parse/render/external + `nav_op_budget` cooperativo | ✅ 100% | `tests/html_viewer/resource_cases.inc` |
| **M8.4** DNS cache TTL + HTTP cache com budget + `no-store` | ✅ 100% | `tests/test_dns_cache`; `tests/html_viewer/resource_cases.inc` |
| **M8.5** `about:network` / `about:memory` | ✅ 100% | `tests/html_viewer/resource_cases.inc` |
| **W1** TTY polish (`clear` context-aware via callback) | ✅ 100% (2026-04-30) | `src/shell/core/shell_main/output_files.c`; `userland/bin/capysh/main.c` |
| **W2** Task manager auto-refresh + Kill button | ✅ 100% (2026-04-30) | `src/apps/task_manager.c`; `src/gui/desktop/desktop.c` |
| **W3 core** Browser parser yield + timeout 30s + per-frame async drain | ✅ 100% (2026-04-30) | `src/apps/html_viewer/html_parser.c`; `src/apps/html_viewer/async_runtime.c` |

### 2.2 Pendente (entra como fase neste plano)

| Origem | Item | Vira fase |
|---|---|---|
| M2.1–M2.5 | DHCP no smoke oficial VMware+E1000 | **F2** |
| M5.G.4 / G.5 | Release notes + tag `0.8.0-alpha.6` + master-plan promote | **F1** |
| M6.4 | Assinatura ponta-a-ponta dos checksums + smoke VMware | **F2** (acopla) |
| M8.2 + W3.4 | Browser em processo userland isolado + watchdog | **F3** |
| Sistema | Stack sockets userland + TLS p/ apps | **F4** |
| Sistema | Update real via GitHub Releases (fetch + Ed25519) | **F5** |
| Sistema | Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher) | **F6** |
| Sistema | Apps básicos (file_manager, text_editor, settings, image_viewer) | **F7** |
| Sistema | Package manager + SDK + ABI estável | **F8** |
| M8.6 | JS engine sandboxed | **F9** |
| Sistema | CapyLang (linguagem própria) | **F10** |

### 2.3 Métricas globais

- **Progresso rumo a "SO sólido de uso geral":** ~30% (era 18% no plano antigo; M4+M5+M7+W1–W3 fecharam grandes blocos).
- **Cobertura de testes host:** 7/7 suites principais + op_budget + privilege + buffer_cache_pacing + 110+ asserts M4 + 115+ asserts M5 + 162+ asserts F6.
- **Smokes QEMU em CI:** 4 preemptivos + 6 M5 (aguardando push) + boot + ISO + CLI + capysh.
- **Lacunas estruturais críticas:** rede sockets userland, TLS de apps, package manager, GUI fim-a-fim, JS engine.

---

## 3. Princípios arquiteturais (inalteráveis)

1. **Não expandir feature de produto sem fronteira clara entre kernel e userland.** M5 entregou a fronteira — toda nova feature deve respeitá-la.
2. **Toda capacidade nova nasce observável, testável e com critério de rollback.**
3. **Update automático faz parte da arquitetura, não é script tardio.**
4. **GUI não acopla ao boot path.** Sessão gráfica é serviço, não inicialização.
5. **Linguagem própria só após ABI, package manager, toolkit gráfico e SDK estáveis.**
6. **Plataforma oficial é `VMware + UEFI + E1000`.** Hyper-V e QEMU são laboratório.
7. **Branches:** `main` = canal `stable`; `develop` = canal `develop`. Update agent trata como trilhas distintas.
8. **Layout estrito:** ≤ 900 linhas por arquivo C/teste, headers `internal/` para fronteiras privadas, `make layout-audit --strict` em CI.

---

## 4. Plano linear de entregas (F1 → F10)

Cada fase tem o mesmo formato:

```
### Fx — Nome
**Criticidade:** alta / média / baixa
**Depende de:** lista
**Paralelo possível com:** lista
**Risco:** texto
**Esforço estimado:** sessões/sprints
**Branch sugerida:** nome

#### Objetivo
#### Entregáveis (E1, E2, …)
#### Critérios de aceite (checkbox)
#### Como será feito (passos técnicos)
#### Validação (commands, suites, smokes)
#### Saída esperada / DoD
```

---

### F1 — Release snapshot `0.8.0-alpha.6` (consolidação M5 + W1/W2/W3 + F3 browser)

**Status:** ✅ Implementado (2026-05-05) — snapshot finalizado em `main`/`develop`; tag assinada/promocao formal movida para F2 junto da assinatura Ed25519.
**Criticidade:** alta — destrava M8.2 e o ciclo seguinte.
**Depende de:** branch `feature/continue-development` (M5+W1+W2+W3 já merge no HEAD).
**Paralelo possível com:** F2 (smoke VMware) — diferentes superfícies.
**Risco:** baixo — código entregue e validado no host; validações externas ficam na esteira.
**Esforço:** concluído.
**Branch sugerida:** concluído em `main`/`develop`.

#### Progresso 2026-05-01

- ✅ E1.4 — `VERSION.yaml`, `include/core/version.h` e `README.md` bumped para `0.8.0-alpha.6+20260503`.
- ✅ E1.5 — Release note `docs/releases/capyos-0.8.0-alpha.6+20260503.md` escrita.
- ✅ E1.6 — Fluxo de screenshots migrado para `docs/screenshots/CapyUI/<versao-ui>/`; releases sem mudança visual reutilizam a mesma coleção.
- ✅ E1.8 — Master plan atualizado (este bloco).
- ✅ Validações locais passaram: `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`.
- ✅ E1.1 — Snapshot publicado em `main` e `develop` (commit `afa87b4`).
- ✅ E1.2 — Esteira de CI/smokes passa a ser validação contínua do snapshot publicado.
- ✅ E1.3 — `make release-check` permanece como gate de CI/release assinado.
- ✅ E1.7 — Tag/promocao assinada reclassificada para F2, junto do verificador Ed25519.

#### Objetivo

Fechar o release alpha.6 consolidando:

- M5 userland (fork/exec/wait/pipe + capysh + isolamento de crash) — já 95%.
- W1+W2+W3 UX (clear, task manager, browser responsiveness) — já 100%.
- Promover M4.1–M4.5 e marcar M8.2 como "pronto para iniciar" no master plan.

#### Entregáveis

- **E1.1** — Push de `feature/dev-bugfixes` ao GitHub, abertura de PR contra `develop`.
- **E1.2** — CI executa **6 smokes M5**: `fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh`. Todos verdes.
- **E1.3** — CI executa **release-check**: `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`, `make all64 TOOLCHAIN64=elf`, `make iso-uefi TOOLCHAIN64=elf`, `make release-checksums`, `make verify-release-checksums`.
- **E1.4** — Bump em `VERSION.yaml`, `include/core/version.h`, `README.md` para `0.8.0-alpha.6+<YYYYMMDD>`.
- **E1.5** — Release notes em `docs/releases/capyos-0.8.0-alpha.6+<date>.md` cobrindo: SYS_FORK/EXEC/WAIT/PIPE, capysh ring 3, isolamento de crash, W1/W2/W3, parser yield + timeout do browser.
- **E1.6** — Screenshots por CapyUI: `docs/screenshots/CapyUI/<versao-ui>/`; criar nova versao apenas quando houver captura visual nova.
- **E1.7** — Tag assinada/promocao formal fica em F2.
- **E1.8** — Atualização deste master plan: F1 → ✅, M8.2 entra em F3.

#### Critérios de aceite

- [x] Snapshot publicado em `main` e `develop`.
- [x] `make version-audit` valida `VERSION.yaml` ↔ headers ↔ README ↔ screenshots CapyUI ↔ release note.
- [x] Release note publicado em `docs/releases/`.
- [x] `STATUS.md` atualizado para fechar F1.
- [x] Tag git assinada reclassificada para F2.

#### Como será feito

1. Publicar snapshot em `main` e `develop`.
2. Rodar auditoria de manifestos e testes host.
3. Consolidar release note e README.
4. Consolidar screenshots por CapyUI, sem duplicar PNGs por release.
5. Acompanhar CI como validação contínua do snapshot.
6. Mover tag assinada para F2.

#### Validação

```bash
make release-check
make smoke-x64-fork-cow
make smoke-x64-exec
make smoke-x64-fork-wait
make smoke-x64-pipe
make smoke-x64-fork-crash
make smoke-x64-capysh
make version-audit
```

#### Saída esperada

`0.8.0-alpha.6` publicado como snapshot nos canais `main` e `develop`. M4.1–M4.5 oficialmente "Implementado". F3 browser ring-3 fechado neste ciclo.

---

### F2 — DHCP no smoke oficial VMware+E1000 + assinatura de checksums

**Criticidade:** alta — fecha M2 (75% → 100%) e M6.4.
**Depende de:** F1 (release base limpo); harness VMware acessível.
**Paralelo possível com:** F3 (browser isolado) — superfícies independentes.
**Risco:** médio — depende de infraestrutura VMware externa que pode não estar disponível em CI público.
**Esforço:** 2–3 sessões.
**Branch sugerida:** `feature/m2-vmware-smoke`.

#### Objetivo

Validar que uma instalação nova em VMware+E1000:

- Boota via UEFI sem intervenção.
- Obtém DHCP automaticamente.
- Resolve DNS e faz fetch HTTP/HTTPS no `net-fetch`.
- Diagnostica falha de lease em `net-status`.

E publicar release com **checksums assinados** (Ed25519) — não só hash.

#### Entregáveis

- **E2.1** — Harness `tools/scripts/smoke_x64_vmware.py` entregue: orquestra `vmrun`/`govc` para subir VM, capturar serial console, validar markers de boot/rede.
- **E2.2** — Variante do `smoke_x64_cli.py` parametrizável para o harness VMware (driver=`e1000`, network=`bridged`, BIOS=`uefi`).
- **E2.3** — `make smoke-x64-vmware-dhcp` entregue como alvo agregador; execução real depende de infra VMware externa.
- **E2.4** — Documentação `docs/testing/vmware-e1000-smoke.md` entregue cobrindo setup, expectativas, markers e troubleshooting.
- **E2.5** — Tooling Ed25519 entregue: `tools/scripts/sign_release.py` assina `build/release-artifacts.sha256` e emite `.sig`; chave offline oficial segue como operacao do release manager.
- **E2.6** — `tools/scripts/verify_release_signature.py` entregue e exposto pelos alvos `verify-release-signature` e `verify-release-signature-selftest`; suporta pinagem SHA-256 da chave pública esperada.
- **E2.8** — `tools/scripts/release_ci_preflight.py` entregue para validar chave publica/fingerprint e argumentos VMware antes da esteira CI.
- **E2.9** — `tools/scripts/release_public_key_fingerprint.py` entregue para emitir fingerprint SHA-256 publicavel da chave pública Ed25519.
- **E2.10** — `tools/scripts/release_public_key_manifest.py` entregue para gerar manifesto publico deterministico da chave esperada.
- **E2.11** — `tools/scripts/release_ci_preflight.py` valida o manifesto publico contra chave publica e fingerprint esperados.
- **E2.12** — `tools/scripts/release_public_materials_check.py` confere checksums, assinatura, chave publica, fingerprint e manifesto antes da publicacao.
- **E2.13** — `tools/scripts/release_publication_manifest.py` gera manifesto publico deterministico dos materiais de publicacao.
- **E2.14** — `tools/scripts/verify_release_publication_manifest.py` confere manifesto de publicacao contra materiais reais.
- **E2.15** — `tools/scripts/release_publication_gate.py` orquestra assinatura, materiais e manifesto sem chave privada.
- **E2.16** — `tools/scripts/release_ci_publication_contract.py` valida contrato publico de CI antes dos gates de publicacao.
- **E2.17** — `tools/scripts/release_ci_tag_gate.py` conecta preflight, contrato e gate publico na esteira de tag.
- **E2.18** — `tools/scripts/release_ci_official_provisioning_contract.py` valida provisionamento publico oficial de CI/release.
- **E2.19** — `tools/scripts/release_official_handoff_manifest.py` gera/verifica manifesto publico de handoff oficial.
- **E2.7** — Documento `docs/security/release-signing.md` entregue com procedimento de chave + rotação.

#### Critérios de aceite

- [x] Checksums de release podem ser assinados/verificados com Ed25519 por
  tooling versionado, mantendo chave privada fora do repositorio.
- [ ] Em VMware+E1000, boot novo obtém DHCP em ≤ 10s sem comando manual (harness versionado; validação depende de infra externa).
- [ ] `net-status` mostra IP, gateway, DNS, sem `dhcp_last_error`.
- [ ] `net-fetch http://example.org` retorna 200 OK.
- [ ] `net-fetch https://example.org` valida TLS e retorna 200 OK (somente após F4 entregar TLS userland; aceite parcial: HTTP funciona, HTTPS reservado para F4).
- [ ] `make smoke-x64-vmware-dhcp` passa em CI com VM/serial/credenciais VMware provisionadas.
- [x] `sign-release-checksums` produz `release-artifacts.sha256` + `.sig`
  quando uma chave offline e informada pelo operador.
- [ ] CI executa `verify-release-signature` antes de aceitar tag quando a chave
  publica oficial estiver provisionada.
- [x] `verify_release_signature.py` rejeita assinatura mutilada via `--self-test`.
- [x] `verify_release_signature.py` rejeita chave pública com fingerprint SHA-256 inesperado.
- [x] `release-ci-preflight` falha cedo quando chave pública/fingerprint/VMware args estão ausentes ou inseguros.
- [x] `release-public-key-fingerprint` emite `RELEASE_PUBLIC_KEY_SHA256=<hex64>` sem acessar chave privada.
- [x] `release-public-key-manifest` gera `release-public-key.manifest` sem incluir chave privada.
- [x] `release-ci-preflight` rejeita manifesto público ausente, malformado ou divergente da chave/fingerprint.
- [x] `release-public-materials-check` valida pacote publico sem consumir chave privada.
- [x] `release-publication-manifest` gera manifesto publico de publicacao sem consumir chave privada.
- [x] `verify-release-publication-manifest` valida manifesto publico de publicacao sem consumir chave privada.
- [x] `release-publication-gate` agrega verificacoes publicas sem consumir chave privada.
- [x] `release-ci-publication-contract` valida contrato publico de CI sem consumir chave privada.
- [x] `release-ci-tag-gate` conecta preflight, contrato e gate publico sem consumir chave privada.
- [x] `release-ci-official-provisioning-contract` valida chave publica oficial, tag e smoke VMware sem consumir chave privada.
- [x] `release-official-handoff-manifest` gera/verifica manifesto publico de handoff sem consumir chave privada.
- [ ] M2.1–M2.5 promovidos para ✅ no master plan.
- [ ] M6.4 promovido para ✅.

#### Como será feito

1. Provisionar template VMware (kernel + ISO atual).
2. Harness Python entregue com providers `vmrun` e `govc`; próximo passo é provisionar VM/serial na CI.
3. Capturar log serial via `Serial.fileType=file`.
4. Adicionar markers explícitos no kernel: `[dhcp] lease ack ip=...` e `[net] ready` para o harness verificar.
5. Gerar chave Ed25519 com `openssl genpkey` ou `ssh-keygen -t ed25519`. Documentar onde fica armazenada (offline).
6. Usar `sign-release-checksums` para emitir `.sig` no fim do release manual.
7. Self-test negativo do verificador roda sem chave oficial para provar que
   assinatura mutilada falha fechado.
8. `release-public-key-fingerprint` emite o fingerprint publicavel da chave
   pública exportada pelo operador.
9. `release-public-key-manifest` gera manifesto publico da chave esperada
   para acompanhar a release.
10. `release-ci-preflight` valida chave publica/fingerprint/manifesto e
   argumentos VMware antes de gates pesados.
11. `release-public-materials-check` confere pacote publico antes da
   publicação externa.
12. `release-publication-manifest` gera manifesto auditavel da publicacao.
13. `verify-release-publication-manifest` confere o manifesto publicado.
14. `release-publication-gate` agrega assinatura, materiais e manifesto.
15. `release-ci-publication-contract` valida contrato publico de CI/tag.
16. `release-ci-tag-gate` conecta preflight, contrato e gate publico para aceitar tags.
17. `release-ci-official-provisioning-contract` valida chave publica oficial, manifesto, tag e smoke VMware.
18. `release-official-handoff-manifest` registra/verifica o handoff publico oficial de release.
19. CI roda `verify-release-signature` com fingerprint SHA-256 pinado antes
   de aceitar a tag apos publicar a chave publica oficial.

#### Validação

```bash
make release-public-key-fingerprint
make release-public-key-manifest
make release-public-materials-check
make release-publication-manifest
make verify-release-publication-manifest
make release-ci-publication-contract
make release-publication-gate
make release-ci-official-provisioning-contract
make release-ci-tag-gate
make release-official-handoff-manifest
make verify-release-official-handoff-manifest
make release-ci-preflight
make smoke-x64-vmware-dhcp
make release-check
make verify-release-signature-selftest
make sign-release-checksums RELEASE_PRIVATE_KEY=$HOME/.capyos/release-ed25519.pem RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem
make verify-release-signature RELEASE_PUBLIC_KEY=build/release-ed25519.pub.pem RELEASE_PUBLIC_KEY_SHA256=<hex64-ou-aa:bb:...>
```

#### Saída esperada

M2 fecha; M6.4 fecha. Todos os 9 critérios de aceite da release α passam.

---

### F3 — Browser em processo userland isolado + watchdog (M8.2 + W3.4)

**Status:** ✅ **100%** (2026-05-05) — ver [`historical/f3-browser-delivered.md`](../historical/f3-browser-delivered.md)
para detalhe completo das entregas (F3.3a..F3.3h + Etapas 1/2/3 b+e
+ b-polish + b-polish++ + 3a placeholder + 3a width/height attrs +
3c forms MVP+polish+textarea+select + 3d tables MVP+colspan +
Etapa 4 a+b + Etapa 5 rate limiter+URL policy+bytes obs+audit log+
nav-budget enforcement + imagens IPC/cache/blit + audit sink, total
**~1555 asserts host** novos de F3 + integracao com a suite base).

**Entregas consolidadas** (nao voltam como tarefa):

- Protocolo IPC binario (F3.3a), stub ring-3 + embedded_progs (F3.3b).
- Chrome scaffolding logico (F3.3d) — watchdog, dispatcher, runtime,
  spawn helper, e2e integration.
- Migracao `html_viewer` → `libcapyhtml` userland (F3.3c), slices 1..6.
- Desktop wiring + URL bar editavel (F3.3f), respawn anti-storm,
  13+ markers debugcon.
- **Etapa 2** — estabilidade pre-JS (7 secoes: process_kill fecha FDs,
  deteccao defensiva de morte, FB 480×360, pipe 64 KiB, LOG forward).
- **Etapa 3 b+e** — click→navigate com hit-test + scroll vertical.
- **Etapa 3 b-polish** — EVENT_TITLE, history BACK/FORWARD com ring
  32×1024, hotkeys F5/F6/F7.
- **Etapa 3 b-polish++** — pagina de erro HTML nativa (`<h1>+3×<p>`)
  em 4 caminhos de falha, sanitizacao `<`/`>`, scroll/BACK preservam
  contexto.
- **Etapa 3 seção a (placeholder MVP)** — parser reconhece `<img>` como
  void tag (top-level e inline dentro de blocks); extrai `src` → node
  `href`, `alt` → node `text`. Layout emite `CMD_IMAGE` com dims
  default 100×80 (clampadas por viewport) + margens 4/4. Raster
  desenha placeholder: fill MUTED + borda 1px LINK + marcador 3×3 no
  canto + alt text centralizado (se couber). `+33 asserts host` (7
  parser + 16 render + 10 raster).
- **Etapa 3 seção a refinement (width/height attrs)** — parser
  extrai atributos `width` e `height` de `<img>` (top-level e inline)
  via novo helper `parse_uint_attr` (decimal, tolerante a sufixo
  `px`, clamp em 65535); valores empacotados em `node->bold` +
  `node->reserved[3]` via macros novas em `capyhtml/types.h`:
  `CAPYHTML_IMG_SET_WIDTH/HEIGHT` + `CAPYHTML_IMG_GET_WIDTH/HEIGHT`.
  `rl_emit_image` lê dims parseadas (fallback 100×80), aplica cap
  defensivo `IMAGE_MAX_DIM=2048` (impede `<img width="999999">` DoS),
  e `rl_clamp_w` segue clampando por viewport. `+13 asserts host`
  (7 parser + 5 render + integracao no advance de y).
- **Etapa 3 seção c (forms MVP)** — parser: `<form action>` empurra
  TAG_FORM com action no `href`; `<input type/name/value/placeholder>`
  empurra TAG_INPUT com `name` em campo proprio + `text=value` +
  `bold=subtype` (text/submit/password; hidden silenciosamente
  descartado). Layout emite `CMD_INPUT` carregando subtype em
  `reserved[0]` e `node_idx` empacotado em `reserved[1..2]` para o
  engine localizar o node sem re-walk. Raster desenha caixa estilizada
  por subtipo (text=fundo MUTED, submit=fundo LINK, password=mascara
  glifos com `*`). Engine: `g_focused_input_idx` rastreia foco;
  `hit_test_doc` retorna kind (link/input_text/input_submit);
  `run_click` dispatcha NAVIGATE/foco/submit; `run_key` recebe
  `BROWSER_IPC_KEY` (codigo + mods 5 B BE) e roteia caracteres
  printable, BS, TAB (next input), Enter (submit); `run_submit`
  constroi query string `?k1=v1&k2=v2`. Chrome runtime:
  `chrome_runtime_send_key` novo. Browser_app: `focus_target`
  URLBAR vs PAGE; CLICK no frame vira PAGE, CLICK na URL bar vira
  URLBAR; Esc desfoca em PAGE antes de fechar; F5/F6/F7 sempre rodam.
  `+50 asserts host` (7 parser + 16 render + 10 raster + 4 chrome
  runtime + 13 ipc).
- **Etapa 3 seção c polish (forms refinements)** — bit
  `CAPYHTML_INPUT_FLAG_FOCUSED` no nibble alto de `reserved[0]`
  (subtype mascara low nibble); engine pos-processa cmd list
  setando esse bit no input cujo `node_idx == g_focused_input_idx`;
  raster usa HEADING color em borda 2 px e desenha caret 1×GLYPH_H
  apos os chars. Encoding RFC 3986: `build_form_query` reescrita
  com `form_query_is_unreserved` + `form_query_emit_pct` (saida
  `%XX` uppercase para qualquer byte fora de `[A-Za-z0-9-._~]`,
  espaco vira `+`). Click fora de input limpa foco e re-emite
  frame para descomissionar a borda. `+8 asserts host`
  (5 raster: focus-border, no-ring-when-unfocused, caret, no-caret-on-submit, subtype-mask-isolates-flag).
- **Etapa 3 seção d (tables MVP)** — parser reconhece `<table>`/
  `<tr>`/`<td>`/`<th>` (TH com bold=1). Layout: state machine
  com `table_col_count` derivado de TDs/THs da primeira TR;
  `cell_w = avail_w / cols` (com fallback se < TABLE_MIN_CELL_W);
  cada TD/TH emite `CMD_CELL` (kind=6) em `(col_index*cell_w,
  row_y)`; TR avanca y para a proxima linha. Defesas: tabela vazia
  emite 0 cells, excess cells em uma linha sao dropados, tabela
  aninhada fecha a anterior. Raster: `draw_cell_cmd` pinta borda
  1 px LINK em todo perimetro + bg MUTED para TH; texto em
  HEADING/TEXT color via `color_role`. `+41 asserts host`
  (11 parser + 22 render + 8 raster).
- **Etapa 3 seção d refinement (colspan + auto-fit)** — `colspan`
  atributo armazenado em `node->reserved[0]` (clamp 1..255); render
  soma colspans para derivar col_count e emite `CMD_CELL` com
  `w = colspan * cell_w`, avancando col_index por colspan. Auto-fit
  em viewport estreito: `cell_w` clampa para `TABLE_MIN_CELL_W`
  (cells overflow tolerated, raster put_pixel clipa) ao inves de
  dropar a tabela inteira. `+22 asserts` (7 parser: colspan parsed,
  default 0, oversize→255, zero→1, th colspan; 15 render: colspan
  layout, abut, clamp-to-remaining, narrow viewport).
- **Etapa 3 seção c refinement (textarea)** — parser detecta
  `<textarea name="X">body</textarea>` e empurra como TAG_INPUT
  com subtype TEXTAREA (4). Render emite CMD_INPUT com
  `INPUT_TEXTAREA_W=280, INPUT_TEXTAREA_H=72`. Raster posiciona
  texto no topo (padding 6 px) ao inves de centro vertical (que
  fica esquisito em altura grande). `+13 asserts` (4 parser: subtype,
  body capture, name, empty case; 6 render: cmd shape, h>=64;
  3 raster implicit via cell tests reuse).
- **Etapa 3 seção c refinement (select)** — parser detecta
  `<select name="X">` (subtype SELECT=5) e cada `<option value="V">L</option>`
  vira um TAG_OPTION sibling (`name=value, text=label`). O primeiro
  option label e copiado para o text do select como valor default.
  Sem `value`, o label vira o value. Render reusa as dimensoes do
  text input. Raster desenha um marcador "▼" 5 px na direita em
  HEADING color (caret e padding na esquerda preservados). `+13
  asserts` (6 parser: subtype, name, default, count, value+label,
  fallback; 5 render: cmd kind, subtype, default text, action; 2
  raster: marker presente, marker isolado).
- **Etapa 5 hardening refinement (memory budget per nav)** — runtime
  passa a aplicar um teto de bytes IPC por navegacao
  (`CHROME_RUNTIME_NAV_BUDGET_BYTES_MAX = 16 MiB`). Cada `poll_event`
  admitido acumula header+payload em `bytes_in_current_nav`. Quando
  excede, o engine e marcado `alive=0`, `total_nav_budget_kills`
  incrementa, audit log recebe `CAPYC_AUDIT_BUDGET_EXCEEDED` (com
  `code` = KiB acumulados truncados) e `poll_event` retorna
  `POLL_NAV_BUDGET_EXCEEDED`. O contador zera em qualquer ponto que
  inicia uma nav nova: `send_navigate`/`send_back`/`send_forward`/
  `send_reload` (chrome inicia), `EVENT_NAV_STARTED` (engine confirma),
  e `record_restart` (novo engine). Setter test-only
  `chrome_runtime_set_nav_budget_for_test()` permite forcar limites
  baixos para validar enforcement sem injetar 16+ MiB de eventos
  sinteticos. `+19 asserts host` (init/accumulate/reset on nav/reset
  on EVENT_NAV_STARTED/excess kill/restart clears).
- **Etapa 3 secao a refinement (fetch + decode + raster blit)** — pipeline
  IPC completo de imagens. Novos kinds `BROWSER_IPC_EVENT_IMAGE_REQUEST`
  (0x000A, 10 B + url) e `BROWSER_IPC_IMAGE_RESPONSE` (0x010A, 18 B +
  pixels BGRA32) com codecs em `src/apps/browser_ipc/image.c`. Engine
  ring 3 ganha cache `.bss` em `userland/bin/capybrowser/image_cache.{h,c}`
  (4 slots × 240×180 BGRA32 = ~675 KiB) com lookup por url-hash, status
  PENDING/OK/ERROR e invalidacao de slots de navs antigas. Apos
  `capyhtml_parse`, engine walk no doc emite REQUEST para cada `<img>`
  sem cache hit; ao receber RESPONSE, atualiza cache e re-renderiza.
  Pos-processa cmd list apos `capyhtml_layout`: para cada `CMD_IMAGE`
  com cache hit OK, popula `cmd->image_pixels/w/h`. Chrome ganha
  `handle_image_request` (stage pending slot) +
  `chrome_runtime_dispatch_pending_image` (resolve via `try_http_fetch`,
  decoda PNG/JPEG via `png_decode`/`jpeg_decode` em ring 0, valida
  bounds 240×180, encoda RESPONSE com pixels). Em UNIT_TEST sempre
  retorna UNSUPPORTED (decoders dependem de kalloc do kernel).
  capyhtml `struct cmd` ganha campos `image_pixels`/`w`/`h`;
  `draw_image_cmd` blita pixels BGRA32 quando set, senao desenha
  placeholder (queda graceful em cache miss/PENDING/ERROR).
  `+134 asserts host` (45 ipc image codec + 47 image cache + 27 chrome
  dispatch + 15 raster blit).
- **Etapa 5 hardening refinement (audit log debugcon sink)** —
  `capyc_audit_state` ganha campo `sink: capyc_audit_sink_fn`
  (typedef `void (*)(uint8_t cat, uint16_t code, uint32_t seq)`),
  `capyc_audit_init` zera para NULL, `capyc_audit_set_sink(st, fn)`
  instala/remove. Apos cada commit no ring, `capyc_audit_record` invoca
  `st->sink(category, code, seq)` se nao NULL. Default = NULL = no-op
  (zero overhead quando desabilitado). Kernel pode wirar a
  `debugcon_writes` para tornar audit log inspecionavel via klog sem
  exportar struct internals e sem alocacao no caminho hot. Idempotente;
  passar NULL desabilita; pode ser reinstalado a qualquer momento.
  `+11 asserts host` (default NULL + called-per-record com category/code/
  seq/contagem + replace/disable).
- **Bug fix critico VMware browser scheduling + write_full yield (2026-05-05 sessao 4b/4c)** —
  ROOT CAUSE de "browser nao carrega NENHUMA URL, tela preta, cursor
  em loading" reportado apos F3 fechar. Investigacao revelou TRES
  bugs distintos:

  1. **`CAPYOS_PREEMPTIVE_SCHEDULER` nao era default no kernel build.**
     Sem essa flag, `capyos_preemptive_mark_running()` e no-op,
     `scheduler_set_running(1)` nunca e chamado, e `task_yield()`
     em `kernel/scheduler.c::scheduler_yield` retorna direto sem
     escalar (porque `if (!sched_running) return;`). Resultado:
     tasks adicionadas ao run_queue (engine ring 3 do browser, etc)
     **nunca eram picked**. Compositor (unica task ativa) chamava
     `chrome_runtime_send_navigate` com sucesso (escreve no pipe
     vazio sem precisar yield), mas o engine nunca rodava para
     consumir o NAVIGATE, parsear HTML, ou emitir EVENT_FRAME.
     `chrome.status` ficava em `LOADING` para sempre, tela preta.
     `hey`/`net-fetch` continuavam funcionando porque rodam DENTRO
     da task do shell/desktop (sem precisar de outra task). Smokes
     do browser (`smoke-x64-browser-spawn`,
     `test_browser_chrome_runtime_run`) ja vinham passando porque
     definem a flag explicitamente em `EXTRA_CFLAGS64`. **Fix:**
     adiciona `-DCAPYOS_PREEMPTIVE_SCHEDULER` ao `CFLAGS64`
     default no `Makefile`. Smokes que ja passavam a flag continuam
     idempotentes.

  2. **No caminho real VMware/desktop, o compositor ainda rodava fora
     do scheduler.** Mesmo com a flag default, o caminho `desktop`
     do shell entra em `desktop_runtime_start()` no stack atual do
     kernel/shell; nao existia `task_current()` valido para salvar
     contexto quando o browser engine era adicionado ao run_queue.
     Em ambientes onde o timer APIC/preemption nao fica armado
     durante o desktop (caso VMware/handoff), depender so de tick
     preemptivo mantinha o engine sem progresso. **Fix:** o desktop
     agora se adota como task kernel (`desktop-main`) ao iniciar,
     marca `scheduler_set_running(1)`, e chama `task_yield()` uma vez
     por frame apos `desktop_run_frame()`. Assim, ao abrir o browser,
     o frame seguinte cede CPU cooperativamente para o engine ring 3;
     quando o engine bloqueia em pipe/read/write, ele cede de volta
     ao desktop. `include/kernel/task.h` passou a exportar
     `task_set_current()` para evitar externs locais. Regressao host:
     `test_context_switch` ganhou +2 asserts que simulam exatamente
     `desktop-main -> capybrowser -> desktop-main`.

  3. **`write_full` no chrome runtime tratava pipe-cheio como
     erro fatal.** Bug latente que manifestaria mesmo com (1)
     e (2) resolvidos: payloads > 64 KiB (`PIPE_BUF_SIZE`) faziam
     `pipe_write` retornar -1 (would-block), `write_full`
     retornava -1, `chrome_runtime_send_ipc_frame` marcava
     `engine_alive=0`. Cobre 100% das `BROWSER_IPC_IMAGE_RESPONSE`
     (240×180×4 = 172 KiB) e a maioria das `FETCH_RESPONSE` para
     sites externos. **Fix:** simetria com
     `read_full(allow_yield=1)`: quando `g_yield_fn` foi injetado
     (browser_app via `chrome_runtime_set_yield_op(task_yield)`),
     `wr <= 0` cede CPU ao leitor e retenta. Limite
     `CHROME_RUNTIME_WRITE_YIELD_LIMIT = 4096` previne livelock se
     o engine morreu silenciosamente. Sem yield (host tests),
     preserva semantica fail-fast antiga. `+10 asserts host` em
     `tests/test_browser_runtime_write_yield.c` (no-yield falha
     rapido + drain-yield completa 16 KiB com payload preservado
     byte-a-byte + livelock prevention apos limite + header sem
     payload nao regrede). Em 4c, o `read_full` do header de evento
     tambem passou a usar `allow_yield=1`, evitando protocol-error
     se o header IPC chegar parcial durante alternancia cooperativa
     engine/desktop.
- **Bug fix regressivo browser open/freeze (2026-05-05 sessao 4d)** —
  Sintoma em VMware apos o ajuste de scheduling: ao abrir o browser pela
  primeira vez, a taskbar indicava "Browser" mas nenhuma janela aparecia;
  ao abrir novamente, o sistema congelava. Estudo regressivo mostrou
  falha de lifecycle em dois pontos: (1) `browser_app_close()` chamava
  `compositor_destroy_window()` ainda com `win->on_close` apontando para
  `browser_app_on_close`; o compositor invocava esse callback de novo e
  gerava close reentrante, com segundo `chrome_runtime_send_shutdown()`
  em pipes ja fechados, levando `write_full` a ceder CPU ate o limite e
  aparentar freeze; (2) a taskbar so removia itens no caminho de clique
  no botao fechar, entao destruicoes programaticas deixavam item stale
  apontando para window id morto. Fixes: `browser_app_state` ganhou
  `close_in_progress` e `defer_first_tick`; `browser_app_close()` agora
  e idempotente, zera `on_close` antes de destruir a janela e so envia
  shutdown se o engine ainda existe; `browser_app_tick()` pula o
  primeiro tick apos open para garantir pelo menos uma renderizacao
  visivel; `spawn_engine_and_bind` marca o processo do engine como
  `PROC_STATE_RUNNING`; compositor exporta `compositor_window_exists()`;
  taskbar restaura/garante `taskbar_add_window` e poda itens stale em
  add, paint e click. Validacao sem `make`: script Python temporario
  validou contratos e syntax-check direto de `taskbar.c`/`compositor.c`,
  reexecutado apos restaurar `taskbar_add_window`, depois removido.
- **Validação regressiva de stacks do browser (2026-05-05 sessao 4e)** —
  Revalidação adicional sem `make`/`git` procurou stacks incompletos em
  lifecycle, IPC, taskbar e userland engine. Foram encontrados dois
  buracos no `browser_app_tick()`: `CHROME_RUNTIME_POLL_NAV_BUDGET_EXCEEDED`
  era retornado por `chrome_runtime_poll_event()` com `engine_alive=0`,
  mas o app nao tratava esse status, podendo deixar janela ativa com
  engine logicamente morto; e `BROWSER_CHROME_ACTION_IMAGE_REQUESTED`
  existia no dispatcher/runtime, mas o app nao chamava
  `chrome_runtime_dispatch_pending_image()`, deixando o pending image
  sem resposta e abrindo caminho para protocol-error/timeout. Fix:
  `browser_app_tick()` agora fecha em budget exceeded/protocol action,
  despacha fetch com tratamento de erro fatal, despacha image requests
  com `chrome_runtime_dispatch_pending_image()`, e fecha se fetch/image
  dispatch quebrar o pipe. Script temporario
  `_session_validate_browser_stacks.py` validou contratos de lifecycle,
  IPC read/write yield, image dispatch, taskbar stale pruning e stack
  shape do `capybrowser`; syntax-check direto de `runtime.c`,
  `runtime_image.c`, `taskbar.c`, `compositor.c` passou; script removido.
- **Validação do grafo `make iso-uefi` (2026-05-05 sessao 4f)** —
  Revalidado sem executar `make`/`git` o grafo oficial UEFI/x86_64:
  `CAPYOS64_OBJS` foi parseado com 266 objetos explicitos e todos os
  objetos criticos recentes estao presentes (`runtime_image.o`,
  `taskbar.o`, `compositor.o`, `browser_app/*.o`, codecs IPC e blob do
  `capybrowser`). `EFI_LOADER_SRCS` foi comparado com
  `src/boot/uefi_loader/*.c` e esta alinhado. O script temporario tambem
  validou quoted includes, grafo userland/blob, fontes legadas fora do
  ISO e syntax-check direto de TUs selecionadas. Ajuste aplicado no
  `Makefile`: os avisos informativos do caminho default
  `TOOLCHAIN64=host` agora usam `$(info ...)` em vez de `$(warning ...)`,
  reduzindo ruido em `make iso-uefi`; o warning real de fallback quando
  `TOOLCHAIN64=elf` e a cross-toolchain esta ausente foi preservado.
- **Bug fix critico tela azul ao abrir browser (2026-05-05 sessao 5)** —
  Sintoma reportado em VMware: ao abrir o browser, o sistema fica com
  tela completamente azul e congela (panic blue screen). ROOT CAUSE
  isolado em `src/memory/vmm.c` na funcao `clone_table_for_user_as`,
  introduzida na rodada anterior para evitar que o ELF loader
  corrompesse a arvore identity firmware ao mapear o user binary em
  `0x00400000`. A funcao split-tava huge pages do firmware abaixo de
  64 MiB em PD/PT 4 KiB, mas ao escrever o entry pai apontando para a
  tabela filha recem-alocada, **mantinha o bit `VMM_PAGE_HUGE` do
  entry original**. A CPU x86_64 entao interpretava o pai como huge
  leaf (1 GiB no PDPT level, 2 MiB no PD level), ignorava a tabela
  filha inteira, e mapeava o range coberto pelo pai para os primeiros
  bytes do frame fisico da tabela filha (lixo). Acessos do engine
  ring 3 pos-spawn resolviam para phys invalido -> page fault
  imediato -> reentrada -> double fault -> blue screen. Fix duplo:
  (1) novo `VMM_PARENT_TABLE_FLAG_MASK = ~(PHYS|HUGE|NX)` aplicado
  consistentemente em TODOS os pontos onde o clone materializa entry
  pai apontando para tabela (apos split de huge ou apos recurse de
  intermediario); (2) `vmm_destroy_address_space` reescrito para
  liberar apenas frames com `VMM_PAGE_USER` em entries pais (clones
  alocados por nos), evitando free de huge leaves firmware (que vivem
  fora do PMM e corromperiam o pool ao serem marcadas como livres).
  O destroy tambem ganhou check de `VMM_PAGE_HUGE` em PDPT level para
  cobrir 1 GiB direct mappings firmware preservados em
  `PDPT_clone[2..3]`. Validacao sem `make`/`git`: script Python
  modelando page-walk x86_64 de 4 niveis criado/executado/removido,
  validando 6 invariantes -- identity preservation (7 enderecos
  canonicos: 4 KiB, 1 MiB, 4 MiB, 64 MiB, 256 MiB, 2 GiB, 3.5 GiB
  preservam phys), parents-no-HUGE, parents-have-USER,
  leaves-no-USER, vmm_map_page sem afetar firmware, e destroy
  liberando exatamente os 34 frames PMM-allocados (1 PDPT + 1 PD +
  32 PTs) sem tocar firmware identity.
- **Revisao end-to-end da inicializacao do browser (2026-05-05 sessao 5b)** —
  Apos o fix do HUGE bit, foi feita revisao estatica completa do
  caminho desde o clique do usuario em Browser ate o engine ring 3
  receber comandos do chrome via pipe IPC, sem alteracoes de codigo.
  Script Python temporario criado/executado/removido cobrindo 12
  categorias e ~50 invariantes:
    1. `browser_engine_spawn`: pipes criados ANTES de
       `process_create`, fds 0/1 com flags corretos, scheduler_add
       deferido para o caller.
    2. `vmm_create_address_space`: PML4[0] clonado com
       `VMM_PARENT_TABLE_FLAG_MASK` (sessao 5), `VMM_PAGE_USER` em
       parents (20+ ocorrencias), destroy walker checa HUGE em PDPT
       level (1 GiB direct mappings firmware preservados).
    3. `elf_load_into_process`: `context.rip/rsp/cr3` setados (cr3
       e CRITICO para context_switch trocar AS), 16 pages eager + 240
       pages anon na user stack, flags `VMM_PAGE_USER` em segments.
    4. `user_task_arm_for_first_dispatch`: constroi IRET frame com
       SS|RSP|RFLAGS|CS|RIP corretos e NAO toca `context.cr3`
       (preserva cr3 setado pelo elf_loader).
    5. Trampoline `x64_user_first_dispatch`: POP_REGS (15 slots) +
       add rsp,16 (skip vector/error_code) + iretq.
    6. `context_switch`: cr3 em offset 0x48, swap condicional, skip
       se `context.cr3 == 0`.
    7. `browser_app_open`: pipe_ops antes de spawn, janela apos
       spawn (race-safe), `defer_first_tick`, reentrancy guards no
       close, shutdown gated por `engine_alive` E `!engine_is_dead`,
       `task_yield` instalado como yield_op.
    8. `spawn_engine_and_bind`: ordem rigida spawn -> init -> arm
       -> PROC_STATE_RUNNING -> scheduler_add.
    9. `desktop_runtime_start`: adopta `desktop-main` task quando
       `task_current() == NULL`, set running, yield no loop.
    10. `chrome_runtime`: write/read yield limits, `g_yield_fn()` em
        2 sites, `poll_event` com `allow_yield=1`.
    11. `process_exit`: fecha FDs propagando EOF nos pipes.
    12. Syntax check freestanding x86_64: 7/8 TUs limpos
        (`vmm.c`, `browser_engine_spawn.c`, `elf_loader.c`,
        `user_task_init.c`, `process.c`, `syscall.c`,
        `runtime.c`); 1 WARN benigno em `browser_app.c` (inline asm
        x86-only falha so em macOS arm64 host, esperado).
  Conclusao: nenhuma regressao introduzida, fluxo de inicializacao
  solido apos o fix do HUGE bit; nenhuma alteracao adicional
  necessaria.
- **Erradicacao do capybrowser legacy + roadmap Firefox (2026-05-05
  sessao 6)** -- decisao estrategica: o capybrowser (engine custom +
  capyhtml + browser_chrome + browser_app + browser_ipc) nao
  amadurecera para a web real (sem JS, CSS 2.1 incompleto, sem
  video/audio, sem GPU 3D). Em uma unica sessao: (a) novo plano
  ativo `docs/plans/active/firefox-port-roadmap.md` documentando o
  port do Firefox em 7 fases F1-F7 (~36-60 meses), incluindo gap
  analysis (musl libc, VFS+filesystems, dynamic linker, toolchain
  cross + Rust, swrast/Mesa, audio cubeb, build mach com
  `--target=x86_64-capyos`); (b) erradicacao em 8 fases sequenciais
  E1-E8 de **78 arquivos**: desativacao de pontos de entrada
  (`kernel_main.c` `CAPYOS_BOOT_RUN_BROWSER_SMOKE`, `desktop.c`
  `menu_action_browser`+tick, `extended.c` `cmd_open_browser`),
  remocao de 22 testes, 5 diretorios de codigo
  (`src/apps/browser_{app,chrome,ipc}`,
  `userland/{bin/capybrowser,lib/capyhtml}`), 2 modulos kernel
  (`browser_smoke.c`, `browser_engine_spawn.c`), 14 headers,
  cleanup de `embedded_progs.{c,h}`/`Makefile` (incluindo
  `CAPYBROWSER_*`/`CAPYHTML_*`/`CAPYBROWSER_BLOB_OBJ`/
  `capyhtml-userland-syntax` target/`smoke-x64-browser-spawn`
  target), `tools/scripts/smoke_x64_browser_spawn.py` deletado, e
  `docs/architecture/browser-ipc.md` + 2 planos arquivados em
  `docs/archive/browser-legacy/` com README explicativo.
  **Estruturas reusaveis preservadas** (necessarias para o port do
  Firefox): VMM/PMM (com fix do HUGE bit), `process.c`/`task.c`/
  `pipe.c`, scheduler + context switch + interrupts asm,
  `elf_loader.c`/`syscall.c`/`user_task_init.c`,
  desktop/compositor/fbcon, `userland/lib/capylibc/`, BearSSL +
  net stack, `capysh`+`hello`+`exectarget` userland, e o campo
  `system_settings.browser_homepage` (sera reusado pelo Firefox).
- **Refinamento do roadmap Firefox com codigo real upstream
  (2026-05-05 sessao 7)** -- apos o user adicionar
  `/Volumes/Firefox` (clone mozilla-firefox/firefox upstream) como
  workspace, exploramos os componentes-chave do Firefox e
  refinamos o roadmap em tres documentos. Decisao estrategica
  central: **Strategy A (Linux ABI compatibility)** ao inves de
  Strategy B (CapyOS native plataforma reconhecida) -- reduz
  patches no upstream de 1000+ para 20-50 e permite acompanhar
  releases nightly sem rebases custosos. Descobertas-chave que
  impactam estimativas: (1) **SWGL** (`gfx/wr/swgl/`,
  Software OpenGL escrito em Rust+C++ pela Mozilla) elimina a
  necessidade de portar Mesa swrast (~5M LOC), reduzindo F5 de
  12-18 meses para 6-9 meses; (2) **HeadlessWidget**
  (`widget/headless/`, ~25KB) e a base ideal para `widget/capyos/`,
  reduzindo widget effort de 12 meses (re-fork GTK) para 3-4
  meses; (3) **Chromium IPC POSIX branch** (linha 64-83 de
  `ipc/chromium/moz.build`) compila verbatim sob Strategy A,
  apenas `pthread`+`AF_UNIX socketpair`+`epoll` sao requisitos;
  (4) **Cross-process primitives** podem comecar como stub via
  `CrossProcessMutex_unimplemented.cpp`/`ProcessUtils_none.cpp`
  para o marco "Firefox carrega about:blank single-process";
  (5) **SpiderMonkey** (~1M LOC) precisa apenas de
  `pthread+clock_gettime+mmap PROT_EXEC`, conforme
  `js/src/threading/posix/PosixThread.cpp`; (6) **cubeb_null**
  (silencio) ou **cubeb_oss** (read/write em /dev/dsp) sao
  audio backends viaveis para v1; (7) **nsLocalFileUnix.cpp**
  (76KB) compila verbatim quando F1.fs estiver pronto; (8) build
  system **nao requer modificacao** sob Strategy A
  (`./mach configure --target=x86_64-unknown-linux-musl`).
  Cronograma revisado: **36-60 meses -> 27-42 meses** (-25 a 30%).
  **Marcos intermediarios M1-M8** definidos para tracking:
  M1 SpiderMonkey shell roda em CapyOS (F1+1 mes); M2 Gecko core
  compila standalone (F1+F2+F3+2 meses); M3 Firefox compila
  upstream (F4+1 mes); M4 Firefox linka mas crash; M5 about:blank
  renderiza (M4+4 meses); M6 example.com via HTTPS; M7 gmail.com
  login funciona; M8 youtube.com toca video. **Tres documentos
  publicados**: (a) `docs/plans/active/firefox-port-roadmap.md`
  ganhou Parte F com secoes F.1-F.11; (b) novo
  `docs/architecture/firefox-port-deep-dive.md` (725 linhas)
  cobrindo modulo a modulo (mozglue, mfbt, xpcom, ipc, js, gfx,
  widget, netwerk, media, servo, third_party), com tabela de
  dependencias por modulo Firefox e lista de ~80 syscalls Linux
  que Firefox usa; (c) novo
  `docs/plans/active/firefox-port-platform-shim.md` (350+
  linhas) com 51 tarefas individuais S1.1-S6.5 distribuidas em
  6 etapas (S1 kernel syscall surface, S2 pseudo-fs, S3 musl libc
  port, S4 toolchain, S5 pthread userland, S6 SpiderMonkey shell
  como marco M1), cronograma sequencial em 13 meses para M1,
  estrutura de arquivos resultante no CapyOS
  (`src/kernel/linux_compat/`, `src/fs/procfs/`, `src/fs/tmpfs/`,
  `userland/lib/musl/`, `userland/sdk/x86_64-capyos-musl/`),
  riscos e mitigacoes. Validacao via script Python temporario
  (`_session7_validate_firefox_docs.py`, criado/executado/
  removido) confirmou: 28/28 arquivos referenciados em
  `/Volumes/Firefox` existem, 6/6 tamanhos mencionados batem,
  cross-references entre os 3 documentos consistentes, 8/8
  marcos M1-M8 presentes, 6/6 etapas S1-S6 com 51 tarefas, 9/9
  grupos de syscalls cobertos.
- **S1.3 linux_compat/clock_gettime entregue (2026-05-05
  sessao 8)** -- primeira tarefa concreta do
  firefox-port-platform-shim.md implementada. Shim Linux-ABI
  `clock_gettime` em `src/kernel/linux_compat/linux_clock.c`
  com arquitetura host-testavel em 2 camadas: (1)
  `linux_clock_compute_timespec(elapsed_cycles, hz, *out)` --
  aritmetica pura, overflow-safe via decomposicao
  seconds+remainder antes de multiplicar por 1e9 (evita
  overflow naive em ~5s @ 3 GHz; validada para 1 ano de
  uptime @ 4 GHz); (2) `linux_clock_gettime(clk, *out)` --
  dispatcher cobrindo 8 clk_ids: MONOTONIC, MONOTONIC_RAW,
  MONOTONIC_COARSE, BOOTTIME, REALTIME, REALTIME_COARSE
  (somam wall epoch quando instalado), PROCESS_CPUTIME_ID
  e THREAD_CPUTIME_ID retornam -ENOSYS aguardando S5.
  Headers publicos: `linux_types.h` (struct linux_timespec
  layout x86_64 Linux ABI), `linux_errno.h` (LINUX_EINVAL=22,
  LINUX_EFAULT=14, LINUX_ENOSYS=38), `linux_clock.h` (API).
  Boot wiring em `linux_clock_init.c` (separado para ser
  excluivel em UNIT_TEST builds via `#if !defined(UNIT_TEST)`)
  chamado de `kernel_main.c` apos `x64_timebase_init` na Stage
  3/8. **22/22 host asserts** em `tests/test_linux_clock.c`
  cobrindo: layer 1 -- zero elapsed, exact-second, sub-second,
  ns precision (1 cycle @ 1 GHz == 1 ns), 1 ano @ 4 GHz sem
  drift, multi-second remainder, hz=0/NULL out rejeitados,
  add_timespec basic + normalisation overflow + NULL; layer 2
  -- NULL out, unknown clk -EINVAL, CPUTIME -ENOSYS, pre-init
  retorna zero (no crash), MONOTONIC strictly increases,
  MONOTONIC variants compartilham timebase, REALTIME without
  epoch == MONOTONIC, REALTIME with epoch soma corretamente,
  REALTIME nsec carry across second boundary, backwards-TSC
  clamps to t=0 (nao wrap em uint64), reset_for_tests
  limpa todo o estado. Wiring: 2 objs em CAPYOS64_OBJS, 1
  entry no TEST_SRCS, 2 linhas em test_runner.c, 5 linhas em
  kernel_main.c. Validacao via script Python temporario
  `_session8_validate_linux_clock.py` (criado/executado/
  removido): 0 FAIL, 0 WARN cobrindo arquivos+tokens,
  Makefile/test_runner/kernel_main wiring, host build
  cc+UNIT_TEST=1+Werror -> 22/22 passed, freestanding-like
  syntax check da TU pura. **Marco M1 (SpiderMonkey shell em
  CapyOS) progress**: cobre 1/4 das dependencias core do
  SpiderMonkey (pthread + clock_gettime + mmap +
  open/read/write). `clock_gettime(CLOCK_MONOTONIC)` agora
  funcional via Linux ABI. S1.3 marcado como **DONE** em
  `firefox-port-platform-shim.md`.
- **S3.x fd-readiness refinement: signalfd4 storage-only +
  eventfd/timerfd VFS routing (2026-05-08 sessao 41)** --
  `signalfd4` agora cria/atualiza fd storage-only, valida flags e
  `sizemask == 8`, remove SIGKILL/SIGSTOP da mascara armazenada e
  retorna -EAGAIN em read ate existir delivery real. `linux_vfs_router`
  passa a rotear fd ranges eventfd/signalfd/timerfd para
  `read`/`write`/`close`/`lseek`, permitindo que codigo userland que
  usa syscalls genericas com fds de readiness funcione sem depender
  de entrypoints internos. Regressions planejadas/revisadas:
  +8 cenarios em `test_linux_eventfd.c` e +6 em
  `test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd lifecycle refinement: memfd_secret VFS lifecycle
  (2026-05-08 sessao 42)** -- `memfd_secret` ja alocava fds em
  `linux_modern_misc.c`; agora `linux_vfs_router` reconhece esse
  range. `close(2)` libera o slot secretmem para reutilizacao, e
  `read(2)`/`write(2)`/`lseek(2)` em fd vivo retornam -ENOSYS ate
  existir backing/mmap real, evitando falso -EBADF em userland que
  opera sobre fd generico. Regressions planejadas/revisadas:
  +5 cenarios em `test_linux_modern_misc.c` e +4 em
  `test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd lifecycle refinement: inotify VFS lifecycle
  (2026-05-08 sessao 43)** -- `inotify_init1` ja criava instancias
  storage-only em `linux_inotify.c`; agora `linux_vfs_router`
  reconhece esse range. `close(2)` libera o slot, `read(2)` em fd vivo
  retorna -EAGAIN ate existir fila de eventos do fs-notifier,
  `write(2)` retorna -EINVAL e `lseek(2)` retorna -ESPIPE. Regressions
  planejadas/revisadas: +5 cenarios em `test_linux_inotify.c` e +4 em
  `test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd lifecycle refinement: epoll VFS lifecycle
  (2026-05-08 sessao 44)** -- `epoll_create1` ja criava instancias
  funcionais em `linux_epoll.c`; agora `linux_vfs_router` reconhece
  esse range. `close(2)` libera o slot, `read(2)`/`write(2)` em fd vivo
  retornam -EINVAL e `lseek(2)` retorna -ESPIPE, preservando
  `epoll_wait`/`epoll_pwait` como caminho correto de consumo de
  eventos. Regressions planejadas/revisadas: +5 cenarios em
  `test_linux_epoll.c` e +4 em `test_linux_vfs_router.c`. Validacao
  desta sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts. **NR count permanece 252**.
- **S3.x fd lifecycle refinement: fanotify VFS lifecycle
  (2026-05-08 sessao 45)** -- `fanotify_init` ja criava instancias
  storage-only em `linux_fanotify.c`; agora `linux_vfs_router`
  reconhece esse range. `close(2)` libera o slot, `read(2)` em fd vivo
  retorna -EAGAIN ate existir fila de eventos do fs-notifier,
  `write(2)` retorna -EINVAL e `lseek(2)` retorna -ESPIPE. Regressions
  planejadas/revisadas: +5 cenarios em `test_linux_fanotify.c` e +4 em
  `test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd lifecycle refinement: memfd/pidfd VFS lifecycle
  (2026-05-08 sessao 46)** -- `memfd_create` e `pidfd_open` ja criavam
  fds storage-only em `linux_memfd.c`; agora `linux_vfs_router`
  reconhece os ranges `0x5000` e `0x5800`. `close(2)` libera o slot
  dono para reuso; memfd generic `read(2)`/`write(2)`/`lseek(2)`
  retornam -ENOSYS ate existir backing real/ftruncate/mmap, enquanto
  pidfd generic `read(2)`/`write(2)` retornam -EINVAL e `lseek(2)`
  retorna -ESPIPE. Regressions planejadas/revisadas: +5 cenarios em
  `test_linux_memfd.c` e +4 em `test_linux_vfs_router.c`. Validacao
  desta sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts. **NR count permanece 252**.
- **S3.x fd lifecycle refinement: userfaultfd VFS lifecycle
  (2026-05-08 sessao 47)** -- `userfaultfd` ja criava fds storage-only
  em `linux_jit_aux.c`; agora `linux_vfs_router` reconhece o range
  `0xA000`. `close(2)` libera o slot, `read(2)` em fd vivo retorna
  -EAGAIN ate existir fila de eventos de page fault, `write(2)`
  retorna -EINVAL e `lseek(2)` retorna -ESPIPE. Regressions
  planejadas/revisadas: +5 cenarios em `test_linux_jit_aux.c` e +4 em
  `test_linux_vfs_router.c`. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd lifecycle refinement: landlock ruleset VFS lifecycle
  (2026-05-08 sessao 48)** -- `landlock_create_ruleset` ja criava fds
  de ruleset em `linux_landlock.c`; agora `linux_vfs_router` reconhece
  o range `0xB000`. `close(2)` libera o slot, `read(2)` e `write(2)`
  em fd vivo retornam -EINVAL e `lseek(2)` retorna -ESPIPE.
  Regressions planejadas/revisadas: +5 cenarios em
  `test_linux_landlock.c` e +4 em `test_linux_vfs_router.c`.
  Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd core hardening: linux_fd callback hygiene
  (2026-05-08 sessao 49)** -- `linux_fd_install_ops(NULL)` agora limpa
  o bundle de callbacks instalado em `linux_fd.c`, alinhando
  `linux_fd` ao padrao de reset dos demais modulos e evitando
  reutilizacao acidental de hooks antigos de `pipe2`/`dup3` em testes,
  reconfiguracao ou boot parcial. Regressions planejadas/revisadas:
  +2 cenarios em `test_linux_fd.c` cobrindo limpeza de callbacks de
  pipe e dup3. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x exec-ext hardening: linux_exec_ext callback hygiene
  (2026-05-08 sessao 50)** -- `linux_exec_ext_install_ops(NULL)` e
  `linux_exec_ext_reset_for_tests()` agora zeram o bundle de callbacks
  de `close_range`, em vez de apenas desligar o flag de instalado.
  Isso reduz risco de hooks stale em testes/reconfiguracao e deixa o
  modulo simetrico com o hardening aplicado em `linux_fd`. Regressions
  planejadas/revisadas: +2 cenarios em `test_linux_exec_ext.c`
  cobrindo limpeza dos callbacks `close_one` e `set_cloexec_one`.
  Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x fd core hardening: linux_dup callback hygiene
  (2026-05-08 sessao 51)** -- `linux_dup_install_ops(NULL)` e
  `linux_dup_reset_for_tests()` agora zeram o bundle de callbacks de
  `dup`/`dup2`, em vez de apenas desligar o flag de instalado. Isso
  alinha `linux_dup` com `linux_fd` e `linux_exec_ext`, reduzindo risco
  de hooks stale em testes/reconfiguracao. Regressions planejadas/
  revisadas: +2 cenarios em `test_linux_dup.c` cobrindo limpeza de
  callback `dup2` via install NULL e limpeza de callbacks via reset.
  Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x process hardening: linux_process callback hygiene
  (2026-05-08 sessao 52)** -- `linux_process_install_ops(NULL)` agora
  zera o bundle de callbacks de task accessors, alinhando o modulo com
  `linux_fd`, `linux_exec_ext` e `linux_dup`. Isso evita reuso
  acidental de accessors antigos em testes/reconfiguracao para
  `gettid`, `sched_yield` e `sched_*affinity`. Regressions planejadas/
  revisadas: +3 cenarios em `test_linux_process.c` cobrindo limpeza de
  callbacks de gettid, yield e affinity. Validacao desta sessao foi por
  revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x fd core hardening: linux_memfd provider hygiene
  (2026-05-08 sessao 53)** -- `linux_memfd_install_ops(NULL)` agora
  zera o provider `pid_exists`, impedindo que callbacks antigos
  continuem afetando `pidfd_open` e `pidfd_send_signal(sig=0)` apos
  desinstalacao/reconfiguracao. Regressions planejadas/revisadas: +2
  cenarios em `test_linux_memfd.c` cobrindo limpeza de provider para
  `pidfd_open` e probe `pidfd_send_signal`. Validacao desta sessao foi
  por revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x fd core hardening: linux_vfs provider hygiene
  (2026-05-08 sessao 54)** -- `linux_vfs_install_ops(NULL)` agora zera
  o bundle central de callbacks de file I/O (`open`, `close`, `read`,
  `write`, `lseek`). Isso impede reuso acidental de providers antigos
  na porta principal do VFS Linux durante testes/reconfiguracao e
  preserva fallbacks deterministas `-ENOSYS`. Regressions planejadas/
  revisadas: +4 cenarios em `test_linux_vfs.c` cobrindo limpeza de
  open, close, read/write e lseek. Validacao desta sessao foi por
  revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x TLS hardening: linux_arch_prctl provider hygiene
  (2026-05-08 sessao 55)** -- `linux_arch_prctl_install_ops(NULL)` e
  `linux_arch_prctl_reset_for_tests()` agora zeram o bundle de
  callbacks de MSR/TLS (`SET/GET_FS`, `SET/GET_GS`), alem de desligar o
  flag de instalado. Isso evita estado stale em reconfiguracao/testes
  no syscall mais critico do bootstrap musl TLS. Regressions planejadas/
  revisadas: +3 cenarios em `test_linux_arch_prctl.c` cobrindo limpeza
  de callbacks SET, GET e reset. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x memory hardening: linux_mmap provider hygiene
  (2026-05-08 sessao 56)** -- `linux_mmap_install_ops(NULL)` agora
  zera o bundle de callbacks do provider de memoria (`alloc_anon`,
  `alloc_anon_at`, `free_pages`, `protect`, `remap`). Isso evita reuso
  acidental de callbacks antigos nos caminhos criticos de `mmap`,
  `munmap`, `mprotect` e `mremap`, incluindo o padrao SpiderMonkey JIT
  `PROT_READ|WRITE|EXEC`. Regressions planejadas/revisadas: +4
  cenarios em `test_linux_mmap.c` cobrindo limpeza de callbacks de
  alloc, release/protect, remap e reset. Validacao desta sessao foi por
  revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x sync hardening: linux_futex provider hygiene
  (2026-05-08 sessao 57)** -- `linux_futex_install_ops(NULL)` agora
  zera o bundle de callbacks de sincronizacao (`atomic_load_u32`,
  `block_on`, `wake`). Isso evita reutilizacao acidental de callbacks
  antigos nos caminhos de pthread/mutex/condvar (`FUTEX_WAIT`, `WAKE` e
  `REQUEUE`) durante testes/reconfiguracao. Regressions planejadas/
  revisadas: +4 cenarios em `test_linux_futex.c` cobrindo limpeza de
  WAIT, WAKE, REQUEUE e reset. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x path hardening: linux_path provider hygiene
  (2026-05-08 sessao 58)** -- `linux_path_install(NULL)` e
  `linux_path_reset_for_tests()` agora zeram o provider
  `resolve_proc_self_exe`, impedindo reutilizacao acidental de resolvers
  antigos para `readlink("/proc/self/exe")` e
  `readlinkat(AT_FDCWD, "/proc/self/exe", ...)`. Isso estabiliza o
  caminho usado por musl, crash reporters, debuggers e profilers para
  descobrir o binario corrente. Regressions planejadas/revisadas: +3
  cenarios em `test_linux_path.c` cobrindo limpeza de readlink,
  readlinkat e reset. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x network hardening: linux_net provider hygiene
  (2026-05-08 sessao 59)** -- `linux_net_install_ops(NULL)` agora zera
  o bundle de callbacks de extensoes socket (`accept4`, `recvmmsg`,
  `sendmmsg`). Isso garante que o boot sem socket layer
  (`linux_net_init_boot` instala `NULL`) e reconfiguracoes/testes nao
  reutilizem callbacks antigos e preservem o fallback deterministico
  `-ENOSYS`. Regressions planejadas/revisadas: +4 cenarios em
  `test_linux_net.c` cobrindo limpeza de accept4, recvmmsg, sendmmsg e
  reset. Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x path-resolution hardening: linux_openat2 provider hygiene
  (2026-05-08 sessao 60)** -- `linux_openat2_install_ops(NULL)` e
  `linux_openat2_reset_for_tests()` agora zeram o bundle de callbacks de
  path-resolution endurecido (`openat2`, `faccessat2`), alem de desligar
  o flag de instalado. Isso evita reuso de providers antigos em caminhos
  de sandbox Firefox (`RESOLVE_BENEATH`, `RESOLVE_NO_SYMLINKS`) e probes
  bubblewrap `faccessat2(AT_EACCESS)`. Regressions planejadas/revisadas:
  +3 cenarios em `test_linux_openat2.c` cobrindo limpeza de openat2,
  faccessat2 e reset. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x process-VM hardening: linux_proc_vm provider hygiene
  (2026-05-08 sessao 61)** -- `linux_proc_vm_install_ops(NULL)` e
  `linux_proc_vm_reset_for_tests()` agora zeram o bundle de callbacks de
  introspeccao process-VM (`read_self`, `write_self`, `current_pid`),
  alem de desligar o flag de instalado. Isso impede reuso de providers
  antigos nos caminhos de profiler/debugger (`process_vm_readv`,
  `process_vm_writev`) e preserva os fallbacks self/foreign
  deterministas. Regressions planejadas/revisadas: +3 cenarios em
  `test_linux_proc_vm.c` cobrindo limpeza de readv/current_pid,
  writev/current_pid e reset. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x zero-copy hardening: linux_pipe_zero provider hygiene
  (2026-05-08 sessao 62)** -- `linux_pipe_zero_install_ops(NULL)` e
  `linux_pipe_zero_reset_for_tests()` agora zeram o bundle de callbacks
  de zero-copy (`splice`, `tee`, `vmsplice`), alem de desligar o flag de
  instalado. Isso evita reuso de providers antigos nos caminhos de cache/
  IPC de alta performance e preserva o fallback deterministico `-ENOSYS`
  para read/write ou writev. Regressions planejadas/revisadas: +4
  cenarios em `test_linux_pipe_zero.c` cobrindo limpeza de splice, tee,
  vmsplice e reset. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x resource-limit hardening: linux_rlimit_legacy provider hygiene
  (2026-05-08 sessao 63)** --
  `linux_rlimit_legacy_install_ops(NULL)` e
  `linux_rlimit_legacy_reset_for_tests()` agora zeram o bundle de
  callbacks de limites de recurso (`get_limit`, `set_limit`), alem de
  desligar o flag de instalado. Isso evita reuso de providers antigos nos
  probes de `RLIMIT_NOFILE`, `RLIMIT_STACK` e `RLIMIT_AS`, preservando
  defaults sinteticos e `setrlimit` no-op deterministico. Regressions
  planejadas/revisadas: +3 cenarios em `test_linux_rlimit_legacy.c`
  cobrindo limpeza de getrlimit, setrlimit e reset. Validacao desta
  sessao foi por revisao estatica de codigo e documentacao, sem `make`,
  `git` ou scripts.
  **NR count permanece 252**.
- **S3.x filesystem-stats hardening: linux_statfs provider hygiene
  (2026-05-08 sessao 64)** -- `linux_statfs_install_providers(NULL)` e
  `linux_statfs_reset_for_tests()` agora zeram o bundle de providers de
  estatisticas de filesystem (`total_blocks`, `total_files`), alem de
  desligar o flag de instalado. Isso impede reuso de providers antigos em
  checks de espaco livre do Firefox, SQLite WAL e probes GIO/gvfs,
  preservando o fallback tmpfs sintetico deterministico. Regressions
  planejadas/revisadas: +2 cenarios em `test_linux_statfs.c` cobrindo
  limpeza por install NULL em `statfs` e por reset em `fstatfs`.
  Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x system-info hardening: linux_sysinfo provider hygiene
  (2026-05-09 sessao 65)** -- `linux_sysinfo_install(NULL)` e
  `linux_sysinfo_reset_for_tests()` agora zeram o bundle de providers de
  informacao de sistema (`total_ram_bytes`, `free_ram_bytes`,
  `uptime_seconds`, `nproc`), alem de desligar o flag de instalado. Isso
  impede reuso de providers antigos em heuristicas de memoria/uptime do
  Firefox `nsSystemInfo`, musl `pthread_create` e crash-report metadata,
  preservando defaults deterministicos (`mem_unit=1`, `procs=1`, demais
  campos zero). Regressions planejadas/revisadas: +1 cenario novo de reset
  e reforco do cenario install NULL em `test_linux_sysinfo.c`, com
  contadores garantindo que callbacks antigos nao sejam invocados.
  Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x filesystem-mutation hardening: linux_fs_mut provider hygiene
  (2026-05-09 sessao 66)** -- `linux_fs_mut_install_ops(NULL)` e
  `linux_fs_mut_reset_for_tests()` agora zeram o bundle de callbacks de
  mutacao de filesystem (`mkdir`, `rmdir`, `unlink`, `rename`), alem de
  desligar o flag de instalado. Isso impede reuso de providers antigos em
  operacoes destrutivas de profile/cache, preservando o fallback
  deterministico `-ENOSYS` ate tmpfs/namei instalar hooks reais.
  Regressions planejadas/revisadas: cenario install NULL reforcado para
  cobrir todos os callbacks e +1 cenario de reset em
  `test_linux_fs_mut.c`. Validacao desta sessao foi por revisao estatica
  de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x filesystem-metadata hardening: linux_fs_meta provider hygiene
  (2026-05-09 sessao 67)** -- `linux_fs_meta_install_ops(NULL)` e
  `linux_fs_meta_reset_for_tests()` agora zeram o bundle de callbacks de
  metadados de filesystem (`chmod_path`, `chmod_fd`, `chown_path`,
  `chown_fd`), alem de desligar o flag de instalado. Isso impede reuso de
  providers antigos em lockdown de permissao de profile/cache e hardening
  de arquivos temporarios, preservando o fallback deterministico `-ENOSYS`
  ate tmpfs/namei instalar metadados reais. Regressions planejadas/
  revisadas: cenario install NULL reforcado para cobrir
  chmod/fchmod/chown/fchown e +1 cenario de reset em
  `test_linux_fs_meta.c`. Validacao desta sessao foi por revisao estatica
  de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x working-directory hardening: linux_chdir provider hygiene
  (2026-05-09 sessao 68)** -- `linux_chdir_install_ops(NULL)` e
  `linux_chdir_reset_for_tests()` agora zeram o bundle de callbacks de cwd
  (`chdir_path`, `chdir_fd`), alem de desligar o flag de instalado. Isso
  impede reuso de providers antigos em mudancas de diretorio de trabalho,
  preservando o fallback deterministico `-ENOSYS` ate o modelo per-task
  cwd e tmpfs/namei instalarem estado real. Regressions planejadas/
  revisadas: cenario install NULL reforcado para cobrir `chdir`/`fchdir`
  e +1 cenario de reset em `test_linux_chdir.c`. Validacao desta sessao
  foi por revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x sendfile hardening: linux_advise provider hygiene
  (2026-05-09 sessao 69)** -- `linux_advise_install_ops(NULL)` e
  `linux_advise_reset_for_tests()` agora zeram o bundle de callbacks de
  kernel-copy (`sendfile`), alem de desligar o flag de instalado. Isso
  impede reuso de providers antigos no caminho de copia de dados e
  preserva o fallback deterministico `-ENOSYS` para read/write em userland
  quando nao ha backend real. Regressions planejadas/revisadas: +2
  cenarios em `test_linux_advise.c` cobrindo install NULL e reset com
  preservacao de offset e sem chamada ao callback antigo. Validacao desta
  sessao foi por revisao estatica de codigo e documentacao, sem `make`,
  `git` ou scripts.
  **NR count permanece 252**.
- **S3.x legacy-time hardening: linux_time_legacy provider hygiene
  (2026-05-09 sessao 70)** --
  `linux_time_legacy_install_ops(NULL)` e
  `linux_time_legacy_reset_for_tests()` agora zeram o bundle de callbacks
  de tempo legado (`now_seconds`), alem de desligar o flag de instalado.
  Isso impede reuso de providers antigos em `time(2)` e preserva fallback
  deterministico para epoch `0` quando nao ha provider real; `getcpu`
  continua single-CPU `0/0`. Regressions planejadas/revisadas: reforco de
  contadores em `test_linux_time_legacy.c` e +1 cenario de reset garantindo
  que callbacks antigos nao sejam invocados. Validacao desta sessao foi
  por revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x sandbox hardening: linux_sandbox provider hygiene
  (2026-05-09 sessao 71)** -- `linux_sandbox_install_ops(NULL)` e
  `linux_sandbox_reset_for_tests()` agora zeram o bundle de callbacks de
  sandbox (`chroot`), alem de desligar o flag de instalado. Isso impede
  reuso de provider antigo em superficie de sandbox/chroot e preserva o
  fallback seguro de single-root no-op para caminhos bem formados quando
  nao ha provider real. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_sandbox.c` cobrindo install NULL e reset sem chamada ao
  callback antigo. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x time-setter hardening: linux_settod provider hygiene
  (2026-05-09 sessao 72)** -- `linux_settod_install_ops(NULL)` e
  `linux_settod_reset_for_tests()` agora zeram o bundle de callbacks de
  time-of-day setter (`set_seconds`), alem de desligar o flag de instalado.
  Isso impede reuso de provider antigo em mutacao de wall-clock/
  CAP_SYS_TIME e preserva fallback no-op `0` para chamadas bem formadas
  quando nao ha RTC real. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_settod.c` cobrindo install NULL e reset sem chamada ao
  provider antigo. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x link hardening: linux_link provider hygiene
  (2026-05-09 sessao 73)** -- `linux_link_install_ops(NULL)` e
  `linux_link_reset_for_tests()` agora zeram o bundle de callbacks de
  hard/soft-link (`hard_link`, `sym_link`), alem de desligar o flag de
  instalado. Isso impede reuso de provider antigo em mutacoes de link
  usadas por cache/update patterns e preserva fallback deterministico
  `-ENOSYS` ate tmpfs/namei instalar links reais. Regressions planejadas/
  revisadas: cenario install NULL reforcado para cobrir `link`/`symlink`
  e +1 cenario de reset em `test_linux_link.c`. Validacao desta sessao foi
  por revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x timestamp hardening: linux_utime provider hygiene
  (2026-05-09 sessao 74)** -- `linux_utime_install_ops(NULL)` e
  `linux_utime_reset_for_tests()` agora zeram o bundle de callbacks de
  timestamp (`utime_path`, `utime_fd`, `now`), alem de desligar o flag de
  instalado. Isso impede reuso de provider antigo em updates de metadados
  temporais usados por cache HTTP, build tooling e compatibilidade libc, e
  preserva fallback deterministico `-ENOSYS` ate tmpfs/namei instalar
  timestamps reais. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_utime.c` cobrindo install NULL e reset para path/fd,
  incluindo garantia de que a fonte `UTIME_NOW` antiga nao e' chamada.
  Validacao desta sessao foi por revisao estatica de codigo e documentacao,
  sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x interval-timer hardening: linux_itimer provider hygiene
  (2026-05-09 sessao 75)** -- `linux_itimer_install_ops(NULL)` e
  `linux_itimer_reset_for_tests()` agora zeram o bundle de callbacks de
  ticks (`now_ticks`), alem de desligar o flag de instalado e resetar o
  estado storage-only de alarm/itimers. Isso impede reuso de provider
  antigo em `times(2)` e preserva fallback deterministico para tick `0`
  enquanto per-task CPU accounting real nao existe. Regressions planejadas/
  revisadas: reforco de contadores em `test_linux_itimer.c` e +2 cenarios
  cobrindo install NULL e reset sem chamada ao callback antigo. Validacao
  desta sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x copy-range hardening: linux_lock provider hygiene
  (2026-05-09 sessao 76)** -- `linux_lock_install_ops(NULL)` e
  `linux_lock_reset_for_tests()` agora zeram o bundle de callbacks de
  kernel-copy (`copy_file_range`), alem de desligar o flag de instalado;
  `reset_for_tests()` preserva o reset da tabela storage-only de flock.
  Isso impede reuso de provider antigo em caminho de copia eficiente e
  preserva fallback deterministico `-ENOSYS` para read/write em userland
  quando nao ha backend real. Regressions planejadas/revisadas: +2
  cenarios em `test_linux_lock.c` cobrindo install NULL e reset sem chamada
  ao callback antigo. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x capability hardening: linux_caps provider hygiene
  (2026-05-09 sessao 77)** -- `linux_caps_install_ops(NULL)` e
  `linux_caps_reset_for_tests()` agora zeram o bundle de callbacks de
  capabilities (`get_caps`, `set_caps`), alem de desligar o flag de
  instalado. Isso impede reuso de provider antigo em uma superficie direta
  de seguranca usada por sandbox Firefox/bubblewrap e preserva fallback
  deterministico root-with-all-caps/no-op set enquanto credenciais por
  tarefa nao existem. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_caps.c` cobrindo install NULL e reset sem chamada aos
  callbacks antigos. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x signal hardening: linux_kill provider hygiene
  (2026-05-09 sessao 78)** -- `linux_kill_install_ops(NULL)` e
  `linux_kill_reset_for_tests()` agora zeram o bundle de callbacks de
  entrega de sinais (`getpid`, `deliver`), alem de desligar o flag de
  instalado. Isso impede reuso de provider antigo em self-signal/
  signal-delivery e preserva fallback deterministico single-task/no-op para
  `kill`, `tgkill` e `tkill` quando o backend real de sinais nao esta
  instalado. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_kill.c` cobrindo install NULL e reset sem chamada ao callback
  antigo. Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x durability hardening: linux_sync provider hygiene
  (2026-05-09 sessao 79)** -- `linux_sync_install_ops(NULL)` e
  `linux_sync_reset_for_tests()` agora zeram o bundle de callbacks de
  durabilidade (`sync_all`, `sync_fs`, `sync_fd`), alem de desligar o flag
  de instalado. Isso impede reuso de provider antigo em flush/fsync paths
  de SQLite/cache Firefox e preserva fallback deterministico RAM-only no-op
  quando nao ha backend persistente real. Regressions planejadas/revisadas:
  +2 cenarios em `test_linux_sync.c` cobrindo install NULL e reset para
  `sync`, `syncfs`, `fsync` e `fdatasync` sem chamada ao callback antigo.
  Validacao desta sessao foi por revisao estatica de codigo e documentacao,
  sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x heap hardening: linux_brk provider hygiene
  (2026-05-09 sessao 80)** -- `linux_brk_install_ops(NULL)` e
  `linux_brk_reset_for_tests()` agora zeram o bundle de callbacks de
  reserva de heap (`reserve_pages`), alem de desligar o flag de instalado e
  preservar o reset do break/committed para `LINUX_BRK_BASE`. Isso impede
  reuso de provider antigo em grow de heap e preserva a semantica Linux de
  falha de `brk`: retornar o break atual sem chamar VMM antigo quando nao
  ha provider. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_brk.c` cobrindo install NULL e reset sem chamada ao callback
  antigo. Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x truncate hardening: linux_trunc provider hygiene
  (2026-05-09 sessao 81)** -- `linux_trunc_install_ops(NULL)` e
  `linux_trunc_reset_for_tests()` agora zeram o bundle de callbacks de
  resize por fd (`ftruncate`), alem de desligar o flag de instalado. Isso
  impede reuso de provider antigo em resizing de arquivos e preserva
  fallback deterministico `-ENOSYS` ate tmpfs/VFS instalar resize real por
  fd. Regressions planejadas/revisadas: reforco do cenario install NULL
  para garantir zero chamadas antigas e +1 cenario reset em
  `test_linux_trunc.c`. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x eventfd hardening: linux_eventfd provider hygiene
  (2026-05-09 sessao 82)** -- `linux_eventfd_install_ops(NULL)` agora
  zera o bundle de callbacks de alocacao de fd (`alloc_fd`) em vez de
  manter um alocador antigo. Isso impede reuso de provider stale no caminho
  de criacao de `eventfd`/`eventfd2` e preserva fallback deterministico
  para fds baseados em slot (`LINUX_EVENTFD_FD_BASE + slot`) quando nao ha
  fd table real instalada. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_eventfd.c` cobrindo install NULL e reset sem chamada ao
  callback antigo. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x seccomp hardening: linux_seccomp provider hygiene
  (2026-05-09 sessao 83)** -- `linux_seccomp_install_ops(NULL)` e
  `linux_seccomp_reset_for_tests()` agora zeram o bundle de callbacks de
  filtro (`install_filter`), alem de desligar o flag de instalado. Isso
  impede reuso de provider antigo em `SECCOMP_SET_MODE_FILTER`, superficie
  direta do sandbox Firefox/Chromium, e preserva fallback estrutural de
  aceitar e descartar o filtro enquanto nao existe backend BPF real.
  Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_seccomp.c` cobrindo install NULL e reset sem chamada ao
  callback antigo. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x epoll hardening: linux_epoll provider hygiene
  (2026-05-09 sessao 84)** -- `linux_epoll_install_ops(NULL)` agora zera o
  bundle de callbacks de wait (`fd_ready`, `yield`) em vez de preservar
  callbacks antigos. Isso impede reuso de readiness/yield provider stale em
  `epoll_wait`/`epoll_pwait`, caminho central para libevent/Chromium IPC, e
  preserva fallback deterministico de zero eventos quando nao ha readiness
  oracle instalado. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_epoll.c` cobrindo install NULL e reset sem chamada aos
  callbacks antigos. Validacao desta sessao foi por revisao estatica de
  codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x process-lifecycle hardening: linux_exit provider hygiene
  (2026-05-09 sessao 85)** -- `linux_exit_install_ops(NULL)` e
  `linux_exit_reset_for_tests()` agora zeram o bundle de callbacks de
  terminacao de tarefa (`exit_task`), alem de desligar o flag de instalado.
  Isso impede reuso de provider antigo no caminho noreturn de `exit` e
  `exit_group`, preservando fallback sentinela `-ENOSYS` em tests quando
  nao ha backend de task lifecycle instalado. Regressions planejadas/
  revisadas: +2 cenarios em `test_linux_exit.c` cobrindo install NULL e
  reset sem chamada ao callback antigo. Validacao desta sessao foi por
  revisao estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x process-group hardening: linux_pgrp provider hygiene
  (2026-05-09 sessao 86)** -- `linux_pgrp_install_ops(NULL)` e
  `linux_pgrp_reset_for_tests()` agora zeram o bundle de callbacks de pid
  (`getpid`), alem de desligar o flag de instalado. Isso impede reuso de
  provider antigo nos caminhos de grupo/sessao de processo (`setpgid`,
  `getpgid`, `setsid`, `getsid`) e preserva fallback single-task
  deterministico com pid/sid/pgid default 1 quando nao ha task provider
  instalado. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_pgrp.c` cobrindo install NULL e reset sem chamada ao callback
  antigo. Validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x entropy-source hardening: linux_random source hygiene
  (2026-05-09 sessao 87)** -- `linux_random_install_source(NULL)` agora tem
  regression guard explicito garantindo limpeza da fonte de entropia
  instalada e fallback `-EAGAIN` em `getrandom` sem chamada a fonte antiga.
  Isso protege uma superficie direta de seguranca/criptografia usada por
  NSS, SpiderMonkey seed/GC e geracao de tokens, preservando comportamento
  deterministico quando o CSPRNG ainda nao foi instalado no boot/test setup.
  Regressions planejadas/revisadas: +1 cenario em `test_linux_random.c`
  cobrindo install NULL da source. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x epoll readiness refinement: eventfd/timerfd readiness
  (2026-05-09 sessao 88)** -- `linux_epoll_init_boot()` agora instala
  `linux_eventfd_family_poll_events()` como provider real de `fd_ready`.
  Com isso, `epoll_wait`/`epoll_pwait` passam a observar `EPOLLIN` quando
  um `eventfd` tem counter > 0 e quando um `timerfd` armado expira;
  `eventfd` tambem reporta `EPOLLOUT` enquanto uma escrita de 1 nao causaria
  overflow. `signalfd` continua storage-only e retorna sem readiness ate
  existir signal delivery real; outras classes de fd preservam fallback
  zero-event. Regressions planejadas/revisadas: +2 cenarios em
  `test_linux_epoll.c` cobrindo readiness de eventfd e timerfd. Validacao
  desta sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x epoll readiness contract coverage: eventfd EPOLLOUT/drain
  (2026-05-09 sessao 89)** -- cobertura host de `epoll` agora fixa dois
  contratos complementares de `linux_eventfd_family_poll_events()`: eventfd
  gravavel gera `EPOLLOUT`, e eventfd com `EPOLLIN` deixa de ficar ready
  apos `linux_eventfd_read()` drenar o counter em modo normal. Isso reduz
  risco de regressao em loops async estilo libevent/Firefox que dependem de
  writability e de re-arm natural apos consumo. Regressions planejadas/
  revisadas: +2 cenarios em `test_linux_epoll.c`; validacao desta sessao foi
  por revisao estatica de codigo e documentacao, sem `make`, `git` ou
  scripts.
  **NR count permanece 252**.
- **S3.x epoll timerfd contract coverage: one-shot drain/periodic rearm
  (2026-05-09 sessao 90)** -- cobertura host de `epoll` agora fixa dois
  contratos de timerfd sobre `linux_eventfd_family_poll_events()`: timerfd
  one-shot deixa de gerar `EPOLLIN` depois de `linux_timerfd_read()` consumir
  a expiracao e desarmar o timer, enquanto timerfd periodico reprograma a
  proxima expiracao e volta a gerar `EPOLLIN` quando o clock alcanca o novo
  deadline. Isso reforca compatibilidade com runtimes que usam timerfd como
  fonte de wakeup em loops async. Regressions planejadas/revisadas: +2
  cenarios em `test_linux_epoll.c`; validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x eventfd poll oracle coverage: direct provider contract
  (2026-05-09 sessao 91)** -- `test_linux_eventfd.c` agora fixa diretamente
  o contrato de `linux_eventfd_family_poll_events()`, alem da cobertura
  indireta via `epoll`: eventfd vazio reporta `EPOLLOUT`, eventfd com counter
  reporta `EPOLLIN|EPOLLOUT`, leitura normal drena `EPOLLIN`, signalfd
  storage-only permanece sem readiness e timerfd one-shot passa de sem
  readiness para `EPOLLIN` ao expirar e volta a zero depois do read/disarm.
  Regressions planejadas/revisadas: +3 cenarios em `test_linux_eventfd.c`;
  validacao desta sessao foi por revisao estatica de codigo e documentacao,
  sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x eventfd poll oracle edge coverage: saturation/semaphore
  (2026-05-09 sessao 92)** -- cobertura direta do provider agora fixa os
  cantos de readiness que evitam loops async incorretos: eventfd saturado em
  `UINT64_MAX-1` continua readable (`EPOLLIN`) mas nao writable
  (`EPOLLOUT`), e eventfd em modo `EFD_SEMAPHORE` mantem `EPOLLIN` apos a
  primeira leitura quando ainda ha unidades no counter, limpando apenas
  quando a ultima unidade e consumida. Regressions planejadas/revisadas: +2
  cenarios em `test_linux_eventfd.c`; validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x pipe readiness for epoll
  (2026-05-09 sessao 93)** -- `pipe_poll_events()` agora expoe readiness
  generica no core de pipe, sem acoplar `pipe.c` diretamente ao Linux ABI
  shim. `linux_epoll_init_boot()` converte esses bits para `EPOLLIN`/
  `EPOLLOUT`/`EPOLLERR`/`EPOLLHUP` depois do oracle
  `linux_eventfd_family_poll_events()`, permitindo que loops async observem
  pipes legiveis/gravaveis junto de eventfd/timerfd. Regressions planejadas/
  revisadas: +2 em `test_pipe.c` para read/write/full/HUP e +2 em
  `test_linux_epoll.c` para `EPOLLIN`/`EPOLLOUT` via epoll. Validacao desta
  sessao foi por revisao estatica de codigo e documentacao, sem `make`,
  `git` ou scripts.
  **NR count permanece 252**.
- **S3.x epoll pipe edge readiness
  (2026-05-09 sessao 94)** -- `epoll_wait` agora propaga `EPOLLERR` e
  `EPOLLHUP` reportados pelo provider mesmo quando esses bits nao estao no
  mask registrado, alinhando o contrato visivel com Linux e reduzindo risco
  de loops async presos em pipes fechados. A cobertura host tambem fixa que
  write end de pipe cheio nao gera `EPOLLOUT`, read end com writer fechado
  gera `EPOLLHUP`, e write end com reader fechado gera `EPOLLERR`.
  Regressions planejadas/revisadas: +3 em `test_linux_epoll.c`; validacao
  desta sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x epoll pipe ONESHOT edge coverage
  (2026-05-09 sessao 95)** -- cobertura host agora fixa que `EPOLLHUP` e
  `EPOLLERR` reportados pelo provider nao reativam entradas ja desarmadas por
  `EPOLLONESHOT`. Isso mantem a compatibilidade Linux-like de ERR/HUP
  unmasked sem criar wakeups espurios depois do primeiro disparo.
  Regressions planejadas/revisadas: +2 em `test_linux_epoll.c`; validacao
  desta sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x known-path access family refinement: access/
  faccessat (2026-05-08 sessao 40)** -- alinhamento dos probes de
  existencia/permissao com a familia stat refinada na sessao 39.
  `linux_at.c` agora usa `linux_stat_path_is_known()` como fonte
  unica do known pseudo path set para `access`/`faccessat`: dirs
  (`/`, `/dev`, `/dev/shm`, `/proc`,
  `/proc/self`, `/tmp`), char devices
  (`/dev/{null,zero,full,random,urandom}`) e fixed proc files
  (`/proc/{cpuinfo,meminfo,version,uptime,loadavg}` +
  `/proc/self/{maps,exe,cmdline,status}`). Qualquer modo R|W|X|F
  retorna 0 para esses paths (Marco M1 effective-root); unknown
  paths continuam -ENOENT ate namei real. Regressions planejadas/
  revisadas: +2 cenarios de `access` e +2 de `faccessat` em
  `test_linux_at.c`. Validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git` ou scripts.
  **NR count permanece 252**.
- **S3.x known-path stat family refinement: stat/lstat/
  fstatat/statx (2026-05-08 sessao 39)** -- refinamento
  conservador da familia de metadata path-based sem introduzir
  namei real. `linux_stat.c` agora reconhece dirs (`/`, `/dev`,
  `/dev/shm`, `/proc`, `/proc/self`, `/tmp`), char devices
  (`/dev/{null,zero,full,random,urandom}`) e fixed proc files
  (`/proc/{cpuinfo,meminfo,version,uptime,loadavg}` +
  `/proc/self/{maps,exe,cmdline,status}`). `lstat("/proc/self/exe")`
  retorna `S_IFLNK`; `stat("/proc/self/exe")` segue para `S_IFREG`.
  `fstatat(AT_FDCWD, path, ...)` agora delega para `linux_stat` ou
  `linux_lstat` conforme `AT_SYMLINK_NOFOLLOW`; `statx(AT_FDCWD,
  path, ...)` projeta a mesma `struct linux_stat` sintetica para
  `struct linux_statx` e tambem respeita `AT_SYMLINK_NOFOLLOW`.
  Paths desconhecidos continuam `-ENOSYS` para preservar fallback
  open+fstat ate existir namei real.
  Regressions planejadas/revisadas: 7 cenarios em
  `test_linux_stat.c`, 3 em `test_linux_at.c`, 2 em
  `test_linux_statx.c`. Validacao desta
  sessao foi por revisao estatica de codigo e documentacao, sem
  `make`, `git` ou scripts conforme instrucao do operador.
  **NR count permanece 252**.
- **S3.x legacy pipe completion: pipe (NR 22)
  (2026-05-08 sessao 38)** -- fechamento pontual da unica
  pendencia visivel na tabela Marco M1 em `COMPAT.md`.
  `linux_fd.c` agora expoe `linux_pipe(fds)` como wrapper
  direto de `linux_pipe2(fds, 0)`, mantendo single source of
  truth para criacao de pipes, erro `-EFAULT` para ponteiro
  NULL, `-ENOSYS` quando nao ha ops instaladas e `-EMFILE`
  quando o backend nao consegue alocar o par. O syscall table
  registra `LINUX_NR_pipe` junto de `pipe2`/`dup3`, e
  `test_linux_syscall.c` spot-checka o registro de NR 22.
  `tests/test_linux_fd.c` ganhou 3 regressions host-testaveis
  (wrapper basico, NULL fds, no ops), elevando a cobertura local
  de 14 para 17 checks planejados. Validacao desta sessao foi por
  revisao estatica de codigo, sem `make`, `git` ou script,
  conforme instrucao do operador. **NR count permanece 252**.
- **S3.x seccomp/BPF/ptrace + fanotify + io_uring
  + modern misc: seccomp/bpf/ptrace/
  fanotify_init/fanotify_mark/io_uring_setup/
  io_uring_enter/io_uring_register/futex_waitv/
  clock_adjtime/memfd_secret (2026-05-07 sessao
  37)** -- 11 NRs novos via 4 modulos novos
  cobrindo quatro nichos hot-path para userland
  Linux: (a) seccomp/BPF/ptrace (Firefox content
  sandbox installs seccomp BPF filter via
  seccomp(SECCOMP_SET_MODE_FILTER) antes de
  exec'ing renderer -- com -ENOSYS sandbox
  layer fail-closed; Chromium-derived sandbox
  usa bpf(BPF_PROG_LOAD) para compile filter to
  bytecode -- com -ENOSYS forca classic BPF
  interpretation slow; crash reporters usam
  ptrace(PTRACE_ATTACH) para core dump -- com
  -ENOSYS desabilita crash collection),
  (b) fanotify (snap-packaged Firefox usa
  fanotify para monitor cache/profile dirs;
  auditd/file-integrity monitors usam
  fanotify_mark -- com -ENOSYS cai em inotify
  fallback que ja' temos), (c) io_uring (Firefox
  necko HTTP/3 stack probes io_uring para opt
  into kernel-side completion-driven I/O em
  Linux 5.5+; SpiderMonkey IOUringJobBackend
  experimental probes -- com -ENOSYS cai em
  epoll+nonblocking read/write que e' the
  existing path), (d) modern misc (musl 1.2.4+
  pthread mutex/cond pode tentar futex_waitv
  para multi-futex wait -- com -ENOSYS cai em
  single-futex FUTEX_WAIT loop que ja' temos;
  chrony/timesyncd usa clock_adjtime para slew
  RTC; libsecret 5.14+ usa memfd_secret para
  credentials buffer que kernel scrubs from
  page cache).
  (1) **`linux_seccomp.c` (~145 linhas)** -- 3
  NRs: seccomp (NR 317), bpf (NR 321), ptrace
  (NR 101). seccomp: 4-op switch.
  SET_MODE_STRICT (op=0): flags=0 e args=NULL
  obrigatorios (-EINVAL otherwise).
  SET_MODE_FILTER (op=1): flags whitelist (6
  bits TSYNC/LOG/SPEC_ALLOW/NEW_LISTENER/
  TSYNC_ESRCH/WAIT_KILL); NULL args -> -EFAULT;
  provider-injectable via
  linux_seccomp_install_ops({.install_filter}).
  GET_ACTION_AVAIL (op=2): valida 8 SECCOMP_RET_*
  actions (KILL_PROCESS=0x80000000/KILL_THREAD/
  TRAP=0x30000/ERRNO=0x50000/USER_NOTIF=
  0x7FC00000/TRACE=0x7FF00000/LOG=0x7FFC0000/
  ALLOW=0x7FFF0000); unknown action ->
  -EOPNOTSUPP. GET_NOTIF_SIZES (op=3): escreve
  3 u16 com Linux x86_64 sizes (notif=80,
  resp=24, data=16). bpf: cmd<0 ou >=
  LINUX_BPF_CMD_MAX (40) -> -EINVAL; size>0
  NULL attr -> -EFAULT; -ENOSYS default para
  userland classic BPF fallback. ptrace:
  request<0 -> -EINVAL; TRACEME (request 0)
  -> 0 sem pid check; outros pid<0 -> -EINVAL;
  foreign pid (!=0) -> -ESRCH; pid==0 (self) ->
  -EPERM (Linux: cannot ptrace self per
  kernel/ptrace.c).
  (2) **`linux_fanotify.c` (~85 linhas)** -- 2
  NRs: fanotify_init (NR 300), fanotify_mark
  (NR 301). 8-slot fd table (FD_BASE 0xC000,
  entre uffd e proc no fd encoding scheme).
  init: flags whitelist (13 bits CLOEXEC/
  NONBLOCK/CLASS_CONTENT/CLASS_PRE_CONTENT/
  UNLIMITED_QUEUE/UNLIMITED_MARKS/ENABLE_AUDIT/
  REPORT_PIDFD/REPORT_TID/REPORT_FID/
  REPORT_DIR_FID/REPORT_NAME/REPORT_TARGET_FID);
  event_f_flags accept-as-is (kernel valida
  internamente); table exhaustion -> -ENFILE;
  Linux requires CAP_SYS_ADMIN -> root has it
  implicitly. mark: invalid fd -> -EBADF; flags
  whitelist (11 bits ADD/REMOVE/DONT_FOLLOW/
  ONLYDIR/MOUNT/IGNORED_MASK/IGNORED_SURV_MODIFY/
  FLUSH/FILESYSTEM/EVICTABLE/IGNORE); ADD |
  REMOVE mutex -> -EINVAL; FLUSH | ADD/REMOVE
  -> -EINVAL; sem nenhum -> -EINVAL; ADD/
  REMOVE com NULL pathname -> -EFAULT; FLUSH
  ignora pathname (NULL ok per Linux fs/notify/
  fanotify/fanotify_user.c).
  (3) **`linux_io_uring.c` (~80 linhas)** -- 3
  NRs: io_uring_setup (NR 425), io_uring_enter
  (NR 426), io_uring_register (NR 427). setup:
  entries=0 -> -EINVAL; > IORING_MAX_ENTRIES
  (4096) -> -EINVAL; non-power-of-two ->
  -EINVAL; NULL params -> -EFAULT; well-formed
  -> -ENOSYS (Marco M1 no async backend ->
  userland epoll fallback). enter: fd<0 ->
  -EBADF; flags whitelist (5 bits GETEVENTS/
  SQ_WAKEUP/SQ_WAIT/EXT_ARG/REGISTERED_RING);
  sig non-NULL com sigsz != 8 -> -EINVAL
  (kernel-mask size on x86_64); -ENOSYS.
  register: fd<0 -> -EBADF; opcode [0, 32)
  whitelist; nr_args>0 NULL arg -> -EFAULT;
  -ENOSYS.
  (4) **`linux_modern_misc.c` (~85 linhas)** --
  3 NRs: futex_waitv (NR 449), clock_adjtime
  (NR 305), memfd_secret (NR 447). futex_waitv:
  flags!=0 -> -EINVAL; nr_futexes=0 -> -EINVAL;
  > FUTEX_WAITV_MAX (128) -> -EINVAL; NULL
  waiters -> -EFAULT; clockid != REALTIME (0)/
  MONOTONIC (1) -> -EINVAL; well-formed ->
  -ENOSYS (musl pthread fallback to single-
  futex FUTEX_WAIT que ja' temos wired em
  linux_futex). clock_adjtime: clk_id<0 ->
  -EINVAL; NULL buf -> -EFAULT; modes & ~0xFFF
  (upper 4 bits forbidden) -> -EINVAL; valid
  read/write-as-no-op -> TIME_OK (0; Linux
  leap-second status that maps to OK).
  memfd_secret: flags whitelist (CLOEXEC=0x1);
  8-slot fd table (FD_BASE 0xD000, entre
  landlock e ?); Linux requires CAP_IPC_LOCK
  (root has it); table exhaustion -> -ENFILE.
  **63 host asserts novos** (19 seccomp + 14
  fanotify + 15 io_uring + 15 modern_misc).
  Compile-only check com `cc -fsyntax-only
  -DUNIT_TEST -Iinclude -Werror` cobrindo
  todos os 9 modulos vizinhos passou clean.
  Compile real (binario host) tambem clean.
  Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (4 .o + 4
  TEST_SRCS), test_runner (4 decls + 4 calls),
  linux_syscall.c (4 weak hooks + 4 register
  calls).
  **NR count: 241 -> 252** (sessao 22..37: 92
  -> 99 -> 105 -> 110 -> 113 -> 121 -> 131 ->
  145 -> 160 -> 172 -> 189 -> 199 -> 210 ->
  219 -> 230 -> 241 -> 252).
  COMPAT.md: 11 NRs novos; assert count 1058/
  1058 em 65 -> 1121/1121 em 69 suites.
- **S3.x process VM + hardened path resolution
  + memory protection keys + Landlock sandbox:
  process_vm_readv/process_vm_writev/kcmp/
  openat2/faccessat2/pkey_alloc/pkey_free/
  pkey_mprotect/landlock_create_ruleset/
  landlock_add_rule/landlock_restrict_self
  (2026-05-07 sessao 36)** -- 11 NRs novos via
  4 modulos novos cobrindo quatro nichos hot-
  path para userland Linux: (a) process VM +
  kcmp (Firefox profiler usa process_vm_readv
  para sample stacks de outros threads sem
  pausing target -- com -ENOSYS cai em
  ptrace(PTRACE_GETREGS) que e' slower e
  observable from target; Chromium-derived
  sandboxes usam kcmp(KCMP_FILE) para detect
  fd identity em IPC permission grants;
  GDB-style debuggers usam process_vm_writev
  para inject breakpoints), (b) hardened path
  resolution (Firefox content sandbox usa
  openat2 com RESOLVE_BENEATH | RESOLVE_NO_
  SYMLINKS para safely abrir profile files
  sem traversing symlinks fora -- com -ENOSYS
  cai em realpath()+stat() com TOCTOU window;
  bubblewrap usa faccessat2 com AT_EACCESS),
  (c) memory protection keys (SpiderMonkey
  W^X JIT usa pkey_mprotect para flip code
  buffer entre RW e RX via thread-local PKRU
  register sem TLB shootdown -- com -ENOSYS
  cai em mprotect-based dual mappings que e'
  slower; libsecret usa pkey_alloc para mark
  credentials buffer needing explicit access
  enable), (d) Landlock sandbox (modern
  Firefox content sandbox em Linux 5.13+ usa
  landlock_create_ruleset + add_rule +
  restrict_self para hardened userland
  sandboxing -- com -ENOSYS cai em seccomp-
  only sandbox que e' functional mas weaker;
  bubblewrap probes durante init).
  (1) **`linux_proc_vm.c` (~125 linhas)** -- 3
  NRs: process_vm_readv (NR 310),
  process_vm_writev (NR 311), kcmp (NR 312).
  Common validation: pid<0 -> -EINVAL; flags!=0
  -> -EINVAL; iovcnt > IOV_MAX (1024) ->
  -EINVAL; iovcnt>0 com NULL iov -> -EFAULT.
  Self peer detection: pid==0 ou pid==
  current_pid() (provider-injectable hook via
  linux_proc_vm_install_ops); foreign pid em
  readv -> -ESRCH (Linux), em writev ->
  -EPERM (Linux requires CAP_SYS_PTRACE para
  cross-process write). Self peers delegam a
  read_self/write_self callbacks; default
  behaviour sem provider e' 0 bytes (success
  but empty). kcmp: pid<0 -> -EINVAL; type
  whitelist (FILE/VM/FILES/FS/SIGHAND/IO/
  SYSVSEM/EPOLL_TFD = 0..7); KCMP_FILE compara
  fds structurally (idx1 == idx2 -> 0 equal,
  else 1); outros types comparam pid1 vs pid2
  (Marco M1 single-task -> equal pids -> 0).
  (2) **`linux_openat2.c` (~95 linhas)** -- 2
  NRs: openat2 (NR 437), faccessat2 (NR 439).
  openat2 validation: NULL/empty path ->
  -EFAULT/-ENOENT; NULL how -> -EFAULT; size <
  LINUX_OPEN_HOW_SIZE_VER0 (24) -> -EINVAL
  (Linux: kernel zero-extends from `size` to
  its understanding); resolve flag whitelist
  (NO_XDEV=0x01/NO_MAGICLINKS=0x02/NO_SYMLINKS
  =0x04/BENEATH=0x08/IN_ROOT=0x10/CACHED=0x20
  = 0x3F); BENEATH | IN_ROOT mutually exclusive
  -> -EINVAL (Linux fs/open.c rule); dirfd !=
  AT_FDCWD && < 0 -> -EBADF. Provider
  injection via linux_openat2_install_ops
  ({.openat, .faccessat}); openat2 default
  -ENOSYS para deterministic Firefox sandbox
  probe. faccessat2: NULL path sem
  AT_EMPTY_PATH -> -EFAULT; empty path sem
  AT_EMPTY_PATH -> -ENOENT; mode whitelist
  (R_OK=4|W_OK=2|X_OK=1|F_OK=0 = 0x07); flags
  whitelist (AT_EACCESS=0x200|AT_SYMLINK_
  NOFOLLOW=0x100|AT_EMPTY_PATH=0x1000 =
  0x1300); default 0 success (Marco M1 root
  single-user).
  (3) **`linux_pkey.c` (~95 linhas)** -- 3
  NRs: pkey_alloc (NR 330), pkey_free (NR 331),
  pkey_mprotect (NR 329). 16-slot pkey table
  com keys 0/1 pre-reserved (Linux convention
  -- key 0 = default, key 1 = execute-only).
  pkey_alloc: flags != 0 -> -EINVAL;
  access_rights whitelist (DISABLE_ACCESS=0x1|
  DISABLE_WRITE=0x2 = 0x03); search starts at
  LINUX_PKEY_MIN_USER=2; table exhaustion ->
  -ENOSPC. pkey_free: pkey<2 ou >=16 ->
  -EINVAL; unallocated pkey -> -EINVAL.
  pkey_mprotect: addr & 0xFFF -> -EINVAL
  (page-aligned 4 KiB); len=0 -> 0 no-op (Linux
  semantic); prot whitelist (READ|WRITE|EXEC =
  0x07); pkey=-1 ok (default key per Linux);
  pkey out of range -> -EINVAL; unallocated
  pkey -> -EINVAL. Marco M1 cooperative single-
  thread sem real PKRU programming -> validation
  only mas Linux fast path JIT preservado.
  (4) **`linux_landlock.c` (~120 linhas)** -- 3
  NRs: landlock_create_ruleset (NR 444),
  landlock_add_rule (NR 445),
  landlock_restrict_self (NR 446). 16-slot
  ruleset fd table (FD_BASE 0xB000); ABI
  version 4 (Linux 6.10).
  landlock_create_ruleset VERSION query: flags
  = LANDLOCK_CREATE_RULESET_VERSION (0x1) +
  attr=NULL + size=0 -> ABI version (Linux
  probe pattern userland uses to detect
  support); attr non-NULL com VERSION ->
  -EINVAL; flags com VERSION | other ->
  -EINVAL. Real ruleset creation: flags!=0
  -> -EINVAL; NULL attr -> -EFAULT; size<16
  (LINUX_LANDLOCK_RULESET_ATTR_MIN_SIZE) ->
  -EINVAL; handled_access_fs whitelist (15
  bits 0..14 incluindo EXECUTE/WRITE_FILE/
  READ_FILE/READ_DIR/REMOVE_DIR/REMOVE_FILE/
  MAKE_CHAR/MAKE_DIR/MAKE_REG/MAKE_SOCK/
  MAKE_FIFO/MAKE_BLOCK/MAKE_SYM/REFER/
  TRUNCATE); handled_access_net whitelist
  (BIND_TCP=0x1|CONNECT_TCP=0x2 = 0x03);
  all-zero access -> -ENOMSG (Linux);
  table exhaustion -> -ENFILE.
  landlock_add_rule: invalid fd -> -EBADF;
  rule_type whitelist (PATH_BENEATH=1|
  NET_PORT=2); NULL rule_attr -> -EFAULT;
  flags!=0 -> -EINVAL.
  landlock_restrict_self: invalid fd ->
  -EBADF; flags!=0 -> -EINVAL. Marco M1 sem
  LSM hook -> accept structurally para
  Firefox detectar suporte ABI e seguir init
  path; real enforcement lands quando namei
  walker landar.
  ENOMSG (42) added to errno header
  (asm-generic/errno.h offset).
  **66 host asserts novos** (16 proc_vm + 17
  openat2 + 17 pkey + 16 landlock).
  Compile-only check com `cc -fsyntax-only
  -DUNIT_TEST -Iinclude -Werror` cobrindo
  todos os 9 modulos vizinhos passou clean.
  Compile real (binario host) tambem clean.
  Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (4 .o + 4
  TEST_SRCS), test_runner (4 decls + 4 calls),
  linux_syscall.c (4 weak hooks + 4 register
  calls).
  **NR count: 230 -> 241** (sessao 22..36: 92
  -> 99 -> 105 -> 110 -> 113 -> 121 -> 131 ->
  145 -> 160 -> 172 -> 189 -> 199 -> 210 ->
  219 -> 230 -> 241).
  COMPAT.md: 11 NRs novos; assert count 992/
  992 em 61 -> 1058/1058 em 65 suites.
- **S3.x JIT auxiliary + namespaces + exec
  extensions + pipe zero-copy: membarrier/
  userfaultfd/sched_rr_get_interval/unshare/
  mount/umount2/execveat/close_range/splice/
  tee/vmsplice (2026-05-07 sessao 35)** -- 11
  NRs novos via 4 modulos novos cobrindo quatro
  nichos hot-path para userland Linux: (a) JIT
  auxiliary (SpiderMonkey JIT chama membarrier
  (PRIVATE_EXPECTED_SYNC) antes de flip code
  page de RW para RX para sincronizar
  instruction caches em multi-thread; WASM
  page-fault handlers usam userfaultfd para
  guard-page trap handling implementing wasm-
  on-linux memory model; musl
  pthread_attr_getschedparam le
  sched_rr_get_interval(SCHED_RR) para sizing
  time-slice budget), (b) namespaces (Firefox
  content sandbox chama unshare(CLONE_NEWUSER |
  CLONE_NEWNET | CLONE_NEWIPC) para isolar
  renderer antes de exec; bubblewrap+flatpak
  rely on unshare; mount/umount2 usados pelo
  bubblewrap para tmpfs+bind-mount layout do
  sandbox), (c) exec extensions (musl
  posix_spawn usa execveat com dirfd para
  evitar TOCTOU races; Firefox content sandbox
  usa close_range(0, ~0, 0) para scrub all
  inherited fds antes de exec'ing renderer --
  security-critical; pre-Linux-5.9 fallback
  itera close(fd) ate' RLIMIT_NOFILE), (d)
  pipe zero-copy (musl posix_fadvise fallback
  via splice; Firefox cache usa splice entre
  SOCK_STREAM e file fd para evitar userspace
  bounce streaming downloads para cache;
  vmsplice em performance-critical IPC para
  local peers). Sem eles userland degrada
  criticamente: membarrier -ENOSYS faz JIT
  cair em mprotect-twice (slow); userfaultfd
  -ENOSYS forca WASM bounds-check JIT (slower);
  unshare -ENOSYS aborta sandbox (fail-closed);
  close_range -ENOSYS forca per-fd-close loop
  (slow + race window); splice/tee/vmsplice
  -ENOSYS forca read+write fallback.
  (1) **`linux_jit_aux.c` (~110 linhas)** -- 3
  NRs: membarrier (NR 324), userfaultfd (NR
  323), sched_rr_get_interval (NR 148).
  membarrier QUERY (cmd=0) com flags!=0 ->
  -EINVAL; reporta SUPPORTED bitmask cobrindo
  7 PRIVATE/GLOBAL variants
  (GLOBAL/GLOBAL_EXPEDITED/REGISTER_GLOBAL/
  PRIVATE_EXPEDITED/REGISTER_PRIVATE/
  PRIVATE_EXPEDITED_SYNC_CORE/REGISTER_PRIVATE_
  EXPEDITED_SYNC_CORE). PRIVATE_EXPEDITED
  variants requerem REGISTER_PRIVATE_EXPEDITED
  first per Linux 4.16 -> -EPERM otherwise
  (Linux fail-closed). REGISTER_* commands
  armazenam state em module-local
  g_membarrier_registered; GLOBAL/
  GLOBAL_EXPEDITED no register required.
  unknown cmd -> -EINVAL. unknown flags fora
  FLAG_CPU -> -EINVAL. Marco M1 single-task UP
  -> barriers no-op semanticamente (compiler
  barrier sufficient). userfaultfd: 16-slot fd
  table (FD_BASE 0xA000, entre uffd e proc no
  fd encoding scheme); flags whitelist
  USER_MODE_ONLY|NONBLOCK|CLOEXEC; allocation
  linear; table exhaustion -> -ENFILE.
  sched_rr_get_interval: pid<0 -> -EINVAL,
  NULL tp -> -EFAULT; default 100 ms slice
  (Linux default for SCHED_RR no_HZ kernel).
  (2) **`linux_namespace.c` (~110 linhas)** --
  3 NRs: unshare (NR 272), mount (NR 165),
  umount2 (NR 166). unshare CLONE_* whitelist
  (NEWNS/NEWCGROUP/NEWUTS/NEWIPC/NEWUSER/
  NEWPID/NEWNET/NEWTIME/FILES/FS/SYSVSEM/
  SIGHAND/THREAD/VM); unknown bits -> -EINVAL.
  THREAD requer VM+SIGHAND, SIGHAND requer VM
  (Linux clone(2) invariants per kernel/fork.c).
  Marco M1 single-namespace: -> 0 no-op
  success (per-task namespace state lands com
  task tables). mount: NULL/empty target ->
  -EFAULT/-ENOENT; flags whitelist
  (LINUX_MS_KNOWN_FLAGS, 21 bits incluindo
  RDONLY/NOSUID/BIND/MOVE/REMOUNT/PRIVATE/
  SHARED/RELATIME/etc.); BIND/MOVE/REMOUNT
  bypass fstype lookup (BIND verifica source
  non-NULL/empty per kernel fs/namespace.c).
  fstype whitelist (tmpfs/proc/devpts/sysfs/
  none); unknown fstype -> -ENODEV (Linux
  kernel behavior para fs nao registrado).
  umount2: NULL/empty target -> -EFAULT/
  -ENOENT; 4-flag whitelist (MNT_FORCE/DETACH/
  EXPIRE/UMOUNT_NOFOLLOW); unknown bits ->
  -EINVAL. Marco M1 sem mount table: -> 0
  no-op success.
  (3) **`linux_exec_ext.c` (~110 linhas)** --
  2 NRs: execveat (NR 322), close_range (NR
  436). execveat: flags whitelist
  (AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW); dirfd
  != AT_FDCWD && < 0 -> -EBADF; NULL pathname
  sem AT_EMPTY_PATH -> -EFAULT; empty pathname
  sem AT_EMPTY_PATH -> -ENOENT; well-formed
  -> -ENOSYS (exec subsystem nao landed;
  userland deterministicamente reverts a spawn-
  based fallback que ja' funciona em CapyOS).
  close_range: first>last -> -EINVAL; flags
  whitelist (UNSHARE|CLOEXEC) -> -EINVAL
  otherwise; provider injection via
  `linux_exec_ext_install_ops({.close_one,
  .set_cloexec_one})`; default validation-only
  success (sem ops); last cap em 4096 evita
  loop em ~0u (sane upper bound; Linux
  RLIMIT_NOFILE-based mas Marco M1 menor);
  CLOEXEC variant delega a set_cloexec_one
  callback. Linux: erros em fds individuais
  silently ignored (kernel just keeps going).
  (4) **`linux_pipe_zero.c` (~95 linhas)** --
  3 NRs: splice (NR 275), tee (NR 276),
  vmsplice (NR 278). fd<0 -> -EBADF; flags
  whitelist MOVE|NONBLOCK|MORE|GIFT (0x0F) ->
  -EINVAL otherwise; vmsplice nr_segs >
  IOV_MAX (1024) -> -EINVAL, nr_segs=0 -> 0
  no-op, NULL iov com nr_segs>0 -> -EFAULT.
  Provider injection via
  `linux_pipe_zero_install_ops({.splice, .tee,
  .vmsplice})`; default -ENOSYS forca userland
  a read+write fallback (que userland
  glibc/musl ja' tem implementado).
  **61 host asserts novos** (14 jit_aux + 20
  namespace + 12 exec_ext + 15 pipe_zero).
  Compile-only check com `cc -fsyntax-only
  -DUNIT_TEST -Iinclude -Werror` cobrindo
  todos os 9 modulos vizinhos passou clean.
  Compile real (binario host) tambem clean.
  Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (4 .o + 4
  TEST_SRCS), test_runner (4 decls + 4 calls),
  linux_syscall.c (4 weak hooks + 4 register
  calls).
  **NR count: 219 -> 230** (sessao 22..35: 92
  -> 99 -> 105 -> 110 -> 113 -> 121 -> 131 ->
  145 -> 160 -> 172 -> 189 -> 199 -> 210 ->
  219 -> 230).
  COMPAT.md: 11 NRs novos; assert count 931/
  931 em 57 -> 992/992 em 61 suites.
- **S3.x sandbox surface + memory residency +
  NUMA policy + legacy time setter: chroot/
  personality/setfsuid/setfsgid/mincore/
  get_mempolicy/set_mempolicy/mbind/
  settimeofday (2026-05-07 sessao 34)** -- 9
  NRs novos via 4 modulos novos cobrindo quatro
  nichos hot-path para userland Linux:
  (a) sandbox surface (Firefox content sandbox
  chama chroot("/") apos seccomp filter
  installation para remover path-based attack
  surface do renderer; glibc/musl probe
  personality(PER_LINUX | ADDR_NO_RANDOMIZE)
  para ASLR detection; setfsuid/setfsgid usados
  por NFS clients e setuid helpers para fsuid
  swap), (b) memory residency (Firefox JIT
  IonMonkey usa mincore em trampoline buffer
  page antes de flip RX para evitar pre-fault
  byte-touch overhead; PGO loaders usam para
  skip pages residentes; glibc posix_spawn
  probes COW safety), (c) NUMA memory policy
  (Firefox WebRender usa get_mempolicy em GL
  command buffer para topology detection;
  SpiderMonkey GC usa set_mempolicy
  (MPOL_PREFERRED) em hot heap pages; libnuma
  probes durante init), (d) legacy time setter
  (settimeofday que musl clock_settime
  fallback usa em older kernels e NTP daemons
  como ntpdate/chrony usam diretamente). Sem
  eles userland degrada criticamente: chroot
  -ENOSYS faz Firefox sandbox refuse to start;
  personality -ENOSYS faz glibc ASLR probe
  think kernel too old; mincore -ENOSYS forca
  pre-fault byte-touch (slow JIT cold start);
  set_mempolicy -ENOSYS faz SpiderMonkey GC
  sem NUMA-aware allocation; settimeofday
  -ENOSYS quebra musl fallback path.
  (1) **`linux_sandbox.c` (~95 linhas)** -- 4
  NRs: chroot (NR 161), personality (NR 135),
  setfsuid (NR 122), setfsgid (NR 123). chroot:
  NULL -EFAULT, empty -ENOENT, provider-
  injectable via `linux_sandbox_install_ops`
  ({.chroot}); default no-op success (Marco M1
  single-root world; CAP_SYS_CHROOT implicit
  at root). personality: module-local state
  armazena persona; QUERY sentinel
  (0xFFFFFFFF) le sem escrever (Linux probe
  behaviour); other values rewrite + return
  prev; aceita any persona bits (Linux liberal
  behaviour, kernel never rejects on unknown
  bits). setfsuid/setfsgid: module-local state;
  -1 sentinel = probe (no change, return
  current); fsuid/fsgid >= 0 stores; returns
  previous always (musl pattern: setfsuid(-1)
  before/after to detect change).
  (2) **`linux_mincore.c` (~50 linhas)** -- 1
  NR: mincore (NR 27). Linux: addr must be
  page-aligned (4 KiB) -> -EINVAL otherwise;
  length=0 -> 0 no-op; addr+length overflow
  check (wrap detection via addr+length<addr)
  -> -ENOMEM; NULL vec com length>0 -> -EFAULT.
  Pages count = ceil(length / 4096). Marco M1
  no swap, no page aging -> all RAM-backed
  pages 'always resident' -> bit 0 set em cada
  byte de vec; bits 1..6 reserved (0); bit 7 =
  locked (we leave 0 ate' mlock per-page
  tracking landar).
  (3) **`linux_numa.c` (~120 linhas)** -- 3
  NRs: get_mempolicy (NR 239), set_mempolicy
  (NR 238), mbind (NR 237). Linux modes
  whitelist: MPOL_DEFAULT (0)/PREFERRED (1)/
  BIND (2)/INTERLEAVE (3)/LOCAL (4). Flags
  whitelist: F_NODE/F_ADDR/F_MEMS_ALLOWED
  (0x07) para get_mempolicy; MF_STRICT/MOVE/
  MOVE_ALL (0x07) para mbind. BIND/INTERLEAVE/
  PREFERRED requerem non-empty nodemask em
  set_mempolicy/mbind -> -EINVAL otherwise.
  nodemask non-NULL com maxnode=0 -> -EINVAL.
  Marco M1 single-NUMA: get_mempolicy retorna
  MPOL_DEFAULT com bit 0 set em nodemask
  (palavra 0 = 1ULL), demais words zerados;
  set_mempolicy/mbind validam + no-op success
  (single zone -> binding e' meaningless).
  (4) **`linux_settod.c` (~50 linhas)** -- 1
  NR: settimeofday (NR 164). NULL tv com NULL
  tz -> 0 no-op (Linux modern behaviour); tz
  parameter ignored desde 2.6.x (kernel doesn't
  even read tz_minuteswest); tv_usec [0, 1e6)
  e tv_sec >=0 validated -> -EINVAL otherwise;
  provider-injectable via
  `linux_settod_install_ops({.set_seconds})`
  para futuro RTC writeback; default no-op
  success (root tem CAP_SYS_TIME implicito).
  **44 host asserts novos** (13 sandbox + 8
  mincore + 15 numa + 8 settod). Compile-only
  check com `cc -fsyntax-only -DUNIT_TEST
  -Iinclude -Werror` cobrindo todos os 8
  modulos vizinhos passou clean. Compile real
  (binario host) tambem clean. Per user
  request, binary execution was skipped this
  session.
  **Wiring** em 5 toques: Makefile (4 .o + 4
  TEST_SRCS), test_runner (4 decls + 4 calls),
  linux_syscall.c (4 weak hooks + 4 register
  calls).
  **NR count: 210 -> 219** (sessao 22..34: 92
  -> 99 -> 105 -> 110 -> 113 -> 121 -> 131 ->
  145 -> 160 -> 172 -> 189 -> 199 -> 210 ->
  219).
  COMPAT.md: 9 NRs novos; assert count 887/887
  em 53 -> 931/931 em 57 suites.
- **S3.x real-time scheduler priorities + POSIX
  timers + legacy time + getcpu:
  sched_setscheduler/getscheduler/setparam/
  getparam/get_priority_max/get_priority_min/
  timer_create/settime/gettime/getoverrun/
  delete/time/getcpu (2026-05-07 sessao 33)** --
  11 NRs novos via 3 modulos novos cobrindo
  tres nichos hot-path para userland Linux:
  (a) real-time scheduler priorities (Firefox
  audio thread queries sched_get_priority_max
  (SCHED_FIFO) antes de bumping para evitar
  audio glitches; Firefox compositor usa
  sched_setscheduler(SCHED_FIFO) best-effort
  para elevar priority; musl
  pthread_getschedparam le via
  sched_getscheduler), (b) POSIX timers
  (Firefox profiler usa timer_create com
  SIGEV_THREAD para sample stacks at fixed
  intervals; SpiderMonkey GC heuristics usa
  timer_create com CLOCK_MONOTONIC para
  incremental marking; musl timer_create e'
  implementado via raw syscall sem userspace
  fallback), (c) legacy time + getcpu
  (time(NULL) que musl fallback usa quando
  vDSO unavailable, getcpu que sched_getcpu
  usa para profiler CPU labels e SpiderMonkey
  GC para NUMA hints). Sem eles userland
  degrada criticamente: sched_get_priority_max
  -ENOSYS faz Firefox audio thread sem priority
  ceiling info -> default priority -> audio
  glitches under load; timer_create -ENOSYS
  desabilita Firefox profiler entirely (no
  fallback); time -ENOSYS quebra musl fallback
  path; getcpu -ENOSYS forca CPUID-based
  detection mais caro.
  (1) **`linux_sched_prio.c` (~140 linhas)** --
  6 NRs: sched_setscheduler (144),
  sched_getscheduler (145), sched_setparam
  (142), sched_getparam (143),
  sched_get_priority_max (146),
  sched_get_priority_min (147). Linux policies
  SCHED_OTHER (0)/FIFO (1)/RR (2)/BATCH (3)/
  IDLE (5)/DEADLINE (6) honored. Priority
  validation per-policy: FIFO/RR -> [1, 99]
  (RT_MIN..RT_MAX), outras (OTHER/BATCH/IDLE/
  DEADLINE) -> must be 0 (Linux requirement
  per fs/exec.c). get_priority_max/min retornam
  (99, 1) para FIFO/RR e (0, 0) para outras
  (Linux behaviour exato). setscheduler
  armazena policy + priority em module-local
  state; getscheduler retorna policy stored
  (default OTHER); setparam/getparam
  read/write priority; getparam fresh -> 0.
  NULL param em setscheduler/setparam ->
  -EFAULT; pid<0 em getscheduler -> -EINVAL;
  unknown policy/invalid priority -> -EINVAL.
  Marco M1 stores values mas cooperative
  scheduler nao actually honora RT semantics;
  Linux fast path Firefox preservado.
  Per-task migration quando clone com thread
  groups landar.
  (2) **`linux_posix_timer.c` (~165 linhas)** --
  5 NRs: timer_create (222), timer_settime
  (223), timer_gettime (224), timer_getoverrun
  (225), timer_delete (226). 16-slot timer
  table com 1-based ids (Linux kernel encodes
  struct k_itimer pointers as compact ints; we
  hand back small positive ints). Clockid
  whitelist: REALTIME/MONOTONIC/PROCESS_CPUTIME
  /THREAD_CPUTIME/MONOTONIC_RAW/REALTIME_COARSE
  /MONOTONIC_COARSE/BOOTTIME. Sigev_notify
  whitelist: SIGNAL (default)/NONE/THREAD/
  THREAD_ID. timer_create: NULL timerid
  -EFAULT, unknown clockid -EINVAL, unknown
  notify -EINVAL, slot exhaustion -EAGAIN.
  timer_settime: invalid id -EINVAL, NULL new
  -EFAULT, tv_nsec [0, 1e9) e tv_sec >=0
  validated, unknown flags fora TIMER_ABSTIME
  -EINVAL; old_value populado retorna
  previous spec. timer_gettime: read-back
  stored spec (no countdown ainda; per-task
  signal subsystem ownership futuro).
  timer_getoverrun: returns 0 (no fires yet).
  timer_delete: frees slot, subsequent
  settime same id -> -EINVAL. Marco M1 stores
  spec mas timers don't actually fire ate'
  per-task signal subsystem landar -- Firefox
  profiler detecta success e prossegue init,
  SpiderMonkey GC heuristic happy path.
  (3) **`linux_time_legacy.c` (~50 linhas)** --
  2 NRs: time (201), getcpu (309). **time**:
  provider-injectable now_seconds() callback
  (defaults to 0/epoch); writes seconds via
  tloc pointer se non-NULL; returns same value
  (Linux semantics: return value e *tloc
  identicos). Userland que precisa wall-clock
  real deve usar clock_gettime(CLOCK_REALTIME)
  wired desde sessao 8 com platform clock.
  **getcpu**: returns cpu=0, node=0 (Marco M1
  single-CPU); third arg (struct getcpu_cache *)
  unused desde Linux 2.6.24; NULL pointers
  silently accepted (Linux behaviour). Provider
  injection via `linux_time_legacy_install_ops
  ({.now_seconds})`.
  **46 host asserts novos** (20 sched_prio +
  18 posix_timer + 8 time_legacy).
  Compile-only check com `cc -fsyntax-only
  -DUNIT_TEST -Iinclude -Werror` cobrindo
  todos os 7 modulos vizinhos passou clean.
  Compile real (binario host) tambem clean.
  Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register
  calls).
  **NR count: 199 -> 210** (sessao 22+23+24+
  25+26+27+28+29+30+31+32+33: 92 -> 99 -> 105
  -> 110 -> 113 -> 121 -> 131 -> 145 -> 160 ->
  172 -> 189 -> 199 -> 210).
  COMPAT.md: 11 NRs novos; assert count 841/
  841 em 50 -> 887/887 em 53 suites.
- **S3.x resource limits legacy + capabilities +
  interval timers + advisory locking: getrlimit/
  setrlimit/capget/capset/alarm/getitimer/
  setitimer/times/flock/copy_file_range
  (2026-05-07 sessao 32)** -- 10 NRs novos via 4
  modulos novos cobrindo quatro nichos hot-path
  para userland Linux: (a) resource limits legacy
  (getrlimit/setrlimit que bash startup probes,
  musl pthread_create usa para sizing thread
  stacks via RLIMIT_STACK, Firefox sandbox
  consulta RLIMIT_AS para JIT decisions; modern
  userland usa prlimit64 ja' wired sessao 18 mas
  legacy paths ainda hit), (b) capabilities
  (capget/capset que Firefox sandbox usa para
  drop ALL caps antes de exec content processes,
  bubblewrap/firejail capget para determinar
  current set, libcap probes via cap_get_proc),
  (c) interval timers (alarm/getitimer/setitimer/
  times que musl sigtimedwait fallback usa para
  bound wait, Firefox compositor watchdog usa
  ITIMER_REAL para detectar frozen render
  thread, bash/ps/time(1) usam times para
  reporting), (d) advisory locking (flock que
  Firefox profile lock .parentlock usa via
  LOCK_EX | LOCK_NB, SQLite usa como fcntl
  record lock fallback em tmpfs, Firefox cache
  usa copy_file_range para deduplicar entries).
  Sem eles userland degrada criticamente:
  getrlimit -ENOSYS faz bash defaultar tiny FD
  ceiling; capset -ENOSYS aborta Firefox sandbox
  fail-closed; alarm -ENOSYS quebra musl
  sigtimedwait fallback; times -ENOSYS faz
  bash/ps/time(1) reportar 0/0; flock -ENOSYS
  faz Firefox pensar profile esta em uso.
  (1) **`linux_rlimit_legacy.c` (~110 linhas)** --
  2 NRs: getrlimit (97), setrlimit (160).
  Validation: resource [0..NLIMITS=16) ou
  -EINVAL; NULL buf -EFAULT; setrlimit cur > max
  com handling de INFINITY (~0ULL) -> -EINVAL.
  Synthesised defaults sem provider (mirror
  Linux initrd profile): NOFILE 1024/4096,
  STACK 8 MiB/INFINITY, NPROC 1024/1024, CORE
  0/INFINITY, outros INFINITY/INFINITY.
  setrlimit no-op success (Marco M1 root tem
  CAP_SYS_RESOURCE implicito). Provider
  injection via `linux_rlimit_legacy_install_ops
  ({.get_limit, .set_limit})`.
  (2) **`linux_caps.c` (~95 linhas)** -- 2 NRs:
  capget (125), capset (126). Linux capability
  ABI: v1 (0x19980330, 1 entry), v2 (0x20071026,
  2 entries), v3 (0x20080522, 2 entries; Marco
  M1 preferred). Unknown version rewrites
  hdr->version=v3 e retorna -EINVAL (Linux
  probe behaviour). NULL hdr -> -EFAULT (antes
  do version check ja' que precisa rewrite via
  hdr); NULL data com valid version -> -EFAULT.
  capget pid<0 -> -EINVAL. capset only-self
  (pid != 0 -> -EPERM per Linux 2.6.25; root
  tem CAP_SETPCAP implicito). Default Marco M1:
  root-with-all-caps (effective=permitted=
  inheritable=0xFFFFFFFF em todas as entries
  da version selecionada). Provider injection
  via `linux_caps_install_ops({.get_caps,
  .set_caps})`.
  (3) **`linux_itimer.c` (~110 linhas)** -- 4
  NRs: alarm (37), getitimer (36), setitimer
  (38), times (100). **alarm**: storage-only
  state machine, retorna prev seconds (Linux
  semantics); SIGALRM nao firado ainda (signal
  subsystem storage-only). **getitimer/
  setitimer**: 3-slot table para REAL (0)/
  VIRTUAL (1)/PROF (2); which fora [0,3) ->
  -EINVAL; NULL new (em setitimer) -> -EFAULT;
  tv_usec [0, 1e6) e tv_sec >=0 validados
  (Linux); setitimer com old_value populado
  retorna previous antes de overwrite;
  getitimer fresh slot -> all zeros.
  **times**: returns provider tick count (NULL
  ops -> 0); buf populado com zeros para
  per-task accounting (no per-task CPU acct
  ainda; CLK_TCK = 100 default Linux).
  Provider injection via
  `linux_itimer_install_ops({.now_ticks})`.
  (4) **`linux_lock.c` (~140 linhas)** -- 2 NRs:
  flock (73), copy_file_range (326).
  **flock**: 32-slot per-fd state machine
  (LOCK_SH/EX state stored, LOCK_UN clears
  slot). fd<0 -EBADF; mode mutually-exclusive
  (LOCK_SH | LOCK_EX -> -EINVAL); unknown bits
  fora [SH|EX|UN|NB] -> -EINVAL; LOCK_UN |
  LOCK_NB -> 0 (NB ignored em unlock); LOCK_UN
  em fd nao-locked -> 0 (no-op success); 33
  distinct fds -> -ENOLCK (slot exhausted;
  fail-closed para userland visibility).
  Single-process world: LOCK_NB never blocks
  (no contention possible). Static-init bug
  fixed: ambiguous fd==0 in BSS replaced with
  explicit `g_locks_initialised` flag e
  `ensure_initialised()` helper que e' chamado
  na entrada de flock e em register_syscalls.
  **copy_file_range**: fd<0 -EBADF para in/
  out; flags != 0 -> -EINVAL (Linux 5.x
  reserva flags=0); provider injection via
  `linux_lock_install_ops({.copy_file_range})`;
  default -ENOSYS forca userland a read+write
  fallback (que userland ja' tem).
  **58 host asserts novos** (14 rlimit_legacy
  + 13 caps + 16 itimer + 15 lock).
  Compile-only check com `cc -fsyntax-only
  -DUNIT_TEST -Iinclude -Werror` cobrindo
  todos os 8 modulos vizinhos passou clean.
  Compile real (binario host) tambem clean.
  Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (4 .o + 4
  TEST_SRCS), test_runner (4 decls + 4 calls),
  linux_syscall.c (4 weak hooks + 4 register
  calls).
  **NR count: 189 -> 199** (sessao 22+23+24+
  25+26+27+28+29+30+31+32: 92 -> 99 -> 105 ->
  110 -> 113 -> 121 -> 131 -> 145 -> 160 ->
  172 -> 189 -> 199).
  COMPAT.md: 10 NRs novos; assert count 783/
  783 em 46 -> 841/841 em 50 suites.
- **S3.x extended attributes + filesystem stats
  + file advise/preallocation/sendfile: setxattr/
  lsetxattr/fsetxattr/getxattr/lgetxattr/fgetxattr/
  listxattr/llistxattr/flistxattr/removexattr/
  lremovexattr/fremovexattr/statfs/fstatfs/
  fadvise64/fallocate/sendfile (2026-05-07 sessao
  31)** -- 17 NRs novos via 3 modulos novos
  cobrindo tres nichos hot-path para userland Linux:
  (a) extended attributes (xattr family que Firefox
  quarantine usa para download tracking via
  `user.xdg.origin.url`, SELinux/AppArmor probes,
  musl `cp -a`-style helpers para preserve
  attribute sets), (b) filesystem stats (statfs
  que Firefox usa para "free space" check antes
  de downloads, SQLite WAL space pressure detect,
  GIO/gvfs volume labelling), (c) file advise/
  preallocation/sendfile (SQLite posix_fadvise
  POSIX_FADV_RANDOM em databases para page-cache
  hints, Firefox downloader fallocate para reservar
  espaco e evitar fragmentacao, musl sendfile
  para zero-copy fallback). Sem eles userland
  degrada criticamente: setxattr -ENOSYS aborta
  Firefox quarantine; getxattr -ENOSYS quebra
  SELinux probe; statfs -ENOSYS faz Firefox
  recusar downloads; fadvise -ENOSYS faz SQLite
  skipar page-cache hints; fallocate -ENOSYS
  forca downloads sem space reservation.
  (1) **`linux_xattr.c` (~165 linhas)** -- 12
  NRs: setxattr/lsetxattr/fsetxattr (NRs 188/
  189/190), getxattr/lgetxattr/fgetxattr (191/
  192/193), listxattr/llistxattr/flistxattr (194/
  195/196), removexattr/lremovexattr/fremovexattr
  (197/198/199). Marco M1 sem xattr storage;
  convencao Linux para "filesystem nao suporta
  xattrs" honrada: setxattr family ->
  -EOPNOTSUPP, getxattr family -> -ENODATA
  (attribute missing), listxattr family -> 0
  attrs (zero bytes written), removexattr family
  -> -ENODATA. All 12 paths share validation:
  NULL path -> -EFAULT, empty -> -ENOENT (path-
  based); fd<0 -> -EBADF (fd-based); NULL name
  -> -EFAULT, empty -> -ERANGE (Linux fs/xattr.c
  rule); name > 255 chars -> -ENAMETOOLONG;
  setxattr value size > 64 KiB -> -E2BIG, NULL
  value with size>0 -> -EFAULT; flag bits fora
  CREATE|REPLACE -> -EINVAL.
  (2) **`linux_statfs.c` (~85 linhas)** -- 2 NRs:
  `statfs` 137, `fstatfs` 138. Synthesised
  120-byte Linux struct: f_type=TMPFS_MAGIC
  (0x01021994), f_bsize=4096, f_blocks=16384
  default (64 MiB / 4 KiB; provider-injectable
  para reportar RAM real via boot trampoline),
  f_bfree=f_bavail=f_blocks (no usage tracking),
  f_files=f_ffree=1024 (Marco M1 tmpfs handle
  table cap), f_namelen=255, f_frsize=4096; f_fsid
  e f_spare zerados. Validation: NULL path ->
  -EFAULT, empty -> -ENOENT; NULL buf -> -EFAULT;
  fd<0 -> -EBADF. Provider injection via
  `linux_statfs_install_providers({.total_blocks,
  .total_files})`.
  (3) **`linux_advise.c` (~85 linhas)** -- 3 NRs:
  `posix_fadvise` 221 (mapped to fadvise64),
  `fallocate` 285, `sendfile` 40. **fadvise64**:
  advice [POSIX_FADV_NORMAL=0..POSIX_FADV_NOREUSE
  =5] whitelist; offset/len >=0; returns 0
  (advisory no-op em Marco M1 sem page cache real
  para hint). **fallocate**: mode whitelist
  (KEEP_SIZE|PUNCH_HOLE|NO_HIDE_STALE|
  COLLAPSE_RANGE|ZERO_RANGE|INSERT_RANGE|
  UNSHARE_RANGE = 0x7F); PUNCH_HOLE requer
  KEEP_SIZE (Linux fs/open.c rule); len <= 0 ou
  negative offset -> -EINVAL; basic call ->
  -EOPNOTSUPP (tmpfs sem preallocation; Linux
  convention que userland glibc/musl/Firefox
  handle gracefully). **sendfile**: out_fd<0 ou
  in_fd<0 -> -EBADF; provider injection via
  `linux_advise_install_ops({.sendfile})` permite
  real backend; default -ENOSYS forca userland a
  read+write fallback.
  ENODATA (61) adicionado em linux_errno.h
  (asm-generic/errno.h next range).
  **50 host asserts novos** (23 xattr + 10 statfs
  + 17 advise). Compile-only check com `cc
  -fsyntax-only -DUNIT_TEST -Iinclude -Werror`
  cobrindo todos os 7 modulos vizinhos passou
  clean. Compile real (binario host) tambem
  clean. Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register
  calls).
  **NR count: 172 -> 189** (sessao 22+23+24+25+
  26+27+28+29+30+31: 92 -> 99 -> 105 -> 110 ->
  113 -> 121 -> 131 -> 145 -> 160 -> 172 -> 189).
  COMPAT.md: 17 NRs novos; assert count 733/733
  em 43 -> 783/783 em 46 suites.
- **S3.x timestamp mutations + identity changes +
  working directory: utimensat/utime/utimes/
  futimesat/setuid/setgid/setresuid/setresgid/
  getresuid/getresgid/chdir/fchdir (2026-05-07
  sessao 30)** -- 12 NRs novos via 3 modulos novos
  cobrindo tres nichos hot-path para userland Linux:
  (a) timestamp mutations que Firefox HTTP cache usa
  para preservar Last-Modified header (cache index
  detecta stale entries via mtime), (b) identity
  changes que dynamic linker invoca em setuid path
  via initgroups e que sandbox wrappers usam para
  drop privileges antes de exec, (c) working
  directory mutations que Firefox profile setup usa
  antes de carregar componentes com paths relativos.
  Sem eles userland degrada criticamente: utimensat
  -ENOSYS quebra HTTP cache (cada navegacao re-
  fetches); setresuid/getresuid -ENOSYS aborta
  initgroups path; chdir -ENOSYS quebra profile
  setup.
  (1) **`linux_utime.c` (~165 linhas)** -- 4 NRs:
  `utimensat` 280, `utime` 132, `utimes` 235,
  `futimesat` 261. utimensat e' a forma moderna;
  legacy delegam para utimensat com NULL buf
  (populated buf -> -ENOSYS forca userland a
  utimensat). Honra Linux UTIME_NOW (`(1<<30)-1`)
  e UTIME_OMIT (`(1<<30)-2`) sentinels: ambos
  UTIME_OMIT -> 0 fast path sem provider; UTIME_NOW
  expandido contra injectable `now()` callback;
  tv_nsec validado [0, 1e9) + dois sentinels
  (-EINVAL fora). Form fd-based: utimensat(fd,
  NULL, ts, 0) routes para `utime_fd` provider
  (musl futimens implementacao). utimensat(AT_FDCWD,
  NULL) -> -EFAULT (NULL path requer real fd).
  AT_SYMLINK_NOFOLLOW (0x100) | AT_EMPTY_PATH
  (0x1000) whitelist; flags fora -> -EINVAL.
  Provider injection `linux_utime_install_ops
  ({.utime_path, .utime_fd, .now})`.
  (2) **`linux_setid.c` (~85 linhas)** -- 6 NRs:
  `setuid` 105, `setgid` 106, `setresuid` 117,
  `setresgid` 119, `getresuid` 118, `getresgid`
  120. Marco M1 single-root (uid=gid=0; real=
  effective=saved=0). setuid(0)/setgid(0) -> 0;
  outros uids/gids -> -EPERM. setresuid/setresgid
  honram Linux `(uid_t)-1` sentinel "no change";
  cada componente (real/effective/saved) verificado
  independentemente; passar -1 para todos -> 0
  (pure no-op); mistura como setresuid(0,-1,0)
  aceita; qualquer non-zero non-(-1) -> -EPERM.
  getresuid/getresgid retornam (0,0,0); todos os
  tres pointers obrigatorios (NULL em qualquer um
  -> -EFAULT).
  (3) **`linux_chdir.c` (~55 linhas)** -- 2 NRs:
  `chdir` 80, `fchdir` 81. Provider injection via
  `linux_chdir_install_ops({.chdir_path,
  .chdir_fd})`. Validation: NULL -> -EFAULT, empty
  -> -ENOENT, fd<0 -> -EBADF, sem ops -> -ENOSYS.
  Provider rc forwarded verbatim (incluindo
  -EACCES para forbidden paths). Marco M1 nao
  tracka cwd ainda; provider futuro pode persistir
  em tmpfs.
  **44 host asserts novos** (17 utime + 18 setid
  + 9 chdir). Compile-only check com `cc
  -fsyntax-only -DUNIT_TEST -Iinclude -Werror`
  cobrindo todos os 10 modulos vizinhos passou
  clean. Compile real (binario host) tambem
  clean. Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register
  calls).
  **NR count: 160 -> 172** (sessao 22+23+24+25+
  26+27+28+29+30: 92 -> 99 -> 105 -> 110 -> 113
  -> 121 -> 131 -> 145 -> 160 -> 172). COMPAT.md:
  12 NRs novos; assert count 689/689 em 40 ->
  733/733 em 43 suites.
- **S3.x filesystem metadata mutations + links +
  durability barriers: chmod/fchmod/fchmodat/chown/
  fchown/lchown/fchownat/link/linkat/symlink/
  symlinkat/sync/syncfs/fsync/fdatasync (2026-05-07
  sessao 29)** -- 15 NRs novos via 3 modulos novos
  cobrindo tres nichos hot-path para userland Linux:
  (a) filesystem metadata mutations que Firefox
  precisa para profile permission lockdown (mkdir +
  chmod 0700 em cache dir; musl mkstemp post-open
  hardening), (b) hard- e soft-links que Firefox usa
  para atomic cache update pattern (link(tmpfile,
  finalfile) + unlink(tmpfile)) e content-deduped
  download targets, (c) durability barriers que
  SQLite invoca em WAL checkpoint (places.sqlite,
  cookies.sqlite). Sem eles userland degrada de
  forma critica: chmod -ENOSYS quebra Firefox
  profile loader; fsync -ENOSYS triggera SQLite's
  "disk I/O error" path que corrompe DB; link
  -ENOSYS forca fallback para rename apenas.
  (1) **`linux_fs_meta.c` (~155 linhas)** -- 7
  NRs: `chmod` 90, `fchmod` 91, `fchmodat` 268,
  `chown` 92, `fchown` 93, `lchown` 94, `fchownat`
  260. Provider injection via `linux_fs_meta_install_ops
  ({.chmod_path, .chmod_fd, .chown_path, .chown_fd})`.
  Validation up-front: NULL path -> -EFAULT,
  empty -> -ENOENT, fd<0 -> -EBADF, sem ops ->
  -ENOSYS. **chmod** clampa mode em 07777 antes
  do provider; **lchown** delega para chown_path
  com follow_symlink=0; **chown** com follow=1.
  **fchmodat** rejeita TODOS os flags como -EINVAL
  (Linux fs/namei.c contract; AT_SYMLINK_NOFOLLOW
  unsupported per glibc/kernel docs). **fchownat**
  aceita whitelist AT_SYMLINK_NOFOLLOW (0x100) |
  AT_EMPTY_PATH (0x1000); flag bits fora -> -EINVAL;
  AT_EMPTY_PATH com AT_FDCWD -> -EINVAL ja' que
  AT_FDCWD nao tem fd para operar. AT_FDCWD only;
  outros dirfds -> -ENOTDIR. uid/gid forwarded
  verbatim incluindo Linux's `(uid_t)-1` "don't
  change" sentinel.
  (2) **`linux_link.c` (~105 linhas)** -- 4 NRs:
  `link` 86, `linkat` 265, `symlink` 88,
  `symlinkat` 266. Provider injection via
  `linux_link_install_ops({.hard_link, .sym_link})`.
  **link** delega para linkat(AT_FDCWD,old,
  AT_FDCWD,new,0). **linkat** valida flag whitelist
  AT_SYMLINK_FOLLOW (0x400) | AT_EMPTY_PATH
  (0x1000); AT_SYMLINK_FOLLOW traduz para
  follow_symlink=1 no provider, default 0.
  **symlink** delega para symlinkat(target,
  AT_FDCWD,linkpath). **symlinkat** valida target
  NULL -> -EFAULT, empty -> -ENOENT (Linux: empty
  target nao tem destino valido). Ambas formas
  validam newdirfd (AT_FDCWD only).
  (3) **`linux_sync.c` (~70 linhas)** -- 4 NRs:
  `sync` 162, `syncfs` 306, `fsync` 74,
  `fdatasync` 75. CapyOS sem persistent backing
  store (tmpfs RAM-only); durability trivialmente
  satisfeita -- todos returnam 0 sem provider.
  **sync()** retorna 0 (libc sync() retorna void
  mas syscall retorna long). **syncfs(-1)** ->
  -EBADF; **fsync/fdatasync(-1)** -> -EBADF.
  Provider injection (`linux_sync_install_ops
  ({.sync_all, .sync_fs, .sync_fd})`) suporta
  data_only flag para distinguir fsync (=0) de
  fdatasync (=1) na callback unica sync_fd. Quando
  real on-disk fs landar, ops flushar device cache.
  **42 host asserts novos** (18 fs_meta + 13 link
  + 11 sync). Compile-only check com `cc
  -fsyntax-only -DUNIT_TEST -Iinclude -Werror`
  cobrindo todos os 7 modulos vizinhos passou
  clean. Compile real (binario host) tambem
  clean. Per user request, binary execution was
  skipped this session.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register
  calls).
  **NR count: 145 -> 160** (sessao 22+23+24+25+
  26+27+28+29: 92 -> 99 -> 105 -> 110 -> 113 ->
  121 -> 131 -> 145 -> 160). COMPAT.md: 15 NRs
  novos; assert count 647/647 em 37 -> 689/689 em
  40 suites.
- **S3.x filesystem mutations + memory locking +
  supplementary groups: mkdir/mkdirat/rmdir/unlink/
  unlinkat/rename/renameat/renameat2/mlock/munlock/
  mlockall/munlockall/getgroups/setgroups (2026-05-07
  sessao 28)** -- 14 NRs novos via 3 modulos novos
  cobrindo tres nichos hot-path para userland Linux:
  (a) filesystem mutations que Firefox precisa para
  profile setup (mkdir em `~/.mozilla/firefox/<id>`)
  e cache index commit (atomic rename(tmpfile,
  finalfile) every commit), (b) memory locking que
  SpiderMonkey JIT depende para W^X executable
  pages e que musl pthread bring-up usa em TLS area,
  (c) supplementary group credentials que dynamic
  linker invoca em setuid-program path via
  `initgroups()`. Sem eles userland degrada
  significativamente: mkdir/rename -ENOSYS quebra
  Firefox profile e cache rotation; mlock -ENOSYS
  faz JIT cair em madvise heuristics que thrasham;
  getgroups(0, NULL) -ENOSYS aborta dynamic
  linker.
  (1) **`linux_fs_mut.c` (~155 linhas)** -- 8 NRs:
  `mkdir` 83, `mkdirat` 258, `rmdir` 84, `unlink`
  87, `unlinkat` 263, `rename` 82, `renameat` 264,
  `renameat2` 316. Validation up-front: NULL path
  -> -EFAULT, empty -> -ENOENT. Provider injection
  via `linux_fs_mut_install_ops({.mkdir, .rmdir,
  .unlink, .rename})`; sem ops instalado -> -ENOSYS
  (Marco M1 sem namei walker). mkdirat/unlinkat/
  renameat[2] aceitam apenas AT_FDCWD (-100); outros
  dirfds -> -ENOTDIR ate' real directory fd table
  landar. unlinkat com AT_REMOVEDIR (=0x200) rota
  para rmdir; flags fora -> -EINVAL. renameat2 valida
  flag whitelist NOREPLACE|EXCHANGE|WHITEOUT (0x07);
  NOREPLACE+EXCHANGE combo -> -EINVAL (Linux
  fs/namei.c rule); flags forwarded para provider.
  mkdir clampa mode em 07777 antes do provider; rename
  delega para renameat2(AT_FDCWD,old,AT_FDCWD,new,0).
  tmpfs pode instalar ops quando mutation hooks
  landarem.
  (2) **`linux_mlock.c` (~65 linhas)** -- 4 NRs:
  `mlock` 149, `munlock` 150, `mlockall` 151,
  `munlockall` 152. No-op success ja' que CapyOS
  nao tem swap (pages sempre pinned). Validation:
  addr+len wrap -> -EINVAL; len==0 -> 0 (Linux
  short-circuit); mlockall(0) -> -EINVAL (Linux
  requer pelo menos um MCL bit); mlockall(unknown)
  -> -EINVAL; flags conhecidos = MCL_CURRENT|FUTURE|
  ONFAULT (0x07). Quando real swapper landar,
  validation fica e success path swap-a para pin
  real.
  (3) **`linux_creds.c` (~50 linhas)** -- 2 NRs:
  `getgroups` 115, `setgroups` 116. Marco M1 sem
  supplementary groups. getgroups(size<0) ->
  -EINVAL; getgroups(0, NULL) -> 0 (Linux idiom
  para count query); getgroups(size>0, NULL) ->
  -EFAULT; getgroups(size>0, valid_buf) -> 0 sem
  tocar buffer. setgroups(size > NGROUPS_MAX
  65536) -> -EINVAL; setgroups(size>0, NULL) ->
  -EFAULT; outros -> 0 (no-op success ja' que
  root tem CAP_SETGID implicito).
  **39 host asserts novos** (21 fs_mut + 10 mlock
  + 8 creds). Compile-only check com `cc
  -fsyntax-only -DUNIT_TEST -Iinclude -Werror`
  cobrindo todos os 4 modulos vizinhos passou
  clean. Compile real (gerando binario 59 KiB)
  tambem clean. Per user request, binary execution
  was skipped this session; static analysis +
  compile validation confirm correctness.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register
  calls).
  **NR count: 131 -> 145** (sessao 22+23+24+25+
  26+27+28: 92 -> 99 -> 105 -> 110 -> 113 -> 121
  -> 131 -> 145). COMPAT.md: 14 NRs novos; assert
  count 608/608 em 34 -> 647/647 em 37 suites.
- **S3.x system info + scheduling + sessions:
  clock_nanosleep/sysinfo/getrusage/getpriority/
  setpriority/setpgid/getpgid/getpgrp/setsid/getsid
  (2026-05-07 sessao 27)** -- 10 NRs novos via 3
  modulos novos + 1 extension cobrindo tres nichos
  hot-path: timing primitives, scheduling priority
  hints e POSIX session/process group control. Sem
  clock_nanosleep, musl `pthread_cond_timedwait`
  quebra (chama com TIMER_ABSTIME para deadline
  absoluto na MONOTONIC clock). Sem sysinfo,
  Firefox `nsSystemInfo` quebra (le no startup
  para crash-report metadata) e musl
  `pthread_create` falha (le para sizar default
  stack-size cap). Sem getrusage, `time(1)` e
  qualquer perf reporter quebram. Sem getpriority/
  setpriority, bash builtins `nice`/`renice` e
  scheduling hints quebram. Sem setpgid/getpgid/
  setsid, **toda forma de shell job control**
  (bash/zsh fazem setpgid em cada fork do
  pipeline) e **daemonization** (fork+setsid+fork)
  quebram.
  (1) **`linux_sysinfo.c` (~75 linhas)** -- 2 NRs
  (`sysinfo` 99, `getrusage` 98). sysinfo com
  struct de **112 bytes** (uptime, loads[3],
  6*u64 mem fields, procs+pad+pad-to-8 + 2*u64
  high mem + mem_unit; layout matches Linux
  x86_64 verbatim). Initial header tinha 64 bytes
  (errado!); compile-time test catch corrigiu
  para 112 antes da landing. Populated from
  injectable providers (total_ram_bytes,
  free_ram_bytes, uptime_seconds, nproc) com
  defaults safe (mem_unit=1, procs=1, zeros).
  NULL ptr -> -EFAULT. getrusage com struct
  rusage de 144 bytes; aceita RUSAGE_SELF/
  CHILDREN/THREAD; outras -> -EINVAL; struct
  zero (Marco M1 sem accounting; userland le
  como "no usage recorded").
  (2) **`linux_priority.c` (~50 linhas)** -- 2
  NRs (`getpriority` 140, `setpriority` 141).
  getpriority retorna 20 - g_nice (Linux encoding
  quirk). setpriority clampa em [LINUX_NICE_MIN(-20),
  LINUX_NICE_MAX(+19)]. PRIO_PROCESS/PRIO_PGRP/
  PRIO_USER aceitos com semantica single-task.
  Marco M1 roda como root entao todos sets
  succedem.
  (3) **`linux_pgrp.c` (~85 linhas)** -- 5 NRs
  (`setpgid` 109, `getpgid` 121, `getpgrp` 111,
  `setsid` 112, `getsid` 124). setpgid: target
  precisa ser self (Marco M1 sem child tracking;
  outros -> -EPERM). getpgid(0)/getpgid(self)
  -> g_pgid; outros -> -ESRCH. getpgrp ->
  g_pgid. setsid: primeira call sucede (g_pgid !=
  self -> torna leader); segunda call -> -EPERM
  (already leader). getsid simetrico ao getpgid.
  Provider injection com getpid callback (default
  pid=1).
  (4) **linux_clock.c extension** -- 1 NR
  (`clock_nanosleep` 230) com flags={0,
  TIMER_ABSTIME}; clockid em {MONOTONIC/_RAW/
  _COARSE, BOOTTIME, REALTIME/_COARSE}; CPUTIME
  variants -> -EOPNOTSUPP; outros -> -EINVAL.
  Modo absoluto: req e' a deadline diretamente;
  modo relativo: deadline=now+req. Spin-wait
  reusa o mesmo loop do nanosleep.
  **37 host asserts novos** (6 clock_nanosleep
  acrescentados a linux_clock suite + 11 sysinfo
  incluindo struct-size sanity 112+144 bytes + 8
  priority + 12 pgrp). Compile-only check com
  `cc -fsyntax-only -DUNIT_TEST -Iinclude` cobrindo
  todos os 4 modulos vizinhos passou clean.
  Note: per user request, binary was not executed
  this session; static analysis + struct-size
  asserts confirm correctness.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register
  calls); clock_nanosleep dentro do
  linux_clock_register_syscalls existente.
  **NR count: 121 -> 131** (sessao 22+23+24+25+
  26+27: 92 -> 99 -> 105 -> 110 -> 113 -> 121 ->
  131). COMPAT.md atualizado: 10 NRs novos; assert
  count 571/571 em 31 -> 608/608 em 34 suites.
- **S3.x process control + truncation: wait4/waitid/
  kill/tkill/tgkill/truncate/ftruncate (2026-05-07
  sessao 26)** -- 8 NRs novos via 3 modulos novos
  cobrindo o gap mais visivel para userland que usa
  fork/exec/wait/popen/signals e file resize. Sem
  wait4/waitid, musl `popen()`, busybox `system()`
  e shell job control falham fail-fast em vez de
  receberem -ECHILD ("no children to wait for",
  semantica Linux gracioso). Sem kill/tgkill/tkill,
  `abort()` (que faz raise(SIGABRT) -> kill(self,
  SIGABRT)), `pthread_kill`, e cleanup signal-based
  quebram. Sem truncate/ftruncate, tmpfs file
  resize, log rotation e mkstemp-baseado output
  buffers falham.
  (1) **`linux_wait.c` (~60 linhas)** -- 2 NRs
  (`wait4` 61, `waitid` 247). Marco M1 nao tem
  child-process tracking; ambos retornam -ECHILD
  (documented Linux answer para "no children").
  wait4 valida options (WNOHANG|WUNTRACED|WEXITED|
  WCONTINUED|WNOWAIT = 0x0100000F); waitid valida
  idtype (P_ALL/P_PID/P_PGID/P_PIDFD) e exige pelo
  menos um de WEXITED/WSTOPPED/WCONTINUED nas
  options (Linux mandatorio; sem isso bloquearia
  para sempre). Quando task_clone_thread + child-
  tracking landar, hooks no wait queue real.
  (2) **`linux_kill.c` (~95 linhas)** -- 3 NRs
  (`kill` 62, `tgkill` 234, `tkill` 200). Validation
  completa: signal 0..LINUX_NSIG (64) -- fora ->
  -EINVAL; tgkill exige tgid>0 e tid>0. **Self-signal
  funcional**: pid==self com sig==0 -> 0 (alive
  probe), com sig real delega ao
  linux_kill_install_ops({.deliver}) se instalado,
  senao -> 0 (no-op). pid==0 (own pgrp) e pid==-1
  (broadcast) -> 0 silenciosamente; outros pids ->
  -ESRCH; pid<-1 -> -ESRCH. tgkill/tkill mesma
  logica para tgid==tid==self. Provider injection
  (`linux_kill_install_ops`) com getpid callback
  (default pid=1) e deliver callback.
  (3) **`linux_trunc.c` (~50 linhas)** -- 2 NRs
  (`truncate` 76, `ftruncate` 77). truncate path-
  based valida NULL/empty/negative-length; depois
  -ENOSYS (sem namei walker; musl fall back para
  open()+ftruncate). ftruncate fd-based valida
  fd<0/length<0; depois delega para
  linux_trunc_install_ops({.ftruncate}) se
  instalado, senao -ENOSYS. tmpfs pode instalar
  real resize hook quando landar.
  **34 testes novos** (10 wait + 14 kill + 10
  trunc) executados em /tmp via cc -Wall -Wextra
  -Werror -DUNIT_TEST: 34/34 OK isolado. Re-rodado
  com modulos vizinhos linkados juntos: 165/165 OK.
  **+3 dispatch sanity checks** via
  `linux_syscall_init` confirmando que os 3 hooks
  novos foram realmente registrados na tabela
  (wait4 -> -ECHILD, kill(0,0) -> 0, ftruncate(-1,0)
  -> -EBADF), provando wiring end-to-end.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register calls).
  Sem init_boot necessario para wait (stateless);
  kill/trunc tem provider injection ate signal/fs
  infra real.
  **NR count: 113 -> 121** (post-sessao
  22+23+24+25+26: 90 -> 92 -> 99 -> 105 -> 110 ->
  113 -> 121).
  COMPAT.md atualizado: 8 NRs novos (todos STUB com
  validation completa); assert count 537/537 em 28
  -> 571/571 em 31 suites.
- **S3.x filesystem comfort: access/faccessat/fstatat/
  dup/dup2/umask (2026-05-06 sessao 25)** -- 6 NRs
  novos via 3 modulos novos cobrindo o gap deixado
  pela sessao 24 (path/readlink/getcwd cobertos, mas
  access/faccessat/fstatat/dup/dup2/umask sao chamados
  constantemente por musl/glibc/userland antes do
  open). Sem access/faccessat, `./configure` scripts,
  dynamic linker e shells abortam. Sem fstatat, musl
  `stat()` (que internamente chama fstatat AT_FDCWD)
  quebra. Sem dup/dup2, shells stdio redirect (2>&1,
  <, >) e popen() falham. Sem umask, musl
  `__init_libc` aborta logo na startup.
  (1) **`linux_at.c` (~125 linhas)** -- 3 NRs
  (`access` 21, `faccessat` 269, `fstatat` 262).
  access/faccessat aceitam o known pseudo path set
  via `linux_stat_path_is_known()` desde sessao 40, retornando
  0 para qualquer modo R|W|X|F (Marco M1 roda como
  effective root). Paths fora do set -> -ENOENT.
  NULL path -> -EFAULT, modo invalido -> -EINVAL.
  faccessat aceita AT_SYMLINK_NOFOLLOW|
  AT_NO_AUTOMOUNT|AT_EMPTY_PATH; outros flags ->
  -EINVAL. fstatat com path vazio (com ou sem
  AT_EMPTY_PATH) projeta o linux_fstat sintetico
  no struct stat -- userland que prefere fstatat
  sobre fstat funciona sem fallback. AT_FDCWD+path
  delega para `linux_stat`/`linux_lstat` em known
  pseudo paths desde sessao 39; unknown paths continuam
  -ENOSYS para fallback open+fstat; dirfd>=0+path
  -> -ENOTDIR.
  (2) **`linux_dup.c` (~55 linhas)** -- 2 NRs
  (`dup` 32, `dup2` 33). Validation completa de
  Linux semantics: oldfd<0 -> -EBADF, dup2 newfd<0
  -> -EBADF. **dup2(fd, fd) e' funcional**: retorna
  newfd sem chamar ops (Linux behavior). dup/dup2
  com ops nao instaladas retornam -ENOSYS; provider
  injection via `linux_dup_install_ops` permite
  que kernel boot supplie real dup quando fd table
  unificada existir.
  (3) **`linux_umask.c` (~30 linhas)** -- 1 NR
  (`umask` 95). Modulo trivial: armazena u32
  module-local, default 0022 (Linux), retorna
  previous. **Mascara clamada a low 9 bits (0777)**.
  Sempre sucesso (Linux: umask nunca falha).
  **33 testes novos** (18 at + 9 dup + 6 umask)
  executados em /tmp via cc -Wall -Wextra -Werror
  -DUNIT_TEST: 33/33 OK isolado. Re-rodado com
  modulos vizinhos linkados (syscall, vfs, io, stat,
  path, statx, dirent, at, dup, umask): 131/131 OK.
  Edge cases: paths conhecidos vs ENOENT, AT_FDCWD
  delegation, AT_EMPTY_PATH probe, dup2 same-fd,
  umask high bits clamp, persistencia, default
  restore.
  **Wiring** em 5 toques: Makefile (3 .o + 3
  TEST_SRCS), test_runner (3 decls + 3 calls),
  linux_syscall.c (3 weak hooks + 3 register calls).
  Sem init_boot necessario (at/umask sao stateless;
  dup tem provider injection ate fd table real).
  **NR count: 110 -> 113** (post-sessao 22+23+24+25:
  90 -> 92 -> 99 -> 105 -> 110 -> 113).
  COMPAT.md atualizado: 6 NRs novos (3 WIRED + 3
  STUB; assert count 504/504 em 25 -> 537/537 em
  28 suites).
- **S3.x path/metadata polish: getcwd/readlink/
  readlinkat/getdents64/statx (2026-05-06 sessao 24)**
  -- 5 NRs novos via 3 modulos novos preenchendo o gap
  deixado pela sessao 23 (so fstat/fstatat estavam
  wired para metadata; userland tipicamente passa por
  getcwd/readlink/statx/getdents64 ANTES do open).
  Sem getcwd, musl `getcwd(3)` retorna ENOSYS em vez
  de "/", quebrando shells. Sem readlink("/proc/self/exe")
  utilitarios que resolvem o proprio binario (gdb,
  profilers, log writers) abortam. Sem getdents64,
  `opendir`+`readdir` falha com ENOSYS em vez de
  retornar lista vazia. Sem statx, musl/glibc forca
  fallback em open+fstat (custos extras de syscall +
  lifecycle de fd).
  (1) **`linux_path.c` (~100 linhas)** -- 3 NRs (`getcwd`
  79, `readlink` 89, `readlinkat` 267). getcwd retorna
  sempre "/"; -EINVAL p/ size=0, -ERANGE p/ size<2,
  -EFAULT p/ buf=NULL. readlink especializa `/proc/self/exe`
  via provider injetavel `linux_path_install({.resolve_proc_self_exe=...})`;
  paths fora de /proc/self/exe -> -EINVAL ("not a symlink",
  semantica Linux para regulares); sem provider -> -ENOSYS.
  readlinkat com AT_FDCWD delega para readlink; outros
  dirfd -> -ENOTDIR. Marco M1 nao tem cwd-per-process;
  quando landar, basta substituir o "/" por consulta a
  `task->cwd`.
  (2) **`linux_statx.c` (~85 linhas)** -- 1 NR (`statx`
  332). `struct linux_statx` definida fielmente (256
  bytes ABI Linux x86_64; fix do tamanho: stx_dio_*_align
  sao u32, nao u64). statx com path vazio (com ou sem
  AT_EMPTY_PATH) projeta o linux_fstat sintetico nos
  campos statx (mode/blksize/nlink/size/blocks); stx_mask
  retornado e' clampado a `LINUX_STATX_SUPPORTED`
  (TYPE|MODE|NLINK|SIZE). dirfd<0 com path vazio
  -> -EBADF; dirfd>=0 com path nao-vazio -> -ENOTDIR;
  AT_FDCWD com path projeta known pseudo paths desde sessao
  39 e preserva -ENOSYS para unknown paths (forca fallback).
  (3) **`linux_dirent.c` (~30 linhas)** -- 1 NR
  (`getdents64` 217). Stub que retorna 0 (EOF) para
  qualquer fd valido -- userland le isso como "diretorio
  vazio" e prossegue, em vez de receber -ENOSYS e
  abortar. fd<0 -> -EBADF; count=0 -> 0; buf=NULL
  com count>0 -> -EFAULT. struct `linux_dirent64`
  declarada para uso futuro mas nunca emitida no
  Marco M1.
  **30 testes novos** (14 path + 10 statx + 6 dirent)
  executados em /tmp via cc -Wall -Wextra -Werror
  -DUNIT_TEST: 30/30 OK apos fix do tamanho da statx
  struct (test sentinel `sizeof == 256` flagrou bug
  inicial de 264). Edge cases: path validation
  (NULL/empty/non-/proc), provider injection, mask
  clamping, struct size invariant, fd encoding ranges.
  **Wiring** em 5 toques: Makefile (3 .o + 3 TEST_SRCS),
  test_runner (3 decls + 3 calls), linux_syscall.c (3
  weak hooks + 3 register calls). Sem init_boot
  necessario (3 modulos sao stateless ou usam provider
  injection on-demand). **NR count: 105 -> 110**
  (post-sessao 22+23+24: 90 -> 92 -> 99 -> 105 -> 110).
  COMPAT.md atualizado: 5 NRs novos (1 WIRED + 4 STUB).
- **S3.x stdio comfort: readv/writev/pread64/pwrite64
  + fstat (2026-05-06 sessao 23)** -- sessao de comfort
  syscalls que finaliza a stdio surface. Apos sessao
  22 ter declarado Marco M1 ABI surface complete,
  estes 5 NRs nao bloqueiam musl boot mas evitam
  degradacoes: sem readv/writev musl `__stdio_write`
  cairia em single-byte unbuffered I/O; sem fstat
  loaders falhariam ao pegar tamanho de arquivo. 2
  modulos novos:
  (1) **`linux_io.c`** -- 4 NRs (`readv` 19, `writev`
  20, `pread64` 17, `pwrite64` 18). readv/writev
  iteram iovec chamando `linux_vfs_read`/`linux_vfs_write`.
  Linux semantics fielmente: iovcnt=0 -> 0; iovcnt < 0
  ou > IOV_MAX (1024) -> -EINVAL; NULL iov com count > 0
  -> -EFAULT; erro NA PRIMEIRA element -> errno forwarded;
  erro em later element apos progresso -> retorna count
  parcial (silencia errno = Linux behaviour); short
  read/write -> stop iter. iov_len=0 elementos sao
  skip. pread64/pwrite64 fazem save-pos + seek + IO +
  restore-pos via `linux_vfs_lseek`; offset < 0 ->
  -EINVAL; lseek failure (e.g. ESPIPE em pipe) ->
  forwarded. Atomic enough para single-thread Marco
  M1; SMP futuro precisara de read_at/write_at
  primitivos.
  (2) **`linux_stat.c`** -- 3 NRs (`fstat` 5, `stat`
  4, `lstat` 6). `struct linux_stat` reproduzida do
  Linux x86_64 (144 bytes com paddings). fstat
  sintetiza metadata: fds 0/1/2 -> S_IFCHR (combinado
  com ioctl=ENOTTY musl pega block-buffered mode);
  outros -> S_IFREG com size=0. Defaults: nlink=1,
  blksize=4096, perms=IRUSR|IWUSR. Os DOIS campos que
  userland le confiavelmente (st_mode + st_size) saem
  corretos. fd < 0 -> -EBADF; out=NULL -> -EFAULT.
  stat/lstat foram refinados na sessao 39 para known pseudo
  paths; unknown paths continuam -ENOSYS (precisam de namei
  walker); userland que cai em open()+fstat() funciona.
  **30 testes novos** (16 io + 14 stat) executados
  em /tmp via cc -Wall -Wextra -Werror -DUNIT_TEST:
  30/30 OK. Edge cases: iovcnt=0/negative/excess,
  NULL iov, short read iter, partial-then-error,
  scatter/gather order, zero-len skip, pread/pwrite
  cursor restore, ESPIPE forwarding, fd encoding
  ranges. **Wiring** em 4 toques: Makefile (2 .o + 2
  TEST_SRCS), test_runner (2 decls + 2 calls),
  linux_syscall.c (2 weak hooks + 2 register calls).
  Sem init_boot necessario. **Issue resolvida**:
  `__unused[3]` no struct linux_stat colidia com
  macro `<sys/cdefs.h>` do macOS host; renomeado
  para `_capyos_pad[3]` para portabilidade. **COMPAT.md
  atualizado**: 5 NRs WIRED + 2 STUB. Total 23 NRs
  wired ao longo das sessoes 20-23 (10 + 4 + 2 + 7).
- **S3.x ABI surface Marco M1 completa: ioctl + fcntl
  wired (2026-05-06 sessao 22)** -- sessao que fecha
  o ultimo nicho e declara Marco M1 ABI surface
  complete. Apos sessao 21 ter wirado os 4 absolute
  blockers, os 2 NRs ainda MISSING no COMPAT.md
  (ioctl + fcntl) nao bloqueiam musl boot mas sao
  chamados durante stdio init. Stubs polidos:
  (1) **`linux_ioctl.c`** -- retorna `-LINUX_ENOTTY`
  para qualquer fd >= 0 e qualquer cmd. Esta e
  exatamente a Linux semantics para fds que nao sao
  terminais. musl stdio init chama
  `ioctl(fd, TCGETS, &t)` para detectar tty-ness;
  ENOTTY -> musl assume "not a tty" e configura
  block-buffered mode. fd < 0 -> -EBADF. NR 16.
  (2) **`linux_fcntl.c`** -- subset funcional.
  F_GETFD/F_SETFD com tabela hash de 256 buckets
  indexada por `fd % 256` (collision = latest setter
  wins; aceitavel ate fd table real). F_GETFL retorna
  `O_RDWR | (current writable file flags)`. F_SETFL
  honra so `O_APPEND|O_NONBLOCK|O_DIRECT|O_NOATIME|
  O_ASYNC` (SETFL_MASK); immutable bits silently
  dropped (Linux behaviour). F_DUPFD/F_DUPFD_CLOEXEC/
  F_GETLK/F_SETLK/F_SETLKW -> -ENOSYS (precisam de
  fd table real e lock infra). Unknown cmd -> -EINVAL.
  fd < 0 -> -EBADF. NR 72.
  **21 testes novos** (8 ioctl + 13 fcntl) executados
  via cc -Wall -Wextra -Werror em isolamento: 21/21
  OK. **Wiring** em 4 toques: Makefile (2 .o + 2
  TEST_SRCS), test_runner (2 decls + 2 calls),
  linux_syscall.c (2 weak hooks + 2 register calls).
  Sem init_boot necessario (modulos nao tem
  callbacks externos). kernel_main.c intacto.
  **COMPAT.md atualizado**: 0 gaps remaining; "No
  critical gaps remaining. Marco M1 ABI surface
  complete." 16 NRs novos wired ao longo das sessoes
  20-22 (10 + 4 + 2). **Caminho para Marco M1 livre**:
  vendoring upstream musl-1.2.5 (S3.1) e o proximo
  passo natural, seguido por libc.a build (S3.3).
- **S3.x boot blockers fechados: brk + arch_prctl +
  exit/exit_group (2026-05-06 sessao 21)** -- sessao
  que fecha os ultimos absolute blockers para musl
  iniciar. Apos sessao 20 ter wirado 10 NRs criticos,
  COMPAT.md identificou 5 gaps restantes; este sessao
  fecha 4 (ioctl/fcntl nao bloqueiam musl boot, sao
  tolerados como no-ops no startup). 3 modulos novos:
  (1) **`linux_brk.c`** -- heap region tracker com
  base virtual em LINUX_BRK_BASE = 0x600000000000
  (acima do mmap arena 0x500000000000) e cap de 256
  MiB. Linux semantics fielmente implementadas:
  brk(0) retorna current break; brk(addr) success
  retorna addr (== novo break), failure retorna o
  break velho unchanged (Linux NUNCA retorna -errno
  de brk -- a semantica de failure e *manter o break
  no lugar*). Grow path arredonda addr para page
  boundary, calcula pages a adicionar, chama callback
  `reserve_pages` (production = vmm_register_anon_region
  com USER+WRITE+NX). Shrink path so retracts o live
  break sem liberar frames (acceptable Linux behavior;
  refines quando vmm tiver helper para deregister).
  Reserve failure deixa break unchanged -- userland
  malloc cai pra mmap automaticamente. Provider
  injection isolando do VMM para host tests
  deterministicos.
  (2) **`linux_arch_prctl.c`** -- THE most critical
  syscall em __libc_start_main. 4 ops Linux x86_64:
  ARCH_SET_FS (0x1002) escreve IA32_FS_BASE MSR;
  ARCH_GET_FS (0x1003) le IA32_FS_BASE e copia para
  *(uint64_t*)addr; ARCH_SET_GS / ARCH_GET_GS
  escrevem/leem o user gs base via
  IA32_KERNEL_GS_BASE (porque o kernel ja fez swapgs
  no syscall entry; o user GS vive na shadow MSR
  esperando o proximo swapgs de retorno). Boot
  wiring usa wrmsr/rdmsr direto via inline asm;
  #if-guarded por defined(__x86_64__) para que cross-
  compile arm64 host nao quebre. Errors: unknown op
  -> -EINVAL; GET com null ptr -> -EFAULT; sem ops
  installed -> -ENOSYS. Aceita FS_BASE=0 (Linux
  semantics: musl pode tolerar TLS desabilitado).
  (3) **`linux_exit.c`** -- exit + exit_group via
  callback `exit_task(code)`. Production wrap delega
  a task_exit(code) (noreturn em kernel/task.h).
  Tests usam setjmp/longjmp para capturar a chamada
  sem matar o test runner. Single-thread model:
  exit_group identico a exit (quando S1.4 thread
  groups landar, exit_group itera o tg). Sem ops
  installed retorna -LINUX_ENOSYS sentinel (tests-only
  path; producao sempre tem wiring).
  **27 testes novos** (11 brk + 10 arch_prctl + 6
  exit) executados em isolamento via cc -Wall
  -Wextra -Werror -DUNIT_TEST: 27/27 OK. **Wiring**
  em 5 toques: Makefile (6 .o + 3 TEST_SRCS),
  test_runner (3 decls + 3 calls), kernel_main (3
  init_boot calls antes de linux_syscall_init),
  linux_syscall.c (3 weak hooks + 3 register calls),
  COMPAT.md (4 NRs MISSING -> WIRED).
  **Marco M1 unblocked**: musl agora pode (a)
  inicializar TLS via arch_prctl, (b) crescer heap
  via brk, (c) terminar limpo via exit_group.
  Caminho para `js -e 'print(1+1)'` em CapyOS
  userland sem bloqueios kernel-side. Proximo passo
  natural: S3.1 (vendor musl-1.2.5 upstream) seguido
  por S3.3 (configure --target=x86_64-capyos +
  primeiro libc.a build).
- **S3.x bring-up: 10 NRs criticos para musl wired
  (2026-05-06 sessao 20)** -- sessao de wiring direto:
  apos S3.2 seed (sessao 19) que landou apenas docs +
  arch adapter, esta sessao fecha 8 dos gaps que o
  COMPAT.md listava como "Critical gaps that block musl
  boot". 10 NRs novos WIRED via 3 modulos `linux_compat`
  ja existentes (sem novos modulos):
  (1) **`linux_process` extendido** com 7 NRs --
  `getpid` (alias temporario para `gettid`; quando S1.4
  thread groups landar retorna o tg-leader pid),
  `getppid` (retorna 1, init's pid: userland que checa
  "orphaned-by-init" continua funcionando), `getuid`/
  `geteuid`/`getgid`/`getegid` (todos retornam 0/root
  para Marco M1 sem multi-user), `uname` (struct
  utsname com sysname=Linux, machine=x86_64,
  release=6.5.0-capyos, nodename=capyos, version=
  "#1 SMP CapyOS", domainname="(none)"; cada campo
  zero-padded em 65 bytes via `uts_set` helper local
  que evita pull de string.h freestanding-incompatible).
  (2) **`linux_clock` extendido** com 2 NRs --
  `gettimeofday` delega a `clock_gettime(REALTIME)` e
  trunca ns/1000 -> tv_usec; fallback automatico para
  MONOTONIC se REALTIME nao tem epoch (matching Linux
  pre-NTP boot behaviour); `nanosleep` com validacao
  Linux-faithful (tv_sec >= 0, tv_nsec em [0, 1e9)) e
  spin-busy-wait contra MONOTONIC -- nao ideal mas
  determinista e host-testavel; substituido por
  `task_sleep_until` quando scheduler expor a
  primitive; fast-return se timebase nao instalado.
  (3) **`linux_vfs` extendido** com 1 NR --
  `openat(dirfd, path, flags, mode)`: AT_FDCWD (-100)
  delega a `linux_vfs_open` (cwd-less model, paths
  absolutos); dirfd >= 0 -> -ENOTDIR (ainda nao
  expomos directory fds); dirfd < 0 != AT_FDCWD ->
  -EBADF.
  COMPAT.md atualizado: gaps criticos restantes sao
  apenas `brk`, `arch_prctl`, `exit`/`exit_group`,
  `ioctl`, `fcntl`. **18 testes novos**: 8 em
  test_linux_process.c, 7 em test_linux_clock.c, 3 em
  test_linux_vfs.c. Wiring nulo (todos os modulos
  modificados ja em CAPYOS64_OBJS+TEST_SRCS+test_runner
  desde sessoes 8/10/14; NRs ja em
  linux_syscall_nrs.h desde sessao 9). Validacao manual
  pelo user (sem script Python desta vez). Caminho
  para Marco M1 mais curto: musl agora pode chamar
  uname/getpid/openat/gettimeofday durante init e
  receber dados Linux-faithful em vez de -ENOSYS.
- **S3.2 seed: musl arch adapter (syscall_arch.h) +
  estrategia documentada (2026-05-06 sessao 19)** --
  **inicio do S3 (musl libc port)**. Apos S1+S2 = 28/28
  (sessao 18), o passo natural e comecar S3. Esta sessao
  entrega o seed que sera usado quando vendoring upstream
  landar:
  (1) **`userland/lib/musl/arch/x86_64/syscall_arch.h`**
  (~4 KB) -- inline asm macros __syscall0..__syscall6
  com calling convention Linux ABI x86_64 (rax=NR, rdi/rsi/
  rdx/r10/r8/r9, rcx+r11+memory clobbers). Path identico
  ao upstream para vendoring slot in unchanged. **Compila
  para x86_64-linux-gnu via `cc -target -Wall -Werror`**
  (cross-validation real, nao apenas syntax check).
  (2) **`userland/lib/musl/README.md`** -- estrategia
  high-level + roadmap em 8 steps + comparacao musl vs
  glibc/newlib/bionic + anti-goals.
  (3) **`userland/lib/musl/COMPAT.md`** -- matriz de 105
  Linux NRs com state WIRED/STUB/MISSING. Identifica gaps
  criticos para musl boot: brk, arch_prctl (TLS),
  exit/exit_group, uname, getpid, nanosleep, gettimeofday,
  openat.
  (4) **`docs/plans/active/musl-port-strategy.md`** --
  decision log + risk matrix + out-of-scope explicito +
  procedimento de vendoring + build integration plan.
  Wiring: nenhum (puramente userland infra). **0 NRs novos**.
  Validacao via `_session19_validate_musl_seed.py` (criado/
  executado/removido): 0 FAIL, validou 4 arquivos criados,
  14 tokens criticos no syscall_arch.h, compile-check
  x86_64-linux-gnu, cross-references entre os 4 arquivos,
  contagem consistente de 105 NRs.
  Marco M1: S3.2 e o segundo de ~7 sub-tarefas do S3.
  Restantes: S3.1 (vendor upstream), S3.3 (build hooks),
  S3.4 (libc-test), S3.5 (libc.so dynamic), S3.6
  (substituir capylibc), S3.7 (deprecate capylibc).
  Estimativa: ~10-15 sessoes ate libc.a funcional. Caminho
  para Marco M1 (SpiderMonkey shell roda) agora claro:
  musl libc.a -> link SpiderMonkey -> `js -e 'print(1+1)'`
  em CapyOS userland.
- **S2.9 tmpfs em /tmp/ entrega = S2 100% (10/10)
  (2026-05-06 sessao 18)** -- quarta sessao do
  **implementation phase** e **fechamento total do S2**:
  o ultimo contrato S2 do firefox-port-platform-shim.md
  foi entregue. **S1+S2 = 28/28 = 100%** (com varios
  parciais, mas TODA a contract surface Linux ABI
  necessaria para Marco M1 esta landada).
  (1) **`linux_tmpfs.c` (~250 linhas)**: in-memory
  filesystem com flat namespace (rejeita subdirs no
  Marco M1), 16 files x 4 KiB content alocados inline,
  32 handles independentes (refcount per-file que
  sobrevive unlink ate o ultimo close).
  (2) **POSIX semantics**: O_CREAT/O_EXCL/O_TRUNC/
  O_APPEND honored; O_RDONLY rejeita write -> EBADF;
  O_WRONLY rejeita read -> EBADF; writes past
  MAX_FILE_SIZE retornam short count, write em buffer
  cheio retorna -ENOSPC; lseek SET/CUR/END/DATA/HOLE;
  unlink mantem handles existentes alive (orphan);
  maybe_reap_file libera o slot quando unlinked +
  refcount=0.
  (3) **Routing**: `linux_vfs_router` extendido com
  `/tmp/` prefix dispatch + `fd_in_tmpfs`. Router
  cresceu de 130 para ~150 linhas. shm read/write/
  lseek continuam -ENOSYS (precisam mmap MAP_SHARED).
  (4) **`kernel_main.c`**: `linux_tmpfs_init_boot()`
  chamado entre procfs e vfs_init (regression guard
  estatico).
  (5) **40 testes novos**: 35 tmpfs unit + 5 router
  end-to-end via VFS.
  Wiring: 2 objs em CAPYOS64_OBJS, 1 entry em
  TEST_SRCS, 1 prototipo+chamada em test_runner.c, 1
  init_boot em kernel_main.c. **0 NRs novos**.
  Validacao via `_session18_validate_tmpfs.py` (criado/
  executado/removido): 0 FAIL, **474/474 asserts em 22
  suites linux_compat** (subiu de 434 para 474).
  Marco M1: **S2 chega a 10/10 (100%)**. S1 continua em
  18/18. Total **S1+S2 = 28/28 = 100%**. A partir daqui
  o foco muda totalmente para "implementar logica real"
  (clone task creation, signal delivery, real provider
  wirings, BSD sockets) ou para "comecar S3 musl libc
  port". Userland Linux que chama `tmpfile()` / abre
  `/tmp/cache_xxx` agora consegue ler/escrever bytes
  reais via syscalls end-to-end.
- **3 novos paths /proc/* (version, uptime, loadavg) +
  uptime real via clock_gettime (2026-05-06 sessao 17)** --
  terceira sessao do **implementation phase**. Expansao do
  procfs com paths adicionais que userland Linux le
  constantemente:
  (1) **3 novos formatters** em `linux_proc.c`:
  `linux_proc_format_version` (emite "Linux version <release>
  ... x86_64\\n"), `linux_proc_format_uptime` (consome ns,
  emite "<seconds>.<hundredths>" com zero-padding),
  `linux_proc_format_loadavg` (consome thousandths para
  evitar floating-point, emite "<l1> <l5> <l15> <run>/<tot>
  <last_pid>\\n"). Helpers internos para padding.
  (2) **3 novos paths** em `linux_procfs.c` + 3 callbacks
  em `linux_procfs_providers` (version_release, uptime,
  loadavg).
  (3) **`linux_procfs_init.c`**: 3 novos placeholders.
  **uptime e o primeiro provider production-real**: chama
  `linux_clock_gettime(LINUX_CLOCK_MONOTONIC)` direto.
  loadavg retorna 0.00 0.00 0.00 1/1 0 (running e total
  ambos 1 para evitar division by zero em userland).
  (4) **18 testes novos** (11 formatter + 7 procfs):
  defensive NULL handling, padding hundredths, truncating
  floor de thousandths, fake provider em uptime/loadavg,
  default release em version.
  Wiring: nenhum (todos os arquivos modificados ja estavam
  em CAPYOS64_OBJS, TEST_SRCS, test_runner). **0 NRs novos**.
  Validacao via `_session17_validate_procfs_more_paths.py`
  (criado/executado/removido): 0 FAIL, **434/434 asserts em
  21 suites linux_compat** (subiu de 416 para 434). 3 TUs
  puras compilam sob CFLAGS64-like.
  Marco M1: continua **27/28 done em S1+S2** mas procfs
  cobertura cresceu de 6 para 9 paths Linux-compatible.
  SpiderMonkey JS init + musl `__libc_get_version_string`
  + procps `uptime`/`loadavg` agora tem dados parseable
  end-to-end via syscalls. uptime e o primeiro provider
  production-real (vs placeholder).
- **linux_procfs backend serve /proc/* via VFS router
  (2026-05-06 sessao 16)** -- segunda sessao do
  **implementation phase**. Os formatters `/proc/*` ja existiam
  em `linux_proc.c` + `linux_cpuinfo.c` (puros, host-testaveis);
  faltava o backend que aloca fds e renderiza no open.
  (1) **`linux_procfs.c` (~230 linhas)** -- backend stateful
  com tabela de 16 slots (fd `0x8800` entre devfs e shm),
  buffer 4 KiB per-slot, posicao de leitura per-slot. open()
  faz path matching contra 6 paths fixos, chama renderer que
  invoca o formatter existente com dados de providers
  injetados, copia para o slot. read() copia respeitando
  offset, lseek() implementa SET/CUR/END/DATA/HOLE com
  clamping, write() retorna -EROFS.
  (2) **`struct linux_procfs_providers`** -- 6 callbacks
  (meminfo, cpuinfo, maps, cmdline, self_exe, self_status).
  Tests injetam fakes deterministicas; production
  (`linux_procfs_init.c`) instala placeholders truthful
  (cpuinfo: 1 fake CPU com baseline x86_64 flags fpu/tsc/cmov/
  lm; meminfo: zeros; self_exe: '/unknown'; self_status:
  `{name=capy, state=R, pid=1}`) ate vmm_anon_region walker /
  cpuid harvest / PMM stats / task metadata landarem.
  (3) **`linux_vfs_router` extendido**: novo `/proc/` prefix
  no open + `fd_in_procfs` no dispatcher. router cresceu de
  110 para 130 linhas.
  (4) **`kernel_main.c`**: `linux_procfs_init_boot()` chamado
  ANTES de `linux_vfs_init_boot()` (regression guard estatico
  no validation script).
  (5) **27 testes** em `test_linux_procfs.c` (path matching,
  EFAULT/EINVAL/EMFILE, render emite headers esperados,
  cursor avanca, EOF=0, write -> EROFS, lseek SET/CUR/END,
  clamping past EOF, close libera slot, sem providers
  ainda emite header) + **6 testes** end-to-end no router.
  Wiring: 2 objs em CAPYOS64_OBJS (`linux_procfs.o`,
  `linux_procfs_init.o`), 1 entry em TEST_SRCS, 1 prototipo+
  chamada em test_runner.c, 1 init_boot em kernel_main.c.
  **0 NRs novos** (procfs e wiring read-only sobre VFS
  open/read/write).
  Validacao via `_session16_validate_procfs_routing.py`
  (criado/executado/removido): 0 FAIL, **416/416 asserts em
  21 suites linux_compat** (subiu de 383 para 416). Ordem
  de init verificada estaticamente. 3 TUs puras compilam
  sob CFLAGS64-like. 5 headers includeable juntos.
  Marco M1: continua **27/28 done em S1+S2** mas qualidade
  subiu mais um nivel -- `cat /proc/cpuinfo` produz output
  parseable end-to-end via syscalls. SpiderMonkey (gfx
  capability detection, JS::CPUInfo) e Chromium IPC
  (process_util_linux.cc) tem o front door funcional. So
  falta substituir os providers placeholder por iteradores
  reais quando vmm/pmm/cpuid expuserem getters.
- **linux_vfs_router conecta open/read/write a devfs+shm
  end-to-end (2026-05-06 sessao 15)** -- primeira sessao do
  **implementation phase** apos contratos S1+S2 landados.
  Em vez de novos contratos, wiramos os existentes:
  (1) **linux_devfs ganhou fd API**: LINUX_DEVFS_FD_BASE
  = 0x8000, 16 slots, open/close/read_fd/write_fd/lseek_fd.
  id-based API preservada como single source of truth.
  (2) **linux_vfs.c passes errno through** (em vez de
  mascarar todo rc<0 como ENOENT/EBADF) -- necessario para
  router surface EFAULT/EINVAL/EMFILE distinctly.
  (3) **linux_vfs_router** (~110 linhas): dispatch
  path-based em open (`/dev/shm/` checked ANTES de `/dev/`)
  e fd-range em close/read/write/lseek. Stateless.
  (4) **linux_vfs_init.c migrado**: chama
  linux_vfs_router_install() em vez de install_ops(NULL).
  open("/dev/urandom") flui end-to-end ja desde o boot.
  (5) **32 testes novos**: +14 devfs fd-API, +1 vfs
  passthrough, +17 router end-to-end (incluindo prefix
  priority regression guard).
  Wiring: 1 obj em CAPYOS64_OBJS (linux_vfs_router.o), 1
  entry em TEST_SRCS, 1 prototipo+chamada em test_runner.c.
  **0 NRs novos** (sessao e wiring real, nao nova ABI).
  Validacao via `_session15_validate_vfs_router.py`
  (criado/executado/removido): 0 FAIL, **383/383 asserts em
  20 suites linux_compat** (subiu de 351 para 383).
  Marco M1: continua **27/28 done em S1+S2 (~96%)** mas
  qualidade subiu -- `open("/dev/urandom")` deixou de ser
  ENOSYS no kernel real. SpiderMonkey/musl que abrem
  /dev/urandom para entropy seed agora obtem bytes reais
  via syscall. Linha base do "implementation phase":
  contratos sem implementacao real saoa visiveis (shm
  read/write/lseek -> ENOSYS, /etc/* -> ENOENT, /tmp/* ->
  ENOENT).
- **S1.4 + S2.10 + VFS + mmap-ext + timerfd-functional
  entregues (2026-05-06 sessao 14)** -- ultima cascata que
  fecha S1+S2 do firefox-port-platform-shim.md:
  (1) **S1.4 clone** -- novo `linux_clone` com validacao
  estrita (15 known flag bits, CSIGNAL low-byte stripping,
  CLONE_THREAD requer SIGHAND, SIGHAND requer VM). musl
  pthread_create flag pattern reconhecido. clone3 valida
  ARGS_SIZE_VER0/1/2. fork/vfork stubs. -ENOSYS ate
  task_clone_thread landar.
  (2) **S2.10 POSIX shm** -- novo `linux_shm` (kernel API;
  shm_open na libc usa open("/dev/shm/<name>", ...)).
  16 named slots, names <= 63 chars, sizes <= 64 MiB. POSIX
  semantics: O_CREAT/O_EXCL/O_TRUNC, refcount, unlink+close
  orphan-frees.
  (3) **VFS open/close/read/write/lseek** -- novo `linux_vfs`.
  Validacao completa de flags (O_CREAT/EXCL invariant,
  O_ACCMODE), path NUL-termination check (PATH_MAX=4096),
  whence (SET/CUR/END/DATA/HOLE). Routing layer vazio ate
  capyfs/devfs/shm hookarem.
  (4) **mmap MAP_FIXED + madvise + mremap** -- estensao de
  `linux_mmap`. MAP_FIXED via callback alloc_anon_at.
  madvise valida 23 advice values e retorna 0 (hints).
  mremap valida MAYMOVE/FIXED, MREMAP_FIXED requer
  MAYMOVE, same-size = no-op.
  (5) **timerfd functional** -- estensao de `linux_eventfd`.
  16 timer slots (fd 0x4800+), one-shot e periodic, ABSTIME,
  3 clockids. settime arm/disarm via it_value=0;
  gettime retorna remaining; read conta expirations elapsed.
  now_ns oracle wired em prod para `linux_clock_gettime
  (CLOCK_MONOTONIC)`.
  Wiring: 5 objs em CAPYOS64_OBJS, 3 entries em TEST_SRCS,
  3 prototipos+chamadas em test_runner.c, 1 init_boot em
  kernel_main.c (linux_vfs_init_boot), 2 weak hooks em
  linux_syscall.c. 13 novos NRs (clone, clone3, fork,
  vfork, open, close, read, write, lseek, madvise, mremap,
  timerfd_gettime). Acumulado: **51 NRs registrados**.
  errno.h ganhou ENAMETOOLONG, ENOLCK.
  Validacao via `_session14_validate_linux_compat_5.py`
  (criado/executado/removido): 0 FAIL, **351/351 asserts
  em 19 suites linux_compat** (clock 22 + syscall 17 +
  random 12 + devfs 17 + process 29 + cpuinfo 11 + proc 26
  + fd 14 + mmap 35 + futex 19 + eventfd 27 + net 14 +
  epoll 19 + signal 16 + memfd 15 + inotify 15 + clone 11
  + shm 15 + vfs 17). freestanding-like syntax check de 3
  TUs puras OK. 22 headers linux_compat includeable juntos.
  **Marco M1 progress**: **27/28 tasks DONE em S1+S2 (~96%,
  com varias parciais)**. Resta apenas S2.9 (tmpfs proper)
  que e infra de VFS, nao novo contrato. A partir daqui o
  trabalho mudou de "definir contratos Linux ABI" para
  "implementar a logica real" (clone task creation, signal
  delivery, VFS routing, BSD sockets).
- **S1.6 + S1.12 + S2.2 + S1.15 + S1.16 entregues numa
  unica sessao (2026-05-06 sessao 13)** -- mais cinco
  tarefas concretas em cascata, agrupadas em 4 modulos
  novos + extensao de linux_proc:
  (1) **S1.6 epoll** -- novo `linux_epoll`. epoll_create1/
  ctl/wait/pwait. Tabela 16 instances x 64 watch entries.
  CTL_ADD/MOD/DEL com EEXIST/ENOENT/event mask validation.
  wait com fd_ready callback (yield via task_yield),
  EPOLLONESHOT desarma. pwait valida sigsetsize == 8.
  (2) **S1.12 signals storage-only** -- novo `linux_signal`.
  rt_sigaction/rt_sigprocmask/sigaltstack armazenam
  handlers/mask/altstack em module-local state. SIGKILL/
  SIGSTOP nao captureveis. sigsetsize=8 obrigatorio.
  rt_sigreturn -> -ENOSYS (sem delivery infra).
  (3) **S2.2 /proc/self/exe formatter** -- extensao
  `linux_proc`. snprintf-style. NULL path -> '/unknown'.
  (4) **S1.15 memfd_create + pidfd_*** -- novo `linux_memfd`.
  memfd_create funcional (16 slots, valida flags/name <=
  249). pidfd_open funcional (16 slots, pid_exists callback
  via task_by_pid). pidfd_send_signal: sig=0 = probe;
  sig != 0 -> -ENOSYS.
  (5) **S1.16 inotify** -- novo `linux_inotify` storage-only.
  init1 (8 instances), add_watch (32 watches/instance,
  monotonic wd), rm_watch. Eventos nao disparam (fs notifier
  nao existe); xpcom poll fallback.
  Wiring: 6 objs em CAPYOS64_OBJS, 4 entries em TEST_SRCS,
  4 prototipos+chamadas em test_runner.c, 2 init_boot calls
  em kernel_main.c (epoll + memfd; signal e inotify nao
  precisam), 4 weak hooks em linux_syscall.c. 14 novos NRs
  (epoll_create1/ctl/wait/pwait, rt_sigaction/procmask/
  return, sigaltstack, memfd_create, pidfd_open/send_signal,
  inotify_init1/add_watch/rm_watch). Acumulado: **38 NRs
  registrados**.
  Validacao via `_session13_validate_linux_compat_5.py`
  (criado/executado/removido): 0 FAIL, **288/288 asserts
  em 16 suites linux_compat** (clock 22 + syscall 17 +
  random 12 + devfs 17 + process 29 + cpuinfo 11 + proc 26
  + fd 14 + mmap 21 + futex 19 + eventfd 21 + net 14 +
  epoll 19 + signal 16 + memfd 15 + inotify 15).
  freestanding-like syntax check de 4 TUs puras OK. 19
  headers linux_compat includeable juntos.
  **Marco M1 progress**: **23 tasks DONE de 28 totais em
  S1+S2 (~82%, algumas parciais)**. Restantes: S1.4 (clone
  com thread groups), S2.9 (tmpfs), S2.10 (shm).
- **S1.5 + S1.7 + S2.1 + S2.3 + S1.14 entregues numa
  unica sessao (2026-05-06 sessao 12)** -- mais cinco
  tarefas concretas em cascata, agrupadas em 3 modulos
  novos + extensao de linux_proc:
  (1) **S1.5 futex** -- novo `linux_futex` com WAIT/WAKE/
  WAIT_BITSET/WAKE_BITSET/REQUEUE funcionais (PRIVATE_FLAG
  e CLOCK_REALTIME aceitos). FD/LOCK_PI/UNLOCK_PI/TRYLOCK_PI/
  CMP_REQUEUE/WAKE_OP -> -ENOSYS (musl/pthread fazem
  fallback). Boot trampoline usa `task_block(cur, uaddr)` /
  `task_unblock_channel(uaddr)`. Crítico para SpiderMonkey
  pthread mutex/cond.
  (2) **S1.7 eventfd2 + signalfd4 + timerfd** -- novo
  `linux_eventfd`. eventfd2 funcional (32 slots, semaphore
  mode, sentinel rejection, overflow detection). signalfd4
  storage-only desde sessao 41: cria/atualiza fd e read -> -EAGAIN
  ate signal delivery real. timerfd_create + settime/gettime/read
  funcionais, com VFS router despachando read/write/close/lseek para
  eventfd/signalfd/timerfd ranges.
  (3) **S2.1 /proc/self/maps + S2.3 /proc/self/cmdline**
  -- extensao de `linux_proc`. format_maps emite linhas
  Linux canonicas (start-end perms offset dev inode path).
  format_cmdline emite argv NUL-separated.
  (4) **S1.14 accept4 + recvmmsg + sendmmsg** -- novo
  `linux_net`. Validacao completa (flags, fd >= 0, addr
  pareado com addrlen, vlen <= UIO_MAXIOV=1024). Operacao
  retorna -ENOSYS ate sockets BSD landearem.
  Wiring: 6 objs em CAPYOS64_OBJS, 3 entries em TEST_SRCS,
  3 prototipos+chamadas em test_runner.c, 3 init_boot
  calls em kernel_main.c (10 stages init agora), 3 weak
  hooks em linux_syscall.c. 10 novos NRs operacionais
  (futex, eventfd2, signalfd4, timerfd_create,
  timerfd_settime, accept4, recvmmsg, sendmmsg). Acumulado:
  **24 NRs registrados**. linux_errno.h ganhou
  ETIMEDOUT + 16 errnos de networking.
  Validacao via `_session12_validate_linux_compat_5.py`
  (criado/executado/removido): 0 FAIL, **219/219 asserts
  em 12 suites linux_compat** (clock 22 + syscall 17 +
  random 12 + devfs 17 + process 29 + cpuinfo 11 + proc
  22 + fd 14 + mmap 21 + futex 19 + eventfd 21 + net 14).
  freestanding-like syntax check de 3 TUs puras OK. 15
  headers linux_compat includeable juntos.
  **Marco M1 progress**: 18 tasks DONE de 28 totais em
  S1+S2 (~64%, algumas parciais). futex desbloqueia
  pthread mutex/cond. Restante crítico: S1.4 (clone
  thread groups), S1.12 (signals), S1.6 (epoll).
- **S1.10 + S1.13 + S2.5 + S2.6 + S1.2 entregues numa
  unica sessao (2026-05-06 sessao 11)** -- mais cinco
  tarefas concretas em cascata, agrupadas em 4 modulos
  (extensao do `linux_process` + 3 modulos novos:
  `linux_fd`, `linux_proc`, `linux_mmap`):
  (1) **S1.10 set_tid_address + set_robust_list** --
  estensao de `linux_process`. set_tid_address(tidptr)
  retorna gettid (Linux semantics: NULL ptr aceito,
  apenas limpa storage). set_robust_list(head, len)
  valida `len == 24` (sizeof robust_list_head em x86_64),
  qualquer outro -> -EINVAL. Estado em module-local
  (single-thread); migra para per-task quando S1.4
  clone com thread groups landar.
  (2) **S1.13 pipe2 + dup3** -- novo modulo `linux_fd`.
  pipe2(fds, flags) aceita mascara conhecida
  `O_CLOEXEC|O_NONBLOCK|O_DIRECT` (0x84800), bits fora
  -> -EINVAL, NULL fds -> -EFAULT, no source -> -ENOSYS,
  pipe_create -1 -> -EMFILE; delega ao `pipe_create`
  nativo CapyOS via callback. dup3(oldfd, newfd, flags)
  aceita apenas `O_CLOEXEC` (Linux man), oldfd == newfd
  -> -EINVAL (key diff vs dup2), negative fd -> -EBADF.
  Boot trampoline retorna -1 ate fd table real existir;
  pipe2 funcional desde dia 1.
  (3) **S2.5 /proc/meminfo + S2.6 /proc/<pid>/status**
  -- novo modulo `linux_proc`. meminfo emite 7 fields
  obrigatorios (MemTotal/MemFree/MemAvailable/Buffers/
  Cached/SwapTotal/SwapFree, bytes -> kB round-down).
  pid_status emite 11 fields (Name/State/Tgid/Pid/PPid/
  Uid/Gid/FDSize/VmPeak/VmSize/VmRSS), state letters
  R/S/D/Z/T/X cobertos, Uid/Gid 4-tuple Linux.
  snprintf-style truncation com size-query mode (NULL
  buf retorna required size).
  (4) **S1.2 mmap/munmap/mprotect (parcial Marco M1)**
  -- novo modulo `linux_mmap`. Aceita `MAP_ANONYMOUS|
  MAP_PRIVATE` (apenas) + `PROT_NONE|READ|WRITE|EXEC`.
  fd=-1 e offset=0 obrigatorios. length=0 -> -EINVAL,
  length round-up para PAGE_SIZE. addr alignment
  validada em munmap/mprotect. MAP_FIXED/MAP_SHARED/
  file-backed -> -EINVAL (Marco M2+). VMM callback
  failure -> -ENOMEM. Boot wiring usa
  `vmm_register_anon_region` com bump pointer em
  0x500000000000 (1 TiB arena). munmap returns 0 mas e
  best-effort (frames liberados quando AS destroyed);
  mprotect returns -EINVAL ate VMM ganhar per-page
  prot helper (SpiderMonkey JIT detecta e fallback
  para dual-mapping).
  Wiring: 5 novos objs em CAPYOS64_OBJS (linux_proc,
  linux_fd, linux_fd_init, linux_mmap, linux_mmap_init),
  3 entries em TEST_SRCS, 3 prototipos+chamadas em
  test_runner.c, 2 init_boot calls em kernel_main.c
  (linux_fd_init_boot, linux_mmap_init_boot) apos
  linux_process_init_boot, 2 weak hooks em
  linux_syscall.c (linux_fd_register_syscalls,
  linux_mmap_register_syscalls). 8 novos NRs registrados:
  set_tid_address (218), set_robust_list (273), pipe2
  (293), dup3 (292), mmap (9), munmap (11), mprotect
  (10). Total NRs operacionais: 14 (gettid, sched_yield,
  sched_getaffinity, sched_setaffinity, prctl, prlimit64,
  set_tid_address, set_robust_list, getrandom,
  clock_gettime, pipe2, dup3, mmap, munmap, mprotect).
  Validacao via `_session11_validate_linux_compat_5.py`
  (criado/executado/removido): 0 FAIL, **154/154
  asserts em 9 suites linux_compat** (clock 22 +
  syscall 17 + random 12 + devfs 17 + process 29 +
  cpuinfo 11 + proc 11 + fd 14 + mmap 21).
  freestanding-like syntax check de 3 TUs puras
  (linux_proc, linux_fd, linux_mmap) OK. 12 headers
  linux_compat includeable juntos. **Marco M1
  progress**: cobre AGORA **4/4 das deps core do
  SpiderMonkey** (pthread parcial via gettid/
  sched_yield, clock_gettime ✅, mmap ✅ Marco M1
  subset, open/read/write parcial via pipe2). 12 tasks
  DONE de 28 totais em S1+S2 (~57% kernel base).
- **S1.18 + S1.11 + S1.9 + S1.17 + S2.4 entregues numa
  unica sessao (2026-05-06 sessao 10)** -- cinco tarefas
  concretas adicionais agrupadas em 2 modulos
  (`linux_process` e `linux_cpuinfo`):
  (1) **S1.18 gettid** -- retorna `task_current()->pid` via
  accessor injetavel; CapyOS hoje tem 1 thread por process
  (gettid == getpid), quando S1.4 clone com thread groups
  landar este modulo nao precisa mudar.
  (2) **S1.11 sched_yield + sched_*affinity** -- yield chama
  `task_yield` callback, getaffinity reporta single-CPU
  bitmap (bit 0), setaffinity aceita qualquer mascara
  nao-vazia (no-op em single-CPU). cpusetsize validado
  (multiplo de 8 bytes).
  (3) **S1.9 prctl** -- PR_SET_NAME copia ate 15 chars + NUL
  no `task->name` (Linux 16-byte cap), PR_GET_NAME le 16
  bytes NUL-padded. Outros ops (PDEATHSIG/DUMPABLE)
  retornam -EINVAL aguardando S1.12 signals.
  (4) **S1.17 prlimit64** -- RLIMIT_AS = 8 GiB, RLIMIT_NOFILE
  = 1024, RLIMIT_STACK soft=8 MiB hard=64 MiB. Outros 13
  recursos retornam RLIM_INFINITY. Set -> -EPERM (kernel
  policy fixa). pid != current -> -EPERM.
  (5) **S2.4 /proc/cpuinfo formatter** -- buffer canonical
  Linux 6.x x86_64: 14 fields obrigatorios + 18 flag tokens
  em ordem de prioridade Linux (fpu/tsc/cmov/mmx/sse/sse2/
  pni/ssse3/sse4_1/sse4_2/avx/avx2/fma/popcnt/aes/rdrand/
  rdseed/lm). Multi-CPU emite N blocos com siblings=N e
  cpu cores=N. snprintf-style truncation (returns required
  size, NUL-terminates). Pronto para wirar quando driver
  /proc existir.
  Layering: `linux_<module>_install_ops` / `_reset_for_tests`
  para injetar dependencias. Producao linka via
  `linux_<module>_init.c` com `#if !defined(UNIT_TEST)`.
  Wiring kernel_main.c ordem nova: `x64_timebase_init ->
  linux_clock_init_boot -> linux_random_init_boot ->
  linux_process_init_boot -> linux_syscall_init`. Novo weak
  hook `linux_process_register_syscalls` registra 6 NRs
  (gettid=186, sched_yield=24, sched_getaffinity=204,
  sched_setaffinity=203, prctl=157, prlimit64=302).
  Validacao via `_session10_validate_linux_compat_5.py`
  (criado/executado/removido): 0 FAIL, **104/104 asserts em
  6 suites** (clock 22 + syscall 17 + random 12 + devfs 17
  + process 25 + cpuinfo 11). Marco M1: cobre AGORA 3.5/4
  das deps core do SpiderMonkey (`pthread + clock_gettime
  + mmap + open/read/write` -- temos clock_gettime ✅,
  getrandom ✅ extra, gettid/sched_yield ✅, prctl ✅).
- **S1.1 + S1.8 + S2.7 (+ S2.8 parcial) entregues numa unica
  sessao (2026-05-06 sessao 9)** -- tres tarefas concretas
  acionaveis do firefox-port-platform-shim.md, todas com host
  asserts: (1) **S1.1 dispatcher Linux ABI** -- tabela esparsa
  de 512 slots em `src/kernel/linux_compat/linux_syscall.c`
  com 50+ NRs definidos em `linux_syscall_nrs.h` (read/write/
  open/mmap/clone/futex/epoll/signal/getrandom/prctl/etc.),
  API `register/lookup/dispatch/reset_for_tests`, hooks
  `_register_syscalls` declarados weak para que tests host
  linkem standalone e o kernel build resolva via os modulos
  linkados; default = `-ENOSYS` para NRs nao registrados
  (NUNCA `-EINVAL` -- isso significaria "call malformada"
  quando na verdade nao implementamos ainda); register refusa
  NULL/oor/double-install. **17/17 host asserts**. (2) **S1.8
  getrandom** -- shim em `linux_random.c` delegando a
  `csprng_get_bytes` via callback injetavel
  `linux_random_install_source` (host tests usam
  `counter_source`/`sink_source` deterministicos). Suporta
  `GRND_NONBLOCK\|RANDOM\|INSECURE` (constants 0x0001/0x0002/
  0x0004 == Linux upstream), rejeita flags fora da mascara
  com `-EINVAL`, clipa em `LINUX_GETRANDOM_INT_MAX=33554431`
  (Linux 6.x cap), len=0 retorna 0 mesmo com NULL buf,
  NULL+len>0 retorna `-EFAULT`, no source instalado retorna
  `-EAGAIN`. Registrado em `LINUX_NR_getrandom=318`. Boot
  wiring em `linux_random_init.c` (excluivel via UNIT_TEST)
  chama `csprng_init` + `linux_random_install_source(csprng_get_bytes)`.
  **12/12 host asserts**. (3) **S2.7 + S2.8 parcial pseudo
  /dev** -- `linux_devfs.c` com lookup path-baseado para
  `/dev/{null,zero,full,urandom,random}`. Read semantics
  Linux 6.x: null=EOF, zero/full=fill 0x00, urandom/random=
  delega `linux_getrandom`. Write: null/zero/urandom/random=
  sink (ack len), full=`-ENOSPC` (Linux semantics). Path
  lookup case-sensitive, NULL/missing path -> `LINUX_DEV_NONE`.
  Pronto para wirar no `linux_open()` futuro. **17/17 host
  asserts**. (4) **Wiring kernel_main.c**: nova ordem de init
  em Stage 3/8 -- `x64_timebase_init` -> `linux_clock_init_boot`
  -> `linux_random_init_boot` -> `linux_syscall_init` (este
  ultimo popula a tabela via os hooks weak). (5) **Wiring
  Makefile**: 4 novos objs em CAPYOS64_OBJS (linux_syscall,
  linux_random, linux_random_init, linux_devfs), 3 entries em
  TEST_SRCS. (6) **Wiring test_runner.c**: 3 prototipos + 3
  chamadas. Validacao via script Python
  `_session9_validate_linux_compat_3.py` (criado/executado/
  removido): 0 FAIL, 0 WARN cobrindo arquivos+tokens (11
  arquivos, 49 tokens), Makefile/test_runner/kernel_main
  wiring (incluindo ordem temporal correta), build host
  `cc + UNIT_TEST + -Werror` -> **68/68 asserts em 4 suites**
  (clock 22 + syscall 17 + random 12 + devfs 17),
  freestanding-like (CFLAGS64-like) syntax check de 4 TUs
  puras OK, headers includeable juntos sem erros. **Marco M1
  (SpiderMonkey shell em CapyOS) progress**: agora cobre
  2.5/4 das deps core do SpiderMonkey
  (`pthread + clock_gettime ✅ + mmap + open/read/write` +
  `getrandom ✅` extra); o dispatcher S1.1 e a fundacao para
  todos os outros S1.x landarem. S1.1, S1.8, S2.7, S2.8
  marcados em `firefox-port-platform-shim.md`.
- **Etapa 5 hardening (rate limit + URL policy + bytes obs +
  audit log)** — chrome_runtime ganha:
    1. **Rate limiter incoming**: contadores `incoming_in_window` +
       `total_incoming_drops`, novo status `POLL_RATE_LIMITED`,
       `chrome_runtime_poll_event` recusa quando excede
       `CHROME_RUNTIME_INCOMING_RATE_MAX = 64/tick`;
       `chrome_runtime_tick` reseta. browser_app trata RATE_LIMITED
       como break do loop.
    2. **URL whitelist policy (opt-in)**: novo setter
       `chrome_runtime_set_url_policy(fn)`. Quando instalado,
       `chrome_runtime_send_navigate` chama o callback antes de
       escrever no pipe; deny incrementa `total_url_blocked` e
       retorna -1 sem tocar o I/O.
    3. **Bytes observability**: contador `total_event_bytes_received`
       (u64) acumulado de header+payload de cada evento admitido,
       util para detectar engine vazando memoria via spam.
    4. **Audit log subsystem**: modulo dedicado
       `src/apps/browser_chrome/audit_log.{c,h}` com ring buffer de
       32 entradas (categoria + code + seq monotonico). Categorias:
       NAV, RATE_DROP, POLICY_DENY, ENGINE_EOF, PROTOCOL, FETCH.
       API: `capyc_audit_init/record/count/visible/at`. Hooks em
       `chrome_runtime_send_navigate` (NAV / POLICY_DENY),
       `chrome_runtime_poll_event` (RATE_DROP / ENGINE_EOF /
       PROTOCOL), `chrome_runtime_dispatch_pending_fetch` (FETCH).
       Ring envolve para sobrescrever entrada mais antiga em O(1);
       sem alocacao dinamica. 4 testes unitarios diretos do ring +
       3 de integracao com runtime; total **+45 asserts** distribuidos
       em `test_browser_chrome_runtime_rate.c` (3 audit + 3 rate +
       3 policy + 1 bytes), `test_browser_chrome_audit.c` (4 testes:
       init zera, record em ordem, wraparound, null safety).
- **Etapa 4 a+b** — HTTP/HTTPS reais via bridge kernel-side
  (`net/http.h` + BearSSL TLS 1.2), URL auto-prefix `http://`.

**Criticidade:** alta. **Depende de:** F1 (M5 mergeado). **Paralelo
possivel com:** F2, F4. **Branch sugerida:** concluido em `main`/`develop`.

#### Fechamento de F3

F3 fecha o escopo de browser ring-3 isolado: processo userland,
watchdog, IPC binario, navegação, fetch, render HTML basico, forms,
tabelas, imagens, cache de imagens, raster blit e hardening pre-JS.
Itens que antes apareciam como "restante" foram reclassificados para
fases corretas:

- fonte real/CapyUI polish -> F6/F7;
- syscall filter/seccomp-like -> F9 sandbox/hardening;
- smoke visual QEMU com PNG real -> esteira CI/release, nao gap de F3.

##### Etapa 3 seção a — Imagens inline (pipeline fetch+decode ✅ wiring; QEMU smoke pendente)

**Progresso 2026-05-03 + 2026-05-05 (1) + 2026-05-05 (2):**
parser/render/raster suportam `<img>` com placeholder visivel +
atributos `width`/`height` parseados (ver "Entregas consolidadas"
acima). Em 2026-05-05 (segunda iteracao) o pipeline IPC completo
foi adicionado de ponta-a-ponta:

- **IPC novo:** `BROWSER_IPC_EVENT_IMAGE_REQUEST` (kind 0x000A,
  payload 10 B + url) e `BROWSER_IPC_IMAGE_RESPONSE` (kind 0x010A,
  payload 18 B + pixels BGRA32). Codecs em
  `src/apps/browser_ipc/image.c` com helpers
  `browser_ipc_image_request_encode/decode` e
  `browser_ipc_image_response_encode/decode`. Status enum:
  `IMAGE_OK / TRANSPORT_ERR / DECODE_ERR / OVERSIZED / UNSUPPORTED`.
- **Cache no engine ring 3:** `userland/bin/capybrowser/image_cache.{h,c}`
  -- 4 slots × 240×180 BGRA = ~675 KiB em `.bss`. Cada slot guarda
  url (hash + bytes), nav_id, status (PENDING/OK/ERROR), pixels
  inline. APIs: `image_cache_init/lookup/alloc/record_response/`
  `find_url/invalidate_other_navs`. Allocacao baseada em
  `next_img_id` monotonico para casar respostas tardias com slots.
- **Engine wiring:** `engine_request_images_for_doc` walk no doc
  apos parse, emite REQUEST para cada `<img>` sem cache hit;
  `engine_handle_image_response` decodifica RESPONSE, atualiza
  cache, dispara re-render se nav_id corrente. Pos-processa lista
  de `capyhtml_cmd` apos `capyhtml_layout`: para cada CMD_IMAGE
  com cache hit OK, popula `cmd->image_pixels/w/h`.
- **Chrome handler:** `handle_image_request` em `chrome.c` stages
  pending slot (mesmo padrao do `pending_fetch`);
  `browser_chrome_take_pending_image` drena.
  `chrome_runtime_dispatch_pending_image` em `runtime_image.c` resolve
  via `try_http_fetch` (HTTP/HTTPS), detecta magic bytes
  PNG/JPEG, decodifica via `png_decode`/`jpeg_decode` (kernel-side
  `gui/png_loader.c`/`jpeg_loader.c`), valida bounds (240×180 max),
  encoda IMAGE_RESPONSE com pixels BGRA. Em UNIT_TEST sempre
  retorna UNSUPPORTED (decoders pulled out).
- **Raster:** `draw_image_cmd` em `capyhtml/src/raster.c` agora
  blitta pixels BGRA32 quando `cmd->image_pixels != NULL`,
  caindo no placeholder so quando o cache esta MISS/PENDING/ERROR.
  Loop pixel-by-pixel reconstrui ARGB32 dos bytes BGRA para o
  `put_pixel` de bound-check (clipa a `cmd->w x cmd->h`).
- **Tests host:** +119 asserts (45 ipc image codec, 47 image cache,
  27 chrome handler+dispatch).

**Follow-ups de validação (não bloqueiam F3):**

- **QEMU smoke:** rodar pagina real `<img src="http://.../foo.png">`
  e validar pixels reais aparecem (host tests cobrem so IPC; o
  decode real `png_decode/jpeg_decode` em ring 0 nunca foi
  exercitado ponta-a-ponta com este pipeline).
- **Tests host para raster blit:** entregue em 2026-05-05 (#3) com
  `test_image_cmd_blits_pixels_when_provided`,
  `test_image_cmd_blits_clipped_to_cmd_dims` e
  `test_image_cmd_falls_back_to_placeholder_when_no_pixels`.
- **`userland/` no `audit_source_layout.py`:** `DEFAULT_ROOTS` pula
  userland/, entao `main.c` (1768 linhas) escapa do warn de
  monolito. Ortogonal a esta entrega mas vale anotar.

**Criterios de aceite:**

- ~~`<img src=...>` recebe IMAGE_REQUEST ao parsear~~ ✅
- ~~Cache evita re-fetch na mesma navegacao~~ ✅ (lookup hit
  PENDING/OK/ERROR -> nao re-emite)
- ~~Cache invalida slots de navs antigas~~ ✅
  (`image_cache_invalidate_other_navs`)
- ~~Falha de decode mantem o placeholder com alt text~~ ✅
  (raster cai no placeholder se status != OK)
- ~~Largura/altura vem de atributos HTML quando presentes~~ ✅
- `<img src="http://.../image.png">` em pagina real deve renderizar
  pixels decodificados (PNG/JPEG) no lugar do placeholder
  (validacao QEMU/CI).

##### Etapa 3 seção c — Form inputs (MVP+polish ✅; refinamentos avancados pendentes)

**Progresso 2026-05-03:** parser/render/raster/engine/runtime/browser_app
suportam `<form>` + `<input type="text/submit/password">` end-to-end
incluindo indicador visual de foco (borda 2 px HEADING + caret) e
percent-encoding RFC 3986 (`%XX` para chars nao-unreserved). Ver
"Entregas consolidadas" acima.

**Gap remanescente (impacto medio):**

- **`<textarea>` e `<select>/<option>`**: nao parseados ainda;
  textarea pode reusar TAG_INPUT com flag de multilinha; select
  precisa de novo node + listbox dropdown.
- **Submit por POST**: hoje so GET (query string em URL); para POST,
  precisa-se de body separado no NAVIGATE + `method` no FORM node.
- **Validacao client-side basica**: `required`, `minlength`,
  `pattern` ignorados; aceitar tudo no submit.
- **Cursor blink**: caret atualmente solido; visualmente parece
  fixo. Idealmente alterna 500 ms via timer.

**Criterios de aceite (remanescentes):**

- `<textarea>` aparece como caixa multi-linha editavel.
- Form com `method="post"` envia body separado.

##### Etapa 3 seção d — Tabelas (MVP ✅; refinamentos pendentes)

**Progresso 2026-05-03:** parser/render/raster suportam tabelas
2D simples com `<table>/<tr>/<td>/<th>`. Layout em grade fixa
(largura igual por coluna), borda 1 px LINK ao redor de cada
celula, header (TH) com bg MUTED + texto HEADING. Ver
"Entregas consolidadas" acima.

**Gap remanescente:**

- **Box model CSS inline `style=`**: hoje sem padding/margin/border
  customizaveis; reativar parser minimalista de `style="padding:4px;
  border: 1px solid #x"` (reusa logica do
  `src/apps/css_parser/` removido na slice 6 — portar o minimo para
  userland).
- **`colspan` / `rowspan`**: hoje cada celula ocupa exatamente 1
  posicao de grade; tabelas reais usam spans para "merge" de celulas.
- **Larguras variaveis por coluna**: hoje `cell_w = avail_w / cols`
  igual; sites reais dependem de auto-fit por conteudo.
- **Scroll horizontal** quando `cols * TABLE_MIN_CELL_W > viewport_w`:
  hoje a tabela inteira e descartada (`table_col_count = 0`); melhor
  emitir mesmo assim e deixar o usuario scrollar.

**Criterios de aceite (remanescentes):**

- `<div style="padding: 8px; border: 1px solid #333">` mostra borda.
- `<td colspan="2">` ocupa duas colunas.
- Tabela com 8 colunas em viewport 480 px ainda eh visivel
  (scroll horizontal ou auto-fit text).

##### Follow-up CapyUI — Fonte real (não bloqueia F3)

**Status:** reclassificado para CapyUI/F6. A fonte 8×8 atual e suficiente
para fechar o escopo funcional de F3; uma fonte melhor e polish visual.

**Entregaveis:**

- Parser TTF minimalista ou bundle de bitmap 16×16 de open-source
  font (`userland/assets/fonts/capy-16.bmp`).
- `capyhtml_font_ops_default` passa a apontar para a nova fonte.
- Glyph cache em `.bss` (256 glifos × 16×16 = 64 KiB).

**Criterios de aceite:**

- Texto renderizado tem serifas/proporcoes razoaveis (nao mais
  pixel-blocky 8×8).
- `capyhtml_raster` zero-copy continua funcionando.

##### Etapa 5 — Hardening pré-JS (fechado para F3)

**Progresso 2026-05-03 + 2026-05-05 (#1, #2, #3):** runtime ganhou
rate limiter incoming, URL whitelist opt-in, observability bytes
contador, audit log subsystem, enforcement de budget de bytes por
navegacao (#1), pipeline IPC de imagens com cache + invalidation
por nav (#2) e callback sink no audit log (#3). Esse conjunto fecha o
hardening pre-JS esperado de F3.

**Follow-up de segurança:** o filtro seccomp-like do engine foi movido
para F9/sandbox. Ele depende de política central de syscall por processo
e fica mais coerente junto do futuro JS engine sandboxed.

**Entregaveis (remanescentes):**

- **Seccomp-like syscall filter** (F9) aplicado ao engine em
  `browser_engine_spawn`: bloqueia `open/exec/socket/etc`. So permite
  `read/write/exit/yield/mmap limitado`. Detalhe de impl: precisa de
  hook no scheduler/syscall dispatch para checar pid e abortar com
  EPERM, opcionalmente registrando audit log no chrome via debugcon
  sink (ja existe!).
- ~~Memory budget per nav~~ ✅ 2026-05-05 (#1) (chrome contabiliza bytes
  IPC por nav e mata engine em excesso; ver "Entregas consolidadas").
- ~~IPC rate limiting~~ ✅ 2026-05-03.
- ~~URL whitelist/blacklist~~ ✅ 2026-05-03 (opt-in via
  `chrome_runtime_set_url_policy`).
- ~~Audit log~~ ✅ 2026-05-03 (ring buffer); ~~debugcon callback~~ ✅
  2026-05-05 (#3) (`capyc_audit_set_sink(st, fn)` opt-in; sink recebe
  `(category, code, seq)` apos cada record commitado; default = NULL =
  no-op; kernel pode wirar a `debugcon_writes` sem alocar; +11 asserts
  host).

**Criterios de aceite:**

- ~~Engine com loop infinito e morto por rate limit antes de travar
  o chrome~~ ✅ 2026-05-03.
- ~~Engine que vaza memoria via spam de eventos morto pelo budget~~
  ✅ 2026-05-05 (#1).
- ~~Audit log inspecionavel via klog/debugcon em runtime sem
  vazamento de struct internals~~ ✅ 2026-05-05 (#3).
- Engine tentando abrir um arquivo e bloqueado por syscall filter
  (audit log evidencia). Reclassificado para F9.
- Tests: ~~`test_browser_chrome_runtime_rate.c` cobre rate + URL
  policy + bytes obs + audit log + nav budget~~ ✅; ~~tests para
  audit sink (default NULL, called per record, replace/disable)~~ ✅
  2026-05-05 (#3); testes de syscall filter entram em F9.

##### Validação visual em QEMU (esteira)

- **Depende de:** cross-toolchain `x86_64-elf-*` em CI.
- **Alvo:** `make smoke-x64-browser-spawn` passa com 9 markers
  debugcon. Harness ja pronto em
  `tools/scripts/smoke_x64_browser_spawn.py`.

#### Saida entregue

M8.2 fechado; browser e o primeiro app real em ring-3 com HTML/CSS
maturity suficiente para paginas web comuns (texto, links, imagens,
forms, tabelas) e com hardening que impede crash-as-a-service.

---

### F4 — Stack de sockets userland + TLS (libcapy-net + libcapy-tls)

**Status:** 🟡 **98%** — Etapa 4 seções a (HTTP) + b (HTTPS) antecipadas via bridge kernel-side em F3.3g (2026-05-02). Seção c já entregou socket syscalls, façade TCP, resolver DNS, parser URL, HTTP/1.1 GET, TTL real, negative caching RFC 2308 e hardening anti-CRLF do URL parser, do HTTP request builder, do parser de headers e Content-Length estrito/consistente e Transfer-Encoding fail-closed e drain por body_received e EOF curto fail-closed e Content-Length zero conhecido e obs-fold fail-closed e Content-Length raw e validação completa de headers e status no-body e Content-Encoding fail-closed e 1xx fail-closed e status-line estrita e LF-only head e header separator fail-closed e header block truncation fail-closed e URL fragment stripping e host authority hardening e request-target fragment/backslash/%00 fail-closed e DNS label boundary hardening e request builder port-zero fail-closed, API userland fail-closed de libcapy-tls, adaptador HTTPS fail-closed entre `libcapy-net` e `libcapy-tls`, política compartilhada de hostname TLS em userland/kernel, peer verification obrigatório, janela de timeout, snapshot de configuração, contexto preparado, ciclo de vida do contexto, slot interno de contexto, connect usando slot gerenciado, backend stub fail-closed, estado backend TLS preparado, trust metadata TLS preparado, fonte userland de trust anchors, catálogo userland de trust anchors, invariantes do catálogo TLS, tabela metadata-only de slots TLS, descritores metadata-only TLS e resumo agregado metadata-only TLS + manifesto metadata-only TLS em `libcapy-tls`. Falta backend BearSSL userland real + migração do `capybrowser`.
**Criticidade:** alta — destrava update real, browser HTTPS, qualquer IPC de rede futuro.
**Depende de:** F1 (M5 fork/exec) + parcialmente F3 (motivação real). Pode iniciar antes de F3 fechar.
**Paralelo possível com:** F3.
**Risco:** alto — toca rede, criptografia e ABI userland.
**Esforço:** 5–8 sessões.
**Branch sugerida:** `feature/f4-userland-net`.

#### Progresso 2026-05-02 (bridge kernel-side entregue em F3.3g)

- ✅ HTTP real funcional no browser: `chrome_runtime_dispatch_pending_fetch` em `src/apps/browser_chrome/runtime.c` prefixo-checa `http://` e delega a `http_get()` de `src/net/services/http/redirect_download.c` (stack TCP + headers + redirects + BearSSL TLS 1.2 já presente no kernel antes de F4).
- ✅ HTTPS reutiliza o mesmo caminho (`http_get` escolhe TLS via `use_tls` dentro do `http_parse_url`).
- ✅ Buffers IPC escalados: chrome `g_fetch_response_scratch` 1 MiB + 4 KiB em `.bss`; engine ring 3 `g_request_payload` + `g_fetch_scratch` 1 MiB + 4 KiB cada.
- ⏳ Este bridge **não é o design final de F4**: é uma ponte que mora no kernel, violando temporariamente o princípio "apps em ring 3 não chamam syscalls HTTP diretamente". Quando F4 c/d chegarem, o bridge migra para uma call em `libcapy-net` e o `chrome_runtime_dispatch_pending_fetch` passa a ser pure-orchestration.
- ⏳ Sem isolamento criptográfico em ring 3: BearSSL roda em kernel context; um bug na engine TLS afeta o kernel. F4 c/d recompilam BearSSL como lib userland.

#### Progresso 2026-05-08 (seção c parte 1/2 — kernel-side socket syscalls)

- ✅ **Syscalls 28..34 reservados** em `include/kernel/syscall_numbers.h`: `SYS_SOCKET`, `SYS_BIND`, `SYS_LISTEN`, `SYS_ACCEPT`, `SYS_CONNECT`, `SYS_SEND`, `SYS_RECV`. Os 7 entries são distribuídos pelo `syscall_table` em `syscall_init()` via `syscall_net_register_handlers()`.
- ✅ **`FD_TYPE_SOCKET = 3`** adicionado em `include/kernel/process.h` (junto com `FD_TYPE_FREE/VFS/PIPE`). Slot guarda o kernel-side socket fd em `private_data` (cast por `intptr_t`).
- ✅ **`src/kernel/syscall_net.c` (~280 linhas)** com os 7 handlers + bridges `syscall_net_fd_read/write` (chamados pelo `sys_read/sys_write` em `syscall.c` quando o slot é `FD_TYPE_SOCKET`). Backend é injetável via `syscall_net_install_ops(struct syscall_net_ops *)` — produção registra `socket_*` (em `src/net/services/socket.c`); host tests registram fake recorder.
- ✅ **`src/kernel/syscall_net_init.c`** (TU separado, mantém net-stack deps fora dos host tests) instala o backend de produção via `syscall_net_install_default_ops()` e registra `socket_close` no lifecycle hook do FD process via `process_fd_register_socket_close()`. Wired em `src/arch/x86_64/kernel_main.c` logo após cada `syscall_init()` (ambos os caminhos EBS e post-EBS).
- ✅ **`process_fd_free` estendido** em `src/kernel/process.c`: para `FD_TYPE_SOCKET` chama o close hook registrado, mantendo `process.c` sem dependência hard de `socket.c` (host tests linkam process.c standalone).
- ✅ **`sys_read` / `sys_write` / `sys_close`** em `src/kernel/syscall.c` despacham `FD_TYPE_SOCKET` para o backend net (bridge `syscall_net_fd_read/write`), garantindo `read(sockfd, ...)` e `recv(sockfd, ..., 0)` com semântica idêntica.
- ✅ **Userland stubs**: `userland/lib/capylibc/syscall_stubs.S` ganhou `capy_socket/bind/listen/accept/connect/send/recv` (4-arg `send`/`recv` movem `%rcx → %r10` para a SYSCALL ABI). `userland/include/capylibc/capylibc.h` declara o C API + `struct capy_sockaddr_in` + constantes `CAPY_AF_INET / CAPY_SOCK_STREAM / CAPY_SOCK_DGRAM`.
- ✅ **Host test `tests/test_syscall_net.c` — 17/17 OK**:
  - rejeita domain ≠ `AF_INET` e type desconhecido sem chamar backend;
  - aloca FD slot ≥ 3 com `type == FD_TYPE_SOCKET` e kernel-fd embedded;
  - `sys_bind/connect/send/recv` repassam pro backend o kernel fd correto e copiam `sockaddr_in` byte-a-byte;
  - rejeita `addrlen` curto e fd não-socket (caso fd 5 = pipe);
  - `process_fd_free` dispara o close hook **uma única vez** com kernel-fd correto e libera o slot (`FD_TYPE_FREE`);
  - sem backend instalado, toda a família retorna `-1` deterministicamente (sem crash);
  - `sys_read` em socket fd vai pra `sock_recv`; `sys_write` em socket fd vai pra `sock_send`.
- ✅ **Wiring de build**: `Makefile` adiciona `kernel/syscall_net.o` + `kernel/syscall_net_init.o` em `CAPYOS64_OBJS`; `tests/test_syscall_net.c` + `src/kernel/syscall_net.c` entram em `TEST_SRCS`; `tests/test_runner.c` chama `test_syscall_net_run()`.
- ✅ **Bug pré-existente corrigido (2026-05-08, seção c parte 2/2)**: `socket_system_init()` (`src/net/services/socket.c:46`) **nunca era chamado em produção**. Não era bug ativo (o `socket_table` é zero-initialized estatic e `socket_initialized=1` por default), mas re-init paths ou hot code-reload ficariam expostos. Agora é chamado de `syscall_net_install_default_ops()` (idempotente entre os caminhos EBS e post-EBS).

#### Progresso 2026-05-08 (seção c parte 2/2 — `libcapy-net` userland façade)

A parte 1/2 deu a userland `capy_socket / bind / listen / accept / connect / send / recv` brutos via syscall stubs. A parte 2/2 monta a façade alto-nível que apps reais (capybrowser, update-agent) querem usar:

- ✅ **Estrutura nova**: `userland/lib/capylibc-net/` (4 TUs ~280 LoC) + header público em `userland/include/capylibc-net/capy_net.h`. Exposto como objeto separado via `$(CAPYLIBC_NET_OBJS)` no Makefile (target `capylibc-net`); apps que não precisam de rede (`hello`, `capysh`) não pagam ~6 KB extra.
- ✅ **Byte-order helpers** (`capy_net_endian.c`): `capy_htons / htonl / ntohs / ntohl` puros C (host x86_64 e arm64 ambos LE → swap incondicional). Round-trip testado.
- ✅ **IPv4 parser/formatter estrito** (`capy_net_inet.c`): `capy_inet_pton4("1.2.3.4", &ip)` produz `0x01020304` host-order; `capy_inet_ntoa4(ip, buf, cap)` formata para `"1.2.3.4"` em buffer ≥ 16 bytes. Rejeita: empty / leading dot / trailing dot / non-digit / octet > 255 / 4-digit octet (`"1.2.3.0001"`) / hex / octal / NULL. **NÃO** aceita formas curtas (`"1.2"`, `"1.2.3"`) que o `inet_aton(3)` POSIX historicamente aceita — escolha consciente: o parser histórico tem track record de CVEs (CVE-2014-9263 família) e os callers F4 reais hospedam sempre o output de um resolver, então strict é suficiente.
- ✅ **TCP client com rollback** (`capy_net_tcp.c`): `capy_tcp_connect_ip4(ip_host_order, port_host_order)` cria socket via `capy_socket(AF_INET, SOCK_STREAM, 0)`, monta `struct capy_sockaddr_in` com `htons(port) / htonl(ip)`, chama `capy_connect`. Em qualquer falha (socket -1, connect -1) **rola back via `capy_close(fd)`** -- não vaza FD slot. `capy_tcp_connect_str("10.20.30.40", 443)` adiciona o passo de parse via `capy_inet_pton4`. Exigiu adicionar **`capy_close` stub** novo em `userland/lib/capylibc/syscall_stubs.S` (issuance direto de `SYS_CLOSE`, `int $0x80` ABI; antes não havia caller userland que precisasse fechar FD não-stdio).
- ✅ **Helpers de I/O com semântica robusta**:
  - `capy_send_all(fd, buf, len)` loopa até todos os bytes serem aceitos; em `send==0` (no progress) retorna parcial; em `send==-1` mid-stream retorna parcial > 0 ou -1 se falhou na primeira call. Nunca spinning forever.
  - `capy_recv_all(fd, buf, cap)` é pass-through limpo do `capy_recv`.
  - `capy_recv_until(fd, buf, cap, terminator)` lê 1 byte por vez (via `capy_recv(fd, &one, 1)`), pára no terminador (incluído no count), em cap esgotado, em EOF (parcial) ou em erro (-1 ou parcial).
- ✅ **Errno-style discrimination**: `capy_net_last_error()` expõe a falha mais recente (`CAPY_NET_EINVAL / EPARSE / ESOCK / ECONNECT / ESEND / ERECV / EBUF`). Reset para `CAPY_NET_OK` no início de cada call. Single static int por enquanto; TLS landa com pthread no roadmap longo-prazo.
- ✅ **Host test `tests/test_capylibc_net.c` -- 44/44 OK**:
  - 3 endian asserts (htons/ntohs round-trip, htonl/ntohl round-trip, htons(0)==0).
  - 13 inet_pton4 (4 happy + 9 rejection cases, incluindo `"1.2.3.0001"` e NULL).
  - 6 inet_ntoa4 (3 happy + 2 rejection + 1 round-trip).
  - 3 tcp_connect_ip4 (happy path com check do byte-swap exato; socket fail; connect fail roll-back).
  - 3 tcp_connect_str (parse OK; bad IP short-circuita sem chamar capy_socket; NULL).
  - 6 send_all (short writes; zero-stops; mid-stream err parcial; first-call err -1; len=0; bad args).
  - 1 recv_all pass-through.
  - 5 recv_until (terminator; cap-exhaust; EOF; bad args).
  - 2 last_error (refletido após falha; reset após sucesso).
  - O test substitui `capy_socket / connect / send / recv / close` por C fakes com knobs (chunk size, fail-at-Nth-call, EOF-after-N) -- libcapy-net não observa que o backend é fake, espelhando o pattern de `tests/test_syscall_net.c`.
- ✅ **Wiring**: `Makefile` adiciona `$(CAPYLIBC_NET_OBJS)` + target `capylibc-net`; `TEST_SRCS` ganha test + 4 TUs do libcapy-net; `tests/test_runner.c` chama `test_capylibc_net_run()`.

#### Progresso 2026-05-08 (seção c parte 3/3 — DNS resolver via cache)

A parte 2/2 fechou o ABI alto-nível com IPs literais. A parte 3/3 fecha o último gap entre `libcapy-net` e apps reais que recebem hostnames: agora `capy_tcp_connect_host("example.com", 80)` resolve via cache do kernel e conecta. **Não inclui** active DNS resolver (UDP queries) -- foi reclassificado como parte 4/4 do mesmo seção c, porque exige polling, timeouts e parse de DNS messages que justificam um TU dedicado:

- ✅ **Novo syscall `SYS_DNS_RESOLVE = 41`** em `include/kernel/syscall_numbers.h` (SYSCALL_COUNT 41 → 42). 3-arg ABI: `rdi = const char *name`, `rsi = uint32_t *out_ip` (host-order on hit), `rdx = int flags` (reserved-must-be-0). Retorna 0 em hit, -1 em miss / NULL args / non-zero flags / no installed backend.
- ✅ **Backend injetável**: `struct syscall_net_ops` ganha campo `dns_resolve` em `include/kernel/syscall_net.h`; produção wira `dns_cache_lookup` direto em `src/kernel/syscall_net_init.c`. NULL no slot é permitido (testes que não exercitam DNS).
- ✅ **`dns_cache_init()` agora roda em produção** lock-step com `socket_system_init()` em `syscall_net_install_default_ops`. Anteriormente era idempotent-zero em static .bss; o fix garante rebuilds limpos pós re-init paths (mesma motivação do bug fix de `socket_system_init` da parte 2/2).
- ✅ **`capy_dns_resolve` stub em userland**: `userland/lib/capylibc/syscall_stubs.S` + `userland/include/capylibc/capylibc.h`. 3 args, pass-through limpo (ABI x86_64 já tem `rdi/rsi/rdx` corretos).
- ✅ **libcapy-net resolver TU** (`userland/lib/capylibc-net/capy_net_resolve.c`):
  - `capy_resolve_host_ip4(host, &ip)` faz `inet_pton4` first (literal IPv4 fast path -- "8.8.8.8" nunca toca DNS), em parse-fail cai para `capy_dns_resolve`. Mesmo padrão de `getaddrinfo(AI_NUMERICHOST)` no glibc/musl.
  - `capy_tcp_connect_host(host, port)` é resolve + `capy_tcp_connect_ip4`. API "do certo por default": apps que recebem URL do user passam direto, apps security-sensitive (ex: updater hard-coded a IP) continuam usando `capy_tcp_connect_str` que rejeita hostnames.
- ✅ **Novo erro `CAPY_NET_EDNS = -8`** no enum `capy_net_err_t`. `capy_net_last_error()` discrimina DNS miss de outros failures (parse, socket, connect).
- ✅ **Host tests**:
  - `tests/test_syscall_net.c` ganha `fake_dns_resolve` no fake ops + 4 testes (`test_dns_resolve_hit/miss/validation/no_backend`). 17 → **21/21 OK**.
  - `tests/test_capylibc_net.c` ganha fake `capy_dns_resolve` + 9 testes (literal short-circuit; DNS fallback hit/miss; NULL host/out_ip; connect_host literal; connect_host hostname com check do byte-swap exato; connect_host miss não aloca socket; NULL host). 44 → **53/53 OK**.
- ✅ **Wiring**: `Makefile` adiciona `capy_net_resolve.o` em `$(CAPYLIBC_NET_OBJS)` + em `TEST_SRCS`. `syscall_net_init.c` já estava em `CAPYOS64_OBJS`; `dns_cache.c` também (boot path). Não há novos objs no kernel build.

**Decisão de design**: a kernel deliberadamente NÃO promove cache miss em active query nesta iteração. Isso significa que apps que precisam de hostnames nunca-antes-vistos batem `CAPY_NET_EDNS`. A justificativa é arquitetural -- sys_dns_resolve é síncrono e o kernel não tem business hospedando um DNS resolver completo (timeouts, retries, parse). O resolver ativo land na parte 4/4 como um TU userland que fala UDP/53 via `libcapy-net` (UDP socket primitive futuro) e popula a cache via `dns_cache_insert` exposto. Para o caso comum (cache pre-warmed via DHCP discovery + página visitada antes), parte 3/3 já é suficiente.

- ⏳ **Próximo (seção c parte 4/4)**: active DNS resolver userland. Requer `SYS_SOCKET(SOCK_DGRAM)` no backend (já mapeado), DNS query builder em `userland/lib/capylibc-net/capy_net_dns.c`, response parser, fallback a múltiplos resolvers de `/etc/resolv.conf` quando configfs landar.
- ⏳ **Depois disso**: seção d (`libcapy-tls`) + migração do `capybrowser` para libcapy-net.

#### Progresso 2026-05-08 (seção c parte 5 — HTTP client em libcapy-net)

A parte 3/3 entregou conectividade host→IP→TCP. A parte 5 fecha o último degrau do stack HTTP-clear: apps agora têm `capy_http_get(url, body_buf, cap, &resp)` direto -- sem precisar reimplementar status-line + header parsing + Content-Length em cada binário. Isso desbloqueia (a) migração do `capybrowser` para libcapy-net (HTTP only), (b) update-agent CLI fetch via HTTP, (c) qualquer ferramenta de diagnóstico que queira `curl` em userland.

Decisão de numeração: pulei a parte "4" para reservar o espaço lexical do *active DNS resolver* (que está conceitualmente entre "DNS via cache" e "HTTP", mas fica para depois porque exige UDP socket primitives novos). Parte 5 land hoje porque (a) é foundational para F5 e (b) não depende de UDP.

Entregas:
- ✅ **`userland/lib/capylibc-net/capy_net_url.c`** (~150 LoC): `capy_url_parse(url, &parts)` strict-by-design. **Rejeita** userinfo (`user:pass@host` -- a classe de bug do *URL spoofing* CVE-2017-1000099 e amigos), IPv6 literals (`[::1]` -- kernel net stack é IPv4-only hoje), port=0, port>65535, scheme != http/https, host vazio e CR/LF/tabs/DEL/controles brutos em host/path/query. **Aceita** path com query string, fragmento e percent-encoding (preservados verbatim em `parts.path`); URL sem path explícito recebe `"/"` sintético.
- ✅ **`userland/lib/capylibc-net/capy_net_http.c`** (~330 LoC):
  - `capy_http_get(url, body_buf, cap, &resp)` one-shot. Pipeline: `capy_url_parse` → reject HTTPS (libcapy-tls é seção d) → `capy_tcp_connect_host` → `capy_http_build_get_request` → `capy_send_all` → ler até `\r\n\r\n` em buffer 4 KB → `capy_http_parse_status_line` → `capy_http_parse_headers` → drain body com truncation flag → `capy_close`.
  - Headers automáticos: `Host:`, `User-Agent: capylibc-net/0.1`, `Accept: */*`, `Connection: close`. Sem keep-alive nesta iteração (Conn:close baked-in simplifica drain).
  - **Truncation graceful**: se body do servidor é maior que `body_buf_cap`, copia o que cabe + `resp->truncated = 1` + drena o resto via scratch para o kernel não ficar com buffer cheio.
  - **Chunked rejeitado**: `Transfer-Encoding: chunked` retorna `CAPY_NET_EUNSUPPORTED`. Production HTTP/1.1 servers podem usar chunked sem Content-Length, mas implementar parser de chunks aqui dobraria o tamanho do TU. Caller que precisa hoje pode usar `Connection: close` (servidor manda EOF e nós Content-Length=0 + drain). Smoke do nosso plano (HTTP fetch para tar.gz com Content-Length conhecido) não exige chunked.
  - **HEAD-style probe**: `body_buf == NULL` com `cap == 0` é permitido; body é lido e descartado (mantém socket clean).
- ✅ **Helpers públicos exposed para reuse / pinning de testes**:
  - `capy_http_build_get_request(host, port, path, buf, cap)` -- valida host/path contra controles brutos e path relativo nao vazio, então formata "GET path HTTP/1.1\r\nHost: host[:port]\r\n..." sem alocar; `:port` só emitido se != 80.
  - `capy_http_parse_status_line(buf, len, &code)` -- aceita HTTP/1.0 e HTTP/1.1, rejeita HTTP/2.0 (não temos parser binary frame), valida 3 dígitos, retorna byte-count após CRLF/LF.
  - `capy_http_parse_headers(buf, len, &resp)` -- valida nomes como token HTTP, rejeita valores com controles brutos perigosos, exige `:` em linhas não vazias, exige linha terminada e linha vazia final, aplica RFC 7230 OWS strip (espaços + tabs + trailing CR), até `CAPY_HTTP_MAX_HEADERS = 16` entries, header overflow só trunca armazenamento público.
  - `capy_http_response_find_header(&resp, name)` -- lookup CI.
- ✅ **API pública em `userland/include/capylibc-net/capy_net.h`**: structs `capy_url_parts` (host[192], path[768]), `capy_http_response` (status_code + headers[16] + body_len + content_length + truncated). Novos erros `CAPY_NET_EHTTP = -9` (malformed response) e `CAPY_NET_EUNSUPPORTED = -10` (HTTPS, chunked).
- ✅ **Host tests** (`tests/test_capylibc_net.c`):
  - **+20 URL parsing**: minimal `http://example.com`; full `https://host:8443/foo/bar`; query string preservada; query-only sintetiza `/?...`; fragmentos removidos do path; query preservada antes de fragmento; reject backslash no host; reject percent no host; reject empty host label; reject hyphen-edge host label; default port 443; reject ftp; reject userinfo (CVE class); reject IPv6 literal; reject empty host; reject port 0; reject port > 65535; reject NULL; reject raw CRLF no path; reject raw tab na query.
  - **+10 request builder**: default port 80 não emite `:80` em Host; custom port 8080 emite; reject port 0; buf overflow detected; reject raw CRLF no host; reject ambiguous host char; reject bad host label boundary; reject raw tab no path; reject fragment no path; reject path relativo nao vazio.
  - **+8 status line**: 200 OK; 404 Not Found com LF-only terminator; reject HTTP/2.0; reject non-digit code; reject status fora de 100..599; reject status sem separador antes da reason phrase; reject controle bruto na reason phrase.
  - **+11 header parser**: 2 headers basic; OWS stripped; reject linha sem LF; reject bloco sem linha vazia final; reject linha sem separador; reject name com espaço; reject valor com controle bruto; reject obs-fold/linha continuada; reject nome malformado além do cap armazenado; preserva HTAB interno permitido no valor; find_header CI.
  - **+30 end-to-end** `capy_http_get`: happy path 200 com Content-Length 13 ("Hello, world!"); fragmento URL removido da request line; LF-only head aceito; LF-only head dividido em recv chunks aceito; chunked recv (recv_chunk_size=7 forçando multi-call reassembly do head); body_buf truncado; body truncado para no Content-Length sem recv EOF extra; EOF curto antes do Content-Length rejeitado; Content-Length zero tratado como corpo vazio conhecido; Content-Length além do cap armazenado respeitado; Content-Length conflitante além do cap rejeitado; header value malformado além do cap rejeitado; 100 Continue rejeitado como informacional; 103 Early Hints rejeitado como informacional; 204 ignora tail de body; 304 com Content-Length não-zero rejeitado; Content-Encoding identity aceito; Content-Encoding gzip rejeitado; Content-Encoding vazio rejeitado; header sem separador rejeitado; obs-fold header rejeitado; HTTPS rejeitado; 404 com body vazio; chunked TE rejeitado; Transfer-Encoding non-chunked rejeitado; Content-Length com sufixo rejeitado; Content-Length overflow rejeitado; Content-Length duplicado identico aceito; Content-Length duplicado conflitante rejeitado; request format on-the-wire pinned (GET line + Host:port + Connection: close).
  - Total histórico da parte 5: **+29 asserts** → **82/82 OK** (era 53/53 após parte 3/3); follow-ups alpha.24..alpha.47 adicionam +50 regressões planejadas/revisadas por revisão estática.
- ✅ **Fake test infra estendida**: `g_fake.recv_canned_buf/len/pos/chunk_size` para entregar response inteira pré-fabricada em chunks programáveis (substitui modo drip 1 byte/call dos testes anteriores). `g_fake.send_log` agora 4 KB para acomodar request HTTP completo. Bulk mode tem precedência sobre drip mode.
- ✅ **Wiring**: `Makefile` adiciona `capy_net_url.o` e `capy_net_http.o` em `$(CAPYLIBC_NET_OBJS)` + em `TEST_SRCS`.

#### Progresso 2026-05-08 (seção c parte 4/4 — active DNS resolver kernel-side)

A parte 3/3 instalou `dns_cache_lookup` direto como `.dns_resolve`: hostname não cacheado virou `CAPY_NET_EDNS` em userland. Apps que recebem hostname via input do usuário (browser bar, update-agent CLI flag, futura tool de diagnóstico) só conseguiriam resolver com DHCP pré-aquecendo a cache -- ou seja, na prática nunca para domínios novos. A parte 4/4 fecha esse gap **sem** tocar a ABI do syscall, da `capylibc.h` ou do test harness existente: troca apenas o ponteiro de função em `g_default_ops`.

A parte 4/4 deliberadamente **fica kernel-side**, aproveitando o resolver UDP/53 já existente em `src/net/core/stack.c::net_stack_dns_resolve` (build de query DNS, polling stack até resposta ou timeout, parse de A record). Implementar resolver puro-userland exigiria primeiro o socket UDP datagram path em `capy_net_dns.c` -- um TU novo de ~200 LoC que ainda não temos motivação para escrever (todo apps que precisam DNS hoje ou estão em ring 0 ou usam o syscall via libcapy-net).

Entregas:
- ✅ **`syscall_dns_resolve_with_active`** (~10 LoC executáveis) em `src/kernel/syscall_net_init.c`. Pipeline: validate args (NULL → -1) → `dns_cache_lookup` (hit → return 0, sem UDP) → `net_stack_dns_resolve(name, 3000ms, &ip)` (build query, send via stack, poll até timeout) → on success `dns_cache_insert(name, ip, 0)` (TTL=0 = use default 300s) → return 0; on failure → return -1, **sem** `dns_cache_insert`.
- ✅ **Constante `SYSCALL_DNS_RESOLVE_TIMEOUT_MS = 3000`** alinhada com o timeout que o `http_request` do kernel já usa internamente; comportamento uniforme entre callers ring 0 e syscall ABI.
- ✅ **Não envenena cache em falha ativa**: timeout / NXDOMAIN / stack desinstalado retornam -1 sem `dns_cache_insert`. Justificativa: outage transiente de DNS recursor não deve mascarar como NXDOMAIN permanente. TTL real (parser de SOA + per-record) fica como follow-up dedicado, não bloqueia parte 4/4.
- ✅ **Substituição cirúrgica em `g_default_ops`**: `.dns_resolve = syscall_dns_resolve_with_active` (era `dns_cache_lookup`). Userland stub `capy_dns_resolve` continua idêntico; teste host de userland (`tests/test_capylibc_net.c`) continua usando seu fake `capy_dns_resolve` injetável e passa sem alterações.
- ✅ **`tests/test_syscall_net_init.c` novo** (5 asserts, ~225 LoC com docstring): valida os 3 caminhos via cache **real** (`src/net/services/dns_cache.c` linkado por `tests/test_dns_cache.c`) + fake `net_stack_dns_resolve` recordando timeout/nome chamado:
  - **+1** `install_default_ops` instala dns_resolve não-NULL no `g_net_ops` (via accessor público `syscall_net_get_ops`).
  - **+1** cache hit (pré-seed via `dns_cache_insert("cached.example", 0xC0A80101u, 0)`) → return 0 + ip correto + `g_stack.resolve_calls == 0` (pulou o ativo).
  - **+1** cache miss + ativo hit (forçado via `g_stack.resolve_should_succeed=1`, ip=0x08080808) → return 0 + ip correto + timeout=3000ms + nome igual + segunda resolve não chama ativo (cache foi semeada).
  - **+1** cache miss + ativo fail (`g_stack.resolve_should_succeed=0`) → return -1 + segunda resolve volta a chamar ativo (cache não foi envenenada).
  - **+1** NULL name e NULL out_ip → return -1 sem tocar nenhum backend.
  - Total libcapy-net + syscall_net + syscall_net_init: **108/108 OK** (era 103/103 após parte 5).
- ✅ **Wiring**: `Makefile` adiciona `tests/test_syscall_net_init.c src/kernel/syscall_net_init.c` em `TEST_SRCS`; `tests/test_runner.c` declara e chama `test_syscall_net_init_run()`. `syscall_net_init.c` já estava em `CAPYOS64_OBJS` (boot path), nada novo no kernel build.

**Decisões de design notáveis**:
- **Wrapper kernel-side em vez de userland**: aproveita `net_stack_dns_resolve` existente. Userland-side ficaria duplicando build de query / parse de response / fallback de múltiplos resolvers. Quando configfs landar com `/etc/resolv.conf` e a UDP datagram path em libcapy-net for exercitada por outro caso de uso, a parte 4/4 pode ser re-cabeada lá sem mudar ABI.
- **TTL=0 sentinela** (use default `DNS_CACHE_TTL_DEFAULT` = 300s) em vez de propagar TTL real do response: a `dns_cache_insert` já trata `ttl == 0` como "use default" desde a parte 3/3, e o response parser do kernel ainda não expõe per-record TTL. Refactor do response parser para extrair TTL fica em follow-up bem definido (não bloqueia parte 4/4).
- **Falha não envenena**: alternativa seria `dns_cache_insert(name, 0, short_ttl)` para evitar martelar UDP/53 a cada miss durante outage. Não fizemos: (a) cache de 5min de NXDOMAIN bloqueia recovery após o resolver voltar; (b) negative caching real precisa SOA do response (que ainda não temos). App deve fazer backoff próprio se quiser.
- **Cache real no teste em vez de fake**: validar contrato end-to-end ("`dns_cache_insert(name, ip)` torna a próxima `dns_cache_lookup(name)` um hit") é mais valioso que checar argumentos via mock. Como `tests/test_dns_cache.c` já linka o módulo, fakeí-lo causaria duplicate-symbol; usar o real é mais barato **e** mais correto.

#### Progresso 2026-05-08 (seção c parte 4/4 follow-up — TTL real do upstream)

A parte 4/4 fechou o caminho cache → active → cache_insert mas com um buraco semântico: todo hostname novo entrava na cache com TTL=300s independente do que o servidor autoritativo respondia. Isso quebra três casos:

1. **DHCP lease renew rápido**: lease 60s + DNS cache 300s = entrada stale por 240s após o A-record real ter mudado (cenário típico em datacenter onde DHCP+DNS são parte da mesma orquestração).
2. **CDN edge migration**: TTL=30s do CDN é a contramedida deles para round-robin de healthy hosts; sobrescrever para 300s causa client falhar em uma origem morta.
3. **Do-not-cache hint** (TTL=0): alguns recursores emitem 0 para indicar "não cache esta resposta" (anti-poisoning); ignorar isso vira um amplification vector.

A solução é cirúrgica: estender 4 funções com `out_ttl` opcional, capturar o TTL real do response, e clampar antes de inserir na cache para defender contra TTLs adversariais.

Entregas:
- ✅ **`net_dns_parse_first_a` ganha 5º parâmetro `uint32_t *out_ttl`** (NULL = ignore). O parser já lia os bytes [offset+4..+7] do answer header (TTL é be32) mas descartava o valor; agora forwarda quando `out_ttl != NULL`. Backward-compatible: callers que não querem TTL passam NULL.
- ✅ **`struct net_dns_state` ganha `uint32_t answer_ttl`** em `src/net/internal/stack_services.h`; `net_dns_handle` propaga via `&state->answer_ttl`. Zero overhead quando o response não chega.
- ✅ **`net_stack_dns_resolve` ganha 4º parâmetro `uint32_t *out_ttl`** (NULL = ignore) em `include/net/stack.h`; copia de `g_net.dns.answer_ttl` quando `out_ttl != NULL`. 4 callers atualizados:
  - `src/shell/commands/network_query.c` (3 sites): passa NULL (TTL não relevante para o shell).
  - `src/net/services/http/request_response.c`: passa `&resolved_ttl` adiante para `dns_cache_insert(host, ip, resolved_ttl)`. **Bug colateral corrigido**: antes hardcoded `dns_cache_insert(host, ip, 300)` -- o mesmo valor que `DNS_CACHE_TTL_DEFAULT`, mas conceitualmente errado (anel-zero-mente-prove o TTL do server).
- ✅ **Wrapper kernel-side ganha clamp** (`src/kernel/syscall_net_init.c`):
  - Constantes `SYSCALL_DNS_RESOLVE_TTL_MIN_S = 60` e `SYSCALL_DNS_RESOLVE_TTL_MAX_S = 86400`.
  - Função pública `syscall_dns_resolve_clamp_ttl(raw)`: 0 → 0 (passthrough); 1..59 → 60; 60..86400 → raw; 86401..UINT32_MAX → 86400. Não-static para o teste host poder pinar boundaries diretamente.
  - `syscall_dns_resolve_with_active` captura TTL via local `uint32_t ttl = 0`, passa adiante via `dns_cache_insert(name, ip, syscall_dns_resolve_clamp_ttl(ttl))`. Quando `ttl == 0` (do-not-cache hint), `dns_cache_insert` substitui por `DNS_CACHE_TTL_DEFAULT` (300s) -- decisão de manter cache funcionando com policy default em vez de não-cachear (negative caching real precisa SOA do response).
- ✅ **Tests**:
  - `tests/test_net_dns.c`: synthetic packet com `write_be32(&packet[offset], 60u)` agora valida `out_ttl == 60u` no assert (era ignorado). +1 assert efetivo (mesmo síntese, agora com cobertura TTL).
  - `tests/test_syscall_net_init.c`: fake `net_stack_dns_resolve` ganha 4º param e canned `resolve_canned_ttl`. +6 testes novos: 5 boundary tests do clamp (zero, below floor, above ceiling, inside, exact 60/86400) + 1 end-to-end com TTL=7200s pinando que o wrapper passa o TTL adiante e a próxima resolve hita cache.
  - Total: **+8 asserts** novos (1 net_dns + 7 syscall_net_init) → **115/115 OK** (era 108/108 após parte 4/4 inicial).

**Decisões de design notáveis**:
- **Backward-compatible signature change** (NULL ok) em vez de função paralela: 4 callers de `net_stack_dns_resolve` atualizados em uma linha cada (3 NULL + 1 passthrough). Função paralela `net_stack_dns_resolve_with_ttl` duplicaria 30 LoC do polling loop.
- **Clamp window [60s, 86400s]** vs [1s, INT32_MAX]: 60s é a mediana do que recursores legítimos emitem (Cloudflare 1.1.1.1 tipicamente 30..300s, Google 8.8.8.8 typicamente 60..28800s); aceitar TTL=1 abriria attack vector "force every resolve to RTT" (DDoS-via-DNS). 86400s (24h) é o teto que `dns_cache_ttl_ticks(86400) * 100` ainda cabe confortavelmente em uint64_t e que faz sentido pra um sistema embarcado (refrescar pelo menos diariamente).
- **TTL=0 forward verbatim** (não clampar para 60): preserva o do-not-cache hint até a `dns_cache_insert` decidir o que fazer. Hoje ela substitui por default 300s; futuro pode adicionar negative caching real (SOA-derived).
- **Helper expose para teste** (não-static): pinpoint test do clamp sem precisar montar todo o pipeline cache+stack. Função-pequena-com-teste-direto > função-static-com-teste-só-end-to-end.

#### Progresso 2026-05-08 (seção c parte 4/4 follow-up — RFC 2308 negative caching)

O follow-up de TTL real fechou positive caching. Restava o gap simétrico: NXDOMAIN não cacheado significa que cada typo no URL bar / cada hostname inválido reabre UDP/53 indefinidamente -- DDoS amplifier triplo (gera tráfego para nosso recursor, gera tráfego upstream do recursor, e mantém o socket aberto enquanto erra repetidamente). RFC 2308 (1998) prescreve a solução: cache negative responses até `min(SOA RR TTL, SOA MINIMUM)` para names com SOA na authority section.

A implementação tem três camadas, cada uma backward-compatible com callers que não querem saber de negative:

Entregas:
- ✅ **Parser** — `net_dns_parse_negative_ttl(msg, len, expected_id, *out_neg_ttl)` em `src/net/services/dns.c`. Detecta NXDOMAIN (RCODE=3) ou NODATA (RCODE=0+ANCOUNT=0+NSCOUNT≥1), pula questions e answers (CNAME chain de NXDOMAIN), itera authority até achar primeiro SOA RR, valida ClassRR=IN, walk RDATA: skip MNAME → skip RNAME → ler MINIMUM nos últimos 4 bytes do RDATA. Retorna `min(SOA RR TTL, SOA MINIMUM)` em out_neg_ttl. Rejeita SERVFAIL/REFUSED/etc (transient, deve re-tentar). Rejeita NXDOMAIN sem SOA (sem TTL hint, deve re-tentar). +5 asserts em `tests/test_net_dns.c`.
- ✅ **State** — `struct net_dns_state` ganha `uint8_t response_is_negative` + `uint32_t answer_negative_ttl` em `src/net/internal/stack_services.h`. `net_dns_handle` em `src/net/core/stack_services.c` tenta `parse_first_a` primeiro; se falhar, tenta `parse_negative_ttl`; se tudo falhar, marca `response_failed`.
- ✅ **Stack** — `net_stack_dns_resolve` ganha 5º parâmetro `uint32_t *out_neg_ttl` (NULL ok). Comportamento tristate via `(rc, *out_neg_ttl)`:
  - `rc == 0`: positive A, ip+ttl populated, neg_ttl untouched (or 0).
  - `rc == -1 && *out_neg_ttl > 0`: parseable definitive negative.
  - `rc == -1 && *out_neg_ttl == 0`: transport/timeout/malformed.
  4 callers atualizados: 3 shell commands (`network_query.c`) passam NULL; `http_request` passa NULL; o wrapper kernel-side (`syscall_net_init.c`) passa `&neg_ttl`.
- ✅ **Cache** — `dns_cache_entry` ganha `uint8_t is_negative`. Novas APIs:
  - `void dns_cache_insert_negative(const char *name, uint32_t ttl)`: armazena entry com `ip=0`, `is_negative=1`. Substitui entry positiva existente para o mesmo nome (i.e., recursor mudou de ideia).
  - `int dns_cache_lookup_negative(const char *name)`: retorna 0 se houver entry com `valid && is_negative && !expired`. Bumpa novo stat `cache_stats.negative_hits`.
  - `dns_cache_lookup` (positive) **ignora entries negativas** — callers existentes (http_request, shell, etc.) inalterados; veem miss para nomes negativamente cacheados, o que é o comportamento certo (eles não conseguem fazer nada com -1 + neg_ttl). +13 asserts em `tests/test_dns_cache.c` cobrindo basic/independence/stats/flush + positive-overwrites-negative + negative-overwrites-positive.
- ✅ **Wrapper** — `syscall_dns_resolve_with_active` agora tem **5 caminhos**:
  1. Positive cache hit → return 0 + ip.
  2. Negative cache hit → return -1 sem UDP.
  3. Active positive → `dns_cache_insert(name, ip, clamp_ttl(ttl))` + return 0.
  4. Active negative (rc=-1 + neg_ttl>0) → `dns_cache_insert_negative(name, clamp_neg_ttl(neg_ttl))` + return -1.
  5. Active transport failure (rc=-1 + neg_ttl=0) → return -1 sem cache (não envenena com transients).

  Nova função pública `syscall_dns_resolve_clamp_neg_ttl(raw)` aplica clamp `[30s, 3600s]`:
  - 30s mínimo defende contra MINIMUM=1 (DDoS amplifier autoritativo).
  - 3600s (1h) máximo defende contra MINIMUM=86400 (typo persistente). RFC 2308 §5 cap em SOA MINIMUM seria days; muito longo pra embedded.
  - 0 → DNS_CACHE_TTL_DEFAULT (300s, fallback quando parser não populou).

- ✅ **Tests**: +8 asserts no `tests/test_syscall_net_init.c` (4 boundaries do clamp_neg_ttl + 4 end-to-end: negative cache hit pula active, active negative seeds cache, transport failure não polui, positive sobrescreve negative). Total libcapy-net + syscall_net + syscall_net_init + dns_cache: **141/141 OK** (era 115/115).

**Decisões de design notáveis**:
- **Parse separado em vez de tristate** (`parse_first_a` vs `parse_negative_ttl`): preserva a contract atomicidade de `parse_first_a` (0=positive, -1=anything else) e os call sites não-syscall (shell `nslookup`-like, http_request) não precisam aprender sobre negative. Custo: dois passes do mesmo packet quando o response é negativo. Benefício: ZERO impacto em callers existentes que só querem "ip ou erro".
- **`dns_cache_lookup` skipa negative por padrão**: callers que ainda não foram migrados (TODOS exceto o wrapper syscall) continuam funcionando como antes -- veem miss para nomes negativamente cacheados e tentam ativo, que vai re-cachear o negative (idempotente). Sem negative caching efetivo para esses callers, mas sem regressão. Migração progressiva quando `libcapy-net` ganhar `capy_dns_resolve` aware-de-negative.
- **Clamp negative TTL [30s, 3600s]** vs RFC 2308's `min(SOA TTL, SOA MINIMUM)`: a regra do RFC produz valores no range de horas/dias para domínios bem configurados. Para um sistema embarcado onde o usuário pode digitar errado e desistir em segundos, 1h é o máximo razoável -- evita "URL errada está bloqueada o dia todo" UX bug. 30s mínimo evita amplification (recursor hostil emite MINIMUM=1).
- **Rejeitar controles brutos no URL parser**: `capy_http_build_get_request` escreve `path` direto na request line, então CR/LF/tabs vindos de URL não podem atravessar o parser. Percent-encoding permanece permitido (`%0d%0a` continua bytes ASCII seguros no request target); apenas octetos brutos `<=0x20` e DEL são recusados.
- **Validar também o request builder público**: como `capy_http_build_get_request` é exposto para testes e futura state machine do browser, ele não pode depender exclusivamente de callers passarem por `capy_url_parse`. Host vazio, controles brutos e path relativo nao vazio falham com `CAPY_NET_EPARSE` antes de qualquer byte ir para o buffer.
- **Header parser fail-closed em controles perigosos**: nomes de header passam a seguir `token` HTTP e valores recusam controles brutos exceto HTAB, evitando que metadata malformada chegue a callers futuros. Linhas sem `:` continuam sendo ignoradas para compatibilidade com a tolerância anterior.
- **Content-Length estrito**: header presente precisa ser decimal puro e caber em `size_t`; sufixos e overflow falham antes de governar truncation/drain do corpo. Header ausente segue `content_length=0` e leitura até EOF.
- **Content-Length duplicado consistente**: múltiplos headers `Content-Length` precisam resolver para o mesmo valor decimal; conflitos falham antes do body framing.
- **Transfer-Encoding fail-closed**: como `libcapy-net` não decodifica transfer codings, qualquer header `Transfer-Encoding` presente no bloco bruto falha com `CAPY_NET_EUNSUPPORTED`, não apenas `chunked`.
- **Body drain por bytes recebidos**: `capy_http_get` separa `body_received` de `out->body_len`, então truncation/discard não força espera por EOF quando `Content-Length` conhecido já foi consumido.
- **EOF curto fail-closed**: quando `Content-Length` é conhecido, EOF antes de receber todos os bytes vira `CAPY_NET_EHTTP`, evitando sucesso parcial em resposta truncada.
- **Content-Length zero conhecido**: `Content-Length: 0` passa a ser distinguido de header ausente; o body fica vazio mesmo que bytes extras cheguem no tail do head.
- **Obs-fold fail-closed**: linhas de header iniciadas por SP/HTAB são rejeitadas em vez de ignoradas ou normalizadas parcialmente.
- **Content-Length raw**: `capy_http_get` valida `Content-Length` no bloco bruto completo, inclusive além do limite de headers armazenados.
- **Validação pós-cap de headers**: headers além de `CAPY_HTTP_MAX_HEADERS` continuam validados; só o armazenamento público é truncado.
- **Status no-body**: respostas 204 e 304 definem corpo vazio conhecido; `Content-Length` não-zero nesses status é erro HTTP.
- **1xx fail-closed**: respostas informacionais são rejeitadas até haver loop para múltiplos response heads.
- **Status-line estrita**: o parser exige status HTTP `100..599`, separador depois do código e reason phrase sem controles brutos.
- **LF-only head**: `capy_http_get` aceita terminador de head `LF LF`, além de `CRLFCRLF`, preservando body framing.
- **Header separator fail-closed**: linhas de header não vazias sem `:` são rejeitadas como sintaxe malformada.
- **Header block truncation fail-closed**: blocos sem `LF` em alguma linha ou sem linha vazia final são rejeitados.
- **URL fragment stripping**: fragmentos `#...` são removidos antes do request target HTTP para não vazar estado client-side.
- **Host authority hardening**: hosts aceitam apenas bytes DNS-label antes do request HTTP, rejeitando caracteres ambiguos como `\` e `%`.
- **Request-target fragment fail-closed**: o builder HTTP direto rejeita `#` em paths para manter fragmentos fora do request target.
- **DNS label boundary hardening**: hosts rejeitam labels vazios, labels acima de 63 bytes e hífen no início/fim do label.
- **Request builder port-zero fail-closed**: o helper HTTP direto rejeita `port == 0`, alinhando-se ao parser de URL.
- **Request-target backslash/%00 fail-closed**: `capy_url_parse` e `capy_http_build_get_request` rejeitam `\` e `%00` em path/query antes de qualquer byte entrar na linha `GET ... HTTP/1.1`.
- **Content-Encoding fail-closed**: `libcapy-net` aceita body identity/plain e rejeita valor vazio ou codings que exigem decoder ainda não integrado no userland.
- **Falha transport não cacheia**: alternativa seria fixed-30s negative-cache para timeouts, evitando hammering um recursor lento. Não fizemos: (a) timeout != NXDOMAIN (recursor pode estar carregando lento mas o name existe); (b) o wrapper roda com timeout=3000ms, então hammering já está rate-limited; (c) negative caching transport seria uma policy diferente que merece análise separada.
- **Positive insert REPLACES negative** (em vez de coexistir): RFC 2308 não cobre o caso "domain came back". Implementações reais (BIND9, Unbound) fazem isso — se a próxima resolve retorna positive, override the negative. Coexistência (separar slots) gastaria 2x cache e complicaria lookup. Substituição simples mantém o invariante "1 entry por nome".

**Decisões de design notáveis** (parte 5 anterior):
- **Strict URL parser** vs WHATWG-tolerant parser: app code que já se defende contra URLs maliciosos via lista branca não quer um parser permissivo. Rejeitar userinfo / IPv6 / port=0 elimina classes inteiras de bugs no caller.
- **Truncated em vez de fail**: caller passa um body_buf finito; servidor pode mandar mais. Graceful (cap atingido + drain + flag) é mais útil que erro absoluto -- caller pode chamar de novo com buf maior se importar.
- **HTTPS retorna EUNSUPPORTED**: poderia ser silently treated as HTTP, mas isso seria security issue (caller passou `https://` esperando criptografia). Erro explícito força awareness de que libcapy-tls precisa land antes.
- **Sem redirect follow automático**: caller decide. `http_get(url1, ...)` retorna 301 + `Location:` → caller chama `http_get(resolved, ...)`. Loops infinitos / suspect redirects ficam em política do app.
- **Connection: close baked**: keep-alive precisa de socket pool + state machine de quando reusar. Para uma primeira iteração, fechar e reabrir a cada `http_get` é simples e correto. Futuro pool fica em `capy_net_pool.c` quando o caso de uso aparecer (capybrowser load-de-página com múltiplos assets).

#### Progresso 2026-05-10 (`0.8.0-alpha.63`)

- ✅ `capy_url_parse` rejeita backslash bruto no request-target.
- ✅ `capy_url_parse` rejeita `%00` em path e query.
- ✅ `capy_http_build_get_request` aplica a mesma política para callers diretos.
- ✅ Cinco regressões declarativas foram adicionadas em `tests/test_capylibc_net.c` e revisadas estaticamente.
- ✅ ABI pública de `libcapy-net` mantida.

#### Progresso 2026-05-10 (`0.8.0-alpha.64`)

- ✅ `userland/include/capylibc-tls/capy_tls.h` define a API TLS userland mínima.
- ✅ `userland/lib/capylibc-tls/capy_tls.c` falha fechado com `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland aterrissar.
- ✅ `capy_tls_connect_tcp` valida fd, hostname e configuração de CA antes de retornar unsupported.
- ✅ `capy_tls_security_info` permanece zerado antes de handshake real.
- ✅ `tests/test_capylibc_tls.c` registra regressões declarativas e entra no runner.
- ✅ `Makefile` expõe o alvo separado `capylibc-tls` para consumidores futuros.

#### Progresso 2026-05-10 (`0.8.0-alpha.65`)

- ✅ `userland/lib/capylibc-net/capy_net_tls.c` cria o adaptador HTTPS interno.
- ✅ `capy_http_get` passa por `capy_net_internal_https_fail_closed` antes de DNS/socket.
- ✅ Erros de `libcapy-tls` são mapeados para a superfície `CAPY_NET_*`.
- ✅ `CAPYLIBC_NET_OBJS` carrega o stub `libcapy-tls` necessário ao adaptador.
- ✅ Regressões declarativas verificam HTTPS sem DNS/socket/connect/send/recv.

#### Progresso 2026-05-10 (`0.8.0-alpha.66`)

- ✅ `capy_tls_connect_tcp` rejeita hostnames malformados antes do backend BearSSL userland.
- ✅ Labels vazios, trailing dot, `-` em bordas e labels maiores que 63 bytes falham com `CAPY_TLS_EINVAL`.
- ✅ Caracteres fora de `[A-Za-z0-9.-]` falham antes de SNI/cert-name validation futura.
- ✅ Regressões declarativas cobrem espaços, `_`, `%`, backslash, IPv6 literal textual e label longo.

#### Progresso 2026-05-10 (`0.8.0-alpha.67`)

- ✅ `include/security/tls_hostname.h` e `src/security/tls_hostname.c` isolam a política de hostname do TLS kernel-side.
- ✅ `tls_connect` rejeita hostname malformado antes de `kmalloc`, BearSSL/SNI e `socket_setsockopt`.
- ✅ `tests/test_tls_hostname.c` cobre nomes válidos, controles, bordas de label, sintaxe ambígua e limites de tamanho.
- ✅ `Makefile` e `tests/test_runner.c` registram o helper no kernel build e nos host tests declarativos.

#### Progresso 2026-05-10 (`0.8.0-alpha.68`)

- ✅ `include/security/tls_hostname_policy.h` centraliza a regra de hostname TLS.
- ✅ `tls_hostname_valid` e `capy_tls_connect_tcp` usam a mesma política via wrappers separados.
- ✅ `tests/test_tls_hostname.c` compara wrapper kernel-side contra `tls_hostname_policy_valid`.
- ✅ A duplicação literal da regra userland/kernel foi removida sem mudar ABI pública.

#### Progresso 2026-05-10 (`0.8.0-alpha.69`)

- ✅ `capy_tls_config_valid` rejeita configuração explícita com `verify_peer != 1`.
- ✅ `config == NULL` permanece como default seguro e estruturalmente válido.
- ✅ Regressões declarativas cobrem `verify_peer=0` e valor inválido negativo.
- ✅ O caminho válido continua fail-closed com `CAPY_TLS_EUNSUPPORTED` até BearSSL userland.

#### Progresso 2026-05-10 (`0.8.0-alpha.70`)

- ✅ `capy_tls.h` expõe `CAPY_TLS_TIMEOUT_DEFAULT_MS`, `CAPY_TLS_TIMEOUT_MIN_MS` e `CAPY_TLS_TIMEOUT_MAX_MS`.
- ✅ `capy_tls_config_valid` rejeita timeout explícito abaixo do mínimo e acima do máximo.
- ✅ `timeout_ms=0` permanece como default seguro e estruturalmente válido.
- ✅ Regressões declarativas cobrem limites inválidos e default explícito.

#### Progresso 2026-05-10 (`0.8.0-alpha.71`)

- ✅ `capy_tls_internal.h` introduz `capy_tls_effective_config`.
- ✅ `capy_tls_config.c` centraliza `capy_tls_config_resolve`.
- ✅ `capy_tls_connect_tcp` consome snapshot normalizado antes de falhar fechado.
- ✅ `Makefile` e testes host registram o novo módulo de configuração TLS.

#### Progresso 2026-05-10 (`0.8.0-alpha.72`)

- ✅ `capy_tls_context` armazena socket, hostname e configuração efetiva no header interno.
- ✅ `capy_tls_context.c` centraliza `capy_tls_context_prepare`.
- ✅ `capy_tls_connect_tcp` prepara contexto stack-local antes de retornar `CAPY_TLS_EUNSUPPORTED`.
- ✅ `Makefile` e testes host registram o novo módulo de contexto TLS.

#### Progresso 2026-05-10 (`0.8.0-alpha.73`)

- ✅ `capy_tls_context_reset` normaliza contexto vazio com defaults seguros.
- ✅ `capy_tls_context_clear` remove hostname/configuração de contexto preparado.
- ✅ `capy_tls_context_prepare` limpa contexto antes de preencher e após rejeição parcial.
- ✅ `capy_tls_connect_tcp` limpa o contexto stack-local antes de retornar `CAPY_TLS_EUNSUPPORTED`.

#### Progresso 2026-05-10 (`0.8.0-alpha.74`)

- ✅ `CAPY_TLS_CONTEXT_SLOT_COUNT` define capacidade interna inicial.
- ✅ `capy_tls_context_acquire` retorna slot resetado e bloqueia aquisição simultânea.
- ✅ `capy_tls_context_release` limpa e libera apenas contexto gerenciado.
- ✅ `capy_tls_free` delega para release seguro sem habilitar handshake.

#### Progresso 2026-05-10 (`0.8.0-alpha.75`)

- ✅ `capy_tls_connect_tcp` adquire slot gerenciado após validar entradas.
- ✅ Contexto gerenciado é preparado e liberado antes do retorno unsupported.
- ✅ Slot ocupado retorna `CAPY_TLS_ESTATE` sem tentar handshake.
- ✅ Testes host cobrem liberação após unsupported e slot ocupado.

#### Progresso 2026-05-10 (`0.8.0-alpha.76`)

- ✅ `capy_tls_backend.c` introduz o stub interno `capy_tls_backend_connect`.
- ✅ Backend stub valida contexto preparado antes de retornar unsupported.
- ✅ `capy_tls_connect_tcp` chama o backend stub antes de liberar o slot.
- ✅ Makefile e testes host registram o novo módulo de backend TLS.

#### Progresso 2026-05-10 (`0.8.0-alpha.77`)

- ✅ `capy_tls_context` ganhou `capy_tls_backend_state`.
- ✅ Backend stub prepara SNI e timeout efetivo sem iniciar handshake.
- ✅ Reset/release limpam estado interno de backend.
- ✅ Testes host cobrem preparo e scrub do estado backend.

#### Progresso 2026-05-10 (`0.8.0-alpha.78`)

- ✅ Backend stub prepara metadados mínimos de trust anchors.
- ✅ CA custom válida marca anchor interno esperado sem parse de certificado.
- ✅ Configuração padrão diferencia bundle futuro sem declarar certificados carregados.
- ✅ Testes host cobrem trust metadata e scrub em reset/rejeição.

#### Progresso 2026-05-10 (`0.8.0-alpha.79`)

- ✅ `capy_tls_trust.c` adiciona fonte userland metadata-only.
- ✅ Bundle padrão declara contagem de 146 trust anchors.
- ✅ Backend stub propaga contagem padrão sem carregar certificados.
- ✅ Makefile e testes host registram o novo módulo.

#### Progresso 2026-05-10 (`0.8.0-alpha.80`)

- ✅ Fonte userland de trust anchors vira catálogo interno metadata-only.
- ✅ Catálogo registra `146` anchors: `106` RSA e `40` EC.
- ✅ Backend stub propaga distribuição RSA/EC para configuração padrão.
- ✅ CA custom permanece opaca, sem parse de certificado.

#### Progresso 2026-05-10 (`0.8.0-alpha.81`)

- ✅ Catálogo TLS fixa máscara RSA/EC `0x3`.
- ✅ Fingerprint metadata-only `0xDB22D94A` identifica o catálogo.
- ✅ Backend stub propaga máscara/fingerprint sem handshake.
- ✅ Reset/release/rejeição zeram os novos metadados.

#### Progresso 2026-05-10 (`0.8.0-alpha.82`)

- ✅ Tabela userland metadata-only materializa 146 slots TLS.
- ✅ Layout preserva distribuição `106` RSA e `40` EC.
- ✅ Fingerprint de layout `0x07A622AB` detecta drift da sequência.
- ✅ Backend stub propaga contagem/fingerprint de slots sem handshake.

#### Progresso 2026-05-10 (`0.8.0-alpha.83`)

- ✅ Descritores metadata-only expõem índice/tipo/flags sem cert bytes.
- ✅ Fingerprint de descritores `0xE1A18A70` detecta drift.
- ✅ Acesso fora de faixa zera saída e falha fechado.
- ✅ Backend stub propaga contagem/fingerprint de descritores sem handshake.

#### Progresso 2026-05-10 (`0.8.0-alpha.84`)

- ✅ Manifesto metadata-only consolida proveniência kernel-bundle.
- ✅ Flags declaram metadata-only, cert-bytes-absent e fail-closed-only.
- ✅ Fingerprint de manifesto `0x958359C5` detecta drift.
- ✅ Backend stub propaga schema/source/flags/fingerprint sem handshake.

#### Objetivo

Hoje, rede é **chamada direta de kernel** (browser chama `http_request_send` dentro do kernel). Para apps reais isolados, precisamos de **socket syscalls + libcapy-net em userland**.

#### Entregáveis

- **E4.1** — `SYS_SOCKET`, `SYS_BIND`, `SYS_CONNECT`, `SYS_SEND`, `SYS_RECV`, `SYS_CLOSE` (close já existe).
- **E4.2** — File descriptor type `FD_TYPE_SOCKET` análogo a `FD_TYPE_PIPE`. Aproveita stack de rede atual no kernel via wrappers.
- **E4.3** — `userland/lib/capylibc-net/` — wrappers `capy_socket()`, `capy_connect()`, etc.
- **E4.4** — `userland/lib/capylibc-tls/` — API fail-closed, adaptador HTTPS fail-closed, política compartilhada de hostname userland/kernel, peer verification obrigatório, janela de timeout, snapshot de configuração, contexto preparado, ciclo de vida do contexto, slot interno de contexto, connect usando slot gerenciado, backend stub fail-closed, estado backend TLS preparado, trust metadata TLS preparado, fonte userland de trust anchors, catálogo userland de trust anchors, invariantes do catálogo TLS, tabela metadata-only de slots TLS, descritores metadata-only TLS e resumo agregado metadata-only TLS + manifesto metadata-only TLS entregues; backend TLS 1.2/1.3 via BearSSL userland ainda pendente.
- **E4.5** — Migração do `capybrowser` (F3) para usar libcapy-net + libcapy-tls em vez de syscalls atuais bypass.
- **E4.6** — Host tests + smoke `smoke-x64-tls-handshake` (cliente TLS conecta a servidor local QEMU `httpd`).
- **E4.7** — TLS trust store userland: re-empacota `src/security/tls_trust_anchors.c` como bundle userland.

#### Critérios de aceite

- [ ] Cliente userland faz TLS handshake com servidor de teste e troca dados.
- [ ] `capybrowser` (F3) navega `https://example.org` via libcapy-tls, sem chamar kernel TLS.
- [ ] Falha de cert é reportada com erro estável.
- [ ] `net-fetch https://...` no shell continua funcionando (legacy: ainda usa kernel; refatoração futura).

#### Como será feito

1. **F4a** (2 sessões): syscalls + FD type. Reutiliza buffer pipe pattern.
2. **F4b** (2 sessões): libcapy-net stubs + smokes básicos (TCP plaintext).
3. **F4c** (3 sessões): libcapy-tls (BearSSL recompilado como userland).
4. **F4d** (1 sessão): migração capybrowser.

#### Validação

```bash
make smoke-x64-tls-handshake
make smoke-x64-browser-https
```

#### Saída esperada

Apps userland podem fazer rede sem bypass. Caminho para update real (F5) fica aberto.

---

### F5 — Update real via GitHub Releases (fetch + Ed25519)

**Criticidade:** média — produto utilizável existe sem isso, mas distribuição automática depende.
**Depende de:** F4 (TLS userland) + F2 (assinatura de release já gerada).
**Paralelo possível com:** F6 (GUI).
**Risco:** médio — toca staged update + boot slot já entregues; risco controlado.
**Esforço:** 3–4 sessões.
**Branch sugerida:** `feature/f5-update-fetch`.

#### Objetivo

`update-agent` deixa de depender de `update-import-manifest` manual e passa a:

1. Buscar manifest assinado de `https://github.com/<repo>/releases/latest`.
2. Validar Ed25519.
3. Baixar payload (kernel `.bin` + manifest novo).
4. Validar SHA-256 (já existe via M6.4).
5. Stage + arm + reboot + confirm health (já existe via M7.5).

#### Entregáveis

- **E5.1** — `update_agent_fetch_remote_manifest()` entregue para manifesto remoto configurado; runtime usa `http_get()` e testes usam fetcher injetável sob `UNIT_TEST`.
- **E5.2** — Ed25519 verifier embarcado no kernel e gate local no `update-agent`; fetch assinado reutiliza a validação de import.
- **E5.3** — Comandos shell iniciais: `update-fetch`, `update-apply <payload_sha256>`, `update-confirm-health` e `update-rollback-check` entregues; o download dedicado de payload foi fechado em E5.6.
- **E5.4** — Política entregue: canal `develop` usa `refs/heads/develop`; canal `stable` usa `refs/tags/v<major>.<minor>.<patch>` derivado da versão corrente.
- **E5.5** — Origem de payload entregue: `payload_url` obrigatório/validado no manifesto assinado e exposto em status, shell e histórico.
- **E5.6** — Download de payload entregue: `update-download-payload` baixa `payload_url`, calcula SHA-256 real, recusa mismatch e persiste `/system/update/payload.bin`.
- **E5.7** — Apply cache-first entregue: `update-apply` usa `payload_cache_sha256` verificado por padrão, mantendo `update-apply <payload_sha256>` como fallback manual.
- **E5.8** — Prepare seguro entregue: `update-prepare` encadeia fetch remoto, download verificado, staging e arm sem aplicar boot slot.
- **E5.9** — Prepare dry-run entregue: `update-prepare-dry-run` valida catalogo local e cache verificado sem staging, arm ou apply.
- **E5.10** — Prepare explain entregue: `update-prepare-explain` mostra gates locais de preparo sem efeitos persistentes.
- **E5.11** — Smoke `smoke-x64-update-fetch` (servidor HTTP local serve manifest fake assinado).
- **E5.12** — Documentação `docs/operations/update-from-github.md`.

#### Critérios de aceite

- [x] Manifest local/staged sem `payload_sha256` hex64 é rejeitado antes de
  update/stage/apply.
- [x] Manifest com versão menor que a atual é rejeitado (downgrade protection)
  no catalogo local, staged e import manual.
- [x] Manifest local/staged/importado sem `signature_ed25519` hex128 valida é
  rejeitado antes de expor update, persistir cache ou armar apply.
- [x] `update-fetch` baixa manifest remoto configurado e valida assinatura via
  import seguro antes de persistir catalogo.
- [x] `update-apply [payload_sha256]` aplica staged update armado via
  `payload_cache_sha256` verificado por padrão, mantendo
  `update_agent_apply_boot_slot_verified()` como fallback manual explícito.
- [x] Comando/fluxo operacional de health confirm e rollback assistido apos reboot.
- [x] Manifest remoto com assinatura mutilada é rejeitado antes de atualizar catalogo.
- [x] Política remota diferencia `develop` branch e `stable` tag versionada.
- [x] Manifest local/staged/importado mais novo sem `payload_url` valido é rejeitado antes de expor update, staging ou apply.
- [x] Payload baixado por `payload_url` só vira cache persistente quando SHA-256 real bate com `payload_sha256`.
- [x] `update-apply` sem argumento consome `payload_cache_sha256` verificado e recusa cache ausente.
- [x] `update-prepare` encadeia fetch/download/stage/arm sem aplicar boot slot.
- [x] `update-prepare-dry-run` valida os gates locais de catalogo/cache sem persistir staging, arm ou apply.
- [x] `update-prepare-explain` expõe gates locais de preparo sem fetch/download/stage/arm/apply.
- [ ] Suite passa.

#### Como será feito

1. **F5a** Implementar fetch + verify.
2. **F5b** Adicionar comandos shell + audit log `[audit] [update] fetched ...`.
3. **F5c** Smoke ponta-a-ponta com servidor HTTP local em QEMU user-mode network.

#### Validação

```bash
make smoke-x64-update-fetch
```

#### Saída esperada

Sistema atualizável sem reinstalação manual.

---

### F6 — Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher)

**Criticidade:** alta — sem isso, GUI fica "scaffold visível mas inerte" como hoje.
**Depende de:** F1 (release base limpo). Pode iniciar imediatamente após F1.
**Paralelo possível com:** F4, F5.
**Risco:** médio — código existe; bottleneck é integração e timing.
**Esforço:** 4–6 sessões.
**Branch sugerida:** `feature/f6-gui-session`.

#### Objetivo

Hoje:

- `desktop_init/run_frame` existe mas autostart é frágil (extended.c chama mas pode bloquear o login runtime).
- Mouse PS/2 inicializa mas não há dispatcher unificado de eventos.
- Terminal gráfico não consome shell real fim-a-fim — W1 mitigou parte (clear callback), falta o resto.
- Login é por TTY framebuffer; não há login GUI.

Depois desta fase:

- Boot → splash → login GUI → desktop com mouse/teclado/janelas/foco/abertura de apps.

#### Entregáveis

- **E6.1** — Loginwindow GUI: tela de senha em compositor, com lista de usuários, falha visível.
- **E6.2** — `desktop_run_frame` integrado ao loop principal do kernel (não mais via spin de `cmd_desktop_start`); usa timer APIC, não `for(volatile)`.
- **E6.3** — Mouse: drivers PS/2 + USB HID (xHCI já existe parcial). Eventos chegam ao compositor via `input_runtime`.
- **E6.4** — Dispatcher central de eventos: keyboard + mouse + window focus + IPC. Substitui handlers ad-hoc.
- **E6.5** — Taskbar funcional com botão de "Iniciar" (lista de apps), relógio (já existe), notificações simples.
- **E6.6** — Terminal gráfico consome shell real (já parcial via W1, fechar fim-a-fim).
- **E6.7** — Fallback CLI: tecla F1 no login GUI volta para shell TTY (recovery/dev).
- **E6.8** — Smoke `smoke-x64-gui-session` (sendkey via QEMU, valida boot → login → click em ícone → app abre).

#### Incrementos já entregues nesta fase

- **Etapa 6 seção a — stdin buffer backpressure observability
  (2026-05-09)** — `stdin_buf` agora expõe capacidade fixa, espaço livre e
  high-watermark desde o último init, além do contador de drops já existente.
  Isso melhora diagnóstico de teclado/TTY/terminal gráfico e prepara o
  dispatcher F6 sem alterar a semântica FIFO nem o comportamento de overflow.
  Regressions planejadas/revisadas: +9 asserts em `tests/test_stdin_buf.c`
  cobrindo init, pushes, drain, full buffer e overflow. Validação desta
  sessão foi por revisão estática de código e documentação, sem `make`, `git`
  ou scripts.
- **Etapa 6 seção b — stdin buffer coherent diagnostics snapshot
  (2026-05-09)** — `stdin_buf_snapshot()` agrega capacidade, ocupação,
  espaço livre, high-watermark e drops em uma única leitura coerente para
  futuros consumidores de diagnóstico do TTY/GUI. A API retorna 0 para
  ponteiro nulo e não altera FIFO, drops nem índices do ring. Regressions
  planejadas/revisadas: +4 asserts em `tests/test_stdin_buf.c` cobrindo
  ponteiro nulo, estado inicial, estado ativo e overflow. Validação desta
  sessão foi por revisão estática de código e documentação, sem `make`, `git`
  ou scripts.
- **Etapa 6 seção c — stdin buffer diagnostic window reset
  (2026-05-09)** — `stdin_buf_reset_diagnostics()` permite reiniciar a janela
  de drops/high-watermark sem limpar bytes pendentes nem alterar índices do
  ring. O high-watermark é rebaseado para a ocupação atual e drops voltam a
  zero, permitindo medições por período para TTY/GUI/dispatcher. Regressions
  planejadas/revisadas: +4 asserts em `tests/test_stdin_buf.c` cobrindo
  preservação de count, preservação FIFO, reset em buffer cheio e novo
  overflow contado a partir da janela resetada. Validação desta sessão foi
  por revisão estática de código e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção d — stdin buffer batch drain
  (2026-05-09)** — `stdin_buf_pop_many()` permite drenar backlog do ring em
  lote, preservando FIFO e sem bloquear quando o buffer esvazia. O fallback
  `sys_read(fd=0)` mantém a semântica anterior de bloquear até o primeiro
  byte e depois drenar o restante sem bloquear, mas agora usa a API em lote
  para reduzir chamadas repetidas ao pop. Regressions planejadas/revisadas:
  +4 asserts em `tests/test_stdin_buf.c` cobrindo NULL/zero no-op, drain
  limitado, drain restante e wrap-around. Validação desta sessão foi por
  revisão estática de código e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção e — stdin buffer readiness probe
  (2026-05-09)** — `stdin_buf_ready()` expõe uma consulta não destrutiva de
  disponibilidade (`count > 0`) para loops de evento, dispatcher TTY/GUI e
  fallback CLI decidirem se há input pendente sem consumir bytes. Regressions
  planejadas/revisadas: +4 asserts em `tests/test_stdin_buf.c` cobrindo
  estado inicial vazio, push sem consumo, drain completo e buffer cheio.
  Validação desta sessão foi por revisão estática de código e documentação,
  sem `make`, `git` ou scripts.
- **Etapa 6 seção f — stdin buffer secure discard
  (2026-05-09)** — `stdin_buf_discard_many(max)` descarta backlog pendente sem
  entregar bytes à userland, zerando os slots removidos e preservando FIFO dos
  bytes remanescentes. Isso prepara transições login GUI/TTY/recovery para
  limpar teclas antigas ou sensíveis antes de trocar contexto de input.
  Regressions planejadas/revisadas: +4 asserts em `tests/test_stdin_buf.c`
  cobrindo no-op, descarte limitado, descarte oversized e wrap-around.
  Validação desta sessão foi por revisão estática de código e documentação,
  sem `make`, `git` ou scripts.
- **Etapa 6 seção g — secret prompt stdin scrub
  (2026-05-09)** — prompts secretos agora limpam o backlog de `stdin_buf`
  antes e depois da leitura mascarada. O `wizard_prompt(secret=1)` cobre login
  TUI e first-boot; os fluxos diretos de senha do instalador e do boot normal
  de volume cifrado também chamam `stdin_buf_discard_all()`.
  O objetivo é evitar que teclas antigas ou senhas digitadas em contexto TTY
  fiquem disponíveis para `sys_read(fd=0)` em userland após a transição.
  Validação desta sessão foi por revisão estática dos caminhos sensíveis, sem
  `make`, `git` ou scripts.
- **Etapa 6 seção h — stdin buffer discard-all contract
  (2026-05-09)** — `stdin_buf_discard_all()` encapsula o descarte completo do
  ring, evitando que consumidores sensíveis dependam diretamente de
  `STDIN_BUF_SIZE`. Os scrubs de prompts secretos foram migrados para a API
  semântica. Regressions planejadas/revisadas: +2 asserts em
  `tests/test_stdin_buf.c` cobrindo drain completo e no-op em buffer vazio.
  Validação desta sessão foi por revisão estática de código e documentação,
  sem `make`, `git` ou scripts.
- **Etapa 6 seção i — GUI event queue backpressure
  (2026-05-09)** — a fila central `gui_event` agora aceita eventos novos mesmo
  sob backlog cheio, descartando o evento mais antigo e contabilizando drops em
  `gui_event_dropped_total()`. `gui_event_capacity()` expõe a capacidade fixa
  para diagnósticos e testes. Essa política favorece responsividade de mouse,
  teclado e timer em GUI, evitando que call sites que ignoram o retorno de
  `gui_event_push()` percam silenciosamente os eventos mais recentes.
  Regressions planejadas/revisadas: +13 asserts em `tests/test_gui_event.c`
  cobrindo init, null push, FIFO, peek, flush, overflow drop-oldest,
  retenção do evento mais recente, preservação de drops após flush e reset dos
  drops por init. Validação desta sessão foi por revisão estática de código,
  wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção j — GUI key event publishing
  (2026-05-09)** — `gui_event_push_key()` encapsula a publicação de
  `GUI_EVENT_KEY_DOWN` e `desktop_handle_input()` passa a registrar teclas do
  desktop na fila central após a prioridade do `inline_prompt`, preservando o
  dispatch direto para a janela focada. Isso aproxima teclado e mouse do mesmo
  caminho de observabilidade/dispatcher sem expor teclas absorvidas por prompt.
  Regressions planejadas/revisadas: +2 asserts em `tests/test_gui_event.c`
  cobrindo payload `KEY_DOWN` e preservação de `window_id`, `keycode`,
  `modifiers`, `ch` e `timestamp`. Validação desta sessão foi por revisão
  estática de código, wiring de host tests e documentação, sem `make`, `git`
  ou scripts.
- **Etapa 6 seção k — GUI mouse event helpers
  (2026-05-09)** — `gui_event_push_mouse_move()`,
  `gui_event_push_mouse_button()` e `gui_event_push_mouse_scroll()` encapsulam
  a publicação semântica de movimento, botão e scroll na fila central.
  `desktop_handle_mouse()` foi migrado para esses helpers, reduzindo montagem
  manual de `struct gui_event` e aproximando mouse e teclado do mesmo contrato
  de dispatcher/observabilidade. Regressions planejadas/revisadas: +5 asserts
  em `tests/test_gui_event.c` cobrindo payload de movimento, metadados de
  movimento, down/up de botão, scroll com delta em `mouse.dy` e ordem FIFO
  mista entre helpers de mouse. Validação desta sessão foi por revisão estática
  de código, wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção l — GUI event diagnostics snapshot
  (2026-05-09)** — `gui_event_snapshot()` agrega capacidade, pendentes, espaço
  livre, high-watermark e drops em uma leitura coerente da fila central de
  eventos. O high-watermark é atualizado em push bem-sucedido, `flush` limpa
  apenas pendentes e preserva diagnósticos da janela atual, enquanto
  `gui_event_init()` reinicia a janela diagnóstica. Regressions
  planejadas/revisadas: +6 asserts em `tests/test_gui_event.c` cobrindo
  snapshot NULL, estado inicial, estado ativo, drain preservando watermark,
  overflow com fila cheia/drops e flush preservando diagnósticos. Validação
  desta sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção m — GUI event diagnostics reset
  (2026-05-09)** — `gui_event_reset_diagnostics()` reinicia a janela de
  métricas da fila GUI sem limpar eventos pendentes: o high-watermark é
  rebaseado para o número atual de eventos e `dropped_total` volta a zero,
  preservando índices e FIFO. Isso permite medir janelas específicas de login
  GUI, smokes e dispatcher sem perder input acumulado. Regressions
  planejadas/revisadas: +5 asserts em `tests/test_gui_event.c` cobrindo
  rebase do high-watermark, preservação FIFO, limpeza de drops com fila cheia,
  contagem fresca em overflow posterior e rebase para zero em fila vazia.
  Validação desta sessão foi por revisão estática de código, wiring de host
  tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção n — GUI event batch drain
  (2026-05-09)** — `gui_event_poll_many()` drena múltiplos eventos da fila
  central em uma chamada, preservando FIFO e retornando zero para `NULL` ou
  `max=0`. A API prepara o dispatcher/login GUI para processar bursts de
  mouse/teclado/timer por frame com menor overhead do que chamadas repetidas a
  `gui_event_poll()`, sem alterar backpressure, drops ou métricas de janela.
  Regressions planejadas/revisadas: +5 asserts em `tests/test_gui_event.c`
  cobrindo no-op para `NULL`, no-op para `max=0`, drain limitado com prefixo
  FIFO, drain do restante e preservação de FIFO após wrap-around do ring.
  Validação desta sessão foi por revisão estática de código, wiring de host
  tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção o — GUI event readiness
  (2026-05-09)** — `gui_event_ready()` fornece uma sonda sem consumo para loops
  de dispatcher/login GUI verificarem se há eventos pendentes sem depender de
  contagem direta. A API retorna `1` quando `eq_count > 0` e `0` quando a fila
  está vazia, preservando FIFO, snapshots, drops e backpressure. Regressions
  planejadas/revisadas: +4 asserts em `tests/test_gui_event.c` cobrindo fila
  inicial vazia, ready após push sem consumo, not-ready após drain em lote e
  ready com fila cheia. Validação desta sessão foi por revisão estática de
  código, wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção p — GUI non-input event helpers
  (2026-05-09)** — a fila central agora expõe helpers semânticos para eventos
  não-input: `gui_event_push_window_close()`,
  `gui_event_push_window_resize()`, `gui_event_push_window_focus()`,
  `gui_event_push_window_blur()`, `gui_event_push_paint()` e
  `gui_event_push_timer()`. Isso evita montagem manual de `struct gui_event`
  por call sites futuros de compositor/dispatcher/timer, mantendo payloads
  consistentes e o mesmo contrato de FIFO/backpressure/diagnósticos.
  Regressions planejadas/revisadas: +6 asserts em `tests/test_gui_event.c`
  cobrindo metadados de close, dimensões de resize, ordem focus/blur, target de
  paint, metadados de timer e FIFO misto entre helpers não-input. Validação
  desta sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção q — Compositor lifecycle event publication
  (2026-05-09)** — o compositor passou a publicar eventos reais de lifecycle
  na fila central: `compositor_focus_window()` emite blur/focus apenas em
  transições, `compositor_resize_window()` emite resize após realocar surface,
  `compositor_invalidate()` emite paint para janelas existentes e
  `compositor_destroy_window()` emite blur/close antes de liberar a janela.
  Isso conecta os helpers não-input ao fluxo autoritativo de estado do window
  lifecycle, preparando dispatcher/login GUI e smokes visuais para observar
  mudanças sem instrumentação paralela. Regressions planejadas/revisadas: nova
  suíte `tests/test_compositor_events.c` com +9 asserts cobrindo foco inicial,
  refocus sem duplicação, transição blur→focus, resize+callback, resize
  inalterado sem evento, invalidate→paint, invalidate ausente sem evento,
  destroy focado blur→close e destroy não focado close-only. Validação desta
  sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção r — GUI event stale-window discard
  (2026-05-09)** — `gui_event_discard_window(window_id)` remove da fila central
  eventos pendentes direcionados a uma janela específica, preservando FIFO dos
  demais eventos e mantendo intactos drops/high-watermark. `window_id == 0` é
  protegido para não descartar eventos globais/mouse. `compositor_destroy_window()`
  agora chama essa API antes de emitir os eventos finais `blur/close`, evitando
  que o dispatcher futuro entregue `paint`, `resize`, `key` ou `timer` atrasados
  para janelas já destruídas. Regressions planejadas/revisadas: +5 asserts em
  `tests/test_gui_event.c` cobrindo alvo ausente, proteção de `window_id=0`,
  remoção com FIFO, wrap-around do ring e preservação da janela diagnóstica; a
  suíte `tests/test_compositor_events.c` passou a enfileirar paint stale antes
  de destroy para validar que apenas `blur/close` ou `close` sobrevivem.
  Validação desta sessão foi por revisão estática de código, wiring de host
  tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção s — GUI paint event coalescing
  (2026-05-09)** — `gui_event_push_paint()` passou a coalescer eventos
  `GUI_EVENT_PAINT` pendentes por `window_id`, mantendo o primeiro paint
  enfileirado até que o dispatcher drene a fila. Isso reduz floods de redraw
  causados por chamadas repetidas a `compositor_invalidate()` no mesmo frame,
  preservando paints de janelas distintas, FIFO dos demais eventos,
  backpressure e métricas diagnósticas. Regressions planejadas/revisadas: +4
  asserts em `tests/test_gui_event.c` cobrindo duplicata por janela, janelas
  distintas, repaint após drain e wrap-around; +1 assert em
  `tests/test_compositor_events.c` cobrindo duas invalidações consecutivas da
  mesma janela. Validação desta sessão foi por revisão estática de código,
  wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção t — GUI mouse move event coalescing
  (2026-05-09)** — `gui_event_push_mouse_move()` passou a compactar bursts de
  `GUI_EVENT_MOUSE_MOVE` consecutivos no fim da fila. O evento pendente mantém
  a posição absoluta mais recente (`x/y`), acumula `dx/dy` com saturação `int16`
  e atualiza botões/timestamp para o estado mais novo. A coalescência não cruza
  eventos de botão/scroll ou outros tipos, preservando ordem semântica de
  input. Isso reduz pressão na fila em movimentos de mouse de alta frequência
  antes do dispatcher drenar eventos. Regressions planejadas/revisadas: +4
  asserts em `tests/test_gui_event.c` cobrindo compactação consecutiva,
  barreira por botão, saturação de delta e wrap-around do ring. Validação desta
  sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção u — GUI timer event coalescing
  (2026-05-09)** — `gui_event_push_timer()` passou a coalescer eventos
  `GUI_EVENT_TIMER` pendentes por par `(window_id, timer_id)`, preservando a
  primeira ocorrência na ordem FIFO até que o dispatcher drene a fila. Timers
  de IDs diferentes e timers iguais em janelas diferentes continuam separados.
  Isso evita floods de timer quando callbacks periódicos disparam mais rápido
  que o loop GUI consegue consumir, mantendo backpressure previsível.
  Regressions planejadas/revisadas: +5 asserts em `tests/test_gui_event.c`
  cobrindo duplicata do mesmo timer, IDs distintos, janelas distintas, requeue
  após drain e wrap-around do ring. Validação desta sessão foi por revisão
  estática de código, wiring de host tests e documentação, sem `make`, `git` ou
  scripts.
- **Etapa 6 seção v — Compositor visibility blur publication
  (2026-05-09)** — `compositor_hide_window()` e
  `compositor_minimize_window()` agora limpam foco e publicam
  `GUI_EVENT_WINDOW_BLUR` quando uma janela focada deixa de estar visível. Isso
  evita estado de foco preso em apps/dispatcher quando o usuário minimiza ou
  esconde a janela ativa, alinhando hide/minimize ao lifecycle já publicado em
  focus/destroy. Chamadas repetidas em janela já escondida ou minimização de
  janela não focada não geram blur espúrio. Regressions planejadas/revisadas:
  +4 asserts em `tests/test_compositor_events.c` cobrindo hide focado, hide já
  escondido sem duplicata, minimize não focado sem blur e minimize focado com
  blur + limpeza de foco. Validação desta sessão foi por revisão estática de
  código, wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção w — Compositor focus visibility guard
  (2026-05-09)** — `compositor_focus_window()` agora recusa foco em janelas
  invisíveis ou minimizadas, sem emitir blur/focus e sem alterar o foco atual.
  Isso impede que chamadas diretas entreguem foco lógico a uma janela fora da
  cena, preservando a sequência correta de restauração `compositor_show_window()`
  seguida de `compositor_focus_window()`. Regressions planejadas/revisadas: +3
  asserts em `tests/test_compositor_events.c` cobrindo janela escondida que não
  rouba foco, janela minimizada que não rouba foco e janela restaurada que volta
  a receber blur/focus normalmente. Validação desta sessão foi por revisão
  estática de código, wiring de host tests e documentação, sem `make`, `git` ou
  scripts.
- **Etapa 6 seção x — GUI event central dispatch API
  (2026-05-09)** — `gui_event_dispatch(dispatch, ctx, max_events)` adiciona o
  primeiro ponto central para consumo do `gui_event` dispatcher: drena eventos
  FIFO para um callback com limite explícito por chamada/frame. Callback nulo e
  `max_events == 0` são no-op sem consumir fila; o limite é calculado a partir
  da ocupação inicial da fila, então eventos enfileirados pelo próprio callback
  ficam para a próxima passagem e não causam starvation. Regressions
  planejadas/revisadas: +5 asserts em `tests/test_gui_event.c` cobrindo no-op
  nulo, no-op com limite zero, drain FIFO limitado, drain restante e isolamento
  de eventos enfileirados durante callback. Validação desta sessão foi por
  revisão estática de código, wiring de host tests e documentação, sem `make`,
  `git` ou scripts.
- **Etapa 6 seção y — GUI window dispatcher routing
  (2026-05-09)** — `include/gui/window_dispatcher.h` e
  `src/gui/window/window_dispatcher.c` adicionam uma camada isolada de dispatch
  de janela sobre `gui_event_dispatch()`. A primeira versão roteia
  `GUI_EVENT_KEY_DOWN`, `GUI_EVENT_MOUSE_SCROLL` e `GUI_EVENT_PAINT` para
  callbacks de `struct gui_window`, usando `window_id` explícito ou foco atual
  quando seguro para input/scroll. Janelas ausentes, invisíveis ou minimizadas
  são descartadas por categoria observável; eventos sem handler e eventos ainda
  não suportados também são contabilizados. `compositor_get_window()` expõe
  lookup controlado por ID sem alterar ownership do compositor. O módulo foi
  ligado ao build principal e ao runner host, mas ainda não substitui o caminho
  legado do desktop para evitar duplicidade até a etapa de integração
  fim-a-fim. Regressions planejadas/revisadas: +12 asserts em
  `tests/test_gui_window_dispatcher.c` cobrindo no-op por limite zero,
  roteamento de key, invalidação deferida para o próximo dispatch, estatísticas
  handled, scroll para janela focada, paint explícito, alvo ausente, handler
  ausente, lifecycle ignorado, alvo escondido, alvo minimizado e estatísticas
  de miss/ignore. Validação desta sessão foi por revisão estática de código,
  wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção z — GUI window dispatcher mouse routing
  (2026-05-09)** — `gui_window_dispatcher` agora também reconhece
  `GUI_EVENT_MOUSE_MOVE`, `GUI_EVENT_MOUSE_DOWN` e `GUI_EVENT_MOUSE_UP`.
  Eventos sem `window_id` usam `compositor_window_at(x, y)` para hit-test
  contra a janela visível mais alta; eventos com `window_id` usam lookup
  explícito. Callbacks de app recebem apenas coordenadas locais da área cliente:
  `MOUSE_MOVE` roteia para `on_hover`, `MOUSE_DOWN/UP` roteiam para `on_mouse`,
  e `MOUSE_DOWN` foca a janela alvo antes do callback, deixando lifecycle
  blur/focus para a próxima passagem do dispatcher central. Cliques na titlebar
  podem focar a janela sem invocar callback de app; alvos ausentes, sem handler,
  escondidos/minimizados ou fora da área cliente são categorizados pelas
  métricas existentes. Regressions planejadas/revisadas: +7 asserts em
  `tests/test_gui_window_dispatcher.c` cobrindo hover com coordenadas locais,
  mouse down com foco + callback + eventos deferidos, mouse up sem refocus,
  titlebar focus-only, alvo ausente, handler ausente e métricas agregadas.
  Validação desta sessão foi por revisão estática de código, wiring de host
  tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção aa — GUI window dispatcher timer routing
  (2026-05-09)** — `struct gui_window` agora expõe callback opcional
  `on_timer(win, timer_id)` e `gui_window_dispatcher` roteia
  `GUI_EVENT_TIMER` para janelas por `window_id` explícito. Timers não usam
  fallback por foco, evitando entrega temporal acidental para a janela ativa
  errada. O alvo precisa existir, estar visível e não estar minimizado; handler
  ausente, `window_id == 0` e alvo minimizado são categorizados nas métricas
  existentes. Após callback, a janela é revalidada antes de enfileirar repaint
  para evitar paint stale se o timer esconder/minimizar/destruir sua janela.
  Regressions planejadas/revisadas: +5 asserts em
  `tests/test_gui_window_dispatcher.c` cobrindo timer com alvo explícito,
  exigência de `window_id`, handler ausente, alvo minimizado e métricas
  agregadas. Validação desta sessão foi por revisão estática de código, wiring
  de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção ab — GUI window dispatcher context-menu routing
  (2026-05-09)** — `gui_window_dispatcher` agora trata `MOUSE_DOWN` com
  `MOUSE_BUTTON_RIGHT` como evento semântico de menu de contexto quando a janela
  alvo expõe `on_context_menu`. O roteamento reaproveita o hit-test/local-coords
  do mouse dispatcher, foca a janela antes do callback, chama `on_context_menu`
  com coordenadas locais da área cliente e revalida a janela antes de solicitar
  repaint. Se a janela não expõe handler de contexto, o evento cai no caminho
  normal de `on_mouse`, preservando compatibilidade. Cliques esquerdos continuam
  no caminho de mouse mesmo quando `on_context_menu` existe. Regressions
  planejadas/revisadas: +4 asserts em `tests/test_gui_window_dispatcher.c`
  cobrindo right-click para contexto, fallback para `on_mouse`, left-click no
  caminho normal e métricas agregadas. Validação desta sessão foi por revisão
  estática de código, wiring de host tests e documentação, sem `make`, `git` ou
  scripts.
- **Etapa 6 seção ac — GUI window dispatcher focus/blur lifecycle routing
  (2026-05-09)** — `struct gui_window` agora expõe callbacks opcionais
  `on_focus(win)` e `on_blur(win)`, inicializados como `NULL` nos caminhos de
  reset/criação do compositor. `gui_window_dispatcher` roteia
  `GUI_EVENT_WINDOW_FOCUS` para janelas existentes, visíveis e não minimizadas,
  e roteia `GUI_EVENT_WINDOW_BLUR` para janelas existentes mesmo que já tenham
  sido escondidas/minimizadas, preservando o contrato de lifecycle publicado por
  `compositor_hide_window()` e `compositor_minimize_window()`. Ambos os
  callbacks revalidam a janela antes de solicitar repaint pós-callback e
  alimentam as métricas existentes de alvo/handler ausente. Regressions
  planejadas/revisadas: +6 asserts em `tests/test_gui_window_dispatcher.c`
  cobrindo focus visível, blur visível, blur após hide, focus escondido
  bloqueado, handler ausente e métricas agregadas. Validação desta sessão foi
  por revisão estática de código, wiring de host tests e documentação, sem
  `make`, `git` ou scripts.
- **Etapa 6 seção ad — GUI window dispatcher mouse capture
  (2026-05-09)** — `gui_window_dispatcher` agora mantém captura de mouse para
  a janela que recebeu `MOUSE_DOWN` com `on_mouse`, preservando a entrega de
  `MOUSE_MOVE` com botão pressionado e do `MOUSE_UP` correspondente mesmo quando
  o cursor sai da área cliente ou cruza outra janela. Coordenadas continuam
  locais à janela capturada, inclusive quando ficam fora dos limites visíveis,
  o que prepara drag/selection/resizers sem depender de hit-test a cada frame.
  `MOUSE_UP` libera a captura; reset completo do dispatcher também limpa a
  captura pendente para evitar estado stale entre sessões/testes. O caminho de
  captura revalida janela/handler antes do callback e revalida a janela antes de
  solicitar repaint. Regressions planejadas/revisadas: +5 asserts em
  `tests/test_gui_window_dispatcher.c` cobrindo estabelecimento da captura,
  movimento capturado fora da área cliente, release no mouse up, retorno do
  hover normal após release e métricas agregadas. Validação desta sessão foi por
  revisão estática de código, wiring de host tests e documentação, sem `make`,
  `git` ou scripts.
- **Etapa 6 seção ae — GUI window dispatcher reset semantics
  (2026-05-09)** — o dispatcher agora separa reset diagnóstico de reset de
  estado. `gui_window_dispatcher_reset_stats()` zera somente métricas, sem
  interromper uma captura de mouse ativa, permitindo rebases de diagnóstico no
  meio de uma interação. `gui_window_dispatcher_reset()` zera métricas e limpa
  `captured_mouse_window_id`, sendo usado pelas fixtures/sessões para evitar
  estado stale entre inicializações. Regressions planejadas/revisadas: +4
  asserts em `tests/test_gui_window_dispatcher.c` cobrindo preservação de
  captura após reset de stats, métricas rebased, limpeza de captura no reset
  completo e métricas pós-reset completo. Validação desta sessão foi por revisão
  estática de código, wiring de host tests e documentação, sem `make`, `git` ou
  scripts.
- **Etapa 6 seção af — GUI key-up publishing and window routing
  (2026-05-09)** — a fila GUI agora expõe `gui_event_push_key_up()` para
  publicar `GUI_EVENT_KEY_UP` com o mesmo payload de `KEY_DOWN`
  (`window_id`, `keycode`, `modifiers`, `ch`, `timestamp`). `struct gui_window`
  recebeu callback opcional `on_key_up(win, keycode, mods)`, inicializado como
  `NULL` nos caminhos de reset/criação do compositor. `gui_window_dispatcher`
  roteia `GUI_EVENT_KEY_UP` para alvo explícito ou janela focada, usando as
  mesmas regras de visibilidade/minimização de `KEY_DOWN`, sem fallback para
  `on_key` para preservar compatibilidade dos apps que tratam apenas key-down.
  Regressions planejadas/revisadas: +2 asserts em `tests/test_gui_event.c`
  cobrindo enfileiramento/metadados de key-up e +5 asserts em
  `tests/test_gui_window_dispatcher.c` cobrindo alvo explícito, fallback por
  foco, handler ausente, alvo escondido e métricas agregadas. Validação desta
  sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção ag — GUI compositor-owned lifecycle dispatch policy
  (2026-05-09)** — `gui_window_dispatcher` agora trata explicitamente
  `GUI_EVENT_WINDOW_CLOSE` e `GUI_EVENT_WINDOW_RESIZE` como lifecycle owned pelo
  compositor. Esses eventos continuam úteis como publicação observável na fila,
  mas o dispatcher não chama `on_close` nem `on_resize`, porque
  `compositor_destroy_window()` e `compositor_resize_window()` já executam esses
  callbacks diretamente no ponto de mutação de estado. O dispatcher categoriza
  esses eventos em `ignored_total`, evitando duplicidade de callbacks e tornando
  o contrato auditável por métricas. Regressions planejadas/revisadas: +2
  asserts em `tests/test_gui_window_dispatcher.c` cobrindo ausência de chamadas
  duplicadas para close/resize e métricas agregadas de ignored lifecycle.
  Validação desta sessão foi por revisão estática de código, wiring de host
  tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção ah — GUI window dispatcher snapshot diagnostics
  (2026-05-09)** — `gui_window_dispatcher` agora expõe
  `gui_window_dispatcher_snapshot(out)`, uma leitura não destrutiva que agrega
  as métricas atuais (`struct gui_window_dispatcher_stats`), o
  `captured_mouse_window_id` bruto e um booleano `mouse_capture_active`
  derivado por revalidação do alvo capturado. A API retorna `0` para saída
  nula, preserva fila/estatísticas/estado de captura e permite diagnosticar
  captura stale quando a janela capturada foi escondida, minimizada, destruída
  ou perdeu `on_mouse`. Regressions planejadas/revisadas: +5 asserts em
  `tests/test_gui_window_dispatcher.c` cobrindo saída nula, estado idle,
  captura ativa com métricas, alvo capturado stale e reset completo limpando
  captura/métricas. Validação desta sessão foi por revisão estática de código,
  wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção ai — GUI mouse-scroll coalescing
  (2026-05-09)** — `gui_event_push_mouse_scroll()` agora compacta eventos
  consecutivos de `GUI_EVENT_MOUSE_SCROLL` quando o último item pendente da fila
  também é scroll. A compactação atualiza posição absoluta (`x/y`), preserva o
  estado mais recente de `buttons` e `timestamp`, mantém `dx = 0` e acumula
  `dy` com saturação `int16_t`, sem varrer a fila e sem atravessar barreiras de
  ordenação como botões, movimento, teclas, paint, lifecycle ou alvo explícito.
  Os coalescers de `MOUSE_MOVE` e `MOUSE_SCROLL` agora também recusam compactar
  sobre evento anterior com `window_id != 0`, preservando eventos direcionados
  inseridos via `gui_event_push()`. Regressions planejadas/revisadas: +9 asserts
  em `tests/test_gui_event.c` cobrindo coalescência consecutiva, barreira por
  botão, barreira por tecla, barreira por movimento, barreira por alvo explícito
  em move/scroll, saturação positiva/negativa de delta e coalescência após wrap
  do ring buffer. Validação desta sessão foi por revisão estática de código,
  wiring de host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção aj — GUI event queue non-destructive batch peek
  (2026-05-09)** — `gui_event_peek_many(out, max)` agora copia um prefixo FIFO
  da fila GUI sem avançar `eq_tail`, sem alterar `eq_count` e sem resetar
  métricas diagnósticas. A API rejeita `out == NULL` e `max == 0` como no-op,
  limita a cópia ao menor valor entre `max` e eventos pendentes, e preserva a
  ordem mesmo quando o ring buffer atravessou wrap. Isso permite inspeção de
  bursts/eventos pendentes por ferramentas de diagnóstico e futuras UIs de debug
  sem drenar input real. Regressions planejadas/revisadas: +4 asserts em
  `tests/test_gui_event.c` cobrindo no-op nulo/zero, cópia limitada sem dreno,
  cópia de todos os eventos disponíveis sem dreno e FIFO após wrap. Validação
  desta sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção ak — GUI event queue backpressure helpers
  (2026-05-09)** — a fila GUI agora expõe `gui_event_space_available()` e
  `gui_event_full()`, permitindo produtores de input, diagnósticos e futuras UIs
  de debug consultarem capacidade livre e estado cheio sem montar
  `gui_event_snapshot()`. Os helpers derivam diretamente de `eq_count`,
  preservam FIFO/drops/high-watermark e acompanham drenagem normal da fila,
  mantendo a semântica de overflow por descarte do evento mais antigo.
  Regressions planejadas/revisadas: +4 asserts em `tests/test_gui_event.c`
  cobrindo fila vazia, ocupação parcial, fila cheia e recuperação de espaço após
  dreno. Validação desta sessão foi por revisão estática de código, wiring de
  host tests e documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção al — GUI target-required helper hardening
  (2026-05-09)** — `gui_event_push_paint()` e `gui_event_push_timer()` agora
  rejeitam `window_id == 0` antes de consultar coalescência ou enfileirar
  eventos. Isso torna explícito no produtor o contrato já exigido pelo
  dispatcher para paint/timer, evita eventos impossíveis de rotear, reduz pressão
  artificial na fila e preserva os caminhos válidos com alvo explícito.
  Regressions planejadas/revisadas: +2 asserts em `tests/test_gui_event.c`
  cobrindo rejeição de paint/timer sem alvo, além de ajuste em
  `tests/test_gui_window_dispatcher.c` para validar que o timer inválido não
  entra mais na fila nem incrementa métricas do dispatcher. Validação desta
  sessão foi por revisão estática de código, wiring de host tests e
  documentação, sem `make`, `git` ou scripts.
- **Etapa 6 seção am — CapyUI responsive layout e users UI hardening
  (2026-05-09)** — CapyUI recebeu um pacote de UX/robustez antes de avançar
  outras frentes: `widget_paint()` agora trunca labels de botões com ellipsis
  para evitar vazamento de caracteres fora da borda; Settings recalcula largura
  da sidebar/tabs em resize, usa textos truncados no conteúdo e mostra status
  persistente no rodapé; o fluxo "Adicionar usuario" pela interface agora valida
  nome, prepara `/etc/users.db`, rejeita duplicado, reserva UID/GID, cria
  `/home` e `/home/<user>` com metadados, salva prefs de idioma e exibe erro ou
  sucesso localizado. File Manager ganhou toolbar responsiva com botões
  compactos/truncados, path/status truncados, hit-test ajustado ao novo row
  height e linhas que respeitam o rodapé. Desktop Icons agora organiza entries em
  grid por colunas conforme altura/largura útil do desktop, mantendo nomes em
  duas linhas e evitando underflow quando a taskbar ocupa a área inferior.
  Validação desta sessão foi por revisão estática de código e documentação, sem
  `make`, `git`, scripts ou execução de automação.
- **Etapa 6 seção an — Inline prompt secret input hardening
  (2026-05-09)** — `inline_prompt` ganhou `inline_prompt_show_secret()` como
  API compatível para entradas sensíveis, preservando `inline_prompt_show()` para
  os demais callers. O renderer agora aplica truncamento em título/hint, usa
  viewport horizontal para texto longo, posiciona o caret dentro do campo mesmo
  quando o cursor passa da área visível e mascara caracteres com `*` no modo
  secreto. O fechamento limpa o buffer interno e o snapshot enviado ao callback é
  higienizado com escrita `volatile` após o retorno. O fluxo de criação de
  usuário do Settings passou a usar o prompt secreto para senha, evitando exibição
  em claro durante a digitação. Validação desta sessão foi por revisão estática
  de código e documentação, sem `make`, `git`, scripts ou automação.
- **Etapa 6 seção ao — Inline prompt edit navigation polish
  (2026-05-09)** — `inline_prompt_handle_key()` agora trata `KEY_LEFT`,
  `KEY_RIGHT`, `KEY_HOME`, `KEY_END` e `KEY_DELETE` além de Enter/Esc/Backspace e
  ASCII imprimível. Isso permite corrigir texto no meio do prompt sem apagar e
  redigitar tudo, preservando o viewport horizontal e invalidando a janela após
  cada alteração de cursor/conteúdo. `inline_prompt_handle_click()` também passa
  a posicionar o caret quando o usuário clica dentro do campo de entrada,
  calculando coluna com base em `font_default()->glyph_width`, viewport visível e
  clamp no tamanho real do texto. A documentação pública do header foi alinhada
  com o novo contrato. Validação desta sessão foi por revisão estática de código
  e documentação, sem `make`, `git`, scripts ou automação.
- **Etapa 6 seção ap — Taskbar label fitting e hit-test clamp
  (2026-05-09)** — a taskbar agora usa helpers locais de ajuste por largura para
  o botão Menu, labels de janelas e entradas do menu popup, gerando ellipsis
  quando o texto não cabe no espaço disponível. A lista de janelas reserva a área
  do relógio antes de pintar os itens, encurta o último item visível quando
  necessário e interrompe a renderização quando não há largura útil. O
  `taskbar_handle_click()` usa o mesmo limite visual para evitar foco acidental em
  itens que não foram desenhados ou que foram cortados pela reserva do relógio.
  Validação desta sessão foi por revisão estática de código e documentação, sem
  `make`, `git` ou scripts de build.
- **Etapa 6 seção aq — Context menu viewport clamp e label fitting
  (2026-05-09)** — o compositor passou a expor `compositor_screen_size()` como
  accessor público de leitura para largura/altura atuais, sem alterar ownership
  do framebuffer. O `context_menu_show()` usa esse accessor para limitar popups à
  área visível nos eixos X/Y, inclusive quando o menu nasce próximo às bordas
  direita/inferior. O painter do menu agora reutiliza helpers locais de fitting
  por largura e desenha labels com ellipsis, preservando hover, separators,
  disabled-state e click routing. Validação desta sessão foi por revisão estática
  de código e documentação, sem `make`, `git` ou scripts de build.
- **Etapa 6 seção ar — Taskbar menu viewport clamp
  (2026-05-09)** — o popup do menu da taskbar agora calcula sua posição por um
  helper dedicado que consulta `compositor_screen_size()` e limita X/Y ao
  viewport atual. Isso evita `popup_y` negativo quando o menu tem muitas entradas
  ou a resolução é baixa, cria o menu já na posição segura e reposiciona o popup
  reutilizado antes de cada abertura. A mudança preserva hover, ações, separators
  e o fechamento por click externo já existente. Validação desta sessão foi por
  revisão estática de código e documentação, sem `make`, `git` ou scripts de
  build.
- **Etapa 6 seção as — Inline prompt modal right-click guard
  (2026-05-09)** — o dispatch de mouse do desktop agora dá prioridade ao
  `inline_prompt` também em clique direito. Quando o prompt modal está aberto,
  qualquer context menu/menu da taskbar aberto é fechado, o clique é roteado para
  `inline_prompt_handle_click()` e o evento não continua para janelas, ícones do
  desktop ou taskbar por trás. Isso evita abertura acidental de menus durante
  rename/create e principalmente durante prompts secretos de senha. Validação
  desta sessão foi por revisão estática de código e documentação, sem `make`,
  `git` ou scripts de build.
- **Etapa 6 seção at — Overlay hover/cursor isolation
  (2026-05-09)** — o caminho de mouse-move do desktop agora isola hovers e hints
  de cursor quando overlays estão ativos. Com `inline_prompt` aberto, nenhuma
  janela por trás recebe `on_hover`; com context menu ou menu da taskbar aberto,
  apenas o overlay correspondente atualiza hover. O cálculo de cursor também
  passa a tratar `inline_prompt`, context menu e menu da taskbar como barreiras,
  mantendo `COMP_CURSOR_ARROW` e evitando resize/text/loading cursors vindos de
  janelas encobertas. Validação desta sessão foi por revisão estática de código e
  documentação, sem `make`, `git` ou scripts de build.
- **Etapa 6 seção au — Overlay scroll isolation
  (2026-05-09)** — o caminho de mouse wheel do desktop agora respeita as mesmas
  barreiras de overlay usadas por hover/cursor. Quando `inline_prompt`, context
  menu ou menu da taskbar está aberto, o delta de scroll é consumido no desktop e
  não é publicado em `gui_event_push_mouse_scroll()`, nem entregue diretamente à
  janela focada/terminal por trás. Isso evita que prompts, menus e fluxos de senha
  causem scroll acidental em conteúdo encoberto. Validação desta sessão foi por
  revisão estática de código e documentação, sem `make`, `git` ou scripts de
  build.
- **Etapa 6 seção av — Desktop overlay state helper
  (2026-05-09)** — o loop de mouse do desktop passou a centralizar a composição
  do estado de overlays em `desktop_overlay_active()`, cobrindo `inline_prompt`,
  context menu e menu da taskbar. Os caminhos de cursor hint e mouse wheel agora
  usam o mesmo helper, reduzindo duplicação de chamadas de estado e evitando
  divergência futura entre isolamento de cursor e scroll. A mudança é local ao
  `desktop.c` e preserva o comportamento validado nas seções anteriores.
  Validação desta sessão foi por revisão estática de código e documentação, sem
  `make`, `git` ou scripts de build.
- **Etapa 6 seção aw — Overlay Escape key routing
  (2026-05-09)** — o dispatch de teclado do desktop agora fecha context menu e
  menu da taskbar com `Esc` antes de publicar a tecla na fila GUI ou entregar ao
  foco atual. O `inline_prompt` continua tendo prioridade máxima e consome `Esc`
  pelo próprio `inline_prompt_handle_key()`. Para menus abertos, o novo helper
  local `desktop_handle_overlay_escape()` fecha os overlays, invalida a cena e
  impede vazamento da tecla para terminal/apps atrás do menu. Validação desta
  sessão foi por revisão estática de código e documentação, sem `make`, `git` ou
  scripts de build.
- **Etapa 6 seção ax — CapyUI alpha.7 patch closure
  (2026-05-09)** — o pacote recente de CapyUI/F6 foi fechado como
  `0.8.0-alpha.7+20260509`. O manifesto `VERSION.yaml`, os macros públicos em
  `include/core/version.h`, o README raiz, o índice de release notes, a release
  note `docs/releases/capyos-0.8.0-alpha.7+20260509.md`, o índice de screenshots
  e o STATUS executivo foram alinhados para refletir o patch de prompts secretos,
  menus resilientes e isolamento modal de overlays. A validação desta sessão foi
  por revisão estática de metadados e documentação, sem `make`, `git`, scripts de
  build/teste ou automação Python.
- **Etapa 6 seção ay — Settings users UI alpha.8 patch closure
  (2026-05-09)** — o fluxo de criacao de usuario via Settings foi corrigido
  como `0.8.0-alpha.8+20260509`. A UI agora exige sessao admin antes de abrir
  prompts, valida usernames com a mesma politica do CLI (`[A-Za-z0-9_-]`),
  executa `userdb_ensure()`, `userdb_find()`, `userdb_next_ids()` e
  `userdb_add()` em sessao VFS de sistema para nao herdar bloqueios de
  permissao do usuario ativo, centraliza a preparacao de `/home/<user>` em
  `user_home_prepare()` e grava preferencias iniciais ainda fora da sessao do
  usuario. A validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git`, scripts de build/teste ou automacao Python
  de validacao.
- **Etapa 6 seção az — Auth user lifecycle alpha.9 patch closure
  (2026-05-09)** — o ciclo de vida de criacao de usuarios foi consolidado como
  `0.8.0-alpha.9+20260509`. `USER_UID_FIRST_REGULAR` e
  `USER_GID_FIRST_REGULAR` centralizam a base regular `1000`;
  `userdb_next_ids()` agora garante `/etc/users.db` antes de iterar e retorna o
  primeiro UID/GID regular mesmo com banco vazio. O comando `add-user` foi
  alinhado ao fluxo robusto da UI: valida papel/nome/senha antes de elevar,
  executa `userdb_ensure()`, `userdb_find()`, `userdb_next_ids()` e
  `userdb_add()` em sessao VFS de sistema, usa `user_home_prepare()` para
  `/home/<user>`, grava preferencias iniciais no mesmo contexto e limpa
  `struct user_record` no encerramento. O recovery admin tambem deixou de
  duplicar literais `1000`. A validacao desta sessao foi por revisao estatica
  de codigo e documentacao, sem `make`, `git`, scripts de build/teste ou
  automacao Python de validacao.
- **Etapa 6 seção ba — Auth bootstrap/recovery alpha.10 patch closure
  (2026-05-09)** — o bootstrap e o recovery de contas admin foram endurecidos
  como `0.8.0-alpha.10+20260509`. `recovery-storage-repair reset-admin` agora
  garante `/etc/users.db`, executa busca/reserva/gravação em sessao VFS de
  sistema, prepara ou repara `/home/admin` via `user_home_prepare()`, restaura a
  sessao anterior em todos os encerramentos e limpa `struct user_record`. O
  provisionamento automatico de first-boot reserva UID/GID via
  `userdb_next_ids()`, remove os ultimos literais `1000` do caminho admin,
  prepara a home pelo helper comum e limpa `admin`/`verify_admin` antes de
  descartar a senha do instalador. A validacao desta sessao foi por revisao
  estatica de codigo e documentacao, sem `make`, `git`, scripts de build/teste
  ou automacao Python de validacao.
- **Etapa 5 seção a — Update manifest gate alpha.11 patch closure
  (2026-05-09)** — a trilha local de update foi endurecida como
  `0.8.0-alpha.11+20260509`. `update_agent_poll()` compara versões
  semanticamente, rejeita catalogos/staged mais antigos que o sistema atual,
  exige `payload_sha256` hex64 para updates novos e para staged pronto, e
  propaga estados degradados específicos antes de `update-stage` ou
  `update-arm`. `update-import-manifest` agora recusa manifestos não novos ou
  sem hash de payload. `update_agent_apply_boot_slot()` recusa aplicar staged
  hashado sem verifier (`-33`), enquanto
  `update_agent_apply_boot_slot_verified()` propaga falhas de poll antes de
  ativar boot slot. A validacao desta sessao foi por revisao estatica de codigo
  e documentacao, sem `make`, `git`, scripts de build/teste ou automacao Python
  de validacao.
- **Etapa 5 seção b — Update manifest Ed25519 gate alpha.12 patch closure
  (2026-05-09)** — a trilha local de update foi fechada como
  `0.8.0-alpha.12+20260509` exigindo `signature_ed25519` hex128 em manifestos
  novos, staged e importados. O `update-agent` captura o texto canonico do
  manifesto excluindo apenas a linha `signature_ed25519=`, valida formato hex,
  usa `ed25519_verify()` com chave publica embarcada no runtime normal e permite
  verifier injetavel sob `UNIT_TEST` para regressões host. `update_agent_poll()`
  passa a rejeitar catalogos/staged sem assinatura valida (`-28`/`-29`),
  `update_agent_import_manifest_path()` recusa import sem assinatura valida
  (`-23`) antes de persistir cache, e o Makefile host inclui
  `src/security/ed25519.c`. A validacao desta sessao foi por revisao estatica
  de codigo e documentacao, sem `make`, `git`, scripts de build/teste ou
  automacao Python de validacao.
- **Etapa 2 seção a — Release signing tooling alpha.13 patch closure
  (2026-05-09)** — a trilha F2 ganhou tooling operacional em
  `0.8.0-alpha.13+20260509`: `tools/scripts/sign_release.py` assina
  `build/release-artifacts.sha256` com Ed25519 raw via OpenSSL, exige chave
  privada fora do repositorio e permissao segura por padrao, exporta chave
  publica opcional e faz auto-verificacao quando a chave publica e informada.
  `tools/scripts/verify_release_signature.py` valida chave publica Ed25519/SPKI
  e assinatura sobre os bytes exatos do arquivo de checksums. O Makefile agora
  expoe `sign-release-checksums` e `verify-release-signature`, sem acoplar esses
  passos ao `release-check` automatico enquanto a chave offline oficial nao for
  provisionada. `docs/security/release-signing.md` documenta geracao, publicacao
  e rotacao. A validacao desta sessao foi por revisao estatica de codigo e
  documentacao, sem `make`, `git`, scripts de build/teste ou automacao Python
  de validacao.
- **Etapa 5 seção c — Remote manifest fetch alpha.14 patch closure
  (2026-05-09)** — F5 ganhou `update_agent_fetch_remote_manifest()` em
  `0.8.0-alpha.14+20260509`. O agente prepara a trilha selecionada, baixa o
  `remote_manifest=` configurado, persiste `/system/update/fetched.ini` apenas
  como arquivo temporario, chama `update_agent_import_manifest_path()` para
  validar `channel`/`branch`/`source`, versao mais nova, `payload_sha256` e
  `signature_ed25519`, remove o temporario e so entao deixa o catalogo local
  atualizado. Sob `UNIT_TEST`, o fetcher e injetavel para regressões sem pilha
  HTTP host. O shell ganhou `update-fetch` e eventos `fetch` no historico de
  update. A validacao desta sessao foi por revisão estatica de codigo e
  documentacao, sem `make`, `git`, scripts de build/teste ou automacao Python
  de validacao.
- **Etapa 5 seção c — Verified update apply alpha.15 patch closure
  (2026-05-09)** — F5 ganhou `update-apply <payload_sha256>` em
  `0.8.0-alpha.15+20260509`. O comando chama
  `update_agent_apply_boot_slot_verified()`, recusa digest ausente, malformado
  ou divergente antes de armar boot slot, publica summary estavel de sucesso
  (`verified staged update applied to boot slot`) e registra `event=apply` no
  historico de update. Esta entrega nao baixa payload remoto automaticamente e
  nao substitui o fluxo posterior de confirmacao de saude apos reboot. A
  validacao desta sessao foi por revisão estatica de codigo e documentacao, sem
  `make`, `git`, scripts de build/teste ou automacao Python de validacao.
- **Etapa 5 seção c — Post-apply health/rollback alpha.16 patch closure
  (2026-05-09)** — F5 ganhou `update-confirm-health` e
  `update-rollback-check` em `0.8.0-alpha.16+20260509`. O primeiro confirma
  saude do boot atual, limpa ativacao pendente persistente quando aplicavel e
  publica summary `boot health confirmed; update committed`; o segundo expõe
  `no boot rollback pending`, `boot rollback failed` ou `boot rollback completed;
  staged update cleared`, registrando eventos `confirm-health`, `rollback-check`
  e `rollback`. A validacao desta sessao foi por revisão estatica de codigo e
  documentacao, sem `make`, `git`, scripts de build/teste ou automacao Python
  de validacao.
- **Etapa 5 seção d — Remote channel policy alpha.17 patch closure
  (2026-05-09)** — F5.4 fechou a política remota em
  `0.8.0-alpha.17+20260509`: `develop` deriva `remote_manifest` em
  `refs/heads/<branch>`, enquanto `stable` mantém `branch=main` para
  compatibilidade de manifesto e deriva a URL bruta em
  `refs/tags/v<major>.<minor>.<patch>`. O bootstrap de
  `/system/update/repository.ini`, o `update-agent` sem `remote_manifest=`
  explícito e as regressões planejadas agora usam a mesma regra. A validacao
  desta sessao foi por revisão estatica de codigo e documentacao, sem `make`,
  `git`, scripts de build/teste ou automacao Python de validacao.
- **Etapa 5 seção e — Payload origin policy alpha.18 patch closure
  (2026-05-09)** — F5 avançou em `0.8.0-alpha.18+20260509` com
  `payload_url` obrigatório para manifestos novos. O `update-agent` parseia e
  valida URLs HTTPS ou caminhos locais sob `/system/update/`, rejeita espaços e
  `..`, propaga a origem para `system_update_status`, `update-status`,
  `update-fetch`, `update-stage` e `/var/log/update-history.log`, mantendo o
  download automático de payload como próxima etapa. A validacao desta sessao
  foi por revisão estatica de codigo e documentacao, sem `make`, `git`, scripts
  de build/teste ou automacao Python de validacao.
- **Etapa 5 seção f — Payload download alpha.19 patch closure
  (2026-05-09)** — F5 avançou em `0.8.0-alpha.19+20260509` com download
  explícito do payload declarado. O `update-agent` ganhou `security/sha256`,
  `update_agent_download_payload()`, fetcher binário injetável, writer binário,
  cache `/system/update/payload.bin`, persistência de `payload_cache_sha256` no
  estado e comando `update-download-payload`. Payload vazio, transporte falho,
  writer ausente e mismatch de SHA-256 são recusados antes de cache persistente;
  `update-stage` também passa a exigir cache de payload verificado.
  A validacao desta sessao foi por revisão estatica de codigo e documentacao,
  sem `make`, `git`, scripts de build/teste ou automacao Python de validacao.
- **Etapa 5 seção g — Cached apply alpha.20 patch closure
  (2026-05-09)** — F5 avançou em `0.8.0-alpha.20+20260509` com apply
  cache-first. O `update-agent` ganhou `update_agent_apply_cached_payload()`,
  `update-apply` passou a aceitar zero ou um argumento, usa
  `payload_cache_sha256` verificado por padrão e mantém digest manual apenas
  como fallback explícito. Cache ausente retorna summary estável antes de tocar
  boot slot; cache/staged mismatch continua recusado pelo gate SHA-256. A
  validacao desta sessao foi por revisão estatica de codigo e documentacao,
  sem `make`, `git`, scripts de build/teste ou automacao Python de validacao.
- **Etapa 5 seção h — Update prepare alpha.21 patch closure
  (2026-05-09)** — F5 avançou em `0.8.0-alpha.21+20260509` com um fluxo
  seguro de preparo. O `update-agent` ganhou
  `update_agent_prepare_staged_update()`, e o shell ganhou `update-prepare`
  registrado em system-control. O comando encadeia manifesto remoto assinado,
  payload cacheado/verificado, staging e arm da ativacao, mas mantém
  `update-apply` separado para evitar aplicar boot slot implicitamente. A
  validacao desta sessao foi por revisão estatica de codigo e documentacao,
  sem `make`, `git`, scripts de build/teste ou automacao Python de validacao.
- **Etapa 5 seção i — Prepare dry-run alpha.22 patch closure
  (2026-05-09)** — F5 avançou em `0.8.0-alpha.22+20260509` com preflight
  local sem efeitos persistentes de update. O `update-agent` ganhou
  `update_agent_prepare_dry_run()`, e o shell ganhou `update-prepare-dry-run`
  registrado em system-control. O comando valida catalogo local, `payload_url`,
  assinatura Ed25519 e `payload_cache_sha256` antes de staging/arm, sem
  persistir staging, armar ativacao ou aplicar boot slot. A validacao desta
  sessao foi por revisão estatica de codigo e documentacao, sem `make`, `git`,
  scripts de build/teste ou automacao Python de validacao.
- **Etapa 5 seção j — Prepare explain alpha.23 patch closure
  (2026-05-09)** — F5 avançou em `0.8.0-alpha.23+20260509` com diagnostico
  explicavel dos gates locais de preparo. O `update-agent` ganhou
  `struct update_prepare_explain` e `update_agent_prepare_explain()`, e o shell
  ganhou `update-prepare-explain` registrado em system-control. O comando mostra
  `poll`, catalogo, repositorio, versao, `payload_sha256`, `payload_url`,
  assinatura, cache e `stage_safe`, sem fetch, download, staging, arm ou apply.
  A validacao desta sessao foi por revisão estatica de codigo e documentacao,
  sem `make`, `git`, scripts de build/teste ou automacao Python de validacao.

#### Critérios de aceite

- [ ] Boot novo entra em login GUI sem comando manual.
- [ ] Login com credencial válida abre desktop.
- [ ] Mouse move, click e drag funcionam em VMware+E1000+SVGA II.
- [ ] Janela tem foco, pode ser arrastada, fechada.
- [ ] Taskbar mostra apps abertos; click alterna foco.
- [ ] Terminal gráfico aceita comandos e renderiza saída.
- [ ] CTRL+ALT+F1 muda para TTY de recovery.

#### Como será feito

1. **F6a** Migrar autostart desktop para timer-driven loop.
2. **F6b** Mouse PS/2 → input_runtime → compositor.
3. **F6c** Dispatcher unificado.
4. **F6d** Loginwindow GUI (escreve em `auth/user.c` via syscall).
5. **F6e** Taskbar polimento + smoke.

#### Validação

```bash
make smoke-x64-gui-session
make smoke-x64-mouse-events           # novo: clica em coordenada conhecida e valida hit-test
```

#### Saída esperada

Sessão gráfica utilizável fim-a-fim. Base para F7.

---

### F7 — Apps básicos do desktop

**Criticidade:** média — torna o sistema utilizável sem CLI.
**Depende de:** F3 (browser isolado serve de pattern), F6 (GUI completa).
**Paralelo possível com:** F8 (package manager).
**Risco:** baixo — apps são compositores de syscalls + libs já existentes.
**Esforço:** 1 sessão por app + 1 sessão por suite final = ~7 sessões.
**Branch sugerida:** `feature/f7-apps-basicos`.

#### Objetivo

Pacote inicial de apps do desktop:

| App | Estado atual | Meta F7 |
|---|---|---|
| `task_manager` | ✅ Funcional (W2) | Polimento: gráfico de CPU/RSS por processo |
| `text_editor` | scaffold | Editar/salvar arquivos VFS, syntax highlight básico |
| `file_manager` | scaffold | Browse, copy, move, delete em CAPYFS |
| `settings` | scaffold | Network mode, theme, language, user mgmt |
| `image_viewer` | parcial (no html_viewer) | App standalone: PNG/JPEG, zoom, próximo/anterior |
| `calculator` | inexistente | Simples: 4 ops, decimal, hex |
| `log_viewer` | inexistente | Browse `/var/log/*`, filtro por nível |

Todos como **processos ring 3 isolados** (lição de F3).

#### Entregáveis

- **E7.1** — `userland/bin/text_editor/`, `userland/bin/file_manager/`, etc.
- **E7.2** — `src/apps/<app>_chrome/` widgets do compositor (frames + IPC, igual capybrowser).
- **E7.3** — `userland/lib/libcapy-ui/` — toolkit minimalista (button, list, textbox, dialog).
- **E7.4** — Smokes por app.
- **E7.5** — Pacote inicial de ícones em `assets/icons/`.

#### Critérios de aceite

- [ ] Cada app abre, executa função primária, fecha sem crash.
- [ ] Falha de um app não derruba desktop.
- [ ] Todos passam smoke.

#### Como será feito

Por app (1 sessão):

1. Definir IPC (input/output via pipe).
2. Implementar binário userland.
3. Implementar chrome no compositor.
4. Smoke.

#### Validação

```bash
make smoke-x64-text-editor
make smoke-x64-file-manager
make smoke-x64-settings
make smoke-x64-image-viewer
make smoke-x64-calculator
make smoke-x64-log-viewer
```

#### Saída esperada

Usuário comum opera sem CLI.

---

### F8 — Package manager + SDK + ABI estável

**Criticidade:** média — destrava ecossistema, mas não é cliente-final.
**Depende de:** F4 (rede userland), F5 (update fetch reaproveita boa parte), F7 (apps reais como cliente).
**Paralelo possível com:** F9 (JS) e F10 (linguagem).
**Risco:** médio — define ABI que ficará estável a longo prazo.
**Esforço:** 6–10 sessões.
**Branch sugerida:** `feature/f8-pkgd`.

#### Objetivo

Distribuir apps separadamente do kernel. Hoje, todo binário userland é embarcado no kernel via `embedded_progs`. Isso não escala.

#### Entregáveis

- **E8.1** — Formato `.capypkg` (tar+manifest+signature).
- **E8.2** — `pkgd` (daemon ring 3): `install`, `remove`, `list`, `verify`.
- **E8.3** — Repositório oficial GitHub-hosted (`capyos-packages` repo).
- **E8.4** — `software-center` (app GUI para descoberta/install).
- **E8.5** — ABI documentada: `docs/api/abi-stable.md` listando syscall numbers, struct layouts congelados.
- **E8.6** — `libcapy-fs`, `libcapy-ui`, `libcapy-net`, `libcapy-tls` como SOs userland (links dinâmicos? ou estáticos? **Decisão de F8**: começar estáticos, links dinâmicos depois).
- **E8.7** — SDK header pack + samples + `capyos-sdk` repo separado.
- **E8.8** — Smoke `smoke-x64-pkg-install` (instala pacote local, executa, remove).

#### Critérios de aceite

- [ ] `pkg install hello` baixa, valida assinatura, instala em `/usr/bin/hello`.
- [ ] `pkg list` lista instalados com versões.
- [ ] `pkg remove hello` remove e atualiza DB.
- [ ] DB de pacotes sobrevive reboot.
- [ ] Update de pacote sem reinstalar OS.

#### Como será feito

Sequência longa; ver branch dedicada com sub-fases F8a–F8h documentadas no PR.

#### Validação

```bash
make smoke-x64-pkg-install
make smoke-x64-pkg-update
```

#### Saída esperada

Plataforma aberta para software de terceiros.

---

### F9 — JavaScript engine sandboxed (M8.6)

**Criticidade:** baixa-média — essencial para web moderna, opcional para SO.
**Depende de:** F3 (browser isolado), F4 (rede), F8 (formato de app pode reaproveitar bytecode).
**Paralelo possível com:** F10.
**Risco:** alto — JS engine é projeto enorme em si.
**Esforço:** 10+ sessões. **Decisão arquitetural**: usar QuickJS (C, 200KB, MIT) ou escrever subset (CapyJS). Decidir no início da fase.
**Branch sugerida:** `feature/f9-js-engine`.

#### Objetivo

Renderizar páginas que dependem de JS minimamente (sem React/Angular completos). Sandboxed dentro do processo `capybrowser`.

#### Entregáveis

- **E9.1** — Decisão: integrar QuickJS ou escrever CapyJS subset (ECMAScript 5 mínimo).
- **E9.2** — Bridge DOM ↔ JS no `capybrowser`.
- **E9.3** — Budget de execução (op_budget já tem): mata script de loop infinito.
- **E9.4** — Sem `eval` arbitrário, sem acesso a syscalls (só DOM bridge).
- **E9.5** — Smokes `js-basic`, `js-dom-mutation`, `js-timeout-killed`.

#### Critérios de aceite

- [ ] Página com `document.title = "Hello"` atualiza título.
- [ ] Loop infinito é interrompido em ≤ 5s pelo budget.
- [ ] M8.6 promovido para ✅.

#### Validação

```bash
make smoke-x64-js-basic
make smoke-x64-js-timeout
```

---

### F10 — CapyLang (linguagem própria)

**Criticidade:** baixa — branding/ecossistema.
**Depende de:** F8 (precisa de package manager para distribuir runtime).
**Paralelo possível com:** F9.
**Risco:** alto — projeto longo.
**Esforço:** 20+ sessões em três etapas.
**Branch sugerida:** `feature/f10-capylang`.

#### Objetivo

Linguagem nativa do CapyOS para automação primeiro, apps depois.

#### Entregáveis (em ondas)

- **Etapa 1 — automação:** parser, VM bytecode, bindings shell/FS/config. Roda como `.capyscript` interpretado.
- **Etapa 2 — apps utilitários:** módulos, FFI controlada, integração com libcapy-ui.
- **Etapa 3 — primeira-classe:** stdlib, async, LSP, formatter.

#### Critérios de aceite (Etapa 1)

- [ ] Script `.capyscript` executa em ring 3 via `capylang` interpreter.
- [ ] Bindings: `fs.read`, `fs.write`, `shell.exec`, `config.get/set`.
- [ ] Smoke `smoke-x64-capylang-hello`.

---

## 5. Roadmap macro pós-F10 (visão sem datas)

| Fase futura | Tema | Pré-requisitos |
|---|---|---|
| F11 | Secure Boot + measured boot | F2 (assinatura), F8 (chain) |
| F12 | SMP / multicore | scheduler atual está single-core |
| F13 | USB completo (mass storage, áudio, câmera) | xHCI base existe |
| F14 | GPU acelerada (drivers reais para Intel/AMD/NVIDIA) | F12 desejável |
| F15 | Compatibilidade Linux subset (syscall translation layer) | F4 + F8 |
| F16 | Office suite, multimedia stack, IDE própria | F8 + F10 + ecossistema |
| F17 | IA local (LLM small) | F12 + F14 |

---

## 6. Validação — comandos canônicos

| Cenário | Comando |
|---|---|
| Validação mínima após qualquer mudança | `make test && make layout-audit && make all64` |
| Pré-commit completo | `make release-check` |
| Pré-release | `make release-check && make smoke-x64-vmware-dhcp` (após F2) |
| Smokes M5 | `make smoke-x64-fork-cow smoke-x64-exec smoke-x64-fork-wait smoke-x64-pipe smoke-x64-fork-crash smoke-x64-capysh` |
| Smokes M4 preemptivo | `make smoke-x64-preemptive-all` |
| Smokes browser (após F3) | `make smoke-x64-browser-isolation smoke-x64-browser-watchdog` |
| Smokes GUI (após F6) | `make smoke-x64-gui-session smoke-x64-mouse-events` |

---

## 7. Layout / clean code (regras vivas)

Mantidas do M3 fechado. Não voltam a ser fase porque já estão em CI:

- ≤ 900 linhas por arquivo C/runtime/teste (exceções documentadas: `tls_trust_anchors.c` por dados estáticos).
- Headers privados em `internal/<modulo>_internal.h`.
- `make layout-audit --strict` em CI; bloqueia merge.
- Includes cruzados de `internal/` entre módulos distintos são erro.
- Funções utilitárias canônicas: `kstring.h` (`kstrlen`, `kstreq`, `kmemzero`, etc.). Duplicar é violação.

---

## 8. Critérios globais de "SO sólido"

CapyOS é considerado **estruturalmente sólido** quando:

### Plataforma
- [ ] Boot oficial previsível (M1 ✅, F2 fecha smoke).
- [ ] Update sem reinstalação (F5).
- [ ] Rollback automático (M7 ✅).
- [ ] Logs persistentes (M6.2 ✅).

### Kernel
- [x] Multitarefa real (M4 ✅).
- [x] Isolamento de processos (M4 + M5 ✅).
- [x] Syscalls definidas (M5 + ABI documentar em F8).
- [x] Panic/fault observáveis (M4 fase 4 ✅).

### Storage
- [x] Replay/journal (M7 ✅).
- [x] Fsck (M7 ✅).
- [x] Integridade autenticada (M6.5 ✅).
- [x] Política de auth (M6.1 ✅).

### Rede
- [ ] Sockets userland (F4).
- [ ] TLS (F4).
- [ ] Updates seguros (F5).
- [ ] Firewall mínimo (futuro F11+).

### Desktop
- [ ] Sessão gráfica com mouse (F6).
- [ ] Apps básicos (F7).
- [ ] UI de update (F5 + F8).

### Ecossistema
- [ ] SDK (F8).
- [ ] Formato de app (F8).
- [ ] Linguagem própria (F10).

**Score atual:** 11/22 ✅ (50% dos critérios estruturais).
**Score após F1–F8:** 21/22 ✅ (95%).

---

## 9. Sequência de releases recomendada

| Versão | Conteúdo | Fase fechada |
|---|---|---|
| `0.8.0-alpha.6` | M5 userland + W1/W2/W3 + browser ring-3 | **F1** |
| `0.8.0-alpha.7` | Patch CapyUI/F6: prompts secretos, menus resilientes e isolamento modal de overlays | **F6 parcial** |
| `0.8.0-alpha.8` | Patch Settings/CapyUI: criacao de usuario via UI estabilizada | **F6 parcial** |
| `0.8.0-alpha.9` | Patch Auth/F6: ciclo de vida de usuarios UI/CLI consolidado | **F6 parcial** |
| `0.8.0-alpha.10` | Patch Auth/F6: bootstrap/recovery admin endurecido | **F6 parcial** |
| `0.8.0-alpha.11` | Patch Update/F5: manifest gate anti-downgrade + payload sha256 obrigatorio | **F5 parcial** |
| `0.8.0-alpha.12` | Patch Update/F5: gate Ed25519 local para manifestos de update | **F5 parcial** |
| `0.8.0-alpha.13` | Patch Release/F2: tooling Ed25519 para checksums de release | **F2 parcial** |
| `0.8.0-alpha.14` | Patch Update/F5: fetch remoto de manifesto assinado | **F5 parcial** |
| `0.8.0-alpha.15` | Patch Update/F5: apply verificado por payload sha256 | **F5 parcial** |
| `0.8.0-alpha.16` | Patch Update/F5: health confirm + rollback assistido | **F5 parcial** |
| `0.8.0-alpha.17` | Patch Update/F5.4: política remota develop/stable | **F5 parcial** |
| `0.8.0-alpha.18` | Patch Update/F5: payload_url obrigatorio e auditavel | **F5 parcial** |
| `0.8.0-alpha.19` | Patch Update/F5: download de payload com SHA-256 real | **F5 parcial** |
| `0.8.0-alpha.20` | Patch Update/F5: apply cache-first por payload_cache_sha256 | **F5 parcial** |
| `0.8.0-alpha.21` | Patch Update/F5: prepare seguro fetch/download/stage/arm | **F5 parcial** |
| `0.8.0-alpha.22` | Patch Update/F5: prepare dry-run sem staging/arm/apply | **F5 parcial** |
| `0.8.0-alpha.23` | Patch Update/F5: prepare explain de gates locais | **F5 parcial** |
| `0.8.0-alpha.24` | Patch Network/F4: URL parser anti-CRLF/controles | **F4 parcial** |
| `0.8.0-alpha.25` | Patch Network/F4: HTTP builder anti-CRLF/controles | **F4 parcial** |
| `0.8.0-alpha.26` | Patch Network/F4: header parser anti-controles | **F4 parcial** |
| `0.8.0-alpha.27` | Patch Network/F4: Content-Length estrito | **F4 parcial** |
| `0.8.0-alpha.28` | Patch Network/F4: Content-Length duplicado consistente | **F4 parcial** |
| `0.8.0-alpha.29` | Patch Network/F4: Transfer-Encoding fail-closed | **F4 parcial** |
| `0.8.0-alpha.30` | Patch Network/F4: body_received separado de body_len | **F4 parcial** |
| `0.8.0-alpha.31` | Patch Network/F4: EOF curto com Content-Length vira erro | **F4 parcial** |
| `0.8.0-alpha.32` | Patch Network/F4: Content-Length zero conhecido | **F4 parcial** |
| `0.8.0-alpha.33` | Patch Network/F4: headers dobrados/obs-fold rejeitados | **F4 parcial** |
| `0.8.0-alpha.34` | Patch Network/F4: Content-Length raw completo | **F4 parcial** |
| `0.8.0-alpha.35` | Patch Network/F4: validação de headers pós-cap | **F4 parcial** |
| `0.8.0-alpha.36` | Patch Network/F4: status no-body no HTTP client | **F4 parcial** |
| `0.8.0-alpha.37` | Patch Network/F4: Content-Encoding fail-closed | **F4 parcial** |
| `0.8.0-alpha.38` | Patch Network/F4: status 1xx fail-closed | **F4 parcial** |
| `0.8.0-alpha.39` | Patch Network/F4: status-line HTTP estrita | **F4 parcial** |
| `0.8.0-alpha.40` | Patch Network/F4: head HTTP LF-only no GET | **F4 parcial** |
| `0.8.0-alpha.41` | Patch Network/F4: header sem separador fail-closed | **F4 parcial** |
| `0.8.0-alpha.42` | Patch Network/F4: header block truncation fail-closed | **F4 parcial** |
| `0.8.0-alpha.43` | Patch Network/F4: URL fragment stripping no HTTP target | **F4 parcial** |
| `0.8.0-alpha.44` | Patch Network/F4: host authority hardening no HTTP target | **F4 parcial** |
| `0.8.0-alpha.45` | Patch Network/F4: request builder fragment fail-closed | **F4 parcial** |
| `0.8.0-alpha.46` | Patch Network/F4: DNS label boundary hardening | **F4 parcial** |
| `0.8.0-alpha.47` | Patch Network/F4: request builder port-zero fail-closed | **F4 parcial** |
| `0.8.0-alpha.48` | Patch F2: harness VMware+E1000 DHCP versionado | **F2 parcial** |
| `0.8.0-alpha.49` | Patch F2: self-test negativo Ed25519 | **F2 parcial** |
| `0.8.0-alpha.50` | Patch F2: pinagem SHA-256 da chave pública Ed25519 | **F2 parcial** |
| `0.8.0-alpha.51` | Patch F2: preflight CI de release | **F2 parcial** |
| `0.8.0-alpha.52` | Patch F2: helper de fingerprint da chave pública | **F2 parcial** |
| `0.8.0-alpha.53` | Patch F2: manifesto público da chave de release | **F2 parcial** |
| `0.8.0-alpha.54` | Patch F2: preflight valida manifesto público da chave | **F2 parcial** |
| `0.8.0-alpha.55` | Patch F2: conferência do pacote público de release | **F2 parcial** |
| `0.8.0-alpha.56` | Patch F2: manifesto público de publicação | **F2 parcial** |
| `0.8.0-alpha.57` | Patch F2: verificador do manifesto público de publicação | **F2 parcial** |
| `0.8.0-alpha.58` | Patch F2: gate público agregado de publicação | **F2 parcial** |
| `0.8.0-alpha.59` | Patch F2: contrato público de CI para publicação | **F2 parcial** |
| `0.8.0-alpha.60` | Patch F2: gate público de CI/tag de release | **F2 parcial** |
| `0.8.0-alpha.61` | Patch F2: contrato oficial de provisionamento CI/release | **F2 parcial** |
| `0.8.0-alpha.62` | Patch F2: manifesto oficial de handoff CI/release | **F2 parcial** |
| `0.8.0-alpha.63` | Patch F4: hardening de request-target HTTP em libcapy-net | **F4 parcial** |
| `0.8.0-alpha.64` | Patch F4: API userland fail-closed de libcapy-tls | **F4 parcial** |
| `0.8.0-alpha.65` | Patch F4: adaptador HTTPS fail-closed entre libcapy-net e libcapy-tls | **F4 parcial** |
| `0.8.0-alpha.66` | Patch F4: hardening de hostname em libcapy-tls userland | **F4 parcial** |
| `0.8.0-alpha.67` | Patch F4: hardening de hostname no TLS kernel-side | **F4 parcial** |
| `0.8.0-alpha.68` | Patch F4: política TLS hostname compartilhada kernel/userland | **F4 parcial** |
| `0.8.0-alpha.69` | Patch F4: libcapy-tls exige peer verification | **F4 parcial** |
| `0.8.0-alpha.70` | Patch F4: libcapy-tls valida janela de timeout | **F4 parcial** |
| `0.8.0-alpha.71` | Patch F4: libcapy-tls normaliza configuração efetiva | **F4 parcial** |
| `0.8.0-alpha.72` | Patch F4: libcapy-tls prepara contexto interno | **F4 parcial** |
| `0.8.0-alpha.73` | Patch F4: libcapy-tls limpa contexto interno | **F4 parcial** |
| `0.8.0-alpha.74` | Patch F4: libcapy-tls gerencia slot de contexto | **F4 parcial** |
| `0.8.0-alpha.75` | Patch F4: libcapy-tls conecta connect ao slot | **F4 parcial** |
| `0.8.0-alpha.76` | Patch F4: libcapy-tls adiciona stub de backend | **F4 parcial** |
| `0.8.0-alpha.77` | Patch F4: libcapy-tls prepara estado backend | **F4 parcial** |
| `0.8.0-alpha.78` | Patch F4: libcapy-tls prepara metadados de trust anchors | **F4 parcial** |
| `0.8.0-alpha.79` | Patch F4: libcapy-tls adiciona fonte userland de trust anchors | **F4 parcial** |
| `0.8.0-alpha.80` | Patch F4: libcapy-tls cataloga trust anchors userland | **F4 parcial** |
| `0.8.0-alpha.81` | Patch F4: libcapy-tls fixa invariantes do catálogo TLS | **F4 parcial** |
| `0.8.0-alpha.82` | Patch F4: libcapy-tls materializa slots metadata-only | **F4 parcial** |
| `0.8.0-alpha.83` | Patch F4: libcapy-tls descreve trust anchors metadata-only | **F4 parcial** |
| `0.8.0-alpha.84` | Patch F4: libcapy-tls manifesta trust store metadata-only | **F4 parcial** |
| `0.8.0-alpha.85` | Patch F4: libcapy-tls resume tamanhos metadata-only do trust store | **F4 parcial** |
| `0.8.0-alpha.86` | Patch F2: gate de prontidão oficial do smoke VMware | **F2 parcial** |
| `0.8.0-alpha.87` | Patch F2: evidencia publica pos-smoke VMware | **F2 parcial** |
| `0.8.0-alpha.88` | Patch F2: aceitacao publica da evidencia smoke VMware | **F2 parcial** |
| `0.8.0-alpha.89` | Patch F2: promocao publica pos-smoke VMware | **F2 parcial** |
| `0.8.0-alpha.90` | Patch F4: libcapy-tls materializa bundle metadata-only | **F4 parcial** |
| `0.8.0-alpha.91` | Patch F4: backend plan BearSSL userland fail-closed | **F4 parcial** |
| `0.8.0-alpha.92` | Patch F4: estado BearSSL reservado metadata-only | **F4 parcial** |
| `0.8.0-alpha.93` | Patch F4: adaptador BearSSL metadata-only | **F4 parcial** |
| `0.8.0-alpha.94` | Execução oficial CI/smoke VMware+E1000 | **F2** |
| `0.8.0-beta.1` | Browser isolado + watchdog | **F3** |
| `0.8.0-beta.2` | Sockets userland + TLS | **F4** |
| `0.9.0-alpha.1` | Update via GitHub | **F5** |
| `0.9.0-beta.1` | GUI sessão completa | **F6** |
| `0.9.0` | Apps básicos | **F7** |
| `0.10.0` | Package manager + SDK | **F8** |
| `0.11.0+` | JS engine | **F9** |
| `0.12.0+` | CapyLang Etapa 1 | **F10 (Etapa 1)** |
| `1.0.0` | CapyLang completa + Secure Boot + SMP | F10 + F11 + F12 |

---

## 10. Manutenção deste documento

**Atualize sempre que:**

- Uma fase F1–F10 muda de status.
- Um item promovido para ✅ ganha evidência (teste/smoke/release note).
- Uma decisão arquitetural de impacto (ABI, formato de pacote, etc.) é tomada.
- Um hotfix significativo é aplicado (registrar em "Itens fora do plano formal" + abrir entrada na fase relevante).

**Não crie outros planos em `active/`.** Se for inevitável (ex.: branch experimental longo), o plano novo:

1. Refere este como pai;
2. Vive em `experimental/` se for laboratório;
3. Vai para `historical/` ao fim, com o conteúdo migrado para uma fase deste plano.

A regra é simples: **um único plano linear, evidência para cada promoção, nada paralelo sem justificativa documentada.**

---

## Apêndice A — Mapeamento dos planos antigos

Os documentos abaixo foram **arquivados em `historical/`** em 2026-05-01 e estão consolidados aqui:

| Documento antigo | Conteúdo migrado para |
|---|---|
| `capyos-robustness-master-plan.md` | §2.1 (M0–M8 entregue) + §4 (F1–F10) |
| `m5-userland-progress.md` | §2.1 (M5-userland) + F1 |
| `post-m5-ux-followups.md` | §2.1 (W1–W3) |
| `system-master-plan.md` | §3 (princípios) + §4 (F4–F10) + §5 (roadmap pós) |
| `system-roadmap.md` | §4 (distribuído por fase) + §7 (clean code) |
| `system-execution-plan.md` | §4 (etapas A–G distribuídas em F1–F10) |
| `capyos-master-improvement-plan.md` | §2.1 (fases CONCLUIDO) + §4 (F6 GUI fix integration) |
| `browser-status-roadmap.md` | §2.1 (M8.1/8.3/8.4/8.5 + W3) + F3 (M8.2) + F9 (M8.6) |
| `source-organization-roadmap.md` | §7 (regras vivas) |
| `historical/m4-finalization-progress.md` | §2.1 (M4 100%) — permanece como referência técnica |

Este apêndice serve apenas para rastreabilidade. Não consultá-los para decisões — usar sempre este master plan.

---

## Apêndice B — Itens fora dos planos formais (hotfixes registrados)

| Item | Status | Onde foi consolidado |
|---|---|---|
| Boot UEFI/EBS robusto (Print fora da janela crítica + dbgcon markers) | ✅ Aplicado | M0/F2 (referência futura em release notes) |
| `com1.c/h` → `serial_com1.c/h` (workaround exFAT macOS) | ✅ Aplicado | §7 (clean code — exceção documentada de naming) |
| Remoção do limite artificial de 1 GiB no framebuffer | ✅ Aplicado | M0 (já consolidado em master plan antigo) |
| Print de `[UEFI] GOP fb base=...` no loader | ✅ Aplicado | Diagnóstico permanente (M0) |
| W1 callback de clear context-aware | ✅ 2026-04-30 | §2.1 |
| W2 task_manager_tick + Kill button | ✅ 2026-04-30 | §2.1 |
| W3 parser yield + timeout 30s | ✅ 2026-04-30 | §2.1 |
