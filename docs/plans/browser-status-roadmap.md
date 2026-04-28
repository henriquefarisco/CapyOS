# Navegador CapyOS - Status e Roadmap

## Objetivo

Conduzir a evolucao do navegador do CapyOS de um `html_viewer` embutido no runtime
para uma trilha de navegacao previsivel, com falha controlada e compatibilidade
progressivamente melhor com conteudo web moderno.

Este documento e a fonte de verdade operacional do programa do navegador.

## Estado Atual

O navegador atual ainda e um `html_viewer` acoplado ao runtime e ao compositor.
Ele ja possui:

- navegacao HTTP/HTTPS basica
- redirects, cookies, cache simples, HTML/CSS parciais
- suporte parcial a PNG/JPEG
- historico, bookmarks e busca na pagina
- arquitetura interna bem mais modular, com extracoes reais para modulos `.c`

Mas continua sem:

- isolamento real por processo
- watchdog de browser
- compatibilidade ampla com sites JS-heavy
- pipeline de renderizacao realmente incremental

## Problemas Confirmados

### Feito

- `mitigado` loading infinito causado por navegacao assíncrona dependente de scheduler indisponivel
- `mitigado` falta de estado explicito de navegacao
- `mitigado` ausencia de identificador de navegacao para descartar resposta obsoleta
- `mitigado` redirect handling fraco e sem rastreamento do URL final
- `mitigado` ausencia de limites claros para documento HTML grande
- `mitigado` ausencia de limites de CSS/imagens externas
- `mitigado` falta de observabilidade minima de etapa e motivo de erro
- `mitigado` ausencia de cancelamento explicito de navegacao na UI
- `mitigado` falta de diagnostico navegavel nas paginas internas do browser
- `mitigado` respostas JavaScript/JSON sendo renderizadas como texto bruto
- `mitigado` HTML util ignorado quando havia bootstrap/script antes do documento
- `mitigado` shell HTML sem conteudo estatico util reexibindo bootstrap/script escondido
- `mitigado` redirects por meta refresh nao sendo seguidos
- `mitigado` CSS/imagens relativos ignorando `<base href>`
- `mitigado` imagens lazy/responsivas sem `src` simples ficando ausentes
- `mitigado` `<picture>/<source>` sem fallback para formato suportado
- `mitigado` imagem WebP/AVIF parecendo sumir sem diagnostico
- `mitigado` CSS externo carregado via preload/as=style ou @import ficando ausente

### Em andamento

- `aberto` compatibilidade visual ainda limitada em paginas modernas
- `aberto` carregamento de recursos externos ainda e sincronizado em varios pontos
- `aberto` paginas pesadas ainda podem degradar a experiencia mesmo sem congelamento total

### Falta

- `aberto` isolamento por processo e watchdog
- `aberto` pipeline incremental real de parser/layout/render
- `aberto` motor JS robusto
- `aberto` telemetria de consumo de memoria e tempo por etapa

### Bloqueadores

- scheduler/multithread ainda nao consolidado no runtime
- modelo de processo/isolamento ainda nao e base confiavel para um browser moderno
- `html_viewer` continua rodando no mesmo contexto do runtime grafico

### Proximo marco

Fase 1 fechada no browser atual. Proximo marco: iniciar a Fase 2 com
isolamento por processo, watchdog e reinicio contido.

## O que ja foi feito

Entrega atual:

- doc oficial de `status + roadmap` criado
- estado de navegacao formalizado no `html_viewer`
- `navigation_id` e `active_navigation_id` adicionados
- `final_url`, `redirect_count`, `safe_mode`, `last_stage` e `last_error_reason`
  adicionados ao estado do app
- respostas antigas agora podem ser descartadas quando uma navegacao nova assume
- redirects passaram a atualizar URL final e contador de hops
- limites de seguranca adicionados para HTML, texto, CSS e imagens
- falha controlada para documento grande em vez de tentar renderizar indefinidamente
- rastreamento de recursos externos carregados adicionado
- cancelamento de navegacao por `Esc` enquanto a pagina estiver carregando
- paginas internas e tela de erro enriquecidas com `stage`, `final_url`,
  `redirect_count`, `safe_mode` e ultimo motivo de falha
- testes de host ampliados para:
  - navegacao simples
  - redirects
  - recursos externos
  - descarte de resposta obsoleta
  - documento grande entrando em modo seguro
  - rejeicao controlada de resposta JavaScript/JSON como documento
  - extracao de HTML depois de ruido/bootstrap inicial
- sniffing de HTML ampliado para detectar documento real alem dos primeiros bytes
- resposta de script/dados agora falha de modo controlado em vez de aparecer como codigo cru
- shell HTML dependente de JavaScript agora mostra pagina degradada explicativa
- texto bootstrap/codeish fica oculto mesmo quando nao sobra conteudo visivel
- suporte a `<base href>` para links, formularios, CSS e imagens
- suporte a meta refresh curto como redirect controlado
- fallback de imagem por `data-src`, `data-lazy-src`, `data-original` e primeiro item de `srcset`
- fallback de `<picture>/<source>` escolhendo fonte PNG/JPEG quando disponivel
- formato de imagem nao suportado agora fica marcado no node e aparece como fallback textual
- links CSS com `rel=preload` e `as=style` entram como stylesheet fallback
- CSS externo passa a enfileirar `@import` relativo ao proprio arquivo CSS
- `<style>` inline tambem passa a enfileirar `@import`
- fila de CSS pendente evita duplicar a mesma folha
- budget externo por navegacao centralizado em `navigation_budget.c`
- esgotamento de budget de CSS/imagens ativa `safe_mode`, preserva motivo em
  `last_error_reason` e registra `[browser]` no `klog`

## Fase 1 - Estabilizacao do browser atual

Objetivo:

- fazer o navegador abrir sites leves e medios com muito mais previsibilidade
- remover necessidade de insistencia manual para casos comuns
- falhar de forma explicita quando exceder limites
- evitar que pagina pesada bloqueie o sistema inteiro por longos periodos

Estado:

- `fechado`

Escopo:

- loading infinito e navegacao obsoleta
- redirects corretos e previsiveis
- URL final e estado de navegacao observaveis
- limites para resposta HTML/texto
- limites para CSS/imagens externas
- modo seguro por navegacao
- erro controlado com motivo e etapa
- cancelamento explicito por teclado durante loading
- visao interna do estado atual em `about:version` e paginas de erro

Criterios de aceite:

- `fechado` navegador sempre termina em `ready`, `failed` ou `cancelled`
- `fechado` redirects 301/302/303/307/308 funcionam de forma consistente
- `fechado` recursos externos pequenos e suportados carregam corretamente
- `fechado` documento grande falha sem congelar a UI inteira
- `fechado` usuario consegue abortar carregamento e inspecionar o estado atual do browser
- `fechado` respostas JS/JSON nao aparecem como texto monocromatico/script cru
- `fechado` HTML valido apos bootstrap inicial e renderizado em modo seguro
- `fechado` HTML sem conteudo estatico util nao reexibe bootstrap/script oculto
- `fechado` meta refresh curto atualiza URL/final URL/historico como redirect
- `fechado` assets relativos respeitam `<base href>`
- `fechado` imagens comuns em lazy loading ganham URL de fallback
- `fechado` `<picture>` basico consegue usar alternativa suportada quando WebP/AVIF vem antes
- `fechado` imagens WebP/AVIF sem alternativa aparecem como `[unsupported image]` em vez de sumirem
- `fechado` folhas CSS preload/as=style e `@import` basico sao carregadas dentro dos limites
- `fechado` imports CSS duplicados sao deduplicados antes de fetch

Nao prometido pela Fase 1:

- compatibilidade ampla com apps web JS-heavy
- isolamento por processo
- watchdog de processo
- decoder real de WebP/AVIF
- engine JavaScript robusta

## Fase 2 - Isolamento por processo e watchdog

Estado:

- `aberto`

Meta:

- mover o navegador para processo isolado
- permitir kill/restart sem derrubar compositor
- watchdog para runaway parse/render/script

Aceite:

- travamento do browser nao derruba o desktop
- timeout e OOM do navegador afetam apenas o processo do browser

## Fase 3 - Nova engine/pipeline web

Estado:

- `aberto`

Meta:

- parser/layout HTML/CSS mais completos
- pipeline de recursos e rendering incremental
- cache, recursos externos e degradacao mais robustos

## Fase 4 - JavaScript robusto

Estado:

- `aberto`

Meta:

- runtime JS forte o bastante para sites modernos selecionados
- budget de CPU/memoria e interrupcao segura

Importante:

- esta fase nao faz parte da promessa da Fase 1
- compatibilidade ampla com apps web JS-heavy nao e prometida na entrega atual

## Matriz de validacao

### Automatizado

- URL sem esquema abre com normalizacao para HTTPS
- redirect unico atualiza `url`, `final_url` e `redirect_count`
- resposta antiga nao sobrescreve navegacao mais nova
- CSS externo pequeno e aplicado
- imagem PNG externa pequena e decodificada
- documento HTML grande entra em `safe_mode` e falha controladamente
- resposta JavaScript/JSON pura entra em falha controlada
- resposta com bootstrap inicial seguido de HTML renderiza o HTML e nao o script
- shell HTML JS-heavy sem conteudo estatico cai em pagina degradada explicativa
- meta refresh curto segue para destino final
- `<base href>` resolve CSS/imagens relativas
- imagens lazy/srcset basicas sao reconhecidas pelo parser
- `<picture>/<source>` escolhe fonte suportada basica
- imagem nao suportada preserva alt text e estado de erro
- `@import` em CSS externo respeita limite de folhas carregadas
- `@import` em bloco `<style>` respeita limite de folhas carregadas
- CSS/imagens externas respeitam budget total por navegacao e registram falha
  persistente quando excedido
- redirects 301/302/303/307/308 sao validados
- cadeia de redirect acima do limite falha de forma controlada
- cancelamento por `Esc` sai de `loading` para `cancelled`

### Manual / smoke

- pagina institucional simples
- pagina com CSS externo e imagens
- pagina com redirect HTTP -> HTTPS
- pagina media de documentacao
- pagina artificial pesada para validar falha controlada

## Riscos e bloqueadores

- mesmo com limites, o browser atual ainda nao tem isolamento real
- fetch e renderizacao ainda nao sao totalmente incrementais
- sites muito dependentes de JS continuarao degradados
- o motor atual ainda e insuficiente para prometer compatibilidade moderna ampla

## Changelog resumido por implementacao concluida

### 2026-04-23 - Entrega inicial da Fase 1

- criado o documento oficial de status e roadmap
- implementado estado formal de navegacao
- implementado descarte de resposta obsoleta
- implementado redirect tracking e URL final
- adicionados limites de seguranca para documentos e recursos externos
- adicionados testes de regressao para os casos principais da Fase 1
- adicionado cancelamento explicito de navegacao via `Esc`
- adicionada observabilidade visivel nas paginas internas e na tela de erro

### 2026-04-23 - Consolidacao estrutural e handoff para Fase 2

- `html_viewer` deixou de ser um bloco monolitico e passou a ter modulos reais
  para estado de navegacao, classificacao de resposta, helpers de URL/texto,
  runtime assincrono, UI, mouse, teclado e aplicacao de respostas
- a base de desenvolvimento do navegador ficou mais legivel e segura para as
  proximas fases
- proximo foco tecnico confirmado: isolamento por processo, watchdog e
  pipeline incremental mais forte

### 2026-04-23 - Mitigacao de paginas exibidas como script/texto

- ampliada a deteccao de HTML dentro da resposta antes de cair para texto simples
- respostas JavaScript/JSON puras passam a gerar erro controlado de documento nao renderizavel
- conteudo bootstrap antes de `<html>` e ignorado quando ha HTML renderizavel depois
- filtro de texto degradado passou a reconhecer marcadores comuns de apps JS modernos
- pagina degradada adicionada para app shell sem HTML estatico util
- adicionados testes de regressao para resposta script e HTML apos ruido inicial

### 2026-04-23 - Redirects HTML e recursos modernos basicos

- implementado parsing de `<base href>`
- links, formularios, CSS e imagens passam a resolver URLs relativas contra a base do documento
- implementado meta refresh curto como redirect com limite de hops
- adicionados fallbacks de imagem para `data-src`, `data-lazy-src`, `data-original` e `srcset`
- adicionado fallback basico de `<picture>/<source>` para PNG/JPEG
- adicionados testes de regressao para meta refresh, base href, imagens lazy/srcset e picture/source
- adicionada marcacao explicita para imagem WebP/AVIF sem decoder
- adicionado carregamento de CSS `rel=preload as=style`
- adicionado enfileiramento limitado de CSS `@import`
- adicionada deduplicacao de folhas CSS pendentes

### 2026-04-23 - CSS externo moderno basico

- `rel` de `<link>` agora e tratado como lista de tokens
- `<link rel="preload" as="style">` passa a ser usado como fallback de stylesheet
- CSS externo pode enfileirar `@import url(...)` e `@import "..."` relativo ao arquivo CSS de origem
- blocos `<style>` tambem podem enfileirar imports externos
- a fila de CSS evita duplicar imports iguais
- imports respeitam `HTML_MAX_PENDING_CSS` e ativam safe mode ao exceder limite
- adicionados testes de regressao para preload/as=style, import CSS relativo e import em `<style>`

### 2026-04-23 - Fechamento da Fase 1

- adicionados testes para redirects 301/302/303/307/308
- adicionada validacao de limite de cadeia de redirect
- adicionada validacao de cancelamento por `Esc`
- Fase 1 marcada como fechada no documento oficial

### 2026-04-28 - Primeiro budget de pipeline externo

- adicionado modulo `navigation_budget.c` para centralizar budget externo por
  navegacao
- fetches de CSS e imagens de rede agora consomem `external_fetch_attempts`
  antes de emitir requisicao HTTP
- esgotamento de budget ativa `safe_mode`, marca
  `resource_budget_exhausted`, preserva motivo controlado e registra
  `[browser] external resource budget exhausted` no `klog`
- adicionado teste de regressao para pagina com excesso de CSS/imagens,
  validando degradacao sem falhar a navegacao

## Proximos passos

1. Validar a Fase 1 em VM com paginas reais leves e medias.
2. Investigar aplicacao de regras CSS importadas em seletores de texto gerados pelo parser.
3. Adicionar decodificador real para WebP/AVIF ou conversao previa no pipeline de recursos.
4. Melhorar fallback visual para paginas JS-heavy que tenham pouco HTML estatico.
5. Estender budget cooperativo para parse/layout/paint.
6. Identificar gargalos restantes de fetch/render para paginas pesadas.
7. Planejar a transicao para Fase 2 com processo isolado e watchdog.
