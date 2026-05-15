# CapyOS 0.8.0-alpha.83+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: os trust anchors padrão agora possuem descritores internos metadata-only. A camada descreve índice, tipo RSA/EC, flag metadata-only e flags explícitas de slot-backed, key-type-known e cert-bytes-absent.

A versão alinhada é `0.8.0-alpha.83+20260510`.

## Principais entregas

- `struct capy_tls_trust_anchor_descriptor` interna.
- Flags `CAPY_TLS_TRUST_ANCHOR_DESCRIPTOR_FLAG_*` para metadata-only, slot-backed, key-type-known e cert-bytes-absent.
- Fingerprint de descritores `CAPY_TLS_DEFAULT_TRUST_DESCRIPTOR_FINGERPRINT=0xE1A18A70`.
- Acesso bounds-checked por `capy_tls_default_trust_anchor_descriptor(index, out)`.
- Saída inválida é zerada antes do retorno fail-closed.
- `capy_tls_default_trust_anchor_descriptors_consistent()` valida descritores contra slots, distribuição e fingerprint.
- Backend stub propaga `trust_anchor_descriptor_count` e `trust_descriptor_fingerprint` para configuração padrão.

## Segurança, performance e compatibilidade

- Nenhum byte de certificado é copiado para userland nesta etapa.
- Nenhum parser ASN.1/X.509 é introduzido.
- Nenhum objeto BearSSL é incluído no caminho userland.
- Nenhum handshake TLS real foi habilitado.
- A ABI pública permanece inalterada.
- CA custom continua opaca e não recebe descritores fabricados.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- descritores cobrem os mesmos `146` slots metadata-only;
- distribuição permanece `106` RSA e `40` EC;
- flags de descriptor marcam ausência de bytes de certificado;
- fingerprint de descritores é `0xE1A18A70`;
- acesso fora de faixa zera o descriptor de saída e falha;
- backend continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente válido;
- `handshake_started` permanece `0`;
- reset, clear, release e rejeição zeram contagem/fingerprint de descritores;
- não restaram scripts temporários `tmp_*.py`.

## Próximos passos

- Portar representação userland real do bundle de trust anchors ainda desconectada do handshake.
- Definir armazenamento interno para bytes de certificados sem expor ABI pública.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
