# CapyOS 0.8.0-alpha.243+20260520

Release alpha `0.8.0-alpha.243+20260520` que corrige o caminho remoto de
bootstrap dos componentes, remove warnings do build x64 e valida a ISO de
instalacao com reboot e persistencia.

## Destaques

- **HTTP remoto resiliente.** O cliente HTTP deixa de esperar EOF em redirects
  bodyless e o bootstrap de modulos passa a concluir o download do indice sem
  cair em `HTTP_ERR_RECV`.
- **Build limpo no caminho critico.** Os warnings visiveis em `all64` e
  `iso-uefi` foram removidos nos pontos de origem.
- **Smoke de instalacao validado.** A ISO oficial passa pelo fluxo completo de
  instalacao, primeiro boot, marker de persistencia e segundo boot.

## Mudancas relevantes

- `src/net/services/http/request_response.c`: encerra a leitura quando o
  response e um redirect sem corpo.
- `include/services/capypkg_bootstrap.h`: comentario de progresso sem sintaxe
  ambigua.
- `src/security/crypt_aes_xts.c` e `src/security/volume_provider_rekey_execute.c`:
  limpeza de warnings no caminho de release.

## Validacao

- `make clean all64 iso-uefi`
- `make smoke-x64-iso TOOLCHAIN64=host`
- `make test`

