# CapyOS 0.8.0-alpha.70+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de robustez em `libcapy-tls`: `timeout_ms` em `struct capy_tls_config` agora é validado antes do backend BearSSL userland existir. A API aceita `0` como default seguro, mas rejeita timeouts explícitos abaixo de `CAPY_TLS_TIMEOUT_MIN_MS` ou acima de `CAPY_TLS_TIMEOUT_MAX_MS`.

A versão alinhada é `0.8.0-alpha.70+20260510`.

## Principais entregas

- Novas constantes públicas em `capy_tls.h`: `CAPY_TLS_TIMEOUT_DEFAULT_MS`, `CAPY_TLS_TIMEOUT_MIN_MS` e `CAPY_TLS_TIMEOUT_MAX_MS`.
- `capy_tls_config_valid()` rejeita timeouts explícitos menores que 100 ms.
- `capy_tls_config_valid()` rejeita timeouts explícitos maiores que 120000 ms.
- `timeout_ms=0` continua representando default seguro.
- Regressões declarativas em `tests/test_capylibc_tls.c` cobrem timeout abaixo do mínimo, acima do máximo e default explícito.

## Segurança, performance e UX

- Evita chamadas futuras com timeout quase zero, que causariam falhas espúrias.
- Evita timeouts excessivos, que poderiam travar UI/update-agent por tempo abusivo.
- Mantém o contrato fail-closed: configurações inválidas retornam `CAPY_TLS_EINVAL`; configurações válidas continuam retornando `CAPY_TLS_EUNSUPPORTED` até o backend real.
- Não muda ABI de `struct capy_tls_config`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `timeout_ms=0` permanece válido como default seguro;
- `timeout_ms < CAPY_TLS_TIMEOUT_MIN_MS` falha quando não é zero;
- `timeout_ms > CAPY_TLS_TIMEOUT_MAX_MS` falha;
- configuração válida `{ 1, 0, 0, 1000 }` continua chegando ao caminho `CAPY_TLS_EUNSUPPORTED`;
- as constantes públicas estão no header userland de `libcapy-tls`.

## Próximos passos

- Usar `CAPY_TLS_TIMEOUT_DEFAULT_MS` quando o contexto real de `capy_tls_connect_tcp()` for materializado.
- Propagar a janela de timeout para socket read/write quando o backend BearSSL userland existir.
- Adicionar métricas de timeout para diagnóstico de HTTPS futuro.
