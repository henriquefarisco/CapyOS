# CapyOS 0.8.0-alpha.242+20260520

Release alpha `0.8.0-alpha.242+20260520` que corrige o download do indice
de modulos no caminho remoto de primeiro boot e preserva os bytes rejeitados
em staging para diagnostico.

## Destaques

- **Redirect GitHub Release tratado de forma integral.** O HTTP layer passa a
  guardar `Location` completo, cresce os buffers de URL/path e evita truncar
  o ultimo hop de `releases/latest/download/`.
- **Staging de capypkg.** `capypkg_install` grava o payload em
  `/var/capypkg/updates/<nome>.bin` antes da validacao e mantem o arquivo em
  caso de falha para inspeccao local.
- **Bootstrap remoto mais diagnostico.** O fluxo de primeiro boot volta a
  conseguir seguir o indice `modules-index.txt` do canal `latest` sem cair em
  `HTTP_ERR_RECV` quando o asset final vem com SAS/JWT longo.

## Mudancas relevantes

- `include/net/http.h`: `http_response` ganha campo `location` dedicado.
- `src/net/services/http/prelude_headers_encoding.c`: copia o `Location`
  completo, nao apenas o valor truncado em headers genericos.
- `src/net/services/http/redirect_download.c`: usa `resp->location` como
  target primario do redirect.
- `src/services/capypkg/capypkg_install.c`: staging em
  `/var/capypkg/updates` antes do verify/commit.
- `tools/scripts/smoke_x64_boot.py`: valida o redirect do indice e o
  comportamento esperado do manifesto sem assinatura valida.

## Validacao

- `make test`
- `make smoke-x64-iso TOOLCHAIN64=host`
- `make version-audit`
