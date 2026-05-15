# CapyOS 0.8.0-alpha.113+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega estabiliza
UX operacional do launcher e do File Manager: o Start Menu ganha viewport com
scroll para impedir flutuação/sobreposição quando recentes e apps excedem a área
útil, o File Manager recebe toolbar mais clara com ação de atualizar, e arquivos,
pastas e ícones do desktop podem ser movidos para pastas por drag-and-drop usando
`vfs_rename()`.

## Principais entregas

- `taskbar` agora mantém `menu_scroll_offset`, calcula altura visível limitada à
  área útil da tela e desenha barra de scroll quando a lista excede o viewport.
- O wheel sobre o Start Menu é consumido por `taskbar_handle_menu_scroll()` antes
  do scroll de janelas, preservando prioridade de overlay.
- O File Manager aumenta a toolbar, adiciona botão `Refresh/Atualizar` explícito
  e diferencia alvos de drop com realce visual.
- Arrastar um item do File Manager sobre uma pasta executa `vfs_rename(src, dst)`
  e recarrega a listagem com status preservado.
- Ícones do desktop também armam drag-and-drop e podem ser soltos sobre pastas do
  próprio desktop para mover o arquivo/pasta correspondente.

## Segurança e compatibilidade

- Moves são restritos a drop sobre diretórios existentes e nunca apagam o destino
  diretamente; conflitos são recusados pelo VFS.
- O Start Menu, context menu e inline prompt continuam com prioridade sobre
  janelas comuns.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.
- A abertura por segundo clique foi preservada como ação no mouse-up sem movimento
  real, usando limiar pequeno para evitar confundir jitter com drag.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `desktop_handle_mouse()` guarda DnD contra
  overlays e preserva Start Menu/context menu antes de janelas.
- Revisão estática confirmou que o File Manager e desktop icons usam
  `vfs_rename()` apenas quando o alvo de drop é diretório válido.
- Revisão estática confirmou que o Start Menu usa altura visível e hit-test
  alinhado ao viewport rolável.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
