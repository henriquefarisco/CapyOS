# Screenshots 0.8.0-alpha.5

> **Pendente captura.** Os PNGs oficiais desta versao serao adicionados
> apos validacao em CI dos 6 smokes M5 e da branch ser mergeada.
>
> Enquanto isso, o `README.md` raiz do repositorio ainda referencia esta
> versao para fins de auditoria de manifesto (`make version-audit`).

## Lista esperada

- `login-system.png` - tela de login pos-boot.
- `bootstage1-iso.png` - estagio inicial de provisionamento via ISO.
- `bootstage1-iso-config1.png` / `bootstage1-iso-config2.png` -
  configuracao no primeiro boot.
- `desktop-browser1.png` / `desktop-browser2.png` - desktop com
  navegador renderizando pagina, agora com timeout de 30s e cancel
  cooperativo.
- `desktop-apps.png` - desktop com varios apps abertos (terminal,
  task manager mostrando processos userland reais).
- `desktop-terminal-dhcp.png` - terminal demonstrando DHCP automatico.
- `desktop-version.png` - tela "Sobre" com versao 0.8.0-alpha.5.

## Novidades visuais nesta versao

- **capysh ring 3** rodando em terminal grafico do desktop com prompt
  proprio (banner CapyOS + builtins listados).
- **task manager** com auto-refresh visivel: lancar app no desktop e
  ver aparecer na lista em ate ~0.5s; botao Kill funcional.
- **browser** que cancela carregamento longo automaticamente apos 30s
  com mensagem de timeout, sem congelar o desktop.
