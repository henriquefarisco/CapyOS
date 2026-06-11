# CapyOS 0.8.0-alpha.265+20260611

**Data:** 2026-06-11
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.265+20260611`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Handoff cross-repo para desbloquear Etapa 6 / Slice 6.4

## Resumo executivo

Esta release registra o handoff explicito da publicacao do
`capy-browser-core` textual para o repo `CapyBrowser`, destravando a Etapa 6 /
Slice 6.4. O `CapyBrowser v0.6.0` publica o pacote
`org.capyos.browser.text` via `make package STAGE=text`, com `depends=` vazio
por design para nao bloquear o navegador textual em codecs de imagem da Etapa
7.

## Mudancas entregues

- Matriz cross-repo atualizada para `CapyBrowser 0.6.0`.
- Novo snapshot de auditoria:
  `docs/reference/integration/compatibility-audit-2026-06-11.md`.
- Contrato `browser-core-integration-contract.md` agora aponta o handoff
  `v0.6.0` / `org.capyos.browser.text`.
- Tracker da Etapa 6 muda a Slice 6.4 de "bloqueada por publicacao" para
  "desbloqueada para adapter CapyOS-side".
- Release notes e `VERSION.yaml` sincronizados em `alpha.265+20260611`.

## Escopo que continua pendente

- A Slice 6.4 ainda nao esta concluida.
- Proximo passo tecnico: adapter CapyOS-side para consumir o pacote textual,
  reusar render/scroll existente e validar `make smoke-x64-vmware-capybrowse-text`.
- O browser grafico e a dependencia `org.capyos.codecs.image-basic` continuam
  Etapa 7-gated.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.264+20260607` | `alpha.265+20260611` | Handoff de Etapa 6 / Slice 6.4 |
| **CapyBrowser** | `0.5.0` | `0.6.0` | Publica `org.capyos.browser.text` sem dependencia de codec |

Sem bump nos demais repositorios: CapyUI `2.22.0`, CapyCodecs `0.0.7`,
CapyLang `0.1.8`, CapyAgent `0.0.7` e CapyBenchmark `0.0.7`.

## Evidencias / validacao

Validações locais esperadas para a tag:

- `CapyBrowser`: `make validate`, `make package STAGE=text`,
  `make package STAGE=core`.
- `CapyOS`: `make version-audit`, `make layout-audit`, `make test`.

Gate de runtime futuro:

- `make smoke-x64-vmware-capybrowse-text`
