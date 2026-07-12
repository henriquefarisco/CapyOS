# CapyOS 0.8.0-alpha.313+20260712

**Data:** 2026-07-12
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.313+20260712`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fix de gate de publicação

## Resumo executivo

Corrige o falso negativo observado apenas no runner Linux depois que o ISO da
`alpha.312` já havia sido compilado. A implementação do browser permanece a
mesma; a `alpha.313` torna o gate remoto reproduzível sem reatribuir a tag
imutável anterior.

## Mudancas

- `tools/scripts/verify_release_siblings.sh`: troca pipelines
  `awk | grep -q`, sujeitos a SIGPIPE sob `set -o pipefail`, por expressões
  diretas sobre a tabela de símbolos completa.
- Atualiza a versão do CapyOS e os pins do `modules-index.txt` para a tag
  `v0.8.0-alpha.313+20260712`.
- Mantém os componentes pinados: CapyBrowser `0.6.7`, CapyUI `2.24.0`, CapyAI
  `0.1.0`, CapyAgent `0.0.10`, CapyCodecs `0.0.12`, CapyLang `0.1.12` e
  CapyBenchmark `0.0.11`.

## Validacao

- `make release-check TOOLCHAIN64=elf` -- verde em build limpo.
- `make layout-audit` / `make version-audit` -- verdes.
- `verify_release_siblings.sh linked` -- símbolos do Browser, Codecs e CapyAI
  confirmados sem falso negativo sob `pipefail`.
- Smokes QEMU do site estático e da sessão CapyAI assíncrona -- verdes.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.312+20260712` | `0.8.0-alpha.313+20260712` | Gate remoto corrigido; runtime/ABI inalterados |

Sem mudança de ABI. Os sete repositórios irmãos permanecem nos mesmos pins.

_Build: `0.8.0-alpha.313+20260712`_
