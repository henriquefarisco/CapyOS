# CapyOS 0.8.0-alpha.256+20260522

**Data:** 2026-05-22
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.256+20260522`
**Plataforma oficial:** VMware + UEFI + E1000
**Escopo:** instalacao remota de modulos via GitHub Release + sincronizacao inicial da Fase A da Etapa 4 com CapyUI

## Resumo

Esta release corrige o fluxo de instalacao full/custom de componentes
remotos apos a ISO. O problema real era duplo: o HTTP runtime ainda
limitava responses a 1 MiB e o capypkg tambem tinha historico de buffer
menor que o contrato de pacote. O asset
`org.capyos.ui.desktop-session.bin` publicado tem mais de 1 MiB, entao
o sistema retornava `rc=-6 fetch-failed` nesse pacote.

Ela tambem retoma a Fase A correta da Etapa 4: o CapyOS passa a
sincronizar a matriz para `CapyUI` `2.13.0` / `capy-ui-widget` v2.13
e adiciona um adapter inicial para consumir o display-list schema v7
real do sister, em vez de declarar uma ABI paralela.

## Mudancas

- `HTTP_MAX_RESPONSE_SIZE` agora cobre o contrato `CAPYPKG_PAYLOAD_MAX`
  de 8 MiB.
- `capypkg_install` usa buffer temporario de ate 8 MiB no kernel e
  continua validando SHA-256 antes de gravar o pacote.
- Falha em um pacote nao aborta o sweep inteiro; os pacotes validos
  continuam instalando e o wizard ativa o que foi instalado.
- Logs do wizard mostram `rc` e label legivel, como `fetch-failed`.
- Perfil `Personalizado` ganhou checklist com `[x]` para os pacotes
  oficiais, mantendo todos selecionados por padrao.
- `modules-index.txt` passa a emitir `official=1` para o catalogo
  gerado pelo agregador.
- Smokes ISO cobrem os perfis `full` e `custom` contra o indice de
  producao publicado no GitHub Release.
- `include/gui/capyui_display_adapter.h` e
  `src/gui/widgets/capyui_display_adapter.c` consomem
  `CapyUI/src/widget/capy_display_list.h` quando o sibling existe e
  falham fechado como indisponiveis quando ele nao existe.
- `docs/reference/integration/compatibility-matrix.md` pina `CapyUI`
  em `2.13.0` e `capy-ui-widget` v2.13/schema v7.
- `docs/reference/integration/compatibility-audit-2026-05-22.md`
  registra o snapshot cross-repo da Fase A corrigida.

## Validacao

- `git diff --check`
- `make version-audit`
- `make layout-audit`
- `make test`
- `make clean all64 iso-uefi verify-release-checksums TOOLCHAIN64=host`
- varredura do log do build sem `warning:` nem `error:`
- `python3 tools/scripts/smoke_x64_iso_install.py --module-profile full --modules-index-url https://github.com/henriquefarisco/CapyUI/releases/download/v2.13.0/modules-index.txt --step-timeout 45`
- `python3 tools/scripts/smoke_x64_iso_install.py --module-profile custom --modules-index-url https://github.com/henriquefarisco/CapyUI/releases/download/v2.13.0/modules-index.txt --step-timeout 45`

Para a fatia adicional do adapter CapyUI, validacao externa recomendada:

- `make layout-audit`
- `make test`
- `make all64`

Esses gates do adapter nao foram executados nesta maquina de edicao.
