# CapyOS - Roadmap de Organizacao de Codigo

## Objetivo

Organizar o projeto inteiro em modulos menores, com fronteiras claras entre
runtime, drivers, apps, ferramentas host, scripts e testes, preservando build e
comportamento a cada passo.

## Estado Atual

- `src/`, `include/`, `tests/`, `tools/` e `docs/` ja existem e separam parte
  das responsabilidades.
- A rede ja esta parcialmente segmentada em `core`, `protocols`, `services`,
  `bootstrap` e `hyperv`.
- Ainda existem monolitos grandes, principalmente:
  - `src/boot/uefi_loader.c`
- `src/fs/capyfs/capyfs.c` ja foi reduzido a orquestrador de fragmentos `.inc`;
  ainda falta promover grupos estaveis para `.c/.o` independentes.
- `src/net/services/http.c` ja foi reduzido a orquestrador de fragmentos `.inc`;
  ainda falta promover grupos estaveis para `.c/.o` independentes.
- `src/config/first_boot/` ja foi promovido para modulos `.c` reais com
  header interno; o proximo passo agora e repetir o mesmo padrao nos outros
  wrappers ainda baseados em `.inc`.
- `src/arch/x86_64/input_runtime.c` ja foi reduzido a orquestrador de
  fragmentos `.inc`; ainda falta promover grupos estaveis para `.c/.o`
  independentes.
- `src/arch/x86_64/kernel_volume_runtime.c` ja foi reduzido a orquestrador de
  fragmentos `.inc`; ainda falta promover grupos estaveis para `.c/.o`
  independentes.
- os arquivos Assembly de `src/arch/x86_64/` ja nao ficam mais misturados na
  raiz do subsistema: entradas de boot, CPU e syscall foram movidas para
  subpastas tematicas.
- `src/boot/uefi_loader.c` ja foi reduzido a orquestrador de fragmentos `.inc`;
  ainda falta promover grupos estaveis para `.c/.o` independentes.
- `src/shell/commands/system_control.c` ja foi reduzido a orquestrador de
  fragmentos `.inc`; ainda falta promover grupos estaveis para comandos `.c`
  independentes.
- `src/shell/commands/system_info.c` ja foi reduzido a orquestrador de
  fragmentos `.inc`; ainda falta promover grupos estaveis para comandos `.c`
  independentes.
- `src/shell/core/shell_main.c` ja foi reduzido a orquestrador de fragmentos
  `.inc`; ainda falta promover grupos estaveis para `.c/.o` independentes.
- `tests/test_html_viewer.c` ja foi reduzido a orquestrador de fragmentos `.inc`;
  ainda falta promover grupos estaveis para testes `.c` independentes.
- `src/apps/html_viewer.c` ja foi reduzido a orquestrador de fragmentos `.inc`;
  ainda falta promover fragmentos estaveis para `.c/.o` independentes.
- `src/apps/css_parser/` ja foi promovido para modulos `.c` reais com header
  interno e serve como referencia para novas migracoes.
- O Makefile ainda carrega muita topologia manual em listas extensas.

## Regras Oficiais

Fonte de verdade: [source-layout.md](../architecture/source-layout.md).

Resumo:

- codigo C/Assembly de runtime fica em `src/`
- headers publicos ficam em `include/`
- scripts Python/shell de host ficam em `tools/scripts/`
- ferramentas C de host ficam em `tools/host/`
- monolitos acima de 900 linhas devem ser splitados antes de receber features grandes

## Fase 0 - Auditoria e contrato de layout

Estado: `fechado`

Feito:

- documento oficial de layout criado
- auditoria automatizada adicionada em `tools/scripts/audit_source_layout.py`
- alvo `make layout-audit` planejado para uso local/CI

Aceite:

- qualquer dev consegue listar monolitos e mistura de linguagens com um comando
- refactors futuros tem regras documentadas

## Fase 1 - Scripts e ferramentas host

Estado: `em andamento`

Meta:

- manter wrappers de raiz pequenos
- mover logica real de shell/Python para `tools/scripts`
- separar ferramentas C em `tools/host/src` e headers em `tools/host/include`

Primeiros alvos:

- `fechado` revisar `install.sh` para ser wrapper ou instalador autocontido documentado
- garantir que scripts Python tenham nomes e responsabilidades especificas

## Fase 2 - Apps

Estado: `aberto`

Meta:

- migrar apps de `src/apps/*.c` para `src/apps/<app>/`
- quebrar `html_viewer.c` em navegacao, parser, recursos, render e UI
- criar helpers comuns de apps somente quando houver duplicacao comprovada

Ordem sugerida:

1. `html_viewer` - split inicial em fragmentos concluido; falta extracao para objetos independentes
1. `css_parser` - promovido para objetos independentes; usado como padrao de migracao
2. `file_manager`
3. `settings`
4. `task_manager`
5. `text_editor`
6. `calculator`

## Fase 3 - Boot e arquitetura

Estado: `em andamento`

Meta:

- dividir `src/boot/uefi_loader.c`
- segmentar `src/arch/x86_64/` em `boot`, `cpu`, `runtime`, `platform` e `storage`
- manter linker script e entradas Assembly com caminhos previsiveis

Risco:

- qualquer movimento aqui exige `make all64` e, idealmente, smoke de boot.

Excecoes documentadas:

- `src/security/tls_trust_anchors.c` e uma tabela estatica de certificados
  gerada/curada para TLS. Ela permanece grande por natureza de dados, nao por
  mistura de responsabilidades; o auditor nao deve exigir split funcional desse
  arquivo.

## Fase 4 - Shell e FS

Estado: `em andamento`

Meta:

- dividir comandos shell extensos por responsabilidade
- reduzir `src/fs/capyfs/capyfs.c` em operacoes, alocacao, diretorios e IO

## Fase 5 - Build system

Estado: `aberto`

Meta:

- reduzir listas manuais gigantes no Makefile
- agrupar objetos por variaveis de modulo
- preparar CI para `make layout-audit`, `make test` e `make all64`

## Fase 6 - Testes

Estado: `em andamento`

Meta:

- dividir testes grandes por modulo e cenario
- manter runners pequenos
- aproximar stubs dos testes que os usam
- preparar suites independentes para navegador, rede, FS e boot

## Criterios de aceite do programa

- nenhum arquivo C proprio acima de 900 linhas sem justificativa documentada
- scripts e ferramentas host nao misturados com runtime
- headers internos e publicos com convencao clara
- Makefile organizado por grupos de modulos
- `make test` verde apos cada fase
- `make all64` verde apos fases que mexem em boot/runtime

## Changelog

### 2026-04-23 - Fase 0

- criado documento oficial de layout de codigo
- criada auditoria automatizada de monolitos, linguagens e headers internos
- roadmap de organizacao criado

### 2026-04-23 - Inicio da Fase 1

- `install.sh` reduzido a wrapper de raiz
- logica de instalacao movida para `tools/scripts/install_deps.sh`
- auditoria passou a incluir scripts/wrappers da raiz

### 2026-04-23 - Inicio da Fase 2

- `src/apps/html_viewer.c` reduzido a orquestrador de uma unica unidade de compilacao
- implementacao do navegador separada em fragmentos por responsabilidade em `src/apps/html_viewer/`
- `src/apps/css_parser.c` reduzido a orquestrador de uma unica unidade de compilacao
- parser CSS separado em fragmentos por responsabilidade em `src/apps/css_parser/`
- `tests/test_html_viewer.c` reduzido a orquestrador de testes do navegador
- casos do navegador separados em parser, stubs, navegacao, recursos e compatibilidade
- `src/shell/commands/system_control.c` reduzido a orquestrador de comandos
  administrativos
- comandos de sistema separados em configuracao, servicos/jobs, updates,
  recovery, storage, energia/runtime e registro
- `src/shell/commands/system_info.c` reduzido a orquestrador de comandos
  informativos
- comandos informativos separados em print basico, status de servico/job,
  update, recovery geral, storage e rede/registro
- `src/shell/core/shell_main.c` reduzido a orquestrador do core CapyCLI
- core do shell separado em contexto/comandos, strings/paths, saida/arquivos,
  diagnosticos e loop principal
- `src/fs/capyfs/capyfs.c` reduzido a orquestrador do runtime CapyFS
- CapyFS separado em prelude/ops, format/mount, file IO, namespace,
  alocacao inode/bloco e entradas de diretorio
- `src/net/services/http.c` reduzido a orquestrador do cliente HTTP
- HTTP separado em headers/encoding, URL/request builder, transporte,
  request/response e redirects/download
- `src/config/first_boot.c` reduzido a orquestrador do provisionamento inicial
- first boot separado em logging, arquivos/usuarios, referencia/deteccao,
  etapas de provisionamento, fluxo de setup e API publica
- `src/config/first_boot/` promovido de fragmentos `.inc` para modulos `.c`
  reais com header interno dedicado
- `src/arch/x86_64/input_runtime.c` reduzido a orquestrador do runtime de input
- input runtime separado em portas/prelude, decode/probe, gerenciamento de
  backends, polling e status/Hyper-V
- `src/arch/x86_64/kernel_volume_runtime.c` reduzido a orquestrador do runtime
  de volume
- volume runtime separado em IO/chaves, persistencia/probe, montagem,
  helpers de filesystem e API publica
- `src/apps/css_parser/` promovido de fragmentos `.inc` para modulos `.c`
  reais com header interno
- `src/apps/html_viewer/` iniciou promocao incremental para modulos `.c`
  reais com `common`, `navigation_state` e `response_classification`,
  mantendo o wrapper apenas para os fragments ainda nao extraidos
- `src/apps/html_viewer/text_url_helpers.c` promovido para modulo `.c` real,
  compartilhando helpers de URL, texto, entidades HTML e utilitarios de tags
  com o restante do navegador via header interno
- `src/apps/html_viewer/async_runtime.c` promovido para modulo `.c` real,
  isolando o runtime de navegacao em background, fila de follow-up e polling
  do estado assíncrono fora do wrapper principal
- `src/apps/html_viewer/ui_runtime.c` promovido para modulo `.c` real,
  isolando quick start, paginas internas `about:*` e cleanup do navegador
  fora do fragmento de callbacks e entrada de UI
- `src/apps/html_viewer/ui_input.c` promovido para modulo `.c` real,
  isolando callbacks de janela, navegacao por teclado, find-in-page, foco de
  formulario e edicao da barra de URL
- `src/apps/html_viewer/ui_mouse.c` promovido para modulo `.c` real,
  isolando hit-test, clique em links/controles, rolagem para anchors e
  interacao de mouse fora do pipeline principal de render
- `src/apps/html_viewer/forms_and_response.c` promovido para modulo `.c`
  real, isolando submissao de formularios, aplicacao de resposta HTTP,
  degradacao controlada e helpers de cookies/query-string fora do wrapper
- arquivos Assembly de `src/arch/x86_64/` movidos para `boot/`, `cpu/` e
  `syscall/`, alinhando a arvore real com o layout documentado e removendo
  mistura desnecessaria com os `.c` da raiz do subsistema
- `src/boot/uefi_loader.c` reduzido a orquestrador do loader UEFI
- loader UEFI separado em prelude/config/arquivos, kernel loader, descoberta e
  streaming, selecao de disco, recovery/GPT, FAT32, instalador, ACPI/log/GOP e
  `efi_main`
- `src/security/tls_trust_anchors.c` classificado como excecao de dados
  estaticos no auditor de layout
- headers internos de `config`, `arch/x86_64`, `drivers/hyperv`,
  `net/bootstrap`, `shell/commands` e `security` movidos para `internal/`
- auditoria passou a reconhecer `.inc` como fragmento C para nao esconder monolitos
