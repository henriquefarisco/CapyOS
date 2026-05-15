# CapyOS 0.8.0-alpha.80+20260510

**Data:** 2026-05-10
**Canal:** alpha
**Track:** UEFI/GPT/x86_64
**Estabilidade:** experimental

## Resumo executivo

Esta release fecha um patch **F4** em `libcapy-tls`: a fonte userland metadata-only de trust anchors agora é um catálogo interno. O catálogo registra a contagem total do bundle padrão, a distribuição de chaves RSA/EC e a existência de slot custom, sem copiar certificados, sem parse ASN.1 e sem ativar BearSSL userland.

A versão alinhada é `0.8.0-alpha.80+20260510`.

## Principais entregas

- `capy_tls_trust_anchor_catalog` interno em `libcapy-tls`.
- Contagem total preservada em `146` anchors.
- Distribuição metadata-only registrada: `106` RSA e `40` EC.
- Backend stub propaga `trust_anchor_rsa_count` e `trust_anchor_ec_count` para configuração padrão.
- CA custom continua opaca: apenas `custom_anchor_len` e `trust_anchor_count=1` são registrados.
- Testes declarativos cobrem catálogo, distribuição RSA/EC e scrub dos novos campos.

## Segurança, performance e compatibilidade

- Nenhum byte de certificado é copiado para userland nesta etapa.
- Nenhum parser de certificado é introduzido.
- Nenhum objeto BearSSL é incluído no novo caminho userland.
- Nenhum handshake TLS real foi habilitado.
- A ABI pública permanece inalterada.
- Rejeições, reset, clear e release continuam zerando todo metadata novo.

## Validação

Nesta sessão, a validação foi feita por revisão estática de código e documentação. Não foram executados `make`, `git`, build, suíte de testes ou automação permanente de validação.

Pontos revisados estaticamente:

- o catálogo soma `106 + 40 = 146`;
- `capy_tls_default_trust_anchors_available()` exige catálogo consistente;
- o backend stub continua retornando `CAPY_TLS_EUNSUPPORTED` para contexto estruturalmente válido;
- `handshake_started` permanece `0`;
- o diretório de scripts temporários não reteve `tmp_*.py` após a aplicação.

## Próximos passos

- Portar representação userland real do bundle de trust anchors ainda desconectada do handshake.
- Definir acesso interno aos anchors para o futuro BearSSL userland.
- Manter `CAPY_TLS_EUNSUPPORTED` até existir handshake real validável.
