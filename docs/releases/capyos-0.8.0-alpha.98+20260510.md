# CapyOS 0.8.0-alpha.98+20260510

## Resumo executivo

Este patch continua somente a Etapa 1 — CapyUI Shell Polish v1. A entrega
refina o wallpaper padrão e o grid de ícones do desktop sem iniciar sessão
gráfica operacional, drivers, CapyLX, Wayland, Mesa/Vulkan ou TLS real.

## Principais entregas

- Wallpaper 2D padrão passa a usar bandas derivadas da paleta ativa.
- Desktop ganha trilho visual suave atrás da coluna de ícones.
- Ícones usam célula 88x86 com margens melhores e destaque de seleção refinado.
- Ícones ganham sombra, brilho superior e contraste preservado por tema.
- Listagem do desktop passa a ser determinística: diretórios antes de arquivos,
  com ordenação case-insensitive por nome.
- Hit-test passa a respeitar margem direita, margem inferior e área do taskbar.

## Segurança e compatibilidade

- Nenhuma persistência nova, driver, serviço ou dependência externa foi criada.
- A ordenação usa apenas armazenamento estático já existente e limite de 64 itens.
- O wallpaper é desenhado no callback 2D existente do desktop.

## Validação estática

- Revisão estática confirmou limites de tela em pintura e hit-test.
- Revisão estática confirmou que a ordenação não aloca memória dinamicamente.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
