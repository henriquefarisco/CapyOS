# CapyOS — Contrato de hostname no TLS kernel-side

## Objetivo

O TLS kernel-side ainda atende caminhos legados enquanto F4 migra HTTPS para userland. Ele precisa aplicar a mesma política conservadora de hostname antes de entregar nomes a BearSSL, SNI ou validação de certificado.

## Contrato em `0.8.0-alpha.67+20260510`

- `tls_connect()` chama `tls_hostname_valid()` antes de alocar contexto TLS.
- `tls_hostname_valid()` aceita hostnames DNS-label e IPv4 literais pontuados dentro da gramática atual.
- Hostnames `NULL`, vazios, com mais de 253 bytes, labels vazios, trailing dot ou labels maiores que 63 bytes são rejeitados.
- Labels que começam ou terminam com `-` são rejeitados.
- Espaços, controles, `_`, `%`, `\`, `*`, `[` e `]` são rejeitados.
- Rejeição ocorre antes de BearSSL/SNI e antes de `socket_setsockopt`.

## Segurança

A regra reduz a superfície de SNI/certificate-name confusion no caminho kernel-side enquanto o browser e demais callers ainda não migraram completamente para `libcapy-net`/`libcapy-tls`.

## Próximo incremento

Em `0.8.0-alpha.68+20260510`, a política convergiu para `tls_hostname_policy_valid()`, compartilhada entre o wrapper kernel-side e `libcapy-tls` userland. O próximo passo é reutilizar essa fronteira ao popular SNI e validar SAN/CN.
