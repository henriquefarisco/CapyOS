# Arquivo: exploracao do port do Firefox + musl (descontinuada)

Esta pasta guarda uma exploracao **futura/superada** de 2026-05 (sessoes 7
e 18-19): portar o Firefox open source para o CapyOS via uma camada de
compatibilidade ABI Linux + a libc **musl**. A estrategia ativa do plano
mestre seguiu outro caminho:

- **Navegador:** `CapyBrowse Text` nativo na Etapa 6 e browser grafico
  estatico na Etapa 7, ambos sobre o **CapyBrowser desacoplado**
  (`capy-browser-core`) — nao um port do Firefox.
- **Libc userland:** `userland/lib/capylibc/` (especifica do CapyOS),
  nao musl.

Portanto estes documentos **nao** descrevem o rumo atual e foram movidos
de `docs/plans/active/` e `docs/architecture/` para ca.

## Conteudo

- `firefox-port-roadmap.md` — roadmap do port (titulo original falava em
  "erradicacao do Capybrowser"; revertido pela estrategia desacoplada).
- `firefox-port-deep-dive.md` — analise tecnica por componente do Firefox.
- `firefox-port-platform-shim.md` — plano da camada de shim de plataforma
  (Strategy A: compatibilidade ABI Linux), origem das tasks S1.x/S2.x.
- `musl-port-strategy.md` — estrategia de vendoring da musl-1.2.5.

## Artefato sobrevivente em producao

A camada `include/kernel/linux_compat/` + `userland/lib/musl/` foi semeada
durante esta exploracao e **permanece in-tree** como base de
compatibilidade Linux-ABI. Os comentarios desses arquivos referenciam
`firefox-port-platform-shim.md` por contexto historico (S1.x/S2.x). O
codigo segue valido; apenas o "grande plano" Firefox/musl esta arquivado.

## Regra

Material em `archive/` serve apenas como contexto historico. Links internos
entre estes documentos podem apontar para os caminhos antigos
(`docs/plans/active/...`, `docs/architecture/...`) anteriores a este move.
Para o rumo atual, use `../../plans/active/capyos-master-plan.md`.
