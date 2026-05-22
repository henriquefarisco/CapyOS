# Cross-repo compatibility audit — 2026-05-21

**Status:** snapshot técnico vigente após `alpha.253` e o fechamento formal da Etapa 3.
**Snapshot anterior:** [`compatibility-audit-2026-05-20.md`](compatibility-audit-2026-05-20.md) (estado em `alpha.244`).
**Matriz autoritativa:** [`compatibility-matrix.md`](compatibility-matrix.md).

**Escopo:** Delta entre `alpha.244` (audit de 2026-05-20) e `alpha.253` (fechamento da Etapa 3, abertura da Etapa 4). Validação estática realizada nesta máquina; nenhum comando foi executado.

**Pergunta dirigente:** após o fechamento da Etapa 3, qual é o estado da coordenação cross-repo entre o CapyOS core e os 6 repositórios apartados (`CapyAgent`, `CapyBrowser`, `CapyCodecs`, `CapyUI`, `CapyLang`, `CapyBenchmark`), e qual gate de Etapa abriu para a próxima fase?

**Resposta executiva:** **a Etapa 3 fechou sem mexer em sister repos (escopo puramente core: drivers + USB HID + storage)**; a Etapa 4 abre o cross-repo handshake oficial com `CapyUI` para promoção do contrato `capy-ui-widget` de v0.6 (widget core legado) para v1 (widget/display-list contract oficial). Demais sister repos continuam inalterados e em seus estados respectivos do audit anterior.

---

## 1. Mudança nesta auditoria

### 1.1 Etapa 3 fechada (alpha.253)

A Etapa 3 — Driver framework + entrada USB HID + storage estável — fechou formalmente em 2026-05-21 com a build `alpha.253` após validação externa do gate `make smoke-x64-vmware-storage-resilience` em VMware + UEFI + E1000.

**Sub-slices entregues nesta janela (alpha.245 → alpha.253):**

| Alpha | Sub-slice | Entrega |
|---|---|---|
| 245 | 3D | USB HID gate `smoke-x64-vmware-usb-hid-keyboard` aprovado externamente |
| 246 | 3E.1 | AHCI/NVMe command builders host-testable |
| 247 | 3E.2.A | Unified block-I/O error classifier (5 classes) |
| 248 | 3E.2.B | Recoverable retry + reset escalation (COMRESET + CLR) |
| 249 | 3E.3 | AHCI slot allocator infrastructure |
| 250 | 3E.4 | Storage smoke marker `[smoke] storage-stack ready` |
| 251 | 3E.5 | External validation gate scaffolding |
| 252 | audit fix | BUG #1 (smoke marker double-emission) + BUG #2 (NVMe CLR queue recreation) |
| 253 | 3E.4.B | `dbg_*` → `klog`/`klog_hex` em ahci.c e nvme.c + bug latente corrigido |

**Evidência externa:** marker `[smoke] storage-stack ready` observado no COM1 exatamente uma vez em VMware oficial; marker `[smoke] usb-hid-keyboard ready` observado anteriormente em `alpha.245`.

**Acceptance criteria fechados (`docs/plans/active/capyos-master-plan.md` §6):**

- [x] VM oficial sobe com storage/rede/vídeo previsíveis.
- [x] Teclado USB funcional fora do `EFI ConIn` em VMware + UEFI.
- [x] Falha de driver não derruba o kernel sem diagnóstico.
- [x] Driver framework documenta ownership, DMA e teardown.

### 1.2 Etapa 4 aberta

A Etapa 4 — CapyDisplay 2D + scheduler/multithread runtime — está agora em andamento. Esta é a primeira Etapa após o fechamento da Etapa 3 que **abre um gate cross-repo de sister repo**: o contrato `capy-ui-widget` com o sister repo `CapyUI`.

**Sub-gate inicial:**

1. **Core CapyOS:** scaffolding do contrato widget/display-list em `include/gui/widget/` e adapter em `src/gui/widget/` que recebe display lists do CapyUI sem acessar compositor diretamente.
2. **Sister CapyUI:** publicar `capy-ui-widget` v1 declarando o contrato de widget/display-list que o CapyOS adapter aceita.
3. **Handshake cross-repo:** bump da matriz quando ambos os lados estiverem prontos; promoção da ABI `capy-ui-widget` de v0.6 (legado) para v1 (oficial Etapa 4).

**Acceptance criteria pendentes (`docs/plans/active/capyos-master-plan.md` §7):**

- [ ] Compositor redesenha somente regiões danificadas quando possível.
- [ ] Cursor e texto não piscam sob resize/move de janela.
- [ ] Fallback framebuffer continua funcionando.
- [ ] Apps single-threaded existentes continuam funcionais como regressão.
- [ ] Thread de app crashando não derruba kernel nem desktop.
- [ ] Widget model desacoplado consegue renderizar display list por adaptador CapyOS sem acessar compositor diretamente.

### 1.3 Mudanças em sister repos desde alpha.244

**Nenhuma.** A Etapa 3 foi inteiramente core-only (drivers + USB HID + storage). Nenhum manifest, ABI, versão pinada ou contrato de adapter mudou em `CapyAgent`, `CapyBrowser`, `CapyCodecs`, `CapyUI`, `CapyLang` ou `CapyBenchmark`. Versões pinadas continuam:

| Sister repo | Versão pinada |
|---|---|
| `CapyAgent` | `0.0.4` (Ed25519 signer ainda pendente) |
| `CapyBrowser` | `0.0.4` (sem runtime ativo) |
| `CapyCodecs` | `0.0.4` (host-only) |
| `CapyUI` | `0.7.3` (desktop session + widget v0.6) |
| `CapyLang` | `0.1.3` (S1 lexer) |
| `CapyBenchmark` | `0.0.4` (sem runtime ativo) |

---

## 2. Estado do adapter (sem mudança desde alpha.244)

Fonte da verdade: `include/services/capypkg.h` + `docs/architecture/capypkg-adapter.md`.

Nenhum incremento de contrato runtime nesta janela. O adapter `capypkg` continua aceitando o pipeline de instalação remota validado em `alpha.244` (download HTTPS + staging CAPYFS + marker de ativação + reboot). O `kernel/module_gate.c` continua consultando `/var/capypkg/<canonical-name>/installed` para `org.capyos.ui.widget-core` e `org.capyos.ui.desktop-session` (CapyUI owner). O `signature_required` continua fail-closed enquanto o verifier do `CapyAgent` está NULL.

---

## 3. Auditoria estática core-only desta janela

### 3.1 Storage stack (alphas 246-253)

Arquivos novos:
- `include/drivers/storage/ahci_commands.h` + `src/drivers/storage/ahci_commands.c` — builders puros host-testable.
- `include/drivers/nvme/nvme_commands.h` + `src/drivers/nvme/nvme_commands.c` — builders puros host-testable.
- `include/drivers/storage/block_error.h` + `src/drivers/storage/block_error.c` — classifier unificado 5 classes.
- `include/drivers/storage/ahci_slot_allocator.h` + `src/drivers/storage/ahci_slot_allocator.c` — bitmap allocator puro 32 slots.
- `include/drivers/storage/storage_smoke.h` + `src/drivers/storage/storage_smoke.c` + `src/drivers/storage/storage_smoke_io.c` — smoke marker gate + emitter.
- `tests/stubs/stub_storage_smoke_io.c` — host stub.
- 6 test files novos em `tests/drivers/` e `tests/fs/`.

Arquivos modificados:
- `src/drivers/storage/ahci.c` — integra builders + classifier + slot allocator + smoke gate + COMRESET escalation + `dbg_*` → `klog` (alpha.253).
- `src/drivers/nvme/nvme.c` — integra builders + classifier + Controller Level Reset + smoke gate + `dbg_*` → `klog` (alpha.253).
- `src/fs/storage/block_device.c` — adicionou `block_device_read_ex`/`block_device_write_ex` com retry loop unificado.
- `include/fs/block.h` — estendeu `block_device_ops` com `read_block_ex`/`write_block_ex`/`reset` opcionais.

Bugs encontrados e corrigidos durante esta janela:
- **BUG #1 (alpha.252):** smoke marker double-emission em VMs dual-storage. Latch movido de per-driver para global em `storage_smoke.c::g_global_state`.
- **BUG #2 (alpha.252):** NVMe Controller Level Reset não reemitia Create I/O CQ/SQ. Corrigido em `nvme_controller_reset`.
- **BUG #3 (alpha.253, latente):** `nvme_controller_reset` chamava `dbg_label_hex32` que é static em ahci.c (undefined-reference no escopo de TU). Resolvido pela migração para `klog_hex`.

### 3.2 USB HID stack (alpha.245)

Sem mudanças nesta janela específica — Slice 3D fechou em `alpha.245` com `smoke-x64-vmware-usb-hid-keyboard` aprovado externamente. Documentação histórica em `docs/operations/etapa-3-external-validation-playbook.md`.

### 3.3 Documentação arquitetural nova

- `docs/architecture/smoke-marker-pattern.md` — pattern canônico para smoke markers no projeto (resultado direto da análise do BUG #1; previne reincidência).
- `docs/operations/etapa-3-slice-3e-validation-playbook.md` — runbook do gate de Slice 3E.5.

---

## 4. Pendências cross-repo conhecidas

Inalteradas desde o audit anterior. Reproduzidas aqui para conveniência:

| Pendência | Owner | Bloqueio |
|---|---|---|
| Ed25519 signer | `CapyAgent` | Etapa 9 — sem isso `signature_required=1` é fail-closed |
| `capy-browser-core` runtime ativo | `CapyBrowser` | Etapa 6/7 — sem runtime |
| `capy-codec-image` adapter GUI | `CapyOS` core + `CapyCodecs` | Etapa 6 |
| `capy-codec-audio` runtime | `CapyCodecs` | Etapa 10 |
| `capy-lang-host` ABI | `CapyLang` + `CapyOS` | Etapa 15 |
| `capy-benchmark-report` baseline | `CapyBenchmark` | Etapa 16 |

A novidade da Etapa 4 é o handshake `capy-ui-widget` v1 com `CapyUI`. Isso requer:

1. Quando o scaffolding do widget contract estiver pronto no core, bumpar a sister repo `CapyUI` para uma versão que publique `capy-ui-widget` v1.
2. Atualizar `docs/reference/integration/compatibility-matrix.md` §1 com a nova pinagem do `CapyUI`.
3. Atualizar `docs/reference/integration/external-core-repositories.md` com nota "starts active" para `capy-ui-widget` v1.
4. Atualizar `../CapyUI/.windsurf/` para refletir que o gate Etapa 4 está ativo (workflow `cross-repo-contract-sync`).

Esses passos NÃO foram executados nesta janela porque o scaffolding do contrato no core ainda não existe — eles são o sub-gate inicial da Etapa 4.

---

## 5. Gates externos recomendados (próximos)

Esta máquina não roda comandos. Os gates abaixo são recomendados ao operador externo:

1. **Baseline pós-fechamento Etapa 3** (build alpha.253):
   - `make check-toolchain && make layout-audit && make test`
   - `make all64 && make iso-uefi && make release-check`
2. **Validação Etapa 4** (planejada, ainda não no Makefile):
   - `make smoke-x64-vmware-compositor-damage-track` (novo, a criar)
   - `make smoke-x64-vmware-scheduler-fairness` (novo, a criar)
   - Conforme `docs/operations/etapa-4-external-validation-playbook.md`.
3. **Sister repo CapyUI** (quando o scaffolding fechar):
   - `cd ../CapyUI && make validate && make package`

---

## 6. Anti-drift sweep desta auditoria

Itens já atualizados nesta janela:

- [x] `VERSION.yaml` em `alpha.253`.
- [x] `docs/plans/STATUS.md` reflete Etapa 3 concluída / Etapa 4 em andamento.
- [x] `docs/plans/active/capyos-master-plan.md` §6 marca Etapa 3 concluída; §7 abre Etapa 4.
- [x] `docs/reference/integration/compatibility-matrix.md` §5 reflete Etapa 3 concluída + Etapa 4 atual.
- [x] `docs/architecture/etapa-3-slice-3e-plan.md` documenta audit fix.
- [x] `docs/architecture/smoke-marker-pattern.md` criado.
- [x] `docs/operations/etapa-3-slice-3e-validation-playbook.md` criado.

Itens ainda a atualizar (anti-drift pendente nesta sessão):

- [ ] `docs/operations/etapa-4-external-validation-playbook.md` (criação).
- [ ] `README.md` se mencionar a Etapa ativa.
- [ ] `.windsurf/README.md`.
- [ ] `.windsurf/skills/capyos-project-map/SKILL.md`.
- [ ] `.windsurf/rules/00-project-authority.md` para refletir Etapa 4 ativa.
- [ ] `docs/reference/integration/external-core-repositories.md` (nota "starts active" para `capy-ui-widget` quando o scaffolding começar).

Sister repo `../CapyUI/.windsurf/` **não** será tocado nesta janela. O gate só abre formalmente quando o scaffolding do widget contract estiver in-tree no core. Workflow `cross-repo-contract-sync` deve ser invocado na primeira alpha de scaffolding.

---

## 7. Local execution policy reafirmada

Esta auditoria é estática e de escritório. Nenhum comando foi executado nesta máquina. Os gates listados em §5 devem ser corridos em CI ou na máquina de validação externa.
