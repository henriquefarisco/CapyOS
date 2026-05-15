# CapyOS 0.8.0-alpha.79+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: a biblioteca passa a ter uma fonte interna userland metadata-only para o bundle padrão de trust anchors. O backend stub usa essa fonte para declarar a contagem esperada do bundle padrão sem importar BearSSL, sem copiar certificados e sem iniciar handshake.

A versão alinhada é `0.8.0-alpha.79+20260510`.

## Principais entregas

- Novo módulo interno `capy_tls_trust.c`.
- Nova macro interna `CAPY_TLS_DEFAULT_TRUST_ANCHOR_COUNT` alinhada ao bundle kernel-side atual de 146 anchors.
- Novas funções internas `capy_tls_default_trust_anchor_count()` e `capy_tls_default_trust_anchors_available()`.
- Backend stub passa a rejeitar ausência de fonte padrão quando não há CA custom.
- Metadata padrão agora registra `trust_anchor_count=146` em contexto pronto.
- Testes declarativos cobrem a fonte padrão e a contagem propagada pelo backend.

## Segurança, performance e compatibilidade

- Nenhum byte de certificado é copiado para userland nesta etapa.
- Nenhum objeto BearSSL é incluído no novo módulo userland.
- Nenhum handshake TLS real foi habilitado.
- Nenhum contexto válido é retornado ao caller.
- A ABI pública permanece opaca e inalterada.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_trust.c` depende apenas do header interno de `libcapy-tls`;
- o novo objeto foi registrado em `CAPYLIBC_TLS_OBJS` e no conjunto host de fontes de teste;
- o backend continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente válido;
- `handshake_started` permanece `0`;
- caminhos de reset/release/rejeição continuam limpando `trust_anchor_count` para `0`.

## Próximos passos

- Portar representação userland real do bundle de trust anchors sem conectar handshake ainda.
- Separar API interna de lookup/contagem para o futuro BearSSL userland.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
