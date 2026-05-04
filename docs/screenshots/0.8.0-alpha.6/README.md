# Screenshots 0.8.0-alpha.6

> **Pendente captura.** Os PNGs oficiais desta versao serao adicionados
> apos validacao em CI da Etapa 2 do roteiro do browser (markers
> debugcon `[O][1][2][3][4][5][K][h][H][F]` em sequencia + `[D]` no
> Task Manager kill).
>
> Enquanto isso, o `README.md` raiz do repositorio referencia esta
> versao para fins de auditoria de manifesto (`make version-audit`).

## Lista esperada

- `login-system.png` - tela de login pos-boot.
- `bootstage1-iso.png` - estagio inicial de provisionamento via ISO.
- `bootstage1-iso-config1.png` / `bootstage1-iso-config2.png` -
  configuracao no primeiro boot.
- `desktop-browser1.png` / `desktop-browser2.png` - desktop com
  navegador 480x384 (Etapa 2 seçao d) renderizando welcome page real.
- `desktop-apps.png` - desktop com varios apps abertos (terminal,
  task manager mostrando processos userland reais).
- `desktop-terminal-dhcp.png` - terminal demonstrando DHCP automatico.
- `desktop-version.png` - tela "Sobre" com versao 0.8.0-alpha.6.
- `task-manager-kill-browser.png` - Task Manager Kill funcionando no
  engine do browser; janela fecha em ~1 frame (Etapa 2 seçoes a-c).

## Novidades visuais nesta versao

- **Janela do browser 480x384 px** (era 192x148; Etapa 2 seçao d) com
  framebuffer 480x360 BGRA renderizando welcome page legivel pela
  primeira vez.
- **Task Manager Kill** agora encerra o engine do browser com fechamento
  da janela em <=1 frame (Etapa 2 seçoes a-c).
- **Pipe kernel 64 KiB** acelera EVENT_FRAME 16x face ao buffer de
  4 KiB anterior (Etapa 2 seçao e).
- **Logs do engine ring-3** agora aparecem no debugcon do operador
  via outb 0xE9 (Etapa 2 seçao f).
