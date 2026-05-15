# CapyOS 0.8.0-alpha.69+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de segurança em `libcapy-tls`: configurações explícitas que tentam desativar `verify_peer` agora são rejeitadas com `CAPY_TLS_EINVAL`. O objetivo é impedir que callers futuros usem HTTPS/TLS sem autenticação de peer ou sem validação de hostname quando o backend BearSSL userland aterrissar.

A versão alinhada é `0.8.0-alpha.69+20260510`.

## Principais entregas

- `capy_tls_config_valid()` passa a exigir `verify_peer == 1` quando `struct capy_tls_config` é fornecida.
- `config == NULL` continua representando o default seguro.
- `verify_peer=0` e valores inválidos falham antes do caminho unsupported.
- Regressões declarativas em `tests/test_capylibc_tls.c` cobrem `verify_peer=0` e `verify_peer=-1`.
- Nenhum handshake real foi habilitado; o comportamento válido continua fail-closed com `CAPY_TLS_EUNSUPPORTED`.

## Segurança e compatibilidade

- Evita TLS sem autenticação por configuração acidental ou permissiva.
- Preserva a ABI pública de `struct capy_tls_config`.
- Preserva fail-closed para todas as conexões TLS estruturalmente válidas.
- Limita o escopo ao caminho userland novo, sem alterar o TLS kernel-side legado.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_config_valid()` aceita `NULL` como default seguro;
- configurações explícitas exigem `verify_peer == 1`;
- configurações com CA inconsistente continuam rejeitadas;
- `capy_tls_connect_tcp()` mantém `CAPY_TLS_EINVAL` para configuração insegura;
- configuração válida `{ 1, 0, 0, timeout }` continua retornando `CAPY_TLS_EUNSUPPORTED` até BearSSL userland.

## Próximos passos

- Usar `verify_peer == 1` como pré-condição do backend BearSSL userland.
- Reutilizar `tls_hostname_policy_valid()` ao popular SNI.
- Adicionar validação SAN/CN compartilhada quando o handshake userland existir.
