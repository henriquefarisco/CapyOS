# CapyOS 0.8.0-alpha.109+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega consolida
a observabilidade do dispatcher central de janelas com um snapshot de saúde que
combina estatísticas do dispatcher, ocupação da fila de eventos, drops e estado
de captura do mouse. O objetivo é preparar a migração fim-a-fim de teclado/mouse
sem trocar ainda o caminho autoritativo de input, sem drenar eventos espelhados e
sem iniciar CapyDisplay, drivers gráficos novos, Wayland/Mesa/Vulkan ou Etapa 3.

## Principais entregas

- `struct gui_window_dispatcher_health` agrega snapshot do dispatcher e da fila
  `gui_event`.
- `gui_window_dispatcher_health_snapshot()` reporta disponibilidade da fila,
  backlog acima de meia capacidade, drops e captura de mouse obsoleta.
- `desktop_session` passa a guardar o último snapshot de saúde e o número de
  amostras realizadas.
- `desktop_run_frame()` amostra a saúde do dispatcher uma vez por frame após a
  coleta de mouse, sem chamar `gui_window_dispatch()` e sem mudar callbacks de
  janela.

## Segurança e compatibilidade

- O patch não altera o caminho autoritativo atual de teclado/mouse.
- O patch não despacha nem drena a fila espelhada de eventos dentro do desktop.
- Não há ABI pública de userland nova; a mudança fica no contrato interno da GUI.
- O snapshot é somente leitura sobre a fila e sobre o estado do dispatcher.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `gui_window_dispatcher_health_snapshot()` não
  chama `gui_event_poll()` nem `gui_event_dispatch()`.
- Revisão estática confirmou que o desktop só amostra o snapshot e não executa o
  dispatcher central por frame.
- Revisão estática confirmou que os campos novos de `desktop_session` são zerados
  por `kmemzero()` em `desktop_init()`.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
