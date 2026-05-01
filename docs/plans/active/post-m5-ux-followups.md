# Post-M5 — Follow-ups de UX/Estabilidade

**Branch sugerida:** `feature/post-m5-ux`
**Iniciado:** 2026-04-30
**Pré-requisito:** M5 userland 100% (ver `active/m5-userland-progress.md`).

> ⚠️ Estes itens **não são** parte de M5. Foram reportados pelo usuário
> durante a validação manual da branch `feature/m5-development` em
> 2026-04-30 e estão **mapeados aqui** para execução pós-merge de M5,
> em ordem crescente de escopo.

## Contexto

Durante o uso manual da build pós-M5, três regressões/lacunas
visíveis ao usuário foram observadas:

1. **Browser congela** ao carregar links e trava permanentemente em
   sites pesados.
2. **Task manager não lista** todos os apps/serviços ativos —
   alguns processos lançados após o boot não aparecem; serviços
   nunca apareceram.
3. **CLI**: comandos do tipo `clear`/`cls`/`mess` não limpam a tela.

Nenhum dos três é fix de M5. Cada um é tratado como um workstream
independente com seu próprio gate.

---

## W1 — TTY polish (clear + missing builtins)

**Escopo:** pequeno, ~1 sessão. Pode ser entregue antes de W2/W3
e antes mesmo do bump de release pós-M5.

**Sintoma:** ao digitar `clear` (ou variantes) na shell, a tela
não é limpa; o comando cai no caminho de "comando desconhecido"
silencioso.

**Causa-raiz:** o capyshell **kernel-side** (em `src/drivers/console/`
+ comandos registrados via `tty.c`) não expõe um builtin `clear`. O
`capysh` ring 3 entregue em M5 também não tem `clear` nos builtins
(`help`, `echo`, `pid`, `ppid`, `exectarget`, `exit`).

**Tarefas:**

- **W1.1** — Adicionar `tty_clear()` (se ainda não existir helper) +
  builtin `clear` no capyshell kernel.
- **W1.2** — Adicionar builtin `clear` em `capysh` (ring 3) emitindo
  a sequência ANSI `\x1b[2J\x1b[H` em fd 1 — funciona no console
  framebuffer e em terminais emulados.
- **W1.3** — Auditar lista de comandos esperados pelo usuário e
  confirmar quais existem; reportar gaps em backlog separado.
- **W1.4** — Host tests: o capyshell kernel não tem ainda teste
  de unidade do parser, então este passo é "smoke manual". Para
  o `capysh` ring 3, expandir o smoke `smoke-x64-capysh` para
  enviar `clear<ENTER>` antes de `exit` e validar que a sequência
  ANSI aparece no debugcon.

**Saída:** `clear` funciona em ambos shells; smoke estendido verde.

---

## W2 — Task manager + service registry observability

**Escopo:** médio, ~2–3 sessões. Não bloqueante para usuários; é
qualidade de vida + telemetria.

**Sintoma:**

- Apps lançados depois do boot não aparecem no task manager (ele
  só lista o snapshot construído no init do desktop).
- Serviços (logger, networkd, update-agent) nunca apareceram no
  task manager.

**Causa-raiz:**

- O widget do task manager constrói uma lista estática durante a
  inicialização do desktop, não relê `process_iter` em runtime.
- `service_manager` não tem ponte para o GUI; ele expõe APIs C
  (`service_manager_iter` etc.) mas o widget nunca consumiu.
- M5 adicionou novos hooks de `process_iter` (para wait/exit) que
  poderiam ser usados, mas o task manager não foi religado.

**Tarefas:**

- **W2.1** — Auditar API atual de `process_iter` / `task_iter` /
  `service_manager_iter`; documentar contrato esperado pelo widget
  do task manager.
- **W2.2** — Implementar refresh periódico no widget (ex.: a cada
  500ms via timer do compositor) que reconstrói a lista chamando
  `process_iter_each` e `service_manager_iter_each`.
- **W2.3** — Diferenciação visual entre **processos** e **serviços**
  (badge, ícone ou seção separada).
- **W2.4** — Botão **kill** funcional usando `process_exit_pid` ou
  equivalente, com confirmação (já parcialmente existe; revisar).
- **W2.5** — Smoke manual: lançar 3 apps após boot, abrir task
  manager, validar que todos aparecem em até 1s.
- **W2.6** — Telemetria: contar quantas vezes o refresh roda e
  exportar via debugcon para diagnosticar travamentos do widget.

**Dependências:** nenhuma — toda a infra existe.

**Saída:** task manager mostra todos os processos + serviços em
tempo real; kill funciona.

---

## W3 — Browser/html_viewer responsiveness

**Escopo:** grande, candidato a sub-milestone próprio (M8.x ou
M9). Vários ciclos de trabalho.

**Sintoma:**

- Ao clicar em um link, todo o desktop congela enquanto a página
  carrega.
- Sites pesados (HTML grande, muitas imagens) deixam o sistema
  permanentemente travado — sem recovery.

**Causa-raiz (várias camadas):**

1. **I/O bloqueante:** o fetch HTTP/TCP do `html_viewer` roda no
   mesmo thread do compositor. Toda chamada de rede trava UI.
2. **Parser sem yield:** `html_parser.c` e `css_parser/parse.c`
   não usam `op_budget` nem cedem CPU em loops longos. Documentos
   grandes ocupam o thread por muitos segundos seguidos.
3. **Sem timeout duro:** não há watchdog que aborte uma fetch
   travada — daí "permanentemente travado".
4. **Single-process browser:** o navegador roda dentro do mesmo
   processo do desktop. Um crash de parser derruba tudo. Esse
   item está em M8.2 (isolamento por processo + watchdog) e
   bloqueado pelo próprio M5 (que agora está pronto).

**Tarefas (faseadas):**

- **W3.1** — `op_budget` integration no parser HTML/CSS. Cada N
  tokens/regras o parser cede via `task_yield`. Já existe
  `src/util/op_budget.c` com host tests; falta consumir.
- **W3.2** — Timeout configurável (default 30s) na fetch, com
  cancelamento limpo se estourar.
- **W3.3** — Mover fetch para um thread/task de I/O separada,
  produzindo bytes para o parser via fila. Usa primitivas de M5
  (pipe ring buffer) ou uma adaptação.
- **W3.4** — (M8.2 stretch) Mover `html_viewer` para processo
  userland separado usando `fork+exec` de M5. Comunicação com
  desktop via pipe.
- **W3.5** — Smoke: carregar página local de 1MB+ e validar que
  o desktop responde a clicks durante o load.

**Dependências:** M5 100% (especialmente Phase D pipes e Phase E
shell se W3.4 for executada). `op_budget` já está pronto.

**Saída:** carregamento não congela desktop; sites pesados podem
ser cancelados sem reboot; (idealmente) crash do parser não
derruba o sistema.

---

## Status

| Workstream | Escopo | Status | Prioridade | Bloqueia |
|---|---|---|---|---|
| W1 — TTY polish | pequeno (~1 sessão) | 🔴 Não iniciado | alta (UX óbvio, fácil) | — |
| W2 — Task manager | médio (~2–3 sessões) | 🔴 Não iniciado | média | — |
| W3 — Browser responsiveness | grande (multi-sessão) | 🔴 Não iniciado | alta (regressão visível) | M8.2 desbloqueia o stretch (W3.4) |

**Sugestão de ordem:** W1 → W2 → W3. W1 e W2 podem inclusive
entrar como hotfixes na release pós-M5 (`0.8.0-alpha.5+`).

---

## Cross-references

- M5 fechado: `active/m5-userland-progress.md`.
- Master plan (M8.2 isolamento de processo do browser):
  `active/capyos-robustness-master-plan.md`.
- Status global: `STATUS.md`.
