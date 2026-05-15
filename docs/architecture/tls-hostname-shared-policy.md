# CapyOS — Política compartilhada de hostname TLS

## Objetivo

A validação de hostname TLS precisa ser idêntica no caminho kernel-side legado e em `libcapy-tls` userland. `0.8.0-alpha.68+20260510` centraliza a regra em `tls_hostname_policy_valid()` para reduzir divergência antes da integração BearSSL userland.

## Contrato em `0.8.0-alpha.68+20260510`

- A política vive em `include/security/tls_hostname_policy.h`.
- `tls_hostname_valid()` é o wrapper kernel-side.
- `capy_tls_connect_tcp()` usa a mesma política por wrapper interno userland.
- A política é inline, freestanding e depende apenas de `<stddef.h>`.
- Hostnames malformados continuam sendo rejeitados antes de BearSSL/SNI.

## Segurança

Uma única fonte de verdade evita que o caminho kernel aceite nomes que o userland rejeita, ou o inverso. Isso é crítico para não criar comportamento diferente entre browser legado, update-agent futuro e `libcapy-net` durante a migração F4.

## Próximo incremento

Quando SAN/CN validation for adicionada ao backend TLS userland, a política de comparação de nomes de certificado deve seguir o mesmo princípio: wrappers separados, regra compartilhada e testes de equivalência.
