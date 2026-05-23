# Cross-repo compatibility audit — 2026-05-23

**Status:** correcao da esteira do deploy `alpha.257` e pinagem vigente para
`alpha.258`.
**Snapshot anterior:** [`compatibility-audit-2026-05-22.md`](compatibility-audit-2026-05-22.md).
**Matriz autoritativa:** [`compatibility-matrix.md`](compatibility-matrix.md).

## Resumo

O deploy `alpha.257` quebrou no GitHub Actions porque os workflows do CapyOS
clonam o sibling `CapyUI` a partir de `main` para builds de `main` e tags.
Naquele momento, `CapyUI/main` ainda apontava para `2.13.0`, sem
`src/desktop/taskbar_display_list.c`, enquanto o Makefile do CapyOS ja
esperava gerar `build/x86_64/capyui-desktop/taskbar_display_list.o`.

Correcoes aplicadas:

- `CapyUI/main` foi fast-forwardado para `v2.13.1`, alinhando `main` e
  `develop` no commit que contem os sources display-list.
- CapyOS passa a declarar `CapyUI` `2.13.1` como versao corrente, minima e
  maxima testada para a Etapa 4.
- A nova release CapyOS `alpha.258` evita reutilizar a tag `alpha.257`, que ja
  possui runs quebrados registrados.

## Estado coordenado

| Repo | Ref esperada no CI | Versao | Observacao |
|---|---|---|---|
| `CapyOS` | `main`, `develop`, `v0.8.0-alpha.258+20260523` | `0.8.0-alpha.258+20260523` | release corrigida |
| `CapyUI` | `main` e `develop` | `2.13.1` | contem `taskbar_display_list.c` e fontes display-list relacionados |

## Gate afetado

Falha observada no release anterior:

```text
make[1]: *** No rule to make target 'build/x86_64/capyui-desktop/taskbar_display_list.o', needed by 'build/capyos64.bin'. Stop.
```

Esse erro e resolvido quando o checkout remoto de `CapyUI` contem os fontes
publicados em `2.13.1`.
