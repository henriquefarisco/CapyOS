# Browser core — contrato de integração com CapyOS

**Status:** referência para desenvolvimento apartado.
**Integração planejada:** `CapyBrowse Text` na Etapa 6; browser gráfico estático na Etapa 7; JavaScript e ports grandes ficam para etapas posteriores.
**Repositório externo atual:** `CapyBrowser`.

## Migração inicial

O browser app histórico não está presente no `src/apps` ativo. O primeiro
snapshot migrado para `CapyBrowser` é o codec BMP portátil em `src/codecs/`.
HTML-to-text e HTML/CSS display-list devem ser implementados no repositório
externo antes da integração CapyOS.

O CapyOS base ainda preserva `browser_homepage` em `struct system_settings`,
`/system/config.ini` e na tela Settings como preferência de usuário para o
adaptador futuro. Essa superfície pertence ao sistema base; o core apartado não
deve depender de internals de configuração do kernel.

## Fronteira

O core apartado pode desenvolver:

- URL parser e normalização;
- HTML-to-text;
- extração de links;
- parser HTML tolerante;
- parser CSS subset;
- layout estático que gere display list;
- cache policy pura;
- fixtures/golden tests.

O CapyOS base fornece:

- DNS/TCP/HTTP/TLS;
- política de certificados;
- identidade HTTP default do sistema base; browser futuro define seu próprio
  `User-Agent` no adaptador;
- storage de cache/cookies;
- preferência de homepage e integração com Settings;
- janela, scroll, input e render backend;
- limites de memória/tempo e sandbox;
- UX de erro e integração com launcher.

## CapyBrowse Text v1

Entrada:

- URL base;
- bytes de resposta;
- metadata HTTP mínima;
- limite máximo de documento.

Saída:

- título;
- blocos de texto normalizados;
- links numerados com URL resolvida;
- warnings de parse;
- status de truncamento.

Regras:

- não executa JavaScript;
- não carrega recursos externos automaticamente;
- falha fechado em URL inválida, redirect perigoso ou conteúdo acima do limite;
- decodifica UTF-8 e entidades HTML comuns.

## Browser gráfico estático v1

O core deve produzir uma display list independente do compositor:

- text runs;
- rectangles;
- image placeholders;
- link bounds;
- form controls básicos;
- scroll extent;
- accessibility labels quando disponível.

O render backend CapyOS decide como desenhar essa display list.

## Segurança

- Scripts bloqueados por padrão até a etapa de JS sandboxed.
- CSS e HTML têm limites de profundidade e tamanho.
- Cookies/cache pertencem ao sistema base, não ao core puro.
- Certificados e mixed content são política do CapyOS base.
- Parser deve retornar erro/warning, nunca abortar o processo.

## Testes apartados obrigatórios

- fixtures HTML-to-text;
- URL resolution tests;
- malformed HTML tests;
- CSS subset golden tests;
- display-list golden tests;
- truncamento e limites.

## Gate de integração CapyOS

Quando as etapas correspondentes estiverem ativas, recomendar execução externa de:

- `make smoke-x64-vmware-capybrowse-text`;
- `make smoke-x64-vmware-browser-https-static`;
- `make smoke-x64-vmware-browser-text-fallback`.
