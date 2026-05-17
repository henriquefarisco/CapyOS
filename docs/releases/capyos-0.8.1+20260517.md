# CapyOS 0.8.1+20260517

`0.8.1+20260517` promove a trilha atual para stable com foco em acabamento
visual, consistencia de licencas dos assets de UI e correcoes do File Manager.

## Entregas

- CapyUI passa a usar assets de referencia de Tabler Icons, Material Symbols e
  X.Org xcursor-themes, com origem e licencas documentadas em
  `docs/legal/third-party-ui-assets.md`.
- Desktop, menu iniciar, botoes de sessao e botoes das janelas recebem icones
  mais coerentes e com melhor contraste.
- File Manager separa o path em uma linha propria acima da toolbar e troca os
  botoes textuais por controles icon-only.
- Acoes do File Manager disparam uma unica vez por clique, sem repetir no
  `mouse up` ou durante movimento com o botao pressionado.
- Exclusao de pastas pelo File Manager passa a usar `vfs_rmdir_recursive`,
  alinhando o comportamento com desktop e terminal.
- Compositor preserva a area do cursor antigo ao trocar o tipo de cursor,
  reduzindo rastro visual em bordas de janelas.

## Validacao esperada

- `make test`
- `make layout-audit`
- `make version-audit`
- `make boot-perf-baseline-selftest`
- `make all64 TOOLCHAIN64=host`
- `make iso-uefi TOOLCHAIN64=host`
- `make verify-release-checksums TOOLCHAIN64=host`
