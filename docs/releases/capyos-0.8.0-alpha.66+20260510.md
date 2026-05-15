# CapyOS 0.8.0-alpha.66+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de hardening em `libcapy-tls`: `capy_tls_connect_tcp()` agora rejeita hostnames malformados antes de qualquer backend TLS real. A mudança prepara a futura integração BearSSL userland evitando que nomes ambíguos cheguem a SNI ou validação de certificado.

A versão alinhada é `0.8.0-alpha.66+20260510`.

## Principais entregas

- Validador interno de hostname em `userland/lib/capylibc-tls/capy_tls.c`.
- Rejeição de espaços, controles, underscore, `%`, backslash, IPv6 literal textual, label vazio, trailing dot e labels maiores que 63 bytes.
- Limite total de hostname em 253 bytes.
- Regressão declarativa em `tests/test_capylibc_tls.c` para entradas malformadas.
- Manutenção do contrato fail-closed: entradas válidas continuam retornando `CAPY_TLS_EUNSUPPORTED` até o backend BearSSL userland aterrissar.

## Segurança e compatibilidade

- Evita que SNI futuro receba nomes com bytes ambíguos ou sintaxe fora do contrato IPv4/hostname atual.
- Evita divergência entre parser de URL de `libcapy-net` e fronteira TLS.
- Não muda ABI pública de `libcapy-tls`.
- Não habilita handshake real; HTTPS continua fail-closed.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_hostname_valid()` rejeita nomes vazios, labels vazios e trailing dot;
- labels com `-` no início/fim são rejeitados;
- caracteres fora de `[A-Za-z0-9.-]` são rejeitados;
- `capy_tls_connect_tcp()` ainda zera security info antes de falhar;
- hostnames válidos continuam chegando ao caminho `CAPY_TLS_EUNSUPPORTED`.

## Próximos passos

- Aplicar regra equivalente no TLS kernel-side antes de migrar callers.
- Conectar `capy_tls_connect_tcp()` ao backend BearSSL userland real.
- Adicionar validação de SAN/CN quando a handshake userland existir.
