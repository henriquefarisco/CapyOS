# CapyOS 0.8.0-alpha.78+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: o backend TLS userland agora prepara metadados internos de trust anchors junto ao SNI e timeout já materializados. O stub continua fail-closed e retorna `CAPY_TLS_EUNSUPPORTED` antes de qualquer carga real de certificados ou handshake BearSSL userland.

A versão alinhada é `0.8.0-alpha.78+20260510`.

## Principais entregas

- `capy_tls_backend_state` agora registra readiness de trust anchors.
- Configuração com CA custom marca `custom_anchor_ready`, `trust_anchor_count=1` e preserva apenas o tamanho da âncora custom.
- Configuração padrão marca trust anchors como preparados sem declarar certificados carregados.
- Rejeições limpam SNI, timeout e metadados de trust anchors antes de retornar erro.
- Regressões declarativas cobrem metadados padrão, CA custom e scrub em reset/release/rejeição.

## Segurança, performance e compatibilidade

- Nenhum certificado é parseado ou copiado para o backend userland nesta etapa.
- Nenhum handshake TLS real foi habilitado.
- Nenhum contexto válido é retornado ao caller.
- Metadados de CA custom guardam somente comprimento e flags internas.
- A ABI pública permanece opaca e inalterada.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_context_reset()` continua zerando todo o contexto privado;
- `capy_tls_backend_connect()` zera backend state antes de validar;
- trust metadata só é preparado depois de socket, hostname e configuração efetiva válidos;
- `handshake_started` permanece `0` em todos os fluxos unsupported;
- rejeições limpam `trust_anchors_ready`, `custom_anchor_ready`, `trust_anchor_count` e `custom_anchor_len`.

## Próximos passos

- Separar preparação de trust metadata em fonte userland reutilizável pelo futuro BearSSL.
- Portar bundle de trust anchors para userland sem conectar handshake ainda.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
