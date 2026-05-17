# Third-party UI assets

CapyOS keeps only the small assets selected for the current UI and converts
the runtime desktop/cursor drawing into static C masks. Full upstream icon
packs are not vendored in the repository.

## Tabler Icons

- Use: primary desktop file/folder, window-control and launcher icon reference.
- Imported files:
  - `assets/third_party/icons/tabler/folder.svg`
  - `assets/third_party/icons/tabler/file.svg`
  - `assets/third_party/icons/tabler/x.svg`
  - `assets/third_party/icons/tabler/square.svg`
  - `assets/third_party/icons/tabler/minus.svg`
  - `assets/third_party/icons/tabler/layout-grid.svg`
  - `assets/third_party/icons/tabler/power.svg`
  - `assets/third_party/icons/tabler/logout.svg`
  - `assets/third_party/icons/tabler/arrow-left.svg`
  - `assets/third_party/icons/tabler/arrow-up.svg`
  - `assets/third_party/icons/tabler/file-plus.svg`
  - `assets/third_party/icons/tabler/folder-plus.svg`
  - `assets/third_party/icons/tabler/refresh.svg`
  - `assets/third_party/icons/tabler/trash.svg`
  - `assets/third_party/icons/tabler/terminal-2.svg`
  - `assets/third_party/icons/tabler/calculator.svg`
  - `assets/third_party/icons/tabler/notes.svg`
  - `assets/third_party/icons/tabler/settings.svg`
  - `assets/third_party/icons/tabler/list-check.svg`
- Upstream: `https://github.com/tabler/tabler-icons`
- License: MIT. The license text is stored at
  `assets/third_party/icons/tabler/LICENSE`.

## Material Symbols

- Use: complementary system/document shape reference.
- Imported files:
  - `assets/third_party/icons/material-symbols/folder_24px.svg`
  - `assets/third_party/icons/material-symbols/draft_24px.svg`
  - `assets/third_party/icons/material-symbols/apps_24px.svg`
  - `assets/third_party/icons/material-symbols/close_24px.svg`
- Upstream: `https://github.com/google/material-design-icons`
- License: Apache License 2.0. The license text is stored at
  `assets/third_party/icons/material-symbols/LICENSE`.

The full `google/material-design-icons` checkout has millions of generated
files, so CapyOS imports selected SVGs by raw upstream URL instead of vendoring
the repository tree.

## X.Org xcursor-themes

- Use: cursor silhouette reference, using the Whiteglass 16 px cursors as the
  closest match to the compositor's static cursor masks.
- Imported files:
  - `assets/third_party/cursors/xorg-whiteglass/left_ptr-16.png`
  - `assets/third_party/cursors/xorg-whiteglass/xterm-16.png`
  - `assets/third_party/cursors/xorg-whiteglass/sb_h_double_arrow-16.png`
  - `assets/third_party/cursors/xorg-whiteglass/sb_v_double_arrow-16.png`
  - `assets/third_party/cursors/xorg-whiteglass/watch-16.png`
- Upstream release archive:
  `https://xorg.freedesktop.org/archive/individual/data/xcursor-themes-1.0.7.tar.gz`
- License: X11. The license text is stored at
  `assets/third_party/cursors/xorg-whiteglass/COPYING`.
