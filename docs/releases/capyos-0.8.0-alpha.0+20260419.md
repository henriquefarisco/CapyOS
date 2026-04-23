# CapyOS 0.8.0-alpha.0+20260419

Data: 2026-04-19
Canal: develop / main
Base: continuacao da trilha `UEFI/GPT/x86_64`

## Destaques

- pilha TCP/IP corrigida: `net-fetch` HTTP e HTTPS agora funcionam de fato
- navegador HTML interno passa a abrir paginas reais (texto) sem travar o desktop
- `hey`, `net-resolve` e `net-fetch` ganharam diagnostico mais util em falhas
- mouse e desktop continuam responsivos durante operacoes de rede bloqueantes

## Correcoes criticas

### TCP

- **checksum do pseudo-header corrigido** em `src/net/protocols/tcp.c`
  - antes, o codigo somava `tcp_htons(6)` (= 0x0600 = 1536) e
    `tcp_htons(tcp_len)` ao acumulador, valores que **nao** correspondem ao que
    o laco em bytes calcula sobre o pseudo-header real
  - resultado: todo segmento TCP saia com checksum invalido; SYN era enviado,
    mas o servidor respondia com SYN-ACK contra outro endereco ou ignorava
    silenciosamente. ICMP nao era afetado porque usa outra rotina
  - correcao: somar os valores ja em ordem de rede (`sum += 6;` e
    `sum += (uint16_t)tcp_len;`)
- **dispatch correto para IPv4 -> TCP** em `src/net/protocols/stack_ipv4.c`
  - `handle_tcp` deixou de descartar o payload e passou a chamar
    `tcp_receive_segment(src_ip, dst_ip, ...)`, alimentando o estado da conexao
- **tratamento explicito de RST** em `tcp_receive_segment`
  - antes, um RST do servidor deixava a conexao em `SYN_SENT` ate o timeout
    completo (5 s), travando o `connect()`
  - agora, o flag RST fecha imediatamente a conexao e incrementa
    `tcp_global_stats.resets`
- **buffer de recepcao compactado quando preciso**
  - novos segmentos eram escritos em `recv_buf + recv_len` mesmo quando dados
    nao lidos viviam em `recv_buf + recv_head`; o write sobrepunha dados nao
    lidos e quebrava o handshake TLS
  - agora, quando `recv_head + recv_len + data_len > recv_cap`, os dados
    pendentes sao movidos para o inicio do buffer antes da escrita
- **retransmissao de SYN com backoff exponencial**
  - novo `tcp_retransmit_syn(conn_id)` em `tcp.c`
  - `socket_wait_for_tcp_state` agora retransmite SYN em 1 s, 2 s, 4 s, ...
    enquanto a conexao continua em `SYN_SENT`, ate `TCP_MAX_RETRIES`

### Sockets

- **byte order da porta remota corrigido** em `socket_connect`
  - `sockaddr_in.sin_port` segue convencao BSD (network byte order), mas
    `tcp_open` espera host order porque `tcp_send_segment` faz `htons()` na
    porta. A porta era invertida duas vezes e o servidor recebia o SYN no
    porto errado
  - agora, `socket_connect` desfaz o byte swap antes de chamar `tcp_open`

### HTTP

- **redirect handling** em `http_get` (`src/net/services/http.c`)
  - segue ate 5 redirects (301, 302, 303, 307, 308)
  - resolve `Location:` absoluto, protocol-relative (`//host/path`),
    origin-relative (`/path`) e document-relative
  - novos codigos de erro `redirect_limit` e `bad_redirect`
- texto de ajuda do `net-fetch` atualizado nas tres linguas

## Diagnostico e UX

- `net-fetch` agora imprime, antes da requisicao,
  `>>> https://host (1.2.3.4) ...`, mostrando o IP resolvido por DNS
- em caso de falha, alem da mensagem de erro, sai uma linha
  `diag: arp=N syn-out=N syn-ack=N arp-before=N` com os deltas de
  `tcp_tx`/`tcp_rx`/`arp_entries` antes e depois da tentativa
- preview textual do corpo da resposta limitado a 192 chars, com filtro de
  bytes nao-imprimiveis
- novo hook `net_stack_set_yield_hook` (em `include/net/stack.h`) permite que
  o desktop renderize o cursor durante esperas de rede; `desktop_runtime` ja
  registra o hook ao iniciar a sessao e desregistra ao sair
- atraso ocupado em `net_stack_delay_approx_1ms` reduzido (60000 -> 6000
  iteracoes de pause) para liberar CPU mais cedo entre polls

## Navegador (parcial)

- `html_viewer` exibe agora uma barra de status `Carregando: <url>` no fim do
  viewport e forca um redraw antes da chamada de rede bloqueante, evitando a
  sensacao de travamento
- paginas leves (mostly text/HTML) abrem de forma utilizavel
- limitacoes conhecidas (ficam em branch separado, `feature/browser-internet-improvements`):
  - paginas grandes ou pesadas ainda podem travar a UI (sem streaming/paging)
  - imagens e videos nao sao decodificados/renderizados
  - sem cache, cookies ou compressao

## Validacao

```bash
make all64
make iso-uefi
```

Smoke serial atualizado em `tools/scripts/smoke_x64_boot.py` agora cobre tambem
um cenario de `net-fetch` HTTPS.

## Observacoes

- o root cause do travamento no `net-fetch` era o checksum TCP. Como ICMP
  funcionava (`hey` respondia), o sintoma sugeria DNS/ARP/firewall, e nao
  uma falha de checksum. A correcao destrava todos os fluxos baseados em TCP
  (HTTP, HTTPS via BearSSL, e qualquer cliente TCP futuro)
- esta nota acompanha consolidacao em `develop` e promocao para `main`, sem
  bump de versao semantica
