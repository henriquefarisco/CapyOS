# CapyOS — Contrato HTTPS fail-closed em libcapy-net

## Objetivo

`libcapy-net` aceita URLs `https://` no parser para preservar a semântica do input, mas não pode enviar HTTP plaintext para uma origem que o caller declarou como TLS. O contrato deste patch é manter HTTPS fail-closed até existir transporte TLS userland real.

## Contrato em `0.8.0-alpha.65+20260510`

- `capy_http_get("https://...")` retorna `-1`.
- `capy_net_last_error()` retorna `CAPY_NET_EUNSUPPORTED`.
- A rejeição acontece antes de DNS, socket, connect, send ou recv.
- O caminho HTTPS passa por `capy_net_internal_https_fail_closed()` em `capy_net_tls.c`.
- Erros de `libcapy-tls` são convertidos para a superfície de erro `CAPY_NET_*`.

## Segurança

A propriedade essencial é não vazar metadados de rede quando a pilha TLS userland ainda não está pronta. Rejeitar antes de resolver o hostname evita tráfego DNS desnecessário; rejeitar antes de abrir socket evita plaintext acidental em porta 443.

## Próximo incremento

Quando `libcapy-tls` tiver backend BearSSL userland, o adaptador deve ser substituído por um transporte HTTPS que abre TCP, negocia TLS, envia a request sobre `capy_tls_send` e lê a resposta por `capy_tls_recv`, mantendo o mesmo contrato de erro para falhas de certificado e handshake.
