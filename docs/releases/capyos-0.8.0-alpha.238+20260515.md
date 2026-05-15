# CapyOS 0.8.0-alpha.238+20260515

`0.8.0-alpha.238+20260515` publica a colecao visual CapyUI v1.1 no README
principal e limpa um artefato visual do desktop.

## Entregas

- README raiz passa a apontar os novos prints oficiais em
  `docs/screenshots/CapyUI/v1.1/`.
- Catalogo de screenshots registra `CapyUI/v1.1` como colecao oficial da
  release.
- Wallpaper do desktop deixa de desenhar a marca retangular decorativa no canto
  inferior direito, que no uso real parecia um popup vazio.
- `.gitignore` bloqueia workspaces locais, logs e temporarios para evitar que
  arquivos operacionais entrem na release.

## Validacao esperada

- `make test`
- `make layout-audit`
- `make version-audit`
- `make all64`
