# CapyOS Source Layout

Este documento define a organizacao alvo do codigo-fonte. A regra principal e:
arquivos devem morar pelo dominio que implementam, nao pelo momento historico em
que foram criados.

## Objetivos

- reduzir monolitos antes de adicionar novas funcionalidades grandes
- separar runtime, drivers, apps, ferramentas host e testes
- manter C, Assembly, Python e shell em areas previsiveis
- preservar includes publicos em `include/` e detalhes internos perto do modulo
- permitir refactors incrementais sem quebrar ISO, linker ou testes

## Raiz do repositorio

- `src/`: codigo do sistema CapyOS que roda no kernel/runtime.
- `include/`: headers publicos entre modulos.
- `tests/`: testes de host e stubs.
- `tools/host/`: ferramentas C executadas no host.
- `tools/scripts/`: automacoes Python/shell executadas no host.
- `docs/`: arquitetura, planos, releases e guias.
- `assets/`: artefatos fonte de marca/recursos.
- `third_party/`: dependencias vendorizadas, sem refactor local salvo patch explicito.

## Regras por linguagem

- C de runtime/kernel: `src/<dominio>/...`.
- Headers publicos: `include/<dominio>/...`.
- Headers migrados/quarentenados: convenção `include/legacy/<dominio>/...`
  reservada para futuras migrações que precisem de fallback explícito
  documentado. Atualmente o diretório está vazio — a higiene completa de
  Etapa 9 removeu todos os antigos tombstones (`include/services/package_manager.h`,
  `include/gui/{bmp,png,jpeg}_loader.h`, `include/lang/capylang.h`).
- Headers internos: `src/<dominio>/internal/...` quando usados apenas por aquele dominio.
- Fragmentos `.inc` de C sao permitidos somente como etapa intermediaria de
  split, dentro do modulo dono, com um `.c` orquestrador mantendo a ordem de
  inclusao.
- Quando um grupo ficar estavel, ele deve ser promovido para `.c/.o` reais com
  um header interno do modulo, como ja foi feito em `src/apps/css_parser/` e
  `src/config/first_boot/`.
- Assembly: `src/arch/<arch>/...`, preferencialmente em subpastas `boot/`, `cpu/` ou `syscall/`.
- Python: `tools/scripts/` ou testes auxiliares em `tests/`.
- Shell: scripts de entrada na raiz apenas quando forem wrappers; logica real deve ir para `tools/scripts/`.
- Linker scripts: `src/arch/<arch>/`.

## Limites de tamanho

- Arquivo C de runtime: alvo maximo de 900 linhas.
- Teste de host: alvo maximo de 900 linhas.
- Arquivo acima do limite deve ganhar um item no roadmap de split antes de receber features grandes.
- Excecoes temporarias devem ficar documentadas no roadmap do modulo.

## Modulos alvo

### Apps

Estrutura alvo:

```text
src/apps/<app>/
include/apps/<app>.h
```

Exemplo futuro para adaptador de navegador quando a etapa correspondente
estiver ativa:

```text
src/apps/html_viewer/
  browser_app.c
  navigation.c
  document_loader.c
  html_parser.c
  css_resources.c
  image_resources.c
  render.c
  ui.c
include/apps/html_viewer.h
```

### Arquitetura x86_64

Estrutura alvo:

```text
src/arch/x86_64/boot/
src/arch/x86_64/cpu/
src/arch/x86_64/runtime/
src/arch/x86_64/platform/
src/arch/x86_64/storage/
```

Assembly deve ficar junto do subsistema que atende, por exemplo
`boot/entry64.S`, `cpu/interrupts_asm.S`, `cpu/context_switch.S`.
Esta separacao ja esta aplicada no `src/arch/x86_64/` atual para os pontos de
entrada/CPU/syscall em Assembly.

### Rede

Estrutura alvo:

```text
src/net/core/
src/net/protocols/
src/net/services/
src/net/bootstrap/
src/net/hyperv/
src/net/internal/
```

Esta estrutura ja esta parcialmente aplicada e deve ser mantida.

### Shell

Estrutura alvo:

```text
src/shell/core/
src/shell/commands/
src/shell/session/
```

Comandos extensos devem ser agrupados por responsabilidade, nao por arquivo
historico.

### Ferramentas host

Ferramentas C ficam em `tools/host/src` com headers em `tools/host/include`.
Automacoes Python/shell ficam em `tools/scripts`.

### Projetos desacoplados

Engines, linguagens, parsers, formatos e codecs desenvolvidos fora do sistema
base não devem ser importados diretamente para `src/` até existir contrato de
integração e adaptador CapyOS pequeno.

Contratos vivem em:

```text
docs/reference/integration/
```

Quando integrados ao repositório principal, a lógica pura deve continuar em
módulo próprio, host-testável, e o código CapyOS deve ficar limitado a
adaptadores por domínio:

- browser/app adapter em `src/apps/`;
- widget/render adapter em `src/gui/`;
- package/apply adapter em `src/services/` ou domínio equivalente;
- codec backend em `src/gui/` ou `src/media/` quando esse domínio existir;
- CapyLang runtime adapter em domínio próprio somente quando a etapa ativa
  permitir.

## Wrappers de raiz

Arquivos executaveis na raiz devem ser pequenos pontos de entrada. A logica real
deve ficar em `tools/scripts/` ou `tools/host/`.

Exemplo atual:

- `install.sh`, `install-linux.sh`, `install-macos.sh` e
  `install-windows.ps1` sao wrappers.
- `tools/scripts/install_deps.sh`, `tools/scripts/install_deps_macos.sh`
  e `tools/scripts/install_deps_windows.ps1` contem a logica de
  instalacao.

## Processo de refactor

1. Usar `make layout-audit` em ambiente externo/CI para identificar monolitos e mistura de camadas.
2. Escolher um modulo por vez.
3. Mover/splitar sem alterar comportamento.
4. Atualizar Makefile/includes no mesmo patch.
5. Recomendar `make test` em ambiente externo/CI.
6. Para areas de boot/runtime, recomendar tambem `make all64` em ambiente externo/CI.

## Auditoria automatizada

Use:

```bash
make layout-audit
```

`make layout-audit` e um gate estrito: falha quando encontra monolitos acima do
limite, headers internos em local ambiguo ou mistura suspeita de linguagens.

Para gerar apenas o relatorio, sem falhar a execucao:

```bash
make layout-audit-report
```
