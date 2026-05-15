# CapyOS 0.8.0-alpha.105+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega revisa o
contrato entre terminal gráfico e shell real sem iniciar CapyDisplay, drivers
gráficos novos, stack Wayland/Mesa/Vulkan ou Etapa 3.

## Principais entregas

- O terminal gráfico monta o prompt com `shell_build_prompt()` usando usuário,
  hostname, configurações e `cwd` da sessão real da shell.
- Comandos de navegação como `go` atualizam o prompt seguinte porque o terminal
  consulta `kernel_desktop_shell_session()` em vez de manter prompt fixo `$ `.
- A saída de comandos da shell é roteada por um par explícito de begin/end para
  `shell_set_output_callbacks()` e `shell_set_clear_callback()`.
- O fechamento do terminal limpa callbacks globais pendentes quando o terminal
  era o sink ativo.
- `bye` passa a encerrar a sessão gráfica pelo estado real da shell
  (`shell_context_should_logout()` / `shell_context_running()`), enquanto `exit`
  continua retornando ao CLI textual.
- `desktop_open_terminal_here()` passa a priorizar a sessão da shell do desktop
  antes de cair para `session_active()`.

## Segurança e compatibilidade

- O patch não altera ABI pública de userland nem tabela de comandos da shell.
- O shell context continua sendo o dono de `cwd`, logout e estado de execução.
- Callbacks globais de saída/clear são restaurados após comando conhecido,
  comando desconhecido e fechamento de terminal.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que os literais C usam escapes corretos e não há NUL
  injetado no `desktop.c`.
- Revisão estática confirmou que o prompt do terminal usa `shell_build_prompt()`
  com sessão real do desktop.
- Revisão estática confirmou que `bye` é propagado pelo estado real do
  `shell_context`.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
