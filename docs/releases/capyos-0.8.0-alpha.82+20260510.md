# CapyOS 0.8.0-alpha.82+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: o catálogo metadata-only de trust anchors agora possui uma tabela userland de 146 slots, um por anchor do bundle padrão kernel-side. Cada slot registra apenas índice, tipo de chave RSA/EC e flag metadata-only.

A versão alinhada é `0.8.0-alpha.82+20260510`.

## Principais entregas

- `struct capy_tls_trust_anchor_slot` interna.
- Tabela `g_capy_tls_default_trust_slots[146]` gerada a partir da sequência RSA/EC do bundle kernel-side.
- Fingerprint de layout `CAPY_TLS_DEFAULT_TRUST_SLOT_LAYOUT_FINGERPRINT=0x07A622AB`.
- Funções internas para consultar slots, contagem, tipo por índice, fingerprint e consistência.
- `capy_tls_default_trust_anchors_available()` exige catálogo e tabela de slots consistentes.
- Backend stub propaga `trust_anchor_slot_count` e `trust_slot_layout_fingerprint` para configuração padrão.
- Testes declarativos cobrem layout inicial, distribuição completa, propagação e scrub.

## Segurança, performance e compatibilidade

- Nenhum byte de certificado é copiado para userland nesta etapa.
- Nenhum parser ASN.1/X.509 é introduzido.
- Nenhum objeto BearSSL é incluído no caminho userland.
- Nenhum handshake TLS real foi habilitado.
- A ABI pública permanece inalterada.
- CA custom continua opaca e não recebe slot layout fabricado.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- a tabela tem 146 slots metadata-only;
- a distribuição permanece `106` RSA e `40` EC;
- o fingerprint de layout é `0x07A622AB`;
- o backend continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente válido;
- `handshake_started` permanece `0`;
- reset, clear, release e rejeição zeram slot count/layout fingerprint;
- não restaram scripts temporários `tmp_*.py`.

## Próximos passos

- Portar representação userland real do bundle de trust anchors ainda desconectada do handshake.
- Definir acesso interno aos anchors para o futuro BearSSL userland.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
