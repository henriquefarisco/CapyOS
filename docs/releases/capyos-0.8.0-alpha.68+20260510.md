# CapyOS 0.8.0-alpha.68+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de manutenibilidade e segurança: a política de hostname TLS foi centralizada em `tls_hostname_policy_valid()`, compartilhada por kernel e `libcapy-tls` userland. Os wrappers permanecem separados, mas a regra que bloqueia nomes ambíguos agora tem uma única fonte de verdade.

A versão alinhada é `0.8.0-alpha.68+20260510`.

## Principais entregas

- Novo header compartilhado `include/security/tls_hostname_policy.h`.
- `src/security/tls_hostname.c` passa a ser wrapper fino de `tls_hostname_policy_valid()`.
- `userland/lib/capylibc-tls/capy_tls.c` remove a cópia local da política e usa a regra compartilhada.
- `tests/test_tls_hostname.c` passa a verificar equivalência entre wrapper kernel e política compartilhada.
- Nenhuma ABI pública muda; apenas a fonte da política foi centralizada.

## Segurança e compatibilidade

- Reduz risco de divergência entre TLS kernel-side e `libcapy-tls` durante a migração F4.
- Mantém rejeição de controles, espaços, `_`, `%`, backslash, wildcard textual e IPv6 literal textual.
- Mantém limites de 253 bytes para hostname e 63 bytes por label.
- Mantém fail-closed: nomes malformados falham antes de BearSSL/SNI e antes de qualquer backend TLS userland real.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `tls_hostname_policy.h` é freestanding e depende apenas de `<stddef.h>`;
- `tls_hostname_valid()` delega diretamente para `tls_hostname_policy_valid()`;
- `capy_tls_connect_tcp()` continua retornando `CAPY_TLS_EINVAL` para hostname malformado;
- `tests/test_tls_hostname.c` inclui `<stddef.h>` explicitamente e compara wrapper/política;
- não há objeto novo obrigatório, pois a política é inline e o wrapper kernel já existia.

## Próximos passos

- Conectar `capy_tls_connect_tcp()` ao backend BearSSL userland real.
- Reutilizar a mesma política ao popular SNI userland.
- Adicionar validação SAN/CN compartilhada quando o handshake userland existir.
