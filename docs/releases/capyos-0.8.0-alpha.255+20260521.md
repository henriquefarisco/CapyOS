# CapyOS 0.8.0-alpha.255+20260521

**Data:** 2026-05-21
**Canal:** alpha (experimental)
**Plataforma oficial:** VMware + UEFI + E1000
**Etapa:** 4 (em andamento, Fase A revertida)

## Resumo executivo

Release alpha `0.8.0-alpha.255+20260521` que reverte os artefatos
entregues em `alpha.254` após
uma descoberta crítica feita durante a preparação da Fase B da
Etapa 4. Antes de invocar o workflow `cross-repo-contract-sync`
para coordenar com o sister `CapyUI`, uma inspeção do sister repo
(disponível como sibling em `/Users/t808981/Desktop/PR/CapyUI/`)
revelou que a ABI `capy-ui-widget` **já está publicada em v2.7**
com display-list schema v7 — muito além do que a matriz cross-repo
do CapyOS indicava (v0.6 legado).

O scaffolding entregue em `alpha.254` foi um **contrato paralelo e
incompatível** com a ABI real do sister. Esta alpha remove os
artefatos do erro e documenta a pendência de sincronização
cross-repo que continua aberta.

## O que estava errado em alpha.254

| Aspecto | alpha.254 (errado) | Realidade no sister CapyUI |
|---|---|---|
| Owner da ABI `capy-ui-widget` | Implicitamente CapyOS | Explicitamente CapyUI (sister) |
| Schema version | `CAPY_UI_WIDGET_ABI_VERSION = 1u` | `CAPY_DISPLAY_LIST_SCHEMA_VERSION = 7u` |
| Magic header | `0x57494443` ('WIDC') inventado | Sem magic; struct nativo |
| Opcode count | 7 | 12 |
| Opcode names | FILL_RECT, STROKE_RECT, DRAW_TEXT, DRAW_GLYPH, DRAW_IMAGE, PUSH_CLIP, POP_CLIP | RECT, BORDER, TEXT, CLIP_PUSH, CLIP_POP, IMAGE_REF, FOCUS_RING, DIRTY_HINT, DPI_SCOPE, TRANSFORM_PUSH, TRANSFORM_POP, PLUGIN_OP |
| Wire format | Byte stream tagged por op | Array nativo de `struct capy_dl_cmd` |
| Header autoritativo | `include/gui/widget/widget_contract.h` (novo no CapyOS) | `CapyUI/src/widget/capy_display_list.h` (sister) |
| Versão sister | `0.7.3` (matriz CapyOS) | `2.7.0` (`CapyUI/VERSION`) |

## Raiz do erro

Em alpha.254 tratei `capy-ui-widget` como uma ABI **owned pelo
CapyOS**. Isso contradiz o contrato cross-repo
(`docs/reference/integration/capyui-widget-integration-contract.md`)
que deixa explícito:

> O projeto apartado [CapyUI] pode desenvolver: ... **display list independente de backend**.

CapyOS é o **consumer** do display list, não o owner. O caminho
correto é incluir o header do sister e consumir `struct
capy_display_list` direto via Makefile sibling detection (mesmo
padrão usado em `alpha.241` para `gui/desktop/`).

## Mudanças neste alpha

### Arquivos removidos

- `include/gui/widget/widget_contract.h`
- `include/gui/widget/widget_adapter.h`
- `src/gui/widget/widget_contract.c`
- `src/gui/widget/widget_adapter.c`
- `tests/gui/test_widget_contract.c`
- `docs/architecture/capy-ui-widget-v1-contract.md`
- `docs/releases/capyos-0.8.0-alpha.254+20260521.md`

Diretórios `include/gui/widget/` e `src/gui/widget/` ficaram vazios
após o cleanup e foram removidos. `tests/gui/` permanece (tem
outros tests anteriores).

### Wiring revertido

- `@/Users/t808981/Desktop/PR/CapyOS/Makefile`: removidos
  `widget_contract.o` + `widget_adapter.o` do bloco runtime objs;
  removidos test source files do `TEST_SRCS`.
- `@/Users/t808981/Desktop/PR/CapyOS/tests/test_runner.c`: removida
  declaração + chamada de `run_widget_contract_tests`.

### VERSION + docs

- `VERSION.yaml`: alpha.254 marcado como `[REVOGADO em alpha.255]`
  com narrativa completa; alpha.255 entrada nova com lição
  operacional para prevenir reincidência.
- `docs/plans/STATUS.md`, `docs/plans/active/capyos-master-plan.md`,
  `docs/reference/integration/compatibility-matrix.md`, README,
  `.windsurf/rules/00-project-authority.md`, `.windsurf/README.md`,
  `.windsurf/skills/capyos-{whatis,project-map}/SKILL.md`:
  versão bumped 254 → 255 e narrativa de Fase A reescrita.

## Pendência crítica documentada (não resolvida nesta sessão)

A matriz cross-repo no CapyOS continua **stale**:

| Sister repo | Pinado na matriz CapyOS | Estado real |
|---|---|---|
| `CapyUI` | `0.7.3` | **`2.7.0`** |
| `CapyUI` ABI declarada | `capy-ui-widget` v0.6 | **`capy-ui-widget` v2.7** (additive sobre 2.6; 1.x em LTS ≥12m) |
| Display-list schema | (não declarado) | **v7** (`CAPY_DISPLAY_LIST_SCHEMA_VERSION = 7u`) |
| CapyOS core pin no sister | `0.8.0-alpha.244+20260520` | (igual, sister também desatualizado) |

Sincronizar isso requer:

1. Workflow `cross-repo-contract-sync` completo:
   - Bump da pinagem de `CapyUI` na matriz §1.
   - Bump do CapyOS pin em `CapyUI/docs/compatibility.md`.
   - Atualização de `external-core-repositories.md`.
2. Novo audit em `docs/reference/integration/compatibility-audit-<date>.md`
   documentando o delta.
3. Recomendação de gates externos no sister (`make validate` +
   `make package` em CapyUI).

**Deferido para próxima sessão com direcionamento específico** do
operador, porque cross-repo sync é uma operação de várias horas
que merece atenção dedicada, não um "continue" automático.

## Compatibilidade preservada

Código de runtime entregue em alphas 245-253 (storage stack + USB
HID + dbg→klog migration) **permanece intacto** e funcional. O
roll-back foi cirúrgico:

- AHCI driver: intocado.
- NVMe driver: intocado.
- Block I/O classifier + retry: intocado.
- Storage smoke gate + USB HID smoke gate: intocados.
- klog migration em ahci.c/nvme.c: intocada.
- Adaptador in-tree `capy-ui-widget` v0.6 (`include/gui/widget.h` +
  `src/gui/widgets/widget.c`): intocado e continua sendo o caminho
  atual de produção desktop.

Etapa 3 continua fechada (build `alpha.253` mantém validação
externa). Etapa 4 continua em andamento mas com Fase A redefinida:
o sub-gate correto agora é **consumir** o sister header, não
inventar contrato paralelo.

## Lição operacional documentada

Para prevenir reincidência: **antes** de começar Fase A de
qualquer Etapa que abre cross-repo gate, ler o sister repo real
(`../<sister>/VERSION` + `../<sister>/docs/compatibility.md` +
headers ABI relevantes) **antes** de qualquer linha de código,
não depois.

Esta lição será refletida em uma atualização do workflow
`etapa-transition.md` quando o operador direcionar (não-bloqueante
imediato).

## Próximos passos

### Imediato

**Pausar para direcionamento explícito.** O próximo passo
operacional (`cross-repo-contract-sync` completo) é não-trivial:

1. Lê CapyUI/VERSION + CapyUI/docs/compatibility.md + CapyUI/src/widget/capy_display_list.h.
2. Bumpa CapyOS matrix §1 (CapyUI 0.7.3 → 2.7.0).
3. Bumpa ABI section §2 (capy-ui-widget v0.6 → v2.7).
4. Bumpa CapyUI/docs/compatibility.md (CapyOS pin 0.8.0-alpha.244 → 0.8.0-alpha.255).
5. Cria novo `compatibility-audit-2026-05-21-cross-repo-sync.md` (ou similar) documentando o delta.
6. Atualiza `external-core-repositories.md`.

### Médio prazo (Etapa 4 Fase A correta)

Depois do sync cross-repo:

1. Implementar adapter CapyOS-side em `src/gui/widget_adapter/` (ou similar) que **inclui** `CapyUI/src/widget/capy_display_list.h` via Makefile sibling detection.
2. Renderer real: caminha `struct capy_display_list` e despacha cada `capy_dl_op` para o compositor CapyOS.
3. Validador de schema version: aceita `dl->version >= 7` (ops mais novos podem ser ignorados como opacos per CapyUI policy).
4. Host tests com display lists sintéticas em formato nativo.

Conforme `@/Users/t808981/Desktop/PR/CapyOS/docs/operations/etapa-4-external-validation-playbook.md`.
