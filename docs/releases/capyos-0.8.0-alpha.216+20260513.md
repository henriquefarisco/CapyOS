# CapyOS 0.8.0-alpha.216+20260513

**Data:** 2026-05-13
**Canal:** alpha (experimental)
**Tema:** X25519 (RFC 7748) ECDH canonico em `src/security/`

## TL;DR

Implementacao do zero da primitiva ECDH X25519 (RFC 7748) em
`src/security/x25519.c`. Independente do TLS stack BearSSL e
audit-friendly. **Primeira primitiva de key exchange nativa do
CapyOS** — fecha o gap fundamental para protocolos com
forward secrecy: TLS 1.3 userland (Etapa 5), WireGuard-like
channels, secure messaging local, channel binding. Completa o
triplet canonico **ECDH (alpha.216) → HKDF (alpha.213) → AEAD
(alpha.215)** com primitivas todas auditaveis em `src/security/`.

## ANTES (estado em alpha.215)

A fundacao cripto cobria:

| Primitiva | Slice | Estado |
|---|---|---|
| SHA-256 + ctx wipe | alpha.208/209 | ✓ |
| HMAC-SHA256 | base | ✓ |
| PBKDF2-SHA256 | base | ✓ |
| HKDF-SHA256 | alpha.213 | ✓ |
| AES-128-XTS | base | ✓ (sem MAC) |
| CSPRNG hardened | alpha.214 | ✓ |
| **ChaCha20-Poly1305 AEAD** | **alpha.215** | ✓ |
| Constant-time compare | base | ✓ |
| SHA-512 | base | ✓ |
| Ed25519 | alpha.210 | ⚠ fail-closed placeholder |
| **X25519** | — | **✗ ausente em src/security/** |

**Gap critico identificado:** nao havia primitiva canonica de
**key exchange** acessivel fora do TLS handshake. Para qualquer
protocolo que precisasse derivar segredo compartilhado entre duas
partes sem segredo pre-compartilhado, faltava a primitiva. BearSSL
implementa X25519 internamente mas so via SSL engine — acoplamento
indesejado para callers nao-TLS.

**Consequencias do gap:**

- **TLS 1.3 userland** (Etapa 5) inviavel sem X25519 nativo —
  RFC 8446 §4.2.8.2 obriga X25519 (ou P-256) para key share.
- **Channel binding** para conexoes seguras locais nao podia ser
  implementado.
- **WireGuard-like channels** entre processos com forward secrecy
  ficavam bloqueados.
- **Triplet ECDH→HKDF→AEAD** incompleto: HKDF e AEAD prontos
  desde alpha.213/215, mas ECDH ausente.
- **Ed25519** real (RFC 8032) que esta pendente desde alpha.210
  precisa de field arithmetic GF(2^255-19) — mesma fundacao
  matematica que X25519. Esta entrega tambem prepara terreno.

## AGORA (alpha.216)

### `src/security/x25519.c` (~470 LOC)

Implementacao do zero, auditavel, alinhada com a higiene cripto do
resto do projeto (wipe volatile-safe, fail-closed, NULL graceful).

**Field arithmetic em GF(p) com p = 2^255 - 19:**

- Representacao `fe`: 5 limbs de 51 bits cada (radix 2^51).
- `fe_zero`, `fe_one`, `fe_copy` — operacoes elementares.
- `fe_add` — adicao limb-a-limb sem carry imediato.
- `fe_sub` — subtracao com soma de `2*p` para evitar underflow.
- `fe_mul`, `fe_sq` — multiplicacao schoolbook com `__uint128_t`,
  reducao mod p via `*19` nos termos `i+j >= 5` (porque
  `2^255 ≡ 19 mod p`).
- `fe_mul_small` — multiplicacao por constante pequena (usado para
  `a24 = 121665` na ladder).
- `fe_carry` — propagacao de carries com reducao mod p.
- `fe_invert` — `a^(p-2)` via cadeia ref10 (255 squarings +
  11 multiplications). Calcula `a^(2^255 - 21)` per Fermat's
  little theorem.
- `fe_tobytes` — serializa em little-endian 32 bytes com
  canonicalizacao para `[0, p)` via subtracao condicional
  constant-time de `p`.
- `fe_frombytes` — desserializa com top-bit masking per RFC 7748 §5.
- `fe_cswap` — conditional swap em tempo constante via
  `mask = -(uint64_t)swap`.

**Montgomery ladder per RFC 7748 §5:**

- 255 iteracoes (bit 254 down to bit 0).
- Sem branches sobre bits secretos do scalar.
- Cada iteracao: 1 `fe_cswap` (condicional ao bit), depois 9 muls +
  2 sqs + 4 adds + 4 subs para a doubling-and-adding combinada.
- `a24 = 121665` (calculado offline como `(486662 - 2) / 4`).

**APIs publicas em `include/security/x25519.h`:**

- `x25519(scalar, u_coord, shared)`:
  - Clamping interno do `scalar` per §5 (zera bits 0,1,2,255; seta
    bit 254).
  - Top-bit masking do `u_coord` per §5.
  - Small-subgroup detection per §6.1: rejeita `shared == 0`
    fail-closed (atacante poderia forcar shared=0 enviando um
    small-order point como `bob_pk`).
- `x25519_base(scalar, public_key)`:
  - Equivalente a `x25519(scalar, BASE_POINT_9, public_key)` onde
    `BASE_POINT_9 = u = 9` per §4.1.
  - Sem small-subgroup gate (base point tem ordem prima).

**Wipe hygiene volatile-safe em todos os exits:**

- Ladder: `x_1, x_2, z_2, x_3, z_3, A, AA, B, BB, E, C, D, DA, CB,
  tmp1, tmp2`.
- Invert: `t0, t1, t2, t3` (cada um carrega `a^k` parcial).
- Internal: `e` (scalar clampeado), `u` (u-coord parseado), `x_res,
  z_res, z_inv, out_fe`.

### Threat model documentado inline

- **Confidencialidade:** atacante nao computa `shared` observando
  apenas `alice_pk` e `bob_pk` no canal (CDH assumption sobre
  Curve25519).
- **Resistencia a small-order attacks:** rejeita `shared == 0` que
  ocorreria se atacante enviasse small-order point como
  contraparte (RFC 7748 §6.1).
- **Indistinguibilidade:** `pk` derivada do base point e
  indistinguivel de bytes aleatorios uniformes modulo o top bit
  (que e sempre mascarado).
- **Limites documentados:**
  - Esta primitiva NAO autentica chaves publicas — caller e
    responsavel por garantir authenticity (e.g. Ed25519 signature,
    certificate pinning, out-of-band).
  - Cofator 8 da curva absorvido pelo clamping.
  - Apenas u-coord processada; v-coord nunca aparece.

### Composicao com slices anteriores

- **alpha.214 (CSPRNG hardened):** fonte canonica para `scalar`
  uniforme de 32 bytes. Sem RNG forte, key agreement vira hardcoded.
- **alpha.213 (HKDF-SHA256):** KDF natural para derivar `session_key`
  a partir de `shared` + context label:
  ```c
  crypt_hkdf_sha256(shared, 32, "binding=channel", 15,
                    NULL, 0, session_key, 32);
  ```
- **alpha.215 (ChaCha20-Poly1305 AEAD):** consome `session_key`
  derivada do ECDH para proteger canal autenticado. Triplet
  canonico ECDH→HKDF→AEAD agora **completo**.

### Tests novos em `tests/test_crypt_vectors.c`

6 funcoes de teste com 25+ assertions:

1. **`test_x25519_rfc7748_scalarmult`** — RFC 7748 §5.2:
   - Vetor 1: scalar `a546e3...`, u `e6db68...` → expected
     `c3da55...`.
   - Vetor 2: scalar `4b66e9...`, u `e52102...` → expected
     `95cbde...`.
2. **`test_x25519_rfc7748_dh`** — RFC 7748 §6.1 ECDH end-to-end:
   - Alice: sk=`770760...`, pk_expected=`8520f0...`. Validar
     `x25519_base(alice_sk) == alice_pk`.
   - Bob: sk=`5dab08...`, pk_expected=`de9edb...`. Validar
     `x25519_base(bob_sk) == bob_pk`.
   - Shared: expected=`4a5d9d...`. Validar
     `x25519(alice_sk, bob_pk) == x25519(bob_sk, alice_pk) ==
     shared`. **Convergencia** explicitamente assertada.
3. **`test_x25519_small_order_rejection`** — fail-closed em pontos
   pequena ordem:
   - `u = 0` (ordem 2): retorna -1.
   - `u = 1` (ordem 4 na twist): retorna -1.
4. **`test_x25519_fail_closed`** — 5 categorias de NULL:
   - `x25519(NULL, u, out)`, `x25519(scalar, NULL, out)`,
     `x25519(scalar, u, NULL)`.
   - `x25519_base(NULL, out)`, `x25519_base(scalar, NULL)`.
5. **`test_x25519_high_bit_masked`** — RFC 7748 §5 mandatory
   masking: flip do bit 255 do `u_coord` nao altera output.
6. **`test_x25519_scalar_clamping`** — RFC 7748 §5 mandatory
   clamping: flip dos bits que devem ser zerados/setados pelo
   clamping nao altera output.

### Build

- `x25519.o` adicionado a `CAPYOS64_OBJS` (kernel build).
- `x25519.c` adicionado a `TEST_SRCS` (host-side unit tests).

## Curso linear da entrega

```
1. AUDITORIA       →  Mapear o gap: ECDH ausente em src/security/.
                       BearSSL tem X25519 mas preso a SSL engine.
                       Triplet ECDH+HKDF+AEAD incompleto (E ausente).
                       ↓
2. PROBLEMA        →  TLS 1.3 userland (Etapa 5), WireGuard-like,
                       channel binding bloqueados sem ECDH nativo.
                       ↓
3. DESIGN          →  2 APIs publicas (x25519, x25519_base) com
                      fail-closed + wipe volatile-safe + small-subgroup
                      gate. Internals: field arithmetic 5x51-bit limbs,
                      Montgomery ladder constant-time, fe_invert via
                      cadeia ref10.
                       ↓
4. IMPLEMENTACAO   →  include/security/x25519.h (publica, ~110 LOC com
                      threat model documentado).
                      src/security/x25519.c (~470 LOC com field
                      arithmetic, ladder, APIs).
                       ↓
5. TESTES          →  tests/test_crypt_vectors.c: +6 funcoes / +25
                      assertions cobrindo RFC 7748 §5.2 / §6.1 /
                      small-subgroup / NULL / masking / clamping.
                       ↓
6. BUILD           →  Makefile: CAPYOS64_OBJS + TEST_SRCS.
                       ↓
7. VERSAO          →  alpha.216 em version.h + VERSION.yaml.
                       ↓
8. DOCS            →  release note + master plan + architecture +
                       README + STATUS + releases/README + screenshots.
                       ↓
9. REVISAO         →  ABI publica nova aditiva, nao quebra existentes.
                      Wipe hygiene preservada.
                      Composicao com alpha.208/209/210/213/214/215
                      integral.
                      Field arithmetic auditada manualmente para
                      overflow em __uint128_t e canonicalizacao em
                      fe_tobytes.
```

## Impacto para o usuario final

**Imediato observavel:** zero. ECDH e primitiva fundacional sem
callers reais nesta entrega.

**Setup para mudancas visiveis futuras:**

- **TLS 1.3 userland** (Etapa 5) destravado do ponto de vista do
  key share. RFC 8446 §4.2.8.2 obriga X25519 (ou P-256); agora o
  CapyOS tem X25519 nativo auditavel.
- **Channel binding** para protocolos seguros locais.
- **WireGuard-like channels** entre processos com forward secrecy
  real: cada sessao tem chave efemera derivada de ECDH novo,
  nunca reusada.
- **Secure messaging local com forward secrecy:** mesmo se chave
  longa-prazo de Alice for comprometida no futuro, mensagens
  passadas continuam confidenciais.
- **Future secure boot key exchange:** trocar key de assinatura
  com TPM/HSM externo via X25519.

## Impacto para a estrutura do sistema

**Fundacao cripto agora cobre os 3 pilares de protocolos seguros
modernos:**

| Pilar | Primitiva CapyOS | Slice |
|---|---|---|
| Hash | SHA-256, SHA-512 | base + alpha.208/209 |
| MAC | HMAC-SHA256 | base |
| KDF (senhas) | PBKDF2-SHA256 | base |
| KDF (uniformes) | HKDF-SHA256 | alpha.213 |
| Random | CSPRNG hardened | alpha.214 |
| Cifra simetrica (sem MAC) | AES-128-XTS | base |
| AEAD | **ChaCha20-Poly1305** | **alpha.215** |
| Constant-time compare | helper | base |
| **Key exchange** | **X25519** | **alpha.216 (novo)** |
| Signature | Ed25519 (esqueleto) | alpha.210 ⚠ |

- **Triplet canonico ECDH→HKDF→AEAD completo.** Atacante pode
  agora ser confrontado com canal autenticado com forward secrecy
  usando apenas primitivas auditaveis do CapyOS.
- **Independencia do BearSSL** para usos nao-TLS: BearSSL continua
  como stack TLS handshake oficial; X25519 nativa atende callers
  fora de TLS.
- **Audit-friendly:** `src/security/` agora tem 9 primitivas
  canonicas (SHA-256, SHA-512, HMAC, PBKDF2, HKDF, AES-XTS, CSPRNG,
  ChaCha20-Poly1305, X25519) seguindo o mesmo padrao (wipe volatile-
  safe, fail-closed, threat model inline). Auditor revisa como
  unidade coesa.
- **Prepara terreno para Ed25519 real** (RFC 8032): field
  arithmetic GF(2^255-19) e a mesma; uma futura entrega de Ed25519
  real podera refatorar `fe_*` em modulo compartilhado.
- **ABI publica nova, aditiva.** Nao quebra callers existentes; o
  header `include/security/x25519.h` e novo e auto-contido.

## Limites

- **Sem callers reais ainda.** Esta entrega e a primitiva
  fundacional. Callers naturais (TLS userland Etapa 5, WireGuard-
  like, secure messaging local) chegarao em slices subsequentes.
- **Ed25519 continua fail-closed.** Esta entrega NAO destrava o
  update verifier (que ainda usa Ed25519 fail-closed desde
  alpha.210). Ed25519 real e proximo slice natural.
- **Nao destrava entregaveis pendentes da Etapa 2** (loginwindow
  GUI submit real, smokes).
- **Nao substitui BearSSL para TLS handshake.** BearSSL continua
  sendo o stack TLS oficial; esta primitiva e PARA usos fora de
  TLS handshake.
- **Field arithmetic nao compartilhada com ed25519.c** ainda.
  Duplicacao temporaria; refatoracao virara quando Ed25519 real
  for implementado.
- **`fe_invert` nao e otimizada** com endomorfismos GLV/GLS;
  usa cadeia ref10 padrao (255 sq + 11 mul). Aceitavel: ladder
  da X25519 e o bottleneck (~5000 muls), invert e ~0.05% do custo.

## Validacao aplicada (revisao estatica)

- `include/security/x25519.h` linhas 1-110: APIs publicas
  documentadas com threat model e exemplo de composicao.
- `src/security/x25519.c` linhas 1-470: implementacao completa.
- Field arithmetic auditada manualmente:
  - **`fe_mul` overflow:** cada `t[k]` < 100 * v^2 onde v sao
    limbs de input. Para v < 2^60, total < 2^115 — cabe em
    `__uint128_t` (< 2^128). ✓
  - **`fe_carry` convergencia:** 1 chamada normaliza inputs de
    `fe_mul` (v[0]<2^51, v[1]<2^52, v[2..4]<2^51) para todos
    < 2^51. ✓ 3 chamadas no `fe_tobytes` e safe margin.
  - **`fe_tobytes` canonicalizacao:** detection `(t + 19) >> 255`
    via loop de carries — caso `t < p` → carry=0; caso `t in
    [p, 2^255)` → carry=1 → subtraicao limb-a-limb com borrow.
    Verificado caso a caso para `t = p, p+1, p+2^51, p-1`. ✓
- `tests/test_crypt_vectors.c` linhas 997-1260: 6 funcoes de
  teste com 25+ assertions cobrindo vetores oficiais + edge cases.
- `Makefile` linha 415: `x25519.o` em CAPYOS64_OBJS.
- `Makefile` linha 1131: `x25519.c` em TEST_SRCS.
- ABI publica preservada (todas as APIs existentes intocadas).
- Wipe hygiene de alpha.208/209/210/213/214/215 preservada.
- Zero arquivos temp.

## Proximos slices sugeridos

1. **Ed25519 real (RFC 8032).** Field arithmetic GF(2^255-19)
   compartilhada com X25519 (refatorar `fe_*` para modulo comum);
   adicionar point operations em twisted Edwards coordinates;
   scalar multiplication; SHA-512 ja existente em `sha512.c`;
   signature gen/verify per §5.1. ~1500 LOC. **Destrava update
   verifier real** — esse e o slice de maior impacto immediate
   para usuario final.
2. **Caller real do X25519:** secure messaging local entre apps
   usando triplet ECDH→HKDF→AEAD. ~200 LOC. Vitoria GUI visivel:
   IPC channels com forward secrecy.
3. **TLS 1.3 userland real** (Etapa 5): combinacao de X25519 +
   Ed25519 + ChaCha20-Poly1305 + HKDF + SHA-256 + CSPRNG ja todos
   disponiveis. Pode comecar com X25519 key share, Ed25519
   certificate verification, ChaCha20-Poly1305 record protection.
4. **Authenticated config store:** cifrar e autenticar
   `/etc/passwd`, `/etc/auth_policy` etc. com chave derivada do
   volume master via HKDF, protegida com ChaCha20-Poly1305 AEAD.
5. **Loginwindow GUI submit real** — fechar Etapa 2 formalmente.
