# Capybrowser legacy (arquivado em 2026-05-05, sessao 6)

Este diretorio contem documentacao do **navegador legacy do CapyOS**
("capybrowser" + "browser_chrome" + "browser_app" + "capyhtml") que
foi totalmente erradicado do sistema na sessao 6. O navegador nao
amadurecia o suficiente para a web real (sem JavaScript, CSS minimo,
sem video/audio decode, sem GPU 3D), entao foi removido para abrir
caminho para um port do Firefox open source em medio prazo.

## Documentos arquivados

- `browser-ipc.md` -- protocolo IPC entre o engine ring 3
  (`capybrowser`) e o chrome ring 0 (`browser_chrome`). Substituido
  no futuro pelo IPC do Firefox (Mozilla IPDL ou similar).
- `f3-3c-html-viewer-userland-slicing.md` -- plano de fatiar o HTML
  viewer em uma lib `capyhtml` ring 3.
- `f3-3f-browser-desktop-wiring.md` -- plano de integrar o navegador
  com a janela do compositor.

## Por que arquivamos em vez de deletar

Esses documentos contem decisoes de arquitetura (protocolo de pipes
para IPC, lifecycle de spawn/teardown, estrategia de rendering) que
podem informar o port do Firefox -- particularmente o capitulo de
sandbox (Firefox usa um modelo multi-process com IPDL) e o capitulo
de janela compositor (Firefox renderiza dentro de uma surface
opaca controlada pelo desktop).

## Ponteiro para o roadmap atual

Ver `docs/plans/active/firefox-port-roadmap.md` para o plano de
substituicao em 7 fases (~36-60 meses).
