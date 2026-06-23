# CapyOS 0.8.0-alpha.276+20260617

**Data:** 2026-06-17
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.276+20260617`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** Workspace P0 -- verifier Ed25519 de pacote registrado no lado CapyOS (entrega antecipatoria da Etapa 9)

## Resumo executivo

O slot de verificacao de assinatura do `capypkg`, que ficava **NULL por
design**, passou a ser preenchido por um verifier CapyOS-side real, ligado ao
`ed25519_verify` auditado do kernel e **host-validado contra o known-answer
vector (KAT) cross-repo** que o signer do CapyAgent produz. **Sem mudanca de
comportamento em producao:** nenhuma chave de publisher e pinada por padrao,
entao repos `signed` continuam **fail-closed** com `CAPYPKG_ERR_SIGNATURE`,
exatamente como antes. Esta release entrega a *maquinaria* de verificacao
(a parte dificil), deixando so o passo de operador (pinar a chave de release
offline real) para habilitar install assinado user-facing.

## Mudancas

- **Verifier (`src/services/capypkg/capypkg_signature.c`, novo TU):**
  `capypkg_ed25519_verify_signature(signed_text, signed_len, signature_hex)`
  (assinatura compativel com `capypkg_verify_signature_fn`): decodifica os 128
  hex da assinatura **fail-closed** (rejeita nao-hex, curto, longo) e chama o
  `ed25519_verify` (RFC 8032, `src/security/ed25519.c`) sobre o descritor
  canonico, usando a chave publica do publisher. `capypkg_set_trusted_publisher_key`
  / `capypkg_clear_trusted_publisher_key` configuram o trust anchor (32 bytes);
  **sem chave pinada -> retorna -1** (fail-closed).
- **Registro (`src/arch/x86_64/kernel_services_capypkg.c`):** o binder do
  kernel agora chama `capypkg_set_signature_verifier(capypkg_ed25519_verify_signature)`
  em vez de deixar o slot NULL. Como nenhum trust anchor e pinado, o efeito em
  producao e identico ao anterior (signed -> `CAPYPKG_ERR_SIGNATURE`).
- **Seguranca:** a chave de **TESTE** do KAT (cuja seed e publica) **nunca** e
  pinada -- pina-la deixaria qualquer um assinar pacotes. Promover um repo
  `signed` a user-facing exige a chave de release **offline** real.

## Validacao

- `make test` -- verde (0 FAIL). 6 casos host-side novos
  (`tests/services/test_capypkg_signature.inc`, agregados em
  `run_capypkg_tests`): a assinatura que o signer do CapyAgent produz (KAT
  congelado em `CapyAgent/docs/compatibility.md`) **verifica** com o
  `ed25519_verify` do kernel; descritor adulterado, assinatura adulterada,
  ausencia de chave, e hex malformado/curto(127)/longo(130) sao **rejeitados**;
  descritor canonico confirmado em 235 bytes.
- `build/x86_64/services/capypkg/capypkg_signature.o` compila limpo sob os
  CFLAGS reais do kernel.
- `make layout-audit` e `make version-audit` -- sem warnings.
- Matrix + audit cross-repo atualizados (verifier registrado, fail-closed ate
  o trust anchor de producao).

## Escopo pendente (para install `signed` user-facing)

1. **Operador:** pinar a chave de release **offline** real via
   `capypkg_set_trusted_publisher_key` (a chave de TESTE do KAT nao serve).
2. **CapyAgent:** KAT externo do signer (`make validate` no CapyAgent).
3. **Doc irma:** refrescar `CapyAgent/docs/compatibility.md` (a nota "matrix row
   stays 'signer pending registration'" agora esta desatualizada -- o verifier
   foi registrado). Follow-up doc-only.

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `alpha.275+20260617` | `alpha.276+20260617` | Verifier Ed25519 do capypkg registrado (fail-closed pendente trust anchor); KAT-validado host-side |

Sem mudanca de ABI base do kernel (a API `capypkg_verify_signature_fn` ja
existia) nem do contrato cross-repo (descritor canonico inalterado); os 6 repos
irmaos permanecem nas versoes de `alpha.266`. A politica de assinatura
permanece **fail-closed** ate a chave de producao ser pinada.

_Build: `0.8.0-alpha.276+20260617`_
