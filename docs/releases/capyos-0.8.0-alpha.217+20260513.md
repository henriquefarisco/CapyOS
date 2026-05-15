# CapyOS 0.8.0-alpha.217+20260513

**Data:** 2026-05-13
**Canal:** alpha (experimental)
**Tema:** Ed25519 (RFC 8032) real — update verifier oficialmente operacional

## TL;DR

Implementacao **real** de Ed25519 (RFC 8032) em `src/security/ed25519.c`
substituindo o esqueleto fail-closed que vinha desde alpha.210. Field
arithmetic GF(2^255-19) refatorado em modulo compartilhado
`fe25519.{h,c}` (extraido de `x25519.c`), agora consumido por
**X25519 + Ed25519**. **Update verifier (`src/services/update_agent.c`)
oficialmente operacional pela primeira vez** — manifests assinados com
chave canonica em producao agora sao aceitos quando criptograficamente
validos. Fundacao cripto canonica do CapyOS encerra o ultimo gap de
primitivas de seguranca modernas.

## ANTES (estado em alpha.216)

Fundacao cripto cobria:

| Primitiva | Slice | Estado |
|---|---|---|
| SHA-256, SHA-512 | base + alpha.208/209 | ✓ |
| HMAC-SHA256 | base | ✓ |
| PBKDF2-SHA256 | base | ✓ |
| HKDF-SHA256 | alpha.213 | ✓ |
| AES-128-XTS | base | ✓ (sem MAC) |
| CSPRNG hardened | alpha.214 | ✓ |
| ChaCha20-Poly1305 AEAD | alpha.215 | ✓ |
| X25519 ECDH | alpha.216 | ✓ |
| Constant-time compare | base | ✓ |
| **Ed25519** | **alpha.210** | **⚠ fail-closed placeholder** |

**Gap critico identificado:**

- `ed25519_verify` retornava `-1` para todos os inputs.
- `ed25519_sign` zerava signature output.
- `ed25519_create_keypair` zerava ambos os outputs.
- **Update verifier em producao rejeitava 100% dos manifests por design** —
  apenas tests `UNIT_TEST` com `g_update_manifest_verifier` conseguiam
  aceitar fixtures.
- Field arithmetic GF(2^255-19) duplicado: `x25519.c` tinha implementacao
  real, `ed25519.c` tinha esqueleto dormante com naming diferente.

## AGORA (alpha.217)

### Refatoracao 1 — `fe25519` compartilhado

`@/Users/t808981/Documents/0/CapyOS/include/security/fe25519.h` expoe
APIs canonicas:

- `fe_zero, fe_one, fe_copy, fe_add, fe_sub, fe_neg, fe_carry, fe_mul,
  fe_sq, fe_mul_small`
- `fe_invert` (255 sq + 11 mul via cadeia ref10)
- **`fe_pow22523`** novo: `a^((p-5)/8)` para sqrt em Ed25519 decode
- `fe_cswap, fe_cmov` constant-time
- `fe_tobytes, fe_frombytes`
- `fe_isnegative, fe_iszero, fe_notequal` (canonicalized comparison)
- `fe_wipe` volatile-safe

`@/Users/t808981/Documents/0/CapyOS/src/security/fe25519.c` (~400 LOC) —
implementacao 5x51-bit limbs com `__uint128_t` multiplication, aliasing
safe, wipe hygiene volatile-safe em todos os exits.

`@/Users/t808981/Documents/0/CapyOS/src/security/x25519.c` reduzida para
Montgomery ladder + APIs publicas (~180 LOC, consome `fe25519`).

### Refatoracao 2 — Ed25519 real

`@/Users/t808981/Documents/0/CapyOS/src/security/ed25519.c` (~1500 LOC):

**Group ops twisted Edwards extended coordinates (X:Y:Z:T) com a = -1:**

- `ge_dbl` (dbl-2008-hwcd: 4 sq + 4 mul + 7 add)
- `ge_add` (add-2008-hwcd-3: 9 mul + 7 add com `T1 * 2d * T2` para
  termo cruzado)
- `ge_neg_p`: -P = (-X, Y, Z, -T)
- `ge_cmov` constant-time

**Scalar multiplication:**

- `ge_scalarmult`: double-and-add constant-time, 256 doubles + 256
  cond-adds, cmov mascarado (sem branch sobre bit secreto)
- `ge_scalarmult_base`: usa `ED_B = (ED_B_X, ED_B_Y, 1, ED_B_T)` fixo
- `ge_double_scalarmult`: para verify (`k*A + S*B`)

**Encoding/decoding compressed (32 bytes):**

- `ge_encode`: serializa `Y/Z` em LE + bit 7 do byte 31 = `sign(x)`
- `ge_decode`: parse y + canonicality check (re-encode comparison),
  candidate `x = u * v^3 * (u*v^7)^((p-5)/8)`, verify `v*x^2 == ±u`,
  multiply by `sqrt(-1)` se `-u`, set sign, reject `x==0 && x_0==1`

**Scalar arithmetic mod L** (L = 2^252 + 27742317777372353535851937790883648493):

- `sc_reduce64`: reduz 64-byte SHA-512 output via signed 21-bit limbs,
  cascading multiply-and-add com `l_lo = [666643, 470296, 654183,
  -997805, 136657, -683901]` (porte ref10)
- `sc_muladd`: `(a*b + c) mod L` via mesma representacao
- `sc_is_canonical`: `S < L` gate constant-time

**Constants verificadas contra dalek-cryptography (5x51-bit limbs):**

- `ED_D = -121665/121666`
- `ED_D2 = 2*ED_D`
- `ED_SQRTM1 = 2^((p-1)/4)` (para decode candidate sqrt)
- `ED_B_X, ED_B_Y, ED_B_T` (base point extended coords)
- `ED_L_BYTES` (L em 32-byte LE)

**SHA-512 helpers** `sha512_two_block` e `sha512_three_block` com wipe
de `struct sha512_ctx`.

**APIs publicas** (em `@/Users/t808981/Documents/0/CapyOS/include/security/ed25519.h`):

- `ed25519_create_keypair(pk, sk, seed)`: `h = SHA-512(seed)`, `s =
  clamp(h[0..32])`, `prefix = h[32..64]`, `A = s*B`, encode A para pk,
  sk = seed || pk.
- `ed25519_sign(sig, M, len, pk, sk)`: `h = SHA-512(seed)`, `s, prefix
  = h`, `r = SHA-512(prefix || M) mod L`, `R = r*B`, `sig[0..32] = R`,
  `k = SHA-512(R || A || M) mod L`, `sig[32..64] = (r + k*s) mod L`.
- `ed25519_verify(sig, M, len, pk)`: check `S < L`, decode R, decode A,
  `k = SHA-512(R || A || M) mod L`, compute `check = S*B - k*A`,
  multiply both by cofator 8, compare via projective equality
  (`X1*Z2 == X2*Z1 && Y1*Z2 == Y2*Z1`).

### Update agent agora oficialmente operacional

`@/Users/t808981/Documents/0/CapyOS/src/services/update_agent.c` chama
`ed25519_verify` real. Comentario fail-closed substituido por documentacao
do gate criptografico ativo. Manifests com:

- Assinatura forjada/corrompida → rejeita (-1)
- `S >= L` (signature malleavel) → rejeita
- `R` ou pk decodificam para ponto invalido → rejeita
- `[8]SB != [8]R + [8](kA)` (cofator 8 catches torsion variants) → rejeita

### Threat model documentado inline

- Cofator 8 em verify per RFC 8032 §5.1.7 step 4 (mandatory). Multiplicar
  R e check por 8 antes da comparacao elimina componentes torsao —
  resistente a strongbinding attacks.
- `S < L` gate previne signature malleability.
- Canonicality check em `ge_decode` (re-encode + compare) previne
  non-canonical encodings.
- `x == 0 && x_0 == 1` rejeita o ponto (0, 1) com sign bit 1 que NAO
  pode existir (ambiguidade do encoded form per RFC §5.1.3 step 5).
- Wipe volatile-safe em todos os intermediarios em todos os exits.

### Tests reformulados

`test_ed25519_failclosed_contract` em
`@/Users/t808981/Documents/0/CapyOS/tests/test_crypt_vectors.c`
substituido (nao deletado — contrato mudou de fail-closed para real
implementation):

- **3 vetores oficiais RFC 8032 §7.1** (empty / 1-byte / 2-byte
  messages): valida `create_keypair -> pk_expected`, `sign ->
  sig_expected`, `verify(sig_expected) == 0`.
- **Tampering rejection**: flip de `sig[0]` (R half) e `sig[32]` (S
  half) deve falhar.
- **Wrong public key rejection**: flip de `pk[0]` deve falhar.
- **Non-canonical S rejection**: `S = L` e `S > L` devem falhar.
- **NULL inputs**: signature/pk NULL devem retornar -1.
- **Tolera NULL output**: `sign(NULL, ...)` e `create_keypair(NULL, NULL, ...)`
  nao crasham.
- **Round-trip**: chave fresca de seed arbitrario assina + verifica
  mensagem de 64 bytes; tamper message deve falhar.
- **Determinismo**: re-assinar mesma mensagem produz mesma signature
  (PureEd25519 e deterministico).

## Curso linear da entrega

```
1. AUDITORIA       →  ed25519.c esqueleto fail-closed desde alpha.210.
                       SHA-512 ja existia. Field arithmetic duplicado
                       entre x25519.c e ed25519.c (com naming
                       diferente). update_agent.c produz gate gate de
                       manifest production rejeitando tudo.
                       ↓
2. REFATORACAO 1   →  fe25519 compartilhado. Extracao de field
                       arithmetic GF(2^255-19) de x25519.c para
                       include/security/fe25519.h + src/security/
                       fe25519.c. Adicao de fe_pow22523, fe_neg,
                       fe_cmov, fe_isnegative, fe_iszero, fe_notequal.
                       x25519.c reduzida para ladder + APIs (consome
                       fe25519).
                       ↓
3. EDWARDS GROUP   →  Implementacao de ge_p3 + ge_dbl + ge_add +
                       ge_neg_p + ge_cmov em src/security/ed25519.c.
                       Constants ED_D, ED_D2, ED_SQRTM1, ED_B_X/Y/T
                       verificadas contra dalek (5x51-bit limbs).
                       ↓
4. SCALAR MULT     →  ge_scalarmult double-and-add constant-time
                       (256 doubles + cmov), ge_scalarmult_base,
                       ge_double_scalarmult.
                       ↓
5. ENCODE/DECODE   →  ge_encode (Y/Z em LE + sign bit), ge_decode
                       (parse y + canonicality + sqrt candidate +
                       sign correction + x==0 reject).
                       ↓
6. SCALAR MOD L    →  sc_reduce64 + sc_muladd (porte ref10 com signed
                       21-bit limbs), sc_is_canonical constant-time.
                       ↓
7. APIs PUBLICAS   →  ed25519_create_keypair, ed25519_sign,
                       ed25519_verify (com cofator 8 em verify).
                       ↓
8. UPDATE AGENT    →  Removido aviso fail-closed em
                       manifest_signature_ed25519_valid. Production
                       gate agora ativo.
                       ↓
9. TESTES          →  test_ed25519_failclosed_contract reformulado:
                       3 vetores RFC 8032 §7.1 + tampering + wrong-pk
                       + non-canonical S + NULL + round-trip +
                       determinism + tamper-message.
                       ↓
10. BUILD          →  fe25519.o em CAPYOS64_OBJS. fe25519.c + sha512.c
                       em TEST_SRCS.
                       ↓
11. VERSAO         →  alpha.217 em version.h + VERSION.yaml.
                       ↓
12. DOCS           →  release note + master plan + architecture +
                       README + STATUS + screenshots.
```

## Impacto end-user

**Imediato observavel:**

- **Updates oficialmente assinados sao agora aceitos** quando a chave
  canonica `update_agent_release_public_key` foi usada para gerar a
  assinatura. Atualizacoes do CapyOS via canal oficial passam pelo
  gate criptografico real pela primeira vez.

**Setup para mudancas futuras visiveis:**

- TLS 1.3 userland (Etapa 5) tem Ed25519 + X25519 + ChaCha20-Poly1305 +
  HKDF + SHA-256 + CSPRNG todos nativos e auditaveis.
- Code signing para apps locais com Ed25519.
- Channel binding com signatures.
- Container signing para package_manager.

## Impacto estrutura

**Fundacao cripto canonica completa.** 10 primitivas modernas em
`src/security/`, todas seguindo o mesmo padrao (wipe volatile-safe,
fail-closed, threat model inline, audit-friendly):

| Pilar | Primitiva | Slice |
|---|---|---|
| Hash | SHA-256, SHA-512 | base + alpha.208/209 |
| MAC | HMAC-SHA256 | base |
| KDF (senhas) | PBKDF2-SHA256 | base |
| KDF (uniformes) | HKDF-SHA256 | alpha.213 |
| Random | CSPRNG hardened | alpha.214 |
| Cifra simetrica | AES-128-XTS | base |
| AEAD | ChaCha20-Poly1305 | alpha.215 |
| Constant-time compare | helper | base |
| Key exchange | X25519 | alpha.216 |
| **Signature** | **Ed25519** | **alpha.217** ✓ |

**Triplet canonico ECDH→HKDF→AEAD completo + autenticidade via Ed25519:**
secure messaging, secure boot, secure update, secure IPC todos com
primitivas auditaveis nativas.

**Sem dependencia obrigatoria do BearSSL para uso fora de TLS.** BearSSL
continua sendo o stack TLS handshake oficial, mas apps locais podem usar
primitivas nativas auditaveis.

**Field arithmetic unificada.** Eliminacao da duplicacao entre
`x25519.c` e `ed25519.c`. Auditor revisa `fe25519.{h,c}` uma vez, sabe
que ambas as curvas usam a mesma implementacao validada.

**Update path operacional.** Pela primeira vez, atualizacoes assinadas
em producao podem ser validadas e instaladas. Path critico de seguranca
do CapyOS funcional end-to-end.

## Limites

- **Performance**: `ge_scalarmult` usa double-and-add simples (sem
  precomputed tables). ~5-10 ms por scalar mult em x86_64. Suficiente
  para sign/verify ocasional (update check, login event). Speedups via
  fixed-base comb tables podem vir em slices futuros se profiling
  indicar gargalo.
- **Sem testes RFC 8032 §7.1 vetores 1024-byte ou SHA-abc** — apenas os
  3 vetores menores. Cobertura adequada para sanity check.
- **Nao destrava entregaveis pendentes da Etapa 2** (loginwindow GUI
  real, smokes).
- **TLS 1.3 userland ainda nao implementado** (Etapa 5). Primitivas
  todas prontas; integracao no stack TLS ainda pendente.
- **Sem ECDSA / RSA / outros schemes** — apenas Ed25519. Adequado para
  todos os casos de uso planejados.
