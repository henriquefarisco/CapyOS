# CapyOS 0.8.0-alpha.286+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.286+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** fix (critico — rede / instalacao de modulos)

## Resumo executivo

alpha.286+20260617: fix CRITICO de rede que impedia a instalacao completa de baixar QUALQUER modulo. ROOT CAUSE: http_transport_connect (src/net/services/http/transport.c) atribuia addr.sin_addr = ip com o IP resolvido por DNS em ordem CANONICA/host (NET_IPV4_ADDR e net_dns_parse_first_a usam read_be32; 140.82.121.3 -> 0x8C527903) SEM o swap host->rede, enquanto socket_connect documenta sin_addr como network-order e aplica um byteswap incondicional antes de tcp_open. Resultado: o endereco de destino saia com os 4 bytes INVERTIDOS na rede (github.com 140.82.121.3 -> SYN para 3.121.82.140; release-assets 44.230.85.241 -> 241.85.230.44), 0 SYN-ACK, e TODO http_get/http_download por hostname (com ou sem TLS) falhava com HTTP_ERR_CONNECT (connection failed, tls=init). Efeito para o usuario: o wizard de first-boot (profile=full/custom) repetia 'Index download failed (rc=-6) connection failed tls=init' e NENHUM dos 7 modulos oficiais (ui.widget-core, ui.desktop-session, browser.core, codecs.image-basic, agent.core, lang.runtime, benchmark.harness) instalava; a base do sistema instalava normalmente. REGRESSAO: a porta ja era convertida host->rede na linha acima (sin_port), mas o swap de sin_addr foi omitido; o byteswap incondicional adicionado em socket_connect (para corrigir connect por IP literal, ex 10.0.2.2) passou a inverter o endereco vindo do DNS. DNS, ARP, gateway, DHCP e o store de 146 trust anchors TLS estavam corretos -- apenas o endereco de destino do connect estava invertido. FIX (1 linha + comentario): converter o ip canonico para network-order em transport.c, espelhando o swap de sin_port, de modo que o byteswap de socket_connect o traga de volta a host-order para tcp_open. REPRODUZIDO e CONFIRMADO em QEMU (instalacao ISO completa real sobre rede SLIRP com NAT de saida): ANTES 36 SYN / 0 SYN-ACK (SYN para IPs invertidos); DEPOIS 18 SYN / 18 SYN-ACK, ~4 MB baixados, IPs corretos (185.199.110.133 etc), 'Completed: 7  Failed: 0' -> 'Install complete. Reboot to activate.'. Validado: make test verde; all64 relink limpo. Sem mudanca de ABI; sem bump de repo irmao; o pin de modulos (CapyUI v2.13.0) e o indice agregado dos 7 modulos estavam integros no GitHub -- nao eram a causa. Lacuna de teste registrada: smoke-x64-iso usa profile=basic + SLIRP restrict=on (sem egress), entao o download real de modulos nao era exercido pela CI; a reproducao networked-full-install que pegou o bug fica documentada como gate manual. Follow-up de robustez (nao-bloqueante) mapeado no plano mestre: resolucao por compatibilidade no momento do publish (indice assinado enderecado por token de ABI + guarda anti-drift) para eliminar pins manuais de versao.

## Mudancas

- **`src/net/services/http/transport.c`** (a correcao): em `http_transport_connect`, o IP de destino resolvido (host/canonical order, ex. `0x8C527903` = 140.82.121.3) passa a ser convertido para **network byte order** antes de ser gravado em `sockaddr_in.sin_addr`, espelhando o swap de `sin_port` da linha imediatamente acima. `socket_connect` aplica o `ntohl` inverso e entrega host-order a `tcp_open`, entao o endereco chega correto na rede. Antes, sem o swap, o destino saia com os 4 bytes invertidos e o TCP SYN ia para um IP morto -> `HTTP_ERR_CONNECT`.
- **Bump de versao** (via `make bump-alpha`): `VERSION.yaml`, `include/core/version.h`, `README.md`, `docs/plans/STATUS.md` (linha de versao) -> `0.8.0-alpha.286+20260617`.
- **`docs/plans/STATUS.md`**: nota do fix + a lacuna de teste do download de modulos.
- **`docs/plans/active/capyos-master-plan.md`**: item de follow-up de robustez (resolucao por compatibilidade no publish), nao-bloqueante.

## Validacao

- `make test` -- **verde** ("Todos os testes passaram").
- `make version-audit` -- **ok** (`current=0.8.0-alpha.286 extended=0.8.0-alpha.286+20260617`). `make layout-audit` -- N/A (sem mudanca de layout; apenas edicao de arquivo existente).
- `make all64` -- relink limpo. **QEMU (dev):** instalacao ISO completa real sobre rede SLIRP confirmou os 7 modulos baixando+instalando (18/18 SYN-ACK, `Completed: 7  Failed: 0`, "Install complete"). **VMware + UEFI + E1000** continua sendo o gate oficial de aceitacao (operador).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.285+20260617` | `0.8.0-alpha.286+20260617` | Fix de runtime; sem mudanca de ABI. |

Sem mudanca de ABI. Os 6 repos irmaos permanecem inalterados (o pin de modulos
CapyUI `v2.13.0` e o indice agregado dos 7 modulos no GitHub estavam integros e
nao foram a causa; nenhum bump de irmao foi necessario).

_Build: `0.8.0-alpha.286+20260617`_
