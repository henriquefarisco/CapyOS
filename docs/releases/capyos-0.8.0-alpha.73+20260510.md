# CapyOS 0.8.0-alpha.73+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de segurança e higiene de estado em `libcapy-tls`: contextos TLS preparados agora têm reset/clear internos explícitos. O caminho `capy_tls_connect_tcp()` prepara um contexto stack-local, limpa esse contexto antes de retornar e continua falhando fechado com `CAPY_TLS_EUNSUPPORTED` enquanto o backend BearSSL userland não existe.

A versão alinhada é `0.8.0-alpha.73+20260510`.

## Principais entregas

- Novas funções internas `capy_tls_context_reset()` e `capy_tls_context_clear()`.
- Reset de contexto define `socket_fd=-1`, hostname vazio, `verify_peer=1` e timeout default.
- `capy_tls_context_prepare()` limpa o contexto antes de preencher e também em rejeições parciais.
- `capy_tls_connect_tcp()` limpa o contexto stack-local preparado antes de retornar unsupported.
- Regressões declarativas cobrem scrub de contexto preparado e limpeza após rejeição.

## Segurança, privacidade e compatibilidade

- Remove hostname e ponteiro de CA do contexto temporário antes de sair do caminho fail-closed.
- Evita retenção de estado antigo quando `capy_tls_context_prepare()` é reutilizado com entrada inválida.
- Mantém a ABI pública opaca de `struct capy_tls_context`.
- Não habilita alocação, retorno de contexto real ou handshake TLS.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_context_reset()` zera o contexto e repõe defaults seguros;
- `capy_tls_context_clear()` delega para reset interno;
- `capy_tls_context_prepare()` chama reset antes de validar/preencher;
- rejeições não preservam socket, hostname ou ponteiros de CA antigos;
- `capy_tls_connect_tcp()` limpa o contexto stack-local antes de publicar `CAPY_TLS_EUNSUPPORTED`.

## Próximos passos

- Definir alocação/liberação real do contexto userland.
- Fazer `capy_tls_free()` limpar e liberar contexto real quando `capy_tls_connect_tcp()` puder retornar ponteiro válido.
- Conectar contexto preparado ao primeiro stub BearSSL userland sem handshake completo.
