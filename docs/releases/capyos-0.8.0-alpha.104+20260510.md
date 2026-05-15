# CapyOS 0.8.0-alpha.104+20260510

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega melhora a
UX operacional do desktop existente sem iniciar CapyDisplay, drivers gráficos
novos, stack Wayland/Mesa/Vulkan ou Etapa 3.

## Principais entregas

- O launcher do botão Capy permanece ancorado ao canto inferior esquerdo e passa
  a separar ações de sessão em botões fixos no rodapé.
- A lista de apps recentes vira um grupo recolhível no launcher, preservando a
  busca textual e a navegação por teclado.
- O tray da taskbar troca o texto `NET:*` por indicador compacto de rede baseado
  em `net_stack_status()`.
- O desktop deixa de desenhar o rail lateral de organização dos ícones.
- O File Manager passa a usar clique simples para selecionar e clique seguinte
  sobre o mesmo item para abrir.
- O File Manager ganha botões de voltar no histórico, subir pasta, criar arquivo,
  criar pasta e apagar item selecionado.
- O menu de contexto do File Manager inclui abrir terminal aqui, criar arquivo,
  criar pasta, atualizar, e ações de navegação quando aplicável.
- `desktop_open_terminal_here()` ajusta o diretório da sessão ativa antes de
  abrir/focar o terminal gráfico.

## Segurança e compatibilidade

- A indicação de bateria não foi simulada: não há backend ACPI/bateria confiável
  exposto no runtime atual, então o patch evita apresentar estado falso.
- O menu de contexto respeita `CONTEXT_MENU_MAX_ITEMS` nos caminhos sobre item e
  área vazia.
- A navegação de histórico do File Manager preserva apenas o caminho anterior e
  não altera VFS, permissões ou isolamento de sessão.
- Nenhum driver gráfico, stack Wayland/Mesa/Vulkan, CapyDisplay ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que o tray de rede usa `net_stack_status()` e não
  inventa estado de bateria.
- Revisão estática confirmou que o File Manager não excede o limite de oito itens
  no menu de contexto.
- Revisão estática confirmou que Etapas 3-15 continuam bloqueadas.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
