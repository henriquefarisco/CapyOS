# CapyOS 0.8.0-alpha.244+20260520

Release alpha `0.8.0-alpha.244+20260520` que fecha a instalacao remota dos
modulos CapyUI via GitHub Release depois da instalacao da ISO.

## Destaques

- **Download remoto robusto.** O cliente HTTP/HTTPS preserva ponteiros de body
  apos crescimento do buffer, rejeita respostas truncadas e usa requisicoes
  `Connection: close` com `Accept-Encoding: identity`.
- **Payload grande instalavel.** `capypkg` grava payloads verificados em partes
  quando o limite atual de arquivo do CAPYFS e menor que o asset publicado.
- **Ativacao do desktop corrigida.** O instalador grava o marker
  `/var/capypkg/<module>/installed`, usado pelo gate de modulos no boot
  seguinte.
- **Wizard sem congelamento.** O prompt de falha persistente dos modulos tem
  fallback temporizado e nao bloqueia a tela azul sem progresso.

## Mudancas relevantes

- `src/net/services/http/request_response.c`: realinha `body_start` depois de
  realocar o buffer de resposta e falha em EOF prematuro.
- `src/security/tls.c` e `src/net/protocols/tcp.c`: endurecem drenagem TLS,
  ACK/window e validacao de checksum no caminho de download.
- `src/services/capypkg/capypkg_install.c`: adiciona fallback
  `payload.partNN` e marker `installed`.
- `src/config/first_boot/modules.c`: fixa o indice em CapyUI `v0.7.3` e evita
  bloqueio indefinido em retry/cancelamento.
- `tools/scripts/smoke_x64_iso_install.py`: cobre perfil `full`, reboot
  planejado e validacao de desktop no segundo boot.

## Validacao

- `make test-capypkg`
- `git diff --check`
- `make clean all64 iso-uefi TOOLCHAIN64=host`
- `python3 tools/scripts/smoke_x64_iso_install.py --module-profile full`
