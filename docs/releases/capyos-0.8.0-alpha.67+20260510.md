# CapyOS 0.8.0-alpha.67+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** que alinha o TLS kernel-side à política de hostname já aplicada em `libcapy-tls` userland. `tls_connect()` agora usa `tls_hostname_valid()` antes de alocar contexto BearSSL, configurar timeouts ou repassar o nome para SNI/validação de certificado.

A versão alinhada é `0.8.0-alpha.67+20260510`.

## Principais entregas

- Novo header `include/security/tls_hostname.h`.
- Novo módulo `src/security/tls_hostname.c`.
- `src/security/tls.c` usa `tls_hostname_valid()` no início de `tls_connect()`.
- Novo teste declarativo `tests/test_tls_hostname.c` registrado no runner.
- `Makefile` inclui `security/tls_hostname.o` no kernel e no host test set.

## Segurança e compatibilidade

- Hostnames malformados são rejeitados antes de BearSSL/SNI.
- Espaços, controles, `_`, `%`, backslash, wildcard textual e IPv6 literal textual são rejeitados.
- Labels vazios, trailing dot, labels maiores que 63 bytes e nomes maiores que 253 bytes são rejeitados.
- A política do TLS kernel-side passa a espelhar a fronteira userland, reduzindo divergência durante a migração F4.
- A ABI pública de `tls_connect()` não muda.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `tls_hostname_valid()` não depende de BearSSL, heap, socket ou kernel state;
- `tls_connect()` chama o validador antes de `kmalloc` e antes de `socket_setsockopt`;
- `tests/test_tls_hostname.c` cobre caminhos aceitos e rejeições críticas;
- `Makefile` referencia o objeto no kernel build e no host test set;
- `tests/test_runner.c` declara e chama `test_tls_hostname_run()`.

## Próximos passos

- Remover duplicação futura entre `tls_hostname_valid()` e o validador userland quando houver uma camada compartilhável.
- Conectar `capy_tls_connect_tcp()` ao backend BearSSL userland real.
- Adicionar validação SAN/CN compartilhada quando o handshake userland existir.
