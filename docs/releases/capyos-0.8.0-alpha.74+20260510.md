# CapyOS 0.8.0-alpha.74+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** de lifecycle em `libcapy-tls`: foi introduzido um slot interno estático para `capy_tls_context`, com aquisição e liberação explícitas. O objetivo é preparar o caminho de contexto real do futuro backend BearSSL userland sem alocar heap, sem retornar contexto válido em `capy_tls_connect_tcp()` e sem habilitar handshake.

A versão alinhada é `0.8.0-alpha.74+20260510`.

## Principais entregas

- Novo limite interno `CAPY_TLS_CONTEXT_SLOT_COUNT`.
- Novo pool estático interno de `capy_tls_context` com um slot gerenciado.
- Nova função interna `capy_tls_context_acquire()`.
- Nova função interna `capy_tls_context_release()`.
- `capy_tls_free()` passa a liberar contexto gerenciado via release interno.
- Regressões declarativas cobrem exclusividade, reuso e scrub do slot.

## Segurança, performance e compatibilidade

- Evita heap antes de existir política userland definitiva de alocação TLS.
- Garante que o slot seja limpo antes de ser reutilizado.
- Ignora ponteiros externos em release, reduzindo risco de free inválido.
- Mantém a ABI pública opaca de `struct capy_tls_context`.
- Mantém `capy_tls_connect_tcp()` fail-closed com `CAPY_TLS_EUNSUPPORTED`.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_context_acquire()` retorna slot resetado e bloqueia segunda aquisição simultânea;
- `capy_tls_context_release()` limpa e libera apenas slot gerenciado;
- `capy_tls_free()` delega para release interno seguro;
- testes declarativos cobrem reuso do slot e limpeza após `capy_tls_free()`;
- nenhum handshake TLS ou retorno de contexto válido foi habilitado.

## Próximos passos

- Decidir quando `capy_tls_connect_tcp()` poderá adquirir e retornar um contexto real.
- Conectar o slot preparado ao primeiro stub BearSSL userland sem handshake completo.
- Expandir a política de slots se múltiplas conexões TLS simultâneas forem necessárias.
