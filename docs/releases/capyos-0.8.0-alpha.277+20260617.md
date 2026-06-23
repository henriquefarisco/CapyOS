# CapyOS 0.8.0-alpha.277+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.277+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Etapa 9 (antecipatoria) -- cobertura end-to-end do verifier Ed25519 pelo gate de install (test-only)

## Resumo executivo

Adiciona a prova **ponta-a-ponta** que faltava ao verifier de assinatura
entregue em `alpha.276`: dois testes host-side instalam um pacote pelo **gate
de install completo** com o verifier Ed25519 real registrado, confirmando que
`build_signed_descriptor` (no caminho de install) reproduz **exatamente** os
bytes do descritor canonico que a assinatura do KAT cobre. Sem mudanca de
codigo de runtime; a politica de assinatura segue **fail-closed**.

## Mudancas (test-only)

- **`tests/services/test_capypkg_signature.inc` (+2 casos):**
  - `test_install_real_verifier_accepts_kat`: `reset_state(1)` ->
    `capypkg_set_signature_verifier(capypkg_ed25519_verify_signature)` + chave do
    KAT pinada; manifesto com os campos do KAT (name/version/payload_sha256/
    payload_url) + a assinatura do KAT; o fake fetcher devolve `"test"` (cujo
    SHA-256 **e** o `payload_sha256` do KAT). `capypkg_install` **aceita** o
    pacote -> prova que o descritor reconstruido pelo gate bate com a assinatura.
  - `test_install_real_verifier_rejects_tampered_kat`: mesma montagem com um
    nibble da assinatura trocado -> `capypkg_install` retorna
    `CAPYPKG_ERR_SIGNATURE` e nada e staged.
- Cobre o unico ponto de integracao que o teste isolado de `alpha.276` nao
  exercitava: o **formato do descritor** construido por `build_signed_descriptor`
  (`src/services/capypkg/capypkg_install.c`) versus o descritor canonico do KAT.

## Validacao

- `make test` -- verde, **0 FAIL**. Os 2 casos novos passam, confirmando que
  `build_signed_descriptor` emite os bytes canonicos exatos cobertos pela
  assinatura do KAT (a assinatura verifica pelo gate completo).
- `make layout-audit` e `make version-audit` -- sem warnings.

## Escopo pendente (inalterado desde alpha.276)

- **Operador:** pinar a chave de release **offline** real via
  `capypkg_set_trusted_publisher_key` (a chave de TESTE do KAT nunca e pinada).
- **CapyAgent:** KAT externo do signer (`make validate`).
- Refresh doc-only de `CapyAgent/docs/compatibility.md` (mirror secundario).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.276+20260617` | `alpha.277+20260617` | Teste end-to-end do verifier Ed25519 pelo gate de install (test-only) |

Sem mudanca de codigo de runtime, de ABI, de contrato cross-repo, nem da
politica de assinatura (segue fail-closed ate a chave de producao). Os 6 repos
irmaos permanecem nas versoes de `alpha.266`.

_Build: `0.8.0-alpha.277+20260617`_
