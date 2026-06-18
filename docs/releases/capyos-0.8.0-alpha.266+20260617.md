# CapyOS 0.8.0-alpha.266+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.266+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Consolidacao de release cross-repo + progresso in-tree da Etapa 6

## Resumo executivo

Esta release consolida o trabalho aditivo acumulado nos 7 repositorios em um
evento de release coordenado. Cada pacote irmao recebe um bump de versao com
mudancas aditivas e host-validadas, e o CapyOS registra o progresso in-tree da
Etapa 6. **A Etapa 6 NAO esta fechada**: os gates externos VMware
`make smoke-x64-vmware-capybrowse-text` e `make smoke-x64-vmware-apps-basic-roundtrip`
seguem pendentes. Esta tag alpha e um snapshot de laboratorio, nao um fecho de
etapa.

## Mudancas entregues — CapyOS (in-tree)

- Gates host-testaveis `apps-roundtrip` e `capybrowse-text`: scaffolding do
  smoke, seam de I/O, stubs e testes de gate ligados em `tests/test_runner.c`.
- Binario userland `capybrowse` e operacoes de string da capylibc.
- Ajustes de diagnostico em `capy_net` / `capy_tls`.
- `make test`, `make layout-audit` e `make version-audit` verdes no host.

## Compliance de versoes (este release e o pivot da matriz)

| Repo | De | Para | Observacao (aditivo, sem quebra de ABI) |
|---|---|---|---|
| **CapyOS** | `alpha.265+20260611` | `alpha.266+20260617` | Consolidacao cross-repo + Etapa 6 in-tree |
| **CapyUI** | `2.22.0` | `2.22.1` | `capy_widget_type_name` (helper puro; ABI `capy-ui-widget` v2.22 inalterada) |
| **CapyCodecs** | `0.0.7` | `0.0.8` | `capy_image_format_name` + flag `CAPY_IMAGE_FEATURE_FORMAT_NAME` |
| **CapyAgent** | `0.0.7` | `0.0.8` | `capy_manifest_emit` rejeita dependencia duplicada (`CAPY_MANIFEST_DEPENDS_INVALID`) |
| **CapyBrowser** | `0.6.0` | `0.6.1` | refs numericas WHATWG NULL/surrogate/>0x10FFFF -> U+FFFD (`ENTITY_INVALID`); `<pre>` preserva whitespace; marcadores de lista `-`/`N.` |
| **CapyLang** | `0.1.8` | `0.1.9` | opcodes aditivos `array_push`/`array_pop` (0x67-0x68) + `array_insert`/`array_remove` (0x69-0x6A); 43 opcodes congelados; frontend S10 (`a.push`/`a.pop`/`a.insert`/`a.remove`/`a.get`/`a.set`/`a.len`, E0022) |
| **CapyBenchmark** | `0.0.7` | `0.0.8` | read-side parse trilogy `replay`/`report`/`evaluation` (round-trip + fail-closed) |

## Escopo que continua pendente

- Etapa 6 aberta: `make smoke-x64-vmware-capybrowse-text` e
  `make smoke-x64-vmware-apps-basic-roundtrip` ainda nao executados/aprovados.
- Assinatura Ed25519 (CapyAgent) segue como P0: publishing assinado permanece
  fail-closed (`CAPYPKG_ERR_SIGNATURE`) ate o signer ser publicado e registrado
  via `capypkg_set_signature_verifier`.

## Evidencias / validacao

Validacoes locais executadas no host (`Automation/remote-exec.sh`), exit=0:

- Cada irmao: `make validate` (CapyUI/CapyAgent/CapyBrowser/CapyCodecs/CapyBenchmark)
  e `make rust-validate` (CapyLang) apos o bump de versao.
- `CapyOS`: `make test`, `make layout-audit`, `make version-audit`.

Gate de runtime futuro (externo, VMware + UEFI + E1000):

- `make smoke-x64-vmware-capybrowse-text`
- `make smoke-x64-vmware-apps-basic-roundtrip`
