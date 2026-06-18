# Etapa 6 / Slice 6.6 — apps-basic-roundtrip orchestration (design proposal)

**Status:** proposta de design (não implementado). Requer acordo cross-repo com
`CapyUI` antes de cabear código de runtime.
**Escopo:** como validar, num smoke automatizado VMware, o critério de aceite da
Slice 6.6 — "cada app abre, executa função primária e fecha sem crash" + "falha
de um app não derruba o desktop".
**Autoridade upstream:** `docs/plans/active/capyos-master-plan.md` §9 +
`docs/architecture/etapa-6-desktop-apps-readiness.md` §4.

---

## 1. O problema

Os smokes existentes (`tls-handshake`, `capybrowse-text`) são binários ring-3
**single-shot**: fazem uma tarefa e `capy_exit(0)`; o latch kernel-side conta o
exit-0 e emite o marker no COM1. Esse modelo é determinístico e já validado.

Os apps básicos do desktop (calculator, file_manager, text_editor, settings,
task_manager) são **apps GUI interativos** do `CapyUI`: renderizam janelas no
desktop session e normalmente só "fecham" por input do usuário. Eles **não
saem 0** sozinhos. Logo o modelo single-shot não se aplica diretamente, e
"abre/usa/fecha" não mapeia trivialmente para um exit-code.

Precisamos de um modelo de **conclusão determinística** por app que: (a)
exercite a função primária; (b) feche limpo; (c) sinalize sucesso de forma que
um latch kernel-side possa contar; sem injeção de input sintético frágil.

## 2. O que já está pronto (não re-fazer)

- **Isolamento de crash (critério "falha de um app não derruba o desktop"):**
  já validado pelo gate `thread-crash-survives` (Etapa 4) — `process_exit` não
  derruba kernel/desktop; o scheduler continua. Não precisa de novo mecanismo.
- **Latch puro `apps_roundtrip_smoke`** (`include/kernel/apps_roundtrip_smoke.h`):
  conta **N saídas limpas** (`code 0`) → marker único `[smoke] apps-basic-roundtrip
  ready`; saída não-limpa (app crashado) não conta e não dispara. Host-testado.
  Falta só ser **alimentado** no build live e a **orquestração de lançamento**.
- **Tema (Etapa 1)** e **localização EN-fallback (6.5)**: entregues; cobertura
  de strings/tema dos apps é responsabilidade do `CapyUI`.

## 3. Opções de modelo de conclusão

### Opção A — Exercitadores headless por app (ring-3, exit-0)
Cada app ganha um **modo smoke** (flag/arg) em que, em vez do loop de eventos
GUI, executa sua função primária de forma headless e `capy_exit(0)`/`1`. Um
orquestrador de boot lança cada app em modo smoke; o latch conta os exit-0.
- **Prós:** determinístico; reusa exatamente o latch + o padrão process_exit;
  sem input sintético; cada app é um processo ring-3 separado (já é o caso via
  `process_create`).
- **Contras:** valida a **lógica** da função primária, não o caminho de render
  GUI (abrir janela/desenhar/fechar). O render é coberto à parte pelos smokes
  do compositor.
- **Fronteira:** `CapyUI` expõe o modo smoke por app (a função primária como
  execução headless + exit-code); `CapyOS` lança + conta.

### Opção B — Sequência dirigida por input sintético
O smoke sobe o desktop e injeta eventos de input (abrir app pelo launcher,
operar, fechar janela), validando o roundtrip GUI completo.
- **Prós:** valida o roundtrip GUI **completo** (abrir/render/usar/fechar) —
  mais próximo literal do critério.
- **Contras:** complexo e frágil (timing, determinismo da injeção); fortemente
  acoplado ao `CapyUI`; difícil de tornar determinístico em CI.
- **Fronteira:** mecanismo de injeção de input + desktop dirigível.

### Opção C — Modo self-test do desktop session
O desktop session (CapyUI), sob flag de smoke, ele mesmo lança cada app, deixa
rodar uma função primária determinística e o fecha, reportando conclusão por
app — **internamente**, sem HID sintético.
- **Prós:** valida o ciclo de vida (launch/render/close) sem input frágil;
  determinístico.
- **Contras:** exige um "modo self-test" no desktop session do `CapyUI`.
- **Fronteira:** `CapyUI` implementa o self-test; `CapyOS` provê latch + gate.

## 4. Recomendação

**Opção A como gate mínimo da Etapa 6, com porta aberta para C na Etapa 7.**

Racional:
- A Opção A reusa o latch já entregue + o padrão process_exit (mesma mecânica
  de `tls_smoke`/`capybrowse`), é determinística e respeita a fronteira: o
  `CapyUI` expõe a função primária por app como execução headless + exit-code; o
  `CapyOS` orquestra o lançamento e conta. Baixo risco, validável em VMware.
- A Opção A satisfaz o núcleo do critério ("executa função primária … sem
  crash") + o isolamento de crash já está coberto. O "abre/fecha" GUI literal
  (render da janela) é melhor validado pelos smokes de compositor/desktop já
  existentes; promover o roundtrip GUI completo (Opção C) cabe na maturação da
  Etapa 7 (browser gráfico + janelas), não bloqueando o fecho da Etapa 6.
- A Opção B é descartada como gate de aceite (fragilidade/determinismo); pode
  existir como teste exploratório manual.

## 5. Funções primárias por app (proposta determinística)

Cada uma deve ser pura/determinística e terminar em `exit(0)` no modo smoke:

| App | Função primária (modo smoke) | Sucesso |
|---|---|---|
| calculator | avaliar `2+2` | resultado `== 4` |
| file_manager | listar um diretório de teste embutido | contagem de entradas esperada |
| text_editor | criar + salvar + reler um arquivo de teste | conteúdo lido `==` escrito |
| settings | ler + escrever + reler uma chave de config | valor persistido `==` escrito |
| task_manager | enumerar a tabela de processos | conta o próprio PID na lista |

`N = APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS` = número de apps no conjunto acima
(5 nesta proposta); o orquestrador define via `-D`.

## 6. Fronteira CapyOS ↔ CapyUI

| Responsável | Item |
|---|---|
| **CapyUI** | Modo smoke por app (função primária headless + exit-code); cobertura de tema/localização das strings dos apps. |
| **CapyOS** | Orquestrador de boot que lança cada app em modo smoke (sob `CAPYOS_APPS_ROUNDTRIP_SMOKE`, pós-desktop-ready, como `kernel_boot_run_capybrowse`); hook em `process_exit` alimentando o latch; o latch + o marker COM1; o alvo `make smoke-x64-vmware-apps-basic-roundtrip`; isolamento de crash (já pronto). |
| **Contrato** | `docs/reference/integration/capyui-widget-integration-contract.md` registra o modo smoke por app como superfície versionada. |

## 7. Como o latch é alimentado (Opção A)

Mesma mecânica do `capybrowse_text_smoke`, mas count-to-N:

1. Boot sobe rede + desktop; sob `CAPYOS_APPS_ROUNDTRIP_SMOKE`, um orquestrador
   lança os N apps em modo smoke (sequencial ou em paralelo).
2. Cada app roda sua função primária e `capy_exit(0)` (sucesso) / `!=0` (falha).
3. `process_exit` (gated) chama `apps_roundtrip_smoke_try_latch_exit_global(code)`;
   ao atingir N saídas limpas, emite `[smoke] apps-basic-roundtrip ready` no COM1.
4. Uma falha (exit != 0) não conta → o gate dá timeout (fail), exatamente o
   comportamento desejado.

> Nota de precisão: o latch conta **qualquer** exit-0 do processo. No build de
> smoke o orquestrador deve garantir que apenas os apps do conjunto rodam +
> saem (ou tagear os apps), para o count refletir o conjunto. Detalhe a fechar
> na implementação.

## 8. Questões abertas (para acordo cross-repo antes de implementar)

1. O modo smoke por app vive no `CapyUI` (recomendado) ou num shim CapyOS-side?
   (Recomendado CapyUI, para não crescer o fallback in-tree — regra 40.)
2. Lançamento sequencial (latch conta 1→N) vs paralelo (race no scheduler)?
3. Como o orquestrador isola o count aos apps do conjunto (tag de processo vs
   "só os apps rodam no build de smoke")?
4. O critério "abre/fecha" GUI literal é exigido no gate da Etapa 6, ou o
   render fica para os smokes de compositor + Etapa 7? (Proposta: o segundo.)

## 9. Referências

- `docs/architecture/etapa-6-desktop-apps-readiness.md` — Slice 6.6 + critérios.
- `docs/operations/etapa-6-external-validation-playbook.md` §5.D — o gate.
- `include/kernel/apps_roundtrip_smoke.h` — o latch (entregue).
- `include/kernel/thread_crash_smoke.h` — isolamento de crash (entregue, Etapa 4).
- `docs/reference/integration/capyui-widget-integration-contract.md` — contrato CapyUI.
- `docs/architecture/smoke-marker-pattern.md` — pattern canônico dos markers.
- `.windsurf/rules/40-decoupled-development.md` — fronteira CapyOS↔CapyUI.
