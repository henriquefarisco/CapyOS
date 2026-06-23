# CapyOS 0.8.0-alpha.275+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.275+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 6 / Slice 6.9 -- CapyBrowse Text so renderiza conteudo textual

## Resumo executivo

O **CapyBrowse Text** passa a checar o `Content-Type` da resposta antes de
renderizar. Conteudo binario (imagem, PDF, `application/octet-stream`, fonte,
...) deixava o core HTML-to-text produzir lixo; agora o app mostra um aviso
claro e localizado ("Conteudo nao-textual ...") e encerra limpo, sem
mangle. **A Etapa 6 NAO esta fechada**: os gates externos VMware seguem
pendentes.

## Mudancas

- **Classificacao de Content-Type (`userland/bin/capybrowse/capybrowse_view.c`):**
  nova funcao pura e host-testada `capybrowse_content_is_text(content_type)`.
  Retorna 1 (exibivel) para `text/*`, e para os familiares `html`/`xml`/`json`
  (busca de substring **case-insensitive**, sem libc -- o ring-3 linka so o
  capylibc minimo), e tambem para um `Content-Type` ausente/vazio (servidores
  costumam omitir; o core e tolerante). Retorna 0 para o restante (binario).
- **Aviso de conteudo nao-textual (mesmo arquivo):**
  `capybrowse_format_content_notice(content_type, lang, out, cap)` emite uma
  linha localizada PT-BR/EN/ES -- "Conteudo nao-textual (`<tipo>`): nao
  exibivel em modo texto" -- com EN como base de fallback.
- **Fluxo do app (`userland/bin/capybrowse/main.c`):** apos a resposta, le o
  tipo via `capy_http_response_find_header(&resp, "Content-Type")`; se nao for
  textual, imprime o aviso e `capy_exit(0)` (encerramento limpo) em vez de
  passar bytes binarios ao core.

## Validacao

- `make test` -- verde. `tests/userland/test_capybrowse_view.c` **45/45**
  (+14 casos: `text/html`, `text/plain`, `application/json`,
  `application/xhtml+xml`, `IMAGE/PNG` (case-insensitive), `application/pdf`,
  `application/octet-stream`, ausente, vazio; e o aviso em PT-BR/EN + tipo
  desconhecido).
- `make capybrowse-elf` -- linka o ELF ring-3.
- `make layout-audit` -- sem warnings.
- `make version-audit` -- verde.
- **Smoke-safe por construcao:** o gate `capybrowse-text` busca `example.com`
  -> `text/html` -> exibivel -> fluxo e marker (`[smoke] capybrowse-text
  ready`) inalterados.

## Escopo pendente

- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`) -- operador.
- Fetch **remoto** de modulos/browsing: assets publicados + verificador
  Ed25519 da CapyAgent (P0). Caminho offline ja funciona (alpha.271).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.274+20260617` | `alpha.275+20260617` | Gating por Content-Type no CapyBrowse Text (Slice 6.9) |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos
irmaos permanecem nas versoes de `alpha.266` (sem bump). Mudanca puramente de
presentation-layer no app ring-3 CapyOS-owned.

_Build: `0.8.0-alpha.275+20260617`_
