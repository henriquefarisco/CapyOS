# CapyOS 0.8.0-alpha.81+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: o catálogo metadata-only de trust anchors agora possui invariantes explícitas, máscara de tipos de chave e fingerprint interno estável. Isso permite detectar drift do bundle padrão antes do futuro BearSSL userland consumir anchors reais.

A versão alinhada é `0.8.0-alpha.81+20260510`.

## Principais entregas

- Máscaras internas `CAPY_TLS_TRUST_ANCHOR_KEYTYPE_RSA_MASK` e `CAPY_TLS_TRUST_ANCHOR_KEYTYPE_EC_MASK`.
- Máscara agregada `CAPY_TLS_DEFAULT_TRUST_ANCHOR_KEY_TYPE_MASK=0x3`.
- Fingerprint metadata-only derivado `CAPY_TLS_DEFAULT_TRUST_CATALOG_FINGERPRINT=0xDB22D94A`.
- `capy_tls_default_trust_catalog_consistent()` passa a validar contagens, slot custom, máscara, fingerprint e presença RSA/EC.
- Backend stub propaga máscara e fingerprint para o estado interno preparado.
- Testes declarativos cobrem identidade estável, propagação e scrub dos novos campos.

## Segurança, performance e compatibilidade

- Nenhum byte de certificado é copiado para userland nesta etapa.
- Nenhum parser ASN.1/X.509 é introduzido.
- Nenhum objeto BearSSL é incluído no caminho userland.
- Nenhum handshake TLS real foi habilitado.
- A ABI pública permanece inalterada.
- CA custom continua opaca e não recebe distribuição/fingerprint fabricados.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- `capy_tls_default_trust_anchors_available()` delega para a consistência do catálogo;
- o catálogo exige `106 + 40 = 146`, máscara `0x3` e fingerprint derivado `0xDB22D94A`;
- o backend continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente válido;
- `handshake_started` permanece `0`;
- reset, clear, release e rejeição zeram máscara/fingerprint;
- não restaram scripts temporários `tmp_*.py`.

## Próximos passos

- Portar representação userland real do bundle de trust anchors ainda desconectada do handshake.
- Definir acesso interno aos anchors para o futuro BearSSL userland.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
