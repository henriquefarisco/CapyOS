# CapyOS 0.8.0-alpha.218+20260514

**Data:** 2026-05-14
**Canal:** alpha (experimental)
**Tema:** Argon2id (RFC 9106) + BLAKE2b (RFC 7693) — password hashing memory-hard

## TL;DR

Implementacao **real e auditavel** de **Argon2id** (RFC 9106) e
**BLAKE2b** (RFC 7693) em `src/security/argon2.c` e
`src/security/blake2b.c`. Argon2id e o vencedor do **Password Hashing
Competition (2015)** e o algoritmo recomendado pela **OWASP** e pelo
**NIST SP 800-63B** para password hashing. Fecha o gap fundamental
contra **brute-force massivo em GPU/ASIC** que atualmente afeta
PBKDF2-SHA256 (default em `src/security/crypt.c`) com speedup tipico de
**1000-10000x sobre CPU comum**. BLAKE2b e a fundacao matematica
obrigatoria do Argon2 (RFC 9106 §3.3) e tambem fica disponivel como
primitiva publica para uso geral.

## ANTES (estado em alpha.217)

Fundacao cripto cobria:

| Primitiva | Slice | Estado |
|---|---|---|
| SHA-256, SHA-512 | base + alpha.208/209 | ✓ |
| HMAC-SHA256 | base | ✓ |
| PBKDF2-SHA256 | base | ✓ (vulneravel a GPU/ASIC) |
| HKDF-SHA256 | alpha.213 | ✓ |
| AES-128-XTS | base | ✓ |
| CSPRNG hardened | alpha.214 | ✓ |
| ChaCha20-Poly1305 AEAD | alpha.215 | ✓ |
| X25519 ECDH | alpha.216 | ✓ |
| Ed25519 signature | alpha.217 | ✓ |
| **Password hashing memory-hard** | — | **⚠ ausente** |
| **BLAKE2b** | — | **⚠ ausente** |

**Gap critico identificado:**

- `userdb` (`src/auth/user.c`) usa PBKDF2-SHA256 com iteracoes para
  hashing de senha de login local.
- PBKDF2 nao tem **memory-hardness** — atacante com GPU/ASIC pode
  avaliar milhares de candidatos por segundo em paralelo.
- Para passwords fracas (8 chars alpha-num lowercase = 36^8 ~= 2.8 *
  10^12), GPU dedicada cracka em **horas**.
- Argon2id reduz speedup ASIC para **<10x** via memory wall: cada
  candidate teste exige alocar `m_cost * 1024` bytes (default 64 MiB).

## AGORA (alpha.218)

### `src/security/blake2b.c` (~270 LOC)

**API publica** em `@/Users/t808981/Documents/0/CapyOS/include/security/blake2b.h`:

- `blake2b_init(ctx, outlen, key, keylen)` — outlen 1..64, keylen 0..64
- `blake2b_update(ctx, in, inlen)` — streaming com lazy compression
- `blake2b_final(ctx, out)` — emite digest de `ctx->outlen` bytes
- `blake2b(out, outlen, key, keylen, in, inlen)` — one-shot
- `blake2b_wipe(ctx)` — volatile-safe wipe

**Internals (RFC 7693):**

- **IV** identico a SHA-512 (8 uint64).
- **Sigma permutation** 12 rodadas (RFC §2.7).
- **G mixing function** com rotations (32, 24, 16, 63).
- **Param block per RFC §2.5** encodado em `h[0]` inicial:
  `h[0] ^= 0x01010000 ^ (keylen << 8) ^ outlen` (fanout=1, depth=1, sem
  salt/personal).
- **Keyed mode** (HMAC-like): key paddado a 128 bytes e absorvido como
  primeiro bloco per RFC §2.9.
- **Lazy compression**: o ultimo bloco *nao* e comprimido em `update` —
  fica pendente em `ctx->buf` para `final` aplicar o flag `f[0] =
  0xFF..FF` (finalization). Permite streaming correto + suporte para
  variable-length output.
- **Wipe hygiene** volatile-safe em `m`, `v` apos cada compressao;
  `blake2b_wipe` zera ctx inteiro.

### `src/security/argon2.c` (~600 LOC)

**API publica** em `@/Users/t808981/Documents/0/CapyOS/include/security/argon2.h`:

```c
int argon2id_hash(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t t_cost, uint32_t m_cost,
                  uint8_t *memory, size_t memory_len,
                  uint8_t *out, size_t out_len);
```

**Limites suportados nesta entrega:**

- `parallelism = 1` fixo (RFC 9106 permite explicitamente p=1).
- Sem secret K (associated authentication key).
- Sem associated data X.
- **memory caller-provided** (sem `malloc` no kernel) — caller fornece
  buffer `m_cost * 1024` bytes via stack/static/heap.

**Internals (RFC 9106):**

**Pre-hash H0 (§3.2):**

```
H0 = BLAKE2b(64, LE32(p=1) || LE32(out_len) || LE32(m_cost)
                || LE32(t_cost) || LE32(version=0x13)
                || LE32(type=2 /* Argon2id */)
                || LE32(pwd_len) || password
                || LE32(salt_len) || salt
                || LE32(0) /* secret length */
                || LE32(0) /* AD length */)
```

**Variable-length H' (§3.3):**

- `T <= 64`: single `BLAKE2b(T, LE32(T) || X)`.
- `T > 64`: `r = ceil(T/32) - 2`; chain `V_1 = BLAKE2b(64, LE32(T) ||
  X)`, `V_i = BLAKE2b(64, V_{i-1})` para `i = 2..r`; output `V_1[0..32]
  || ... || V_r[0..32] || V_{r+1}` onde `V_{r+1} = BLAKE2b(T - 32*r,
  V_r)`.

**Block initialization (§3.2):**

- `B[0][0] = H'(1024, H0 || LE32(0) || LE32(0))`
- `B[0][1] = H'(1024, H0 || LE32(1) || LE32(0))`

**G compression function 1024-byte (§3.6):**

- `R = X XOR Y` onde X, Y, R sao 128 uint64.
- View R como matriz 8x8 de **registers 16-byte (= 2 uint64)**.
- Apply P **row-wise**: 8 chamadas, cada uma sobre uma row (16 uint64).
- Apply P **column-wise**: 8 chamadas, cada uma sobre uma column (16
  uint64 interleaved no buffer).
- `Z = (result) XOR R`.

**P round function:**

- 8 chamadas a `GB(a, b, c, d)` com round schedule identico a BLAKE2.
- `GB` usa **fBlaMka**: `a = a + b + 2 * (a_lo * b_lo)` ao inves da
  soma simples — acrescenta multiplicacao 32x32->64 que aumenta
  cost-per-op em ASIC dedicado.

**Address block generation (§3.4.1.1, data-independent path):**

- `input_block = LE64(pass) || LE64(lane=0) || LE64(slice) ||
  LE64(m') || LE64(t_cost) || LE64(type=2) || LE64(counter) ||
  zero(968)`
- `address_block = G(zero_block, G(zero_block, input_block))`
- Counter incrementado por bloco de 128 enderecos.
- Para slice 0 pass 0 (start_index=2), pre-geracao manual antes do
  loop principal porque `index % 128 == 2 != 0` no inicio.

**Argon2id mode selection (§3.4):**

- Pass 0, slice 0 e 1: **data-independent** (resistente a cache
  side-channel).
- Pass 0, slice 2 e 3 + Pass > 0: **data-dependent** (resistente a
  TMTO).

**Reference index alpha (§3.4.1.2):**

```c
ref_area_size = (pass == 0)
    ? ((slice == 0) ? (index - 1)
                    : (slice * segment_length + index - 1))
    : (lane_length - segment_length + index - 1);

rel_pos = (J1 * J1) >> 32;
rel_pos = ref_area_size - 1 - ((ref_area_size * rel_pos) >> 32);

start_pos = (pass == 0)
    ? 0
    : ((slice == 3) ? 0 : (slice + 1) * segment_length);

ref_pos = (start_pos + rel_pos) % lane_length;
```

Distribuicao **nao-uniforme** com J1^2 concentra referencias no inicio
do reference set — refoco da memory-hardness contra TMTO.

**Block computation (§3.4.2):**

- Pass 0: `B[abs_pos] = G(B[prev], B[ref])`
- Pass > 0: `B[abs_pos] = B[abs_pos] XOR G(B[prev], B[ref])`
  (semantica Argon2 v1.3 overwrite-XOR)

**Finalization (§3.5):**

- Para p=1: `final_block = B[lane_length - 1]`.
- `tag = H'(out_len, final_block)`.

**Wipe hygiene volatile-safe em todos os intermediarios:**

- `H0`, `V` chain do H', blocos `prev_block`/`ref_block`/`new_block`/
  `existing` da compressao, `input_block`, `address_block`,
  `zero_block`, `final_block`.
- Wipeados antes do retorno em sucesso e em todos os paths de erro.
- **Memory buffer caller-provided NAO e wipeado automaticamente** —
  permite reuse em loops sem perder o malloc; caller decide.

## ABI publica

### Novas exposicoes

```c
/* include/security/blake2b.h */
#define BLAKE2B_BLOCK_SIZE  128u
#define BLAKE2B_DIGEST_SIZE 64u
#define BLAKE2B_KEY_SIZE    64u

struct blake2b_ctx { /* state interno */ };

int  blake2b_init(struct blake2b_ctx *ctx, size_t outlen,
                  const uint8_t *key, size_t keylen);
void blake2b_update(struct blake2b_ctx *ctx, const uint8_t *in, size_t inlen);
void blake2b_final(struct blake2b_ctx *ctx, uint8_t *out);
int  blake2b(uint8_t *out, size_t outlen,
             const uint8_t *key, size_t keylen,
             const uint8_t *in, size_t inlen);
void blake2b_wipe(struct blake2b_ctx *ctx);

/* include/security/argon2.h */
#define ARGON2_BLOCK_SIZE     1024u
#define ARGON2_VERSION_13     0x13u
#define ARGON2_TYPE_ID        2u
#define ARGON2_SYNC_POINTS    4u
#define ARGON2_MIN_OUT_LEN    4u
#define ARGON2_MIN_SALT_LEN   8u
#define ARGON2_MIN_T_COST     1u
#define ARGON2_MIN_M_COST     8u

int argon2id_hash(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t t_cost, uint32_t m_cost,
                  uint8_t *memory, size_t memory_len,
                  uint8_t *out, size_t out_len);
```

### Mudancas backward-incompatible

**Nenhuma.** PBKDF2-SHA256 em `src/security/crypt.c` continua
disponivel — `userdb` antigo nao quebra. Migracao incremental para
Argon2id e slice futuro com algorithm prefix nos hashes armazenados.

## Tests

`@/Users/t808981/Documents/0/CapyOS/tests/test_crypt_vectors.c`

**BLAKE2b (RFC 7693):**

- `test_blake2b_rfc7693_abc` — vetor canonico RFC 7693 Appendix A:
  `BLAKE2b("abc") = ba80a53f981c4d0d...4009923` (64 bytes).
- `test_blake2b_empty` — `BLAKE2b("") = 786a02f742015903...fe9be2ce`
  (Python `hashlib.blake2b(b"")`).
- `test_blake2b_multiblock` — `BLAKE2b("The quick brown fox...")`
  (Python `hashlib.blake2b(...)`).
- `test_blake2b_streaming_equals_oneshot` — update em chunks (50, 78
  cruzando boundary 128, 1, 127 cruzando 256, 44) produz o mesmo
  digest que one-shot.
- `test_blake2b_variable_output` — outlen 16/32/64 produzem outputs
  distintos (param block inclui outlen em `h[0]` inicial).
- `test_blake2b_keyed` — keys diferentes produzem outputs diferentes.
- `test_blake2b_fail_closed` — NULL out, outlen=0, outlen=65,
  keylen=65, NULL key com keylen>0.

**Argon2id (RFC 9106):**

- `test_argon2id_smoke` — KAT + propriedades estruturais com m_cost=8 KiB:
  - **KAT cross-checked vs `argon2-cffi` reference impl** (Python):
    `argon2id(p='password', s='somesalt', t=1, m=8, p=1, len=32,
    version=0x13)` =
    `f137f8e186a403a679ccd0606e5ab5dcdafe43c1640855ac8c6e33e9bd63eeb3`.
  - Determinismo cross-call.
  - Sensibilidade a password (avalanche).
  - Sensibilidade a salt.
  - Sensibilidade a t_cost.
  - Sensibilidade a m_cost (com buffer maior).
  - Sensibilidade a out_len (H' inclui T_le no input).
  - Empty password aceito (RFC permite).
  - Fail-closed: NULL salt, salt < 8 bytes, t_cost=0, m_cost=7, memory
    insuficiente, out_len < 4, NULL out, NULL memory.

**Cross-validation contra `argon2-cffi` (reference impl Python).**
Antes do commit final, 4 combinacoes adicionais foram verificadas
manualmente, todas com **MATCH** byte-a-byte:

| Params | Reference impl | Nosso impl |
|---|---|---|
| `t=1, m=8, len=32` | `f137f8e1...bd63eeb3` | **MATCH** |
| `t=2, m=8, len=32` | `fdb4ddb6...1071646` | **MATCH** |
| `t=1, m=16, len=32` | `3fd1f4fd...81d25f8d` | **MATCH** |
| `t=1, m=8, len=16` | `f73a73d2...12bdf86` | **MATCH** |
| `empty pwd, t=1, m=8, len=32` | `65eeedf5...28145651` | **MATCH** |

Validacao confirma que pre-hash H0 + variable-length H' + G compression
function + address block generation + index alpha + finalization estao
todos RFC 9106 corretos.

**Nota sobre KATs RFC 9106 §A.3.** O vetor canonico do RFC 9106 §A.3
usa `parallelism=4` com secret + AD, fora do escopo desta
implementacao (p=1, sem secret/AD). Para validacao com p=1, usamos
o reference impl `argon2-cffi` (que tambem implementa RFC 9106).

## Build

`@/Users/t808981/Documents/0/CapyOS/Makefile`:

- `CAPYOS64_OBJS += blake2b.o argon2.o`
- `TEST_SRCS += src/security/blake2b.c src/security/argon2.c`

## Threat model

**BLAKE2b (RFC 7693 §2.10):**

- Resistencia a colisoes: 2^256 operacoes (digest 64 bytes).
- Resistencia a preimage: 2^512.
- Indistinguibilidade de PRF quando chaveado.
- Resistencia a length-extension via flag `f[0]` distinto entre blocos
  intermediarios (0) e ultimo bloco (0xFF..FF).

**Argon2id (RFC 9106 §1.1):**

- **Brute-force massivo em GPU/ASIC**: m_cost * 1024 bytes por
  candidate test. Com m_cost = 65536 (64 MiB), ASIC com 1 GB de
  memoria avalia <= 16 candidates em paralelo (vs >10000 para
  PBKDF2-SHA256).
- **TMTO resistance**: reducao de memoria em fator k aumenta tempo em
  fator >= k^2 (ate k = sqrt(m_cost)). Argon2id estende para hibrido.
- **Side-channel timing**: data-independent na primeira metade da
  pass 0 (resistente a cache-timing); data-dependent depois
  (cache-friendly). Hibrido equilibra defesa server-side com
  resistencia a ranking-TMTO.

**Limites de seguranca esta entrega:**

- **memory buffer caller-provided NAO e wipeado automaticamente** —
  caller deve zerar via `volatile_secure_zero` se buffer contem
  material sensivel.
- **password buffer caller NAO e wipeado** — caller responsibility.
- **parallelism = 1 fixo** — Argon2id puro single-threaded; multi-lane
  fora de escopo (CapyOS verifica passwords serialmente).
- **Sem secret K**: nao ha proteccao via server-side key (atacante com
  database tem todo o material para offline crack — apenas memory wall
  protege).

## Mapa de fundacao cripto pos-alpha.218

| Primitiva | Slice | Estado |
|---|---|---|
| SHA-256, SHA-512 | base + alpha.208/209 | ✓ |
| HMAC-SHA256 | base | ✓ |
| PBKDF2-SHA256 | base | ✓ (deprecated para passwords novos) |
| HKDF-SHA256 | alpha.213 | ✓ |
| AES-128-XTS | base | ✓ |
| CSPRNG hardened | alpha.214 | ✓ |
| ChaCha20-Poly1305 AEAD | alpha.215 | ✓ |
| X25519 ECDH | alpha.216 | ✓ |
| Ed25519 signature | alpha.217 | ✓ |
| **BLAKE2b** | **alpha.218** | **✓ novo** |
| **Argon2id password hash** | **alpha.218** | **✓ novo** |

**Cobertura completa de primitivas modernas para password hashing
memory-hard.** Fundacao cripto canonica pronta para integracao em
`userdb`, encrypted volume KEK derivation, e qualquer caller que
precise de KDF resistente a hardware attacks.

## Proximos passos sugeridos

1. **userdb migracao**: novo formato de armazenamento com algorithm
   prefix (`$argon2id$v=19$m=...,t=...,p=1$salt$hash`); validacao
   automatica do PBKDF2 antigo + re-hash com Argon2id em proximo login
   bem-sucedido.
2. **Encrypted volume KEK**: derivar KEK do password do usuario via
   Argon2id em vez de PBKDF2 (matches NIST SP 800-63B guidelines).
3. **Multi-lane Argon2id**: se cargas de auth simultaneas justificarem,
   adicionar parallelism > 1 com thread sync.
4. **Test vectors KAT externos**: adicionar 2-3 vetores Argon2id v1.3
   p=1 gerados via reference impl para validacao deterministica
   cross-version.
