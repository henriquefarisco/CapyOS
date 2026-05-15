# Screenshots do CapyOS

Os screenshots oficiais agora seguem a versao da interface, nao a versao
da release. Uma nova pasta so deve ser criada quando houver mudanca visual
ou de layout que justifique comparar a UI.

## Regra

- colecoes visuais ficam em `docs/screenshots/CapyUI/<versao-ui>/`
- releases sem mudanca visual apontam para a mesma versao de CapyUI
- releases com mudanca visual aguardam uma nova captura oficial antes de
  trocar o ponteiro do README raiz
- pastas por release (`0.8.0-alpha.X/`) nao devem ser recriadas para evitar
  duplicacao de PNGs identicos

## Versoes de interface

| CapyUI | Releases cobertas | Status |
| --- | --- | --- |
| `v1` | `0.8.0-alpha.1` ate `0.8.0-alpha.4` | Capturas oficiais |
| `v2` | `0.8.0-alpha.5` ate `0.8.0-alpha.103` | Pendente |
| `v3` | `0.8.0-alpha.104` ate `0.8.0-alpha.112` | Pendente — mudanca visual aguardando captura oficial |
| `v4` | `0.8.0-alpha.113` ate `0.8.0-alpha.218` | Pendente — Start Menu com scroll, toolbar revisada, DnD e preview de loginwindow aguardam captura oficial |
| `v1.1` | `0.8.0-alpha.238` | Capturas oficiais — desktop com Settings, Task Manager, Text Editor, File Manager e terminal |

## Estrutura atual

- `CapyUI/v1/`
  - `login-system.png`
  - `bootstage1-iso.png`
  - `bootstage1-iso-config1.png`
  - `bootstage1-iso-config2.png`
  - `desktop-browser1.png`
  - `desktop-browser2.png`
  - `desktop-apps.png`
  - `desktop-terminal-dhcp.png`
  - `desktop-version.png`
- `CapyUI/v1.1/`
  - `desktop-version1.png`
  - `desktop-version.png`
  - `desktop-terminal.png`
