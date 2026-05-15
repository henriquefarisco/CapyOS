# CapyOS 0.8.0-alpha.84+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: o trust store padrão agora possui um manifesto interno metadata-only que consolida proveniência, flags e fingerprints do catálogo, da tabela de slots e dos descritores.

A versão alinhada é `0.8.0-alpha.84+20260510`.

## Principais entregas

- `struct capy_tls_trust_store_manifest` interna.
- Schema de manifesto `CAPY_TLS_DEFAULT_TRUST_MANIFEST_SCHEMA_VERSION=1`.
- Proveniência `CAPY_TLS_DEFAULT_TRUST_MANIFEST_SOURCE_KERNEL_BUNDLE=0x1`.
- Flags `CAPY_TLS_TRUST_MANIFEST_FLAG_*` para metadata-only, kernel-bundle-derived, cert-bytes-absent e fail-closed-only.
- Fingerprint de manifesto `CAPY_TLS_DEFAULT_TRUST_MANIFEST_FINGERPRINT=0x958359C5`.
- Accessors internos para schema, source, flags e fingerprint do manifesto.
- `capy_tls_default_trust_store_manifest_consistent()` valida manifesto contra catálogo, slots, descritores, flags e fingerprint derivado.
- Backend stub propaga `trust_manifest_schema_version`, `trust_manifest_source_id`, `trust_manifest_flags` e `trust_manifest_fingerprint` para configuração padrão.

## Segurança, performance e compatibilidade

- Nenhum byte de certificado é copiado para userland nesta etapa.
- Nenhum parser ASN.1/X.509 é introduzido.
- Nenhum objeto BearSSL é incluído no caminho userland.
- Nenhum handshake TLS real foi habilitado.
- A ABI pública permanece inalterada.
- CA custom continua opaca e não recebe manifesto fabricado.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- manifesto referencia os mesmos `146` anchors metadata-only;
- distribuição permanece `106` RSA e `40` EC;
- manifesto referencia fingerprints `0xDB22D94A`, `0x07A622AB` e `0xE1A18A70`;
- fingerprint de manifesto é `0x958359C5`;
- flags marcam metadata-only, proveniência kernel bundle, ausência de bytes de certificado e fail-closed-only;
- backend continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente válido;
- `handshake_started` permanece `0`;
- reset, clear, release e rejeição zeram metadados de manifesto;
- não restaram scripts temporários `tmp_*.py`.

## Próximos passos

- Portar representação userland real do bundle de trust anchors ainda desconectada do handshake.
- Definir armazenamento interno para bytes de certificados sem expor ABI pública.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
