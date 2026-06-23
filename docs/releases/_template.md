# CapyOS {VERSION}

**Data:** {HDATE}
**Canal:** alpha (experimental)
**Versao:** `{VERSION}`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** <preencher: slice / fix / hardening / release de coordenacao>

## Resumo executivo

{SUMMARY}

## Mudancas

- <preencher: o que mudou, por arquivo/area>

## Validacao

- `make test` -- <resultado>
- `make layout-audit` / `make version-audit` -- <resultado>
- `make all64` (+ gates/QEMU/VMware aplicaveis) -- <resultado>

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `<anterior>` | `{VERSION}` | <observacao> |

<Sem mudanca de ABI / detalhar coordenacao cross-repo se houver. Os 6 repos
irmaos permanecem inalterados, salvo nota acima.>

_Build: `{VERSION}`_
