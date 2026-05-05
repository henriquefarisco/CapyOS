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
|---|---|---|
| `v1` | `0.8.0-alpha.1` ate `0.8.0-alpha.4` | Capturas oficiais disponiveis |
| `v2` | `0.8.0-alpha.5` / `0.8.0-alpha.6` | Captura pendente apos smoke visual |

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
