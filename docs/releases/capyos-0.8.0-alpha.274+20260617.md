# CapyOS 0.8.0-alpha.274+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.274+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 6 / Slice 6.8 -- aviso claro de erro HTTP no CapyBrowse Text

## Resumo executivo

O **CapyBrowse Text** passa a exibir um aviso claro e localizado quando o
servidor responde com um status HTTP de erro (4xx/5xx) -- por exemplo
`HTTP 404: Pagina nao encontrada` -- em vez de simplesmente renderizar o
corpo da pagina de erro como se fosse conteudo normal. Fecha a parte **HTTP**
do criterio de i18n da Etapa 6 ("mostra erros claros de DNS/TLS/HTTP"). **A
Etapa 6 NAO esta fechada**: os gates externos VMware seguem pendentes.

## Mudancas

- **Aviso de status HTTP (`userland/bin/capybrowse/capybrowse_view.c`):** nova
  funcao pura e host-testada `capybrowse_format_status_notice(status_code,
  lang, out, cap)`. Para `status_code >= 400` escreve uma linha
  `HTTP <code>: <frase localizada>` -- 404 (Pagina nao encontrada), 403 (Acesso
  negado), 500 (Erro interno do servidor), 503 (Servico indisponivel) e
  fallbacks genericos para 4xx/5xx -- em PT-BR/EN/ES, com EN como base de
  fallback (mesma convencao `es`-antes-de-`en` do `net_pick` de
  `capylibc-net`). Para `< 400` (sucesso/redirect) nao escreve nada.
- **Fluxo do app (`userland/bin/capybrowse/main.c`):** apos uma resposta
  entregue, se `resp.status_code >= 400` o app imprime o aviso **antes** de
  renderizar o corpo (o corpo ainda e mostrado, para nao perder detalhe
  fornecido pelo servidor). O idioma da sessao agora e resolvido **uma unica
  vez** no inicio de `main` e passado para `cb_fail` e para o aviso (uma
  chamada de `SYS_GET_SESSION_LANG`).

## Validacao

- `make test` -- verde. `tests/userland/test_capybrowse_view.c` **31/31**
  (+11 casos: codigo presente no aviso, frases por familia em PT/EN/ES,
  `200`/`3xx` sem aviso, buffer limitado, `NULL` seguro).
- `make capybrowse-elf` -- linka o ELF ring-3 com o novo caminho.
- `make layout-audit` -- sem warnings.
- `make version-audit` -- verde.
- **Smoke-safe por construcao:** o gate `capybrowse-text` busca `example.com`
  -> `200` -> nenhum aviso -> saida e marker (`[smoke] capybrowse-text ready`)
  inalterados.

## Escopo pendente

- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`) -- operador.
- Fetch **remoto** de modulos/browsing: assets publicados + verificador
  Ed25519 da CapyAgent (P0). Caminho offline ja funciona (alpha.271).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.273+20260617` | `alpha.274+20260617` | Aviso de erro HTTP localizado no CapyBrowse Text (Slice 6.8) |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos
irmaos permanecem nas versoes de `alpha.266` (sem bump). Mudanca puramente de
presentation-layer no app ring-3 CapyOS-owned.

_Build: `0.8.0-alpha.274+20260617`_
