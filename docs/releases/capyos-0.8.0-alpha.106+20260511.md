# CapyOS 0.8.0-alpha.106+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega adiciona
um fallback operacional para retornar da sessão gráfica ao TTY textual usando
`CTRL+ALT+F1` nos backends nativos de scancode, sem iniciar CapyDisplay, drivers
gráficos novos, stack Wayland/Mesa/Vulkan ou Etapa 3.

## Principais entregas

- `KEY_TTY_FALLBACK` foi reservado como sentinel interno de tecla especial.
- `struct x64_input_runtime` agora acompanha `ctrl_active` e `alt_active` junto
  ao estado de `shift_active` já existente.
- O polling PS/2 reconhece `CTRL+ALT+F1` antes de converter a tecla para ASCII ou
  sequência VT100.
- O polling Hyper-V keyboard reconhece o mesmo chord quando o backend VMBus está
  ativo.
- `desktop_runtime_start()` trata `KEY_TTY_FALLBACK`, registra mensagem no TTY e
  chama `desktop_stop()` para sair da sessão gráfica pelo fluxo normal de
  shutdown do desktop.

## Segurança e compatibilidade

- EFI ConIn e COM1 não simulam modificadores; continuam como caminhos textuais
  seguros sem chord gráfico inventado.
- O fallback não encerra a sessão de usuário nem altera `shell_context`: apenas
  retorna ao TTY textual como rota de recuperação operacional.
- A saída passa pelo shutdown normal do desktop, preservando `desktop_shutdown()`,
  restauração de `session_active()` e limpeza do yield hook de rede.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `KEY_TTY_FALLBACK` não conflita com `KEY_SUPER`
  nem com `KEY_F1`-`KEY_F12`.
- Revisão estática confirmou que PS/2 e Hyper-V atualizam os estados de Ctrl/Alt
  em press/release antes de avaliar o chord.
- Revisão estática confirmou que o desktop trata o sentinel antes de encaminhar
  input para janelas focadas.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
