# CapyOS 0.8.0-alpha.108+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega exibe uma
prévia passiva do futuro loginwindow GUI durante a renderização da tela de login,
usando o view model fail-closed introduzido no alpha anterior, sem coletar senha,
sem substituir o login textual, sem iniciar CapyDisplay, drivers gráficos novos,
Wayland/Mesa/Vulkan ou Etapa 3.

## Principais entregas

- `login_render_window_preview()` monta uma prévia textual/passiva do loginwindow
  a partir de `login_window_contract_evaluate()` e
  `login_window_view_model_build()`.
- A prévia mostra título, estado, mensagem, motivo de bloqueio, estado do campo de
  senha, ação de recuperação e fallback textual.
- `login_render_screen()` chama a prévia após o banner e antes do aviso de
  manutenção, preservando o fluxo existente de login.
- O campo de senha da prévia informa quando o contrato está pronto, mas declara
  que a entrada GUI continua desabilitada neste alpha.
- O fallback textual permanece explicitamente autoritativo.

## Segurança e compatibilidade

- O renderer usa apenas `ops->print()` e não chama `ops->readline()`.
- O renderer não chama `system_login()`, não coleta senha e não mantém buffers de
  credenciais.
- A autenticação textual continua no mesmo ponto do fluxo existente.
- A mudança permanece interna ao runtime de login e não altera ABI pública de
  userland.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que o preview não chama `readline()` nem
  `system_login()`.
- Revisão estática confirmou que o preview usa somente `login_window_contract` e
  `login_window_view_model`.
- Revisão estática confirmou que o login textual permanece no mesmo fluxo de
  autenticação.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
