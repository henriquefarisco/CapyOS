# CapyOS — Contrato de hostname em libcapy-tls

## Objetivo

`libcapy-tls` vai alimentar SNI e validação de certificado quando o backend BearSSL userland existir. Por isso, a fronteira pública precisa rejeitar nomes malformados antes de qualquer handshake.

## Contrato em `0.8.0-alpha.66+20260510`

- `capy_tls_connect_tcp()` rejeita `NULL`, string vazia e hostnames com mais de 253 bytes.
- Labels vazios, trailing dot e labels com mais de 63 bytes são rejeitados.
- Labels que começam ou terminam com `-` são rejeitados.
- Apenas `[A-Za-z0-9.-]` é aceito nesta etapa.
- Espaços, controles, `_`, `%`, `\`, `[` e `]` são rejeitados.
- Hostnames estruturalmente válidos continuam falhando fechado com `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland.

## Segurança

A regra evita confusão entre parser de URL, SNI e validação de SAN/CN. Ela também impede que bytes ambíguos ou sintaxes não suportadas, como IPv6 literal textual, atravessem a fronteira TLS antes de haver uma política explícita para elas.

## Próximo incremento

Quando o backend TLS userland existir, esta validação deve ser compartilhada com o caminho que popula SNI e com a política de hostname validation pós-certificado. Em `0.8.0-alpha.67+20260510`, o caminho TLS kernel-side legado recebeu uma política equivalente em `tls_hostname_valid()`. Em `0.8.0-alpha.68+20260510`, ambos passaram a depender de `tls_hostname_policy_valid()` como fonte única da regra.
