# CapyOS — CapyUI Shell Polish v1

## Objetivo

A Etapa 1 do plano sequencial melhora layout, UI e UX de forma incremental,
lembrando Ubuntu e Windows 7 sem exigir GPU 3D. O foco é reforçar
familiaridade visual, consistência de tema e usabilidade básica mantendo a
identidade do CapyOS.

## Incremento `0.8.0-alpha.94+20260510`

- `classic-modern` adiciona uma paleta aubergine/laranja/azul inspirada em
  Ubuntu e Windows 7.
- `config-theme classic-modern` aplica e salva o tema; `classic` e `ubuntu7`
  são aliases aceitos e normalizados.
- Settings mostra `classic-modern` na aba Display.
- O fallback framebuffer reconhece a mesma paleta para splash/console.
- A taskbar passa a destacar o botão `Capy`, itens focados e relógio com pill
  visual.
- Notificações passam a usar a paleta ativa via `compositor_theme()`.

## Incremento `0.8.0-alpha.95+20260510`

- O launcher `Capy` passa a exibir busca textual no topo do menu.
- Entradas são agrupadas visualmente em `Apps`, `System` e `Session`.
- O menu aberto aceita digitação, Backspace/Delete, setas cima/baixo e
  Enter para ativar a seleção atual.
- A taskbar exibe um tray inicial textual com snapshot local de rede e
  usuário, sem serviços novos.

## Incremento `0.8.0-alpha.96+20260510`

- Terminal, Arquivos, Editor e Calculadora viram apps fixados no launcher.
- A taskbar mantém até três apps recentes em memória de sessão.
- Recentes aparecem acima dos grupos fixados/sistema e participam da busca.
- A ativação de recentes reutiliza as mesmas ações registradas do launcher.

## Incremento `0.8.0-alpha.97+20260510`

- A decoração de janelas passa a diferenciar melhor foco ativo/inativo.
- Títulos longos são truncados antes dos botões de janela.
- Controles de minimizar, maximizar/restaurar e fechar recebem polish 2D.
- A mudança é puramente render-time e preserva os hit-tests existentes.

## Incremento `0.8.0-alpha.98+20260510`

- O desktop passa a desenhar wallpaper 2D derivado da paleta ativa.
- O grid de ícones usa células maiores, margens melhores e seleção refinada.
- A listagem visual fica determinística: diretórios antes de arquivos.
- Hit-test de ícones respeita margem direita, margem inferior e taskbar.

## Incremento `0.8.0-alpha.99+20260510`

- O tray da taskbar passa a exibir `NET/SND/SYS/USR`.
- `SND:n/a` preserva compatibilidade enquanto não há backend de áudio.
- `SYS` agrega estados do `service_manager` sem polling ou serviços novos.
- O tray continua atualizado junto ao relógio para baixo overhead.

## Fechamento `0.8.0-alpha.100+20260510`

- A Etapa 1 fecha os entregáveis do CapyUI Shell Polish v1.
- A tecla Super/Windows alterna o launcher Capy sem afetar input ASCII.
- A Etapa 2 fica desbloqueada como próxima etapa, mas não é iniciada.

## Limites

- A entrega não implementa GPU 3D, Wayland, Mesa, Vulkan ou drivers novos.
- A entrega não inicia Etapa 2, sessão gráfica operacional ou drivers novos.
- A Etapa 1 está fechada; a Etapa 2 permanece sem implementação neste patch.
- A entrega não altera o modelo de segurança nem a ABI pública.

## Próxima etapa permitida

Etapa 2 — Sessão gráfica operacional, ainda sem implementação iniciada.
