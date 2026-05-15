# CapyOS 0.8.0-alpha.107+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega prepara o
futuro loginwindow GUI com um view model determinístico derivado do contrato
fail-closed existente, sem coletar senha, sem substituir o login textual, sem
iniciar CapyDisplay, drivers gráficos novos, Wayland/Mesa/Vulkan ou Etapa 3.

## Principais entregas

- `struct login_window_view_model` foi adicionado ao contrato interno de login.
- `login_window_view_model_build()` transforma `login_window_contract` em estado
  apresentável para a futura UI.
- O estado `ready` habilita renderização e campo de senha apenas quando o contrato
  já declarou input, sessão, settings, callbacks de shell/auth/UI e runtime
  completo.
- Ausência de input tem precedência fail-closed e não expõe ação gráfica de
  recuperação.
- Modo manutenção gera estado renderizável separado, com aviso e ação de
  recuperação somente quando `recovery_available` estiver presente.
- Estados bloqueados continuam exigindo fallback textual.

## Segurança e compatibilidade

- O patch não coleta senha nem executa autenticação por GUI.
- O login textual continua sendo o único caminho operacional de autenticação.
- O view model usa somente literais/localização estática e não mantém credenciais
  ou buffers de senha.
- A mudança permanece interna ao runtime de login e não altera ABI pública de
  userland.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que o view model inicializa todos os campos antes de
  retornar.
- Revisão estática confirmou que `password_enabled` só fica ativo em contrato
  `ready`.
- Revisão estática confirmou que ausência de input bloqueia recuperação gráfica.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
