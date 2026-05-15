# CapyOS 0.8.0-alpha.96+20260510

## Resumo executivo

Este patch continua somente a Etapa 1 — CapyUI Shell Polish v1. A entrega
fecha o primeiro slice de apps fixados/recentes no launcher sem iniciar sessão
gráfica operacional, drivers, CapyLX, Wayland, Mesa/Vulkan ou TLS real.

## Principais entregas

- Entradas essenciais do launcher passam a ser fixadas: Terminal, Arquivos,
  Editor e Calculadora.
- Apps fixados são marcados como recentes quando acionados pelo launcher.
- O launcher mostra até três apps recentes acima dos grupos fixados/sistema.
- Busca textual também encontra apps recentes pelo grupo `Recent`.
- Ativação por mouse, Enter e navegação por setas funciona sobre recentes e
  entradas fixadas com o mesmo caminho seguro de ação.

## Segurança e compatibilidade

- Recentes ficam somente em memória da sessão da taskbar; nada é persistido.
- Nenhuma ação nova com privilégio foi adicionada.
- Todas as listas usam limites fixos e validam índices antes de chamar ações.

## Validação estática

- Revisão estática confirmou que recentes são deduplicados e limitados a três.
- Revisão estática confirmou que separadores não são ativáveis nem entram na
  busca de apps.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
