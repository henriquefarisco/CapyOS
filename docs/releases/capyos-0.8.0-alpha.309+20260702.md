# CapyOS 0.8.0-alpha.309+20260702

**Data:** 2026-07-02
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.309+20260702`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** correcao critica pos-reteste real em VMware (coordenado com CapyUI 2.23.1)

## Resumo executivo

alpha.309+20260702: corrige o travamento de campo do VMware relatado no reteste do alpha.308 -- abrir o navegador grafico numa sessao de desktop viva deixava a tela inteira azul (wallpaper 0x002244) sem diagnostico. Root cause (analise dos logs do host VMware + codigo; a VM nao tinha porta serial, entao nenhum log guest estava disponivel): o build default roda o scheduler PREEMPTIVO (CAPYOS_PREEMPTIVE_SCHEDULER -> policy PRIORITY, APIC 100Hz chamando scheduler_tick de dentro do IRQ). Com o capygfx spawnado na fila, o tick podia (1) preemptar a task do desktop NO MEIO de desktop_run_frame/compositor_render por exaustao de quantum -- o capygfx entao mutava a tabela de janelas/surfaces/damage por syscall e, ao retomar, a varredura do compositor lia estado inconsistente -- e (2) colher zumbis EM CONTEXTO DE IRQ (task_kill -> process_destroy -> observer de teardown do gfx destruindo janela do compositor) no meio do frame. Resultado: frame nunca completa apos pintar o wallpaper -> tela solida azul. Mesma familia do alpha.306 (estado compartilhado sem lock encontrando um segundo contexto de execucao), agora no lado compositor; nenhum smoke pegava porque nenhum combina desktop real + capygfx + tick preemptivo -- exatamente a lacuna registrada desde o alpha.304 (integracao com o loop real desktop_runtime_start nao testada). CORRECAO em duas partes: (a) scheduler ganha um PREEMPT GUARD contado (scheduler_preempt_disable/enable/disabled): com o guard erguido, scheduler_tick mantem contabilidade e wake de sleepers mas ADIA reaping de zumbis e preempcao por quantum; trocas voluntarias (task_yield/task_sleep) seguem funcionando; (b) o loop do desktop (CapyUI 2.23.1, desktop_runtime_start) envolve cada frame no guard -- o frame vira unidade atomica de escalonamento e o task_yield/frame delay no fim do loop volta a ser o UNICO ponto onde o capygfx (e o reaper) rodam, sempre entre frames com o compositor quiescente. IRQs continuam habilitados (input nao perde eventos; so a troca de contexto e adiada) -- e o design cooperativo da Etapa 4 aplicado ao caminho preemptivo. Teste host novo test_preempt_guard_defers_reap_and_switch em test_context_switch.c (guard adia switch+reap, mantem wake de sleeper, enable clampa em zero, tick seguinte retoma reap+preempcao). Nota operacional: configure uma porta SERIAL na VM VMware (playbook etapa-2, secao de console serial) -- sem ela o COM1 do kernel (klog/markers) vai para o nada e crashes ficam sem diagnostico, como neste reteste. Validado: make test verde (incl. o teste novo); make validate CapyUI verde; smoke-x64-qemu-capygfx-desktop-spawn verde pos-mudanca. Reteste VMware do operador necessario para confirmar o fim da tela azul.

## Mudancas

- `src/kernel/scheduler.c` + `include/kernel/scheduler.h`: novo preempt guard
  contado (`scheduler_preempt_disable`/`scheduler_preempt_enable`/
  `scheduler_preempt_disabled`). Com o guard erguido, `scheduler_tick` mantem
  contabilidade e wake de sleepers mas ADIA o reaping de zumbis em contexto de
  IRQ e a preempcao por exaustao de quantum; trocas voluntarias
  (`task_yield`/`task_sleep`) seguem normais.
- **CapyUI 2.23.1** (`src/desktop/desktop_runtime.c`): o loop do desktop
  envolve cada frame no guard — o frame vira unidade atomica de escalonamento
  e `task_yield`/frame delay volta a ser o unico ponto onde o capygfx (e o
  reaper) rodam, sempre entre frames com o compositor quiescente. IRQs
  continuam habilitados (input nao perde eventos).
- `tests/kernel/test_context_switch.c`: novo
  `test_preempt_guard_defers_reap_and_switch` (guard adia switch+reap, mantem
  wake, clampa em zero, tick seguinte retoma reap+preempcao).
- Docs: matriz (CapyUI 2.23.1), plano mestre, STATUS.

## Validacao

- `make test` -- verde (incl. o teste novo do preempt guard).
- `make validate` (CapyUI 2.23.1) -- verde.
- `make version-audit` -- verde.
- `make smoke-x64-qemu-capygfx-desktop-spawn` -- verde pos-mudanca (mecanismo
  de spawn + marker preservados).
- **Reteste VMware do operador NECESSARIO** para confirmar o fim da tela azul
  (menu Navegador / open-browser-graphical numa sessao logada).

## Nota operacional (diagnostico)

A VM VMware usada no reteste **nao tem porta serial configurada** — todo o
COM1 do kernel (klog, markers, prints do capygfx) foi descartado, por isso
"sem logs aparentes". Configure a serial para arquivo (ver
`docs/operations/etapa-2-external-validation-playbook.md`, secao console
serial) antes do proximo teste; qualquer crash futuro fica diagnosticavel.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.308+20260702` | `0.8.0-alpha.309+20260702` | Preempt guard no scheduler; fix da corrida compositor vs capygfx/reaper (tela azul VMware). Aditivo, sem mudanca de ABI cross-repo. |
| **CapyUI** | `2.23.0` | `2.23.1` | Frame do desktop atomico via preempt guard (exige CapyOS >= alpha.309). |

Os demais 5 repos irmaos permanecem inalterados.

_Build: `0.8.0-alpha.309+20260702`_
