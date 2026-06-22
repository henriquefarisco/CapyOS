# CapyOS 0.8.0-alpha.269+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.269+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Correcao de ordem de bytes no connect TCP + gate QEMU capybrowse (Etapa 6)

## Resumo executivo

Corrige a ordem de bytes do endereco de destino no caminho de connect TCP do
kernel -- um connect literal para `10.0.2.2` chegava ao fio como `2.2.0.10`
(endereco trocado duas vezes). Com o fix, o gate de feedback de dev
`smoke-x64-qemu-capybrowse-text` passa ponta-a-ponta (handshake TCP + roundtrip
HTTP). **A Etapa 6 NAO esta fechada**: os gates externos VMware
`smoke-x64-vmware-capybrowse-text` e `smoke-x64-vmware-apps-basic-roundtrip`
seguem pendentes (QEMU = feedback de dev; VMware = aceite oficial).

## Root cause + fix

- **Root cause (`src/net/services/socket.c`):** `socket_connect` convertia
  apenas a porta de network para host order (`ntohs`, convencao BSD
  `sockaddr_in`) antes de chamar `tcp_open`, mas passava `sin_addr` direto.
  O userland (`capy_tcp_connect_ip4`) grava `sin_addr` em network order
  (`htonl`) e a camada IPv4 (`stack_ipv4`) troca os bytes do destino de novo no
  envio (`htonl`), entao o endereco era efetivamente trocado duas vezes: um
  connect literal para `10.0.2.2` saia no fio como `2.2.0.10`. Tanto
  `capy_inet_pton4` quanto o resolver DNS do kernel (`dns.c`, `read_be32`)
  retornam host order, entao o bug afetava literais e DNS igualmente no caminho
  de socket.
- **Fix (`src/net/services/socket.c`):** byte-swap de `sin_addr` simetrico ao de
  `sin_port`, para `tcp_open` receber o destino em host order que espera.

## Gate de feedback de dev (novo)

- **`smoke-x64-qemu-capybrowse-text`:** boot do full profile sob QEMU+OVMF com
  NIC E1000 em SLIRP user networking, dirigindo o `capybrowse-text` in-tree
  contra um endpoint HTTP local hermetico no gateway SLIRP
  (`http://10.0.2.2:18080/`). Literal de IP sobre HTTP puro mantem o gate
  autocontido: sem DNS (o resolver CapyOS ainda e cache-only) e sem TLS.
- **Captura pcap opcional** (`filter-dump`) nos smokes em rede, gravada em
  `build/ci/qemu_net.pcap` -- foi instrumental no diagnostico desta correcao.

## Validacao (host, QEMU+OVMF)

- `make test` -- verde (sem regressao; o connect test usa `8.8.8.8`, palindromo
  de bytes, que nao distingue a ordem).
- `make smoke-x64-qemu-capybrowse-text` -- PASSA: pcap confirma o `SYN` do guest
  mirando `10.0.2.2`, three-way handshake + roundtrip HTTP, marker
  `[smoke] capybrowse-text ready`.
- `make version-audit` -- verde.

## Escopo pendente

- Gates externos VMware da Etapa 6 (`smoke-x64-vmware-capybrowse-text` +
  `smoke-x64-vmware-apps-basic-roundtrip`) seguem como aceite oficial.
- Resolver DNS ativo (hoje cache-only) para navegacao por hostname real.
- Assinatura Ed25519 (CapyAgent) segue P0.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.268+20260617` | `alpha.269+20260617` | Fix de ordem de bytes no connect TCP + gate QEMU capybrowse (Etapa 6) |

Sem mudanca de ABI base do kernel nem de contrato cross-repo; os 6 repos irmaos
permanecem nas versoes de `alpha.266` (sem bump).

_Build: `0.8.0-alpha.269+20260617`_
