#include "security/argon2.h"
#include "security/blake2b.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Argon2id (RFC 9106): implementacao canonica audit-friendly,
 * parallelism = 1, caller-provided memory.
 *
 * Visao geral do algoritmo (RFC 9106 §3):
 *
 *  1. H0 = BLAKE2b(64, p || tag_len || m || t || version || type ||
 *                  pwd_len || pwd || salt_len || salt ||
 *                  secret_len=0 || ad_len=0)
 *     — pre-hash de todos os parametros + entradas.
 *
 *  2. Inicializa primeiros 2 blocos:
 *     B[0][0] = H'(1024, H0 || LE32(0) || LE32(0))
 *     B[0][1] = H'(1024, H0 || LE32(1) || LE32(0))
 *     (H' = variable-length BLAKE2b per §3.3)
 *
 *  3. Para cada pass r in 0..t-1:
 *     Para cada slice s in 0..3:
 *       Para cada index i no slice:
 *         abs_pos = s * segment_length + i
 *         prev = abs_pos - 1 (wrapping no inicio do lane se pass > 0)
 *         pseudo_rand = address (data-indep para Argon2id em pass=0,
 *                                slice<2; data-dep cc)
 *         ref_pos = ref_area(...) + map(pseudo_rand) mod lane_length
 *         B[abs_pos] = G(B[prev], B[ref])  (pass 0)
 *         B[abs_pos] ^= G(B[prev], B[ref])  (pass > 0)
 *
 *  4. final_block = B[0][q-1]  (p=1, sem XOR cross-lane)
 *     tag = H'(out_len, final_block)
 *
 * Layout do memory buffer:
 *
 *  - Caller fornece buffer plano de m_cost * 1024 bytes.
 *  - Blocos sao serializados em little-endian (RFC 9106 §3.2 — cada
 *    bloco e tratado como 128 uint64_t LE).
 *  - Lane 0 (unica) ocupa todo o buffer; bloco i comeca em
 *    memory + i * 1024.
 *
 * Threat model preservado:
 *
 *  - Constant-time NAO se aplica a Argon2 por design — as funcoes
 *    devem ser memoria-hard, nao tempo-constante.
 *  - Wipe hygiene em H0, address_block, input_block, zero_block,
 *    final_block, blocos intermediarios da compressao.
 *  - Memory buffer NAO e wipeado automaticamente (caller decide).
 *  - Validacao de parametros fail-closed antes de qualquer escrita.
 */

/* ============================================================
 * Helpers
 * ============================================================ */

static uint64_t rotr64(uint64_t x, unsigned n) {
  return (x >> n) | (x << (64u - n));
}

static uint64_t load64_le(const uint8_t *p) {
  return  (uint64_t)p[0]        | ((uint64_t)p[1] << 8)  |
         ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
         ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static void store64_le(uint8_t *p, uint64_t x) {
  p[0] = (uint8_t)(x       & 0xFFu);
  p[1] = (uint8_t)((x >>  8) & 0xFFu);
  p[2] = (uint8_t)((x >> 16) & 0xFFu);
  p[3] = (uint8_t)((x >> 24) & 0xFFu);
  p[4] = (uint8_t)((x >> 32) & 0xFFu);
  p[5] = (uint8_t)((x >> 40) & 0xFFu);
  p[6] = (uint8_t)((x >> 48) & 0xFFu);
  p[7] = (uint8_t)((x >> 56) & 0xFFu);
}

static void store32_le(uint8_t *p, uint32_t x) {
  p[0] = (uint8_t)(x        & 0xFFu);
  p[1] = (uint8_t)((x >>  8) & 0xFFu);
  p[2] = (uint8_t)((x >> 16) & 0xFFu);
  p[3] = (uint8_t)((x >> 24) & 0xFFu);
}

static void wipe_bytes(void *p, size_t n) {
  volatile uint8_t *vp = (volatile uint8_t *)p;
  while (n--) {
    *vp++ = 0u;
  }
}

/* Block load/store: serializa 1024 bytes <-> 128 uint64_t LE. */
static void load_block(uint64_t out[128], const uint8_t in[ARGON2_BLOCK_SIZE]) {
  for (int i = 0; i < 128; ++i) {
    out[i] = load64_le(in + 8 * i);
  }
}

static void store_block(uint8_t out[ARGON2_BLOCK_SIZE], const uint64_t in[128]) {
  for (int i = 0; i < 128; ++i) {
    store64_le(out + 8 * i, in[i]);
  }
}

/* ============================================================
 * H' (variable-length BLAKE2b) per RFC 9106 §3.3
 * ============================================================ */

/*
 * H'(T, X): produz T bytes a partir de entrada X via BLAKE2b iterado.
 *
 * Se T <= 64: blake2b(out, T, NULL, 0, LE32(T) || X).
 * Caso contrario:
 *   r = ceil(T / 32) - 2
 *   V_1 = blake2b(64, NULL, 0, LE32(T) || X)
 *   out[0..32] = V_1[0..32]
 *   Para i = 2..r:
 *     V_i = blake2b(64, NULL, 0, V_{i-1})
 *     out[32*(i-1)..32*i] = V_i[0..32]
 *   V_{r+1} = blake2b(T - 32*r, NULL, 0, V_r)
 *   out[32*r..T] = V_{r+1}
 *
 * NB: a parte tail emite (T - 32*r) bytes; quando T % 32 == 0,
 * isso e 32 bytes; quando T % 32 != 0, e o residuo. RFC 9106 §3.3
 * specifica isso explicitamente.
 *
 * Wipe: V buffer zerado em todos os exits.
 */
static int hash_prime(uint8_t *out, uint32_t T,
                      const uint8_t *X, size_t X_len) {
  if (!out || T == 0u) {
    return -1;
  }

  uint8_t T_le[4];
  store32_le(T_le, T);

  if (T <= BLAKE2B_DIGEST_SIZE) {
    struct blake2b_ctx ctx;
    if (blake2b_init(&ctx, T, NULL, 0) != 0) {
      blake2b_wipe(&ctx);
      return -1;
    }
    blake2b_update(&ctx, T_le, sizeof(T_le));
    if (X_len > 0u) {
      blake2b_update(&ctx, X, X_len);
    }
    blake2b_final(&ctx, out);
    blake2b_wipe(&ctx);
    return 0;
  }

  /* Long output: r intermediate 64-byte blocks, each contributing 32
   * bytes; then a final block of size (T - 32*r) bytes.
   * r = ceil(T/32) - 2. */
  uint32_t r = ((T + 31u) / 32u) - 2u;

  uint8_t V[BLAKE2B_DIGEST_SIZE];

  /* V_1 = BLAKE2b(64, LE32(T) || X) */
  {
    struct blake2b_ctx ctx;
    if (blake2b_init(&ctx, BLAKE2B_DIGEST_SIZE, NULL, 0) != 0) {
      blake2b_wipe(&ctx);
      wipe_bytes(V, sizeof(V));
      return -1;
    }
    blake2b_update(&ctx, T_le, sizeof(T_le));
    if (X_len > 0u) {
      blake2b_update(&ctx, X, X_len);
    }
    blake2b_final(&ctx, V);
    blake2b_wipe(&ctx);
  }
  for (uint32_t i = 0; i < 32u; ++i) {
    out[i] = V[i];
  }

  /* V_i for i = 2..r */
  for (uint32_t j = 2u; j <= r; ++j) {
    uint8_t V_next[BLAKE2B_DIGEST_SIZE];
    struct blake2b_ctx ctx;
    if (blake2b_init(&ctx, BLAKE2B_DIGEST_SIZE, NULL, 0) != 0) {
      blake2b_wipe(&ctx);
      wipe_bytes(V, sizeof(V));
      wipe_bytes(V_next, sizeof(V_next));
      return -1;
    }
    blake2b_update(&ctx, V, BLAKE2B_DIGEST_SIZE);
    blake2b_final(&ctx, V_next);
    blake2b_wipe(&ctx);
    for (uint32_t i = 0; i < 32u; ++i) {
      out[32u * (j - 1u) + i] = V_next[i];
    }
    for (uint32_t i = 0; i < BLAKE2B_DIGEST_SIZE; ++i) {
      V[i] = V_next[i];
    }
    wipe_bytes(V_next, sizeof(V_next));
  }

  /* Tail: V_{r+1} of size (T - 32*r) */
  uint32_t tail_len = T - 32u * r;
  {
    struct blake2b_ctx ctx;
    if (blake2b_init(&ctx, tail_len, NULL, 0) != 0) {
      blake2b_wipe(&ctx);
      wipe_bytes(V, sizeof(V));
      return -1;
    }
    blake2b_update(&ctx, V, BLAKE2B_DIGEST_SIZE);
    blake2b_final(&ctx, out + 32u * r);
    blake2b_wipe(&ctx);
  }

  wipe_bytes(V, sizeof(V));
  return 0;
}

/* ============================================================
 * G compression function per RFC 9106 §3.6
 * ============================================================ */

/*
 * fBlaMka(x, y) = x + y + 2 * (x_lo * y_lo)
 * onde x_lo, y_lo sao os 32 bits baixos.
 *
 * Esta e a modificacao Argon2 da G function de BLAKE2 — adiciona
 * uma multiplicacao 32x32->64 que aumenta o cost-per-op em ASIC.
 */
static uint64_t fBlaMka(uint64_t x, uint64_t y) {
  const uint64_t m = 0xFFFFFFFFu;
  uint64_t xy = (x & m) * (y & m);
  return x + y + 2u * xy;
}

#define ARGON2_GB(a, b, c, d) do {              \
    a = fBlaMka(a, b); d = rotr64(d ^ a, 32);   \
    c = fBlaMka(c, d); b = rotr64(b ^ c, 24);   \
    a = fBlaMka(a, b); d = rotr64(d ^ a, 16);   \
    c = fBlaMka(c, d); b = rotr64(b ^ c, 63);   \
  } while (0)

/*
 * P: aplica uma rodada do BLAKE2 round function (sem sigma — identidade)
 * sobre 16 uint64_t v[0..15]. Equivalente a BLAKE2b round 0 mas com
 * fBlaMka ao inves de soma simples.
 */
static void argon2_P(uint64_t v[16]) {
  ARGON2_GB(v[ 0], v[ 4], v[ 8], v[12]);
  ARGON2_GB(v[ 1], v[ 5], v[ 9], v[13]);
  ARGON2_GB(v[ 2], v[ 6], v[10], v[14]);
  ARGON2_GB(v[ 3], v[ 7], v[11], v[15]);
  ARGON2_GB(v[ 0], v[ 5], v[10], v[15]);
  ARGON2_GB(v[ 1], v[ 6], v[11], v[12]);
  ARGON2_GB(v[ 2], v[ 7], v[ 8], v[13]);
  ARGON2_GB(v[ 3], v[ 4], v[ 9], v[14]);
}

/*
 * G(X, Y) -> Z onde X, Y, Z sao blocos de 1024 bytes (128 uint64_t).
 *
 *  R = X XOR Y
 *  View R como matriz 8x8 de registers 16-byte (= 2 uint64_t).
 *  Apply P row-wise: 8 chamadas a P, cada uma sobre 8 registers
 *    consecutivos = 16 uint64_t (= 128 bytes = uma "row").
 *  Apply P column-wise: 8 chamadas, cada uma sobre 8 registers numa
 *    coluna = 16 uint64_t (interleaved no buffer).
 *  Z = (intermediate result) XOR R
 *
 * Wipe: R, Z, col intermediarios zerados antes do retorno.
 */
static void argon2_G(const uint64_t X[128], const uint64_t Y[128],
                     uint64_t Z_out[128]) {
  uint64_t R[128];
  uint64_t Z[128];

  for (int i = 0; i < 128; ++i) {
    R[i] = X[i] ^ Y[i];
    Z[i] = R[i];
  }

  /* Row-wise: 8 rows, each 16 uint64. */
  for (int row = 0; row < 8; ++row) {
    argon2_P(&Z[row * 16]);
  }

  /* Column-wise: 8 columns, each 8 registers (16 uint64) interleaved.
   * Column c uses Z[r*16 + 2c + 0] and Z[r*16 + 2c + 1] for r=0..7. */
  for (int col = 0; col < 8; ++col) {
    uint64_t v[16];
    for (int row = 0; row < 8; ++row) {
      v[2 * row + 0] = Z[row * 16 + 2 * col + 0];
      v[2 * row + 1] = Z[row * 16 + 2 * col + 1];
    }
    argon2_P(v);
    for (int row = 0; row < 8; ++row) {
      Z[row * 16 + 2 * col + 0] = v[2 * row + 0];
      Z[row * 16 + 2 * col + 1] = v[2 * row + 1];
    }
    wipe_bytes(v, sizeof(v));
  }

  for (int i = 0; i < 128; ++i) {
    Z_out[i] = Z[i] ^ R[i];
  }
  wipe_bytes(R, sizeof(R));
  wipe_bytes(Z, sizeof(Z));
}

/* ============================================================
 * Address block generation (data-independent indexing path)
 * ============================================================ */

/*
 * next_addresses: regenera o address block invocando G duas vezes
 * com zero_block como X e input_block (ou address atual) como Y.
 *
 *   tmp          = G(zero, input_block)
 *   address_block = G(zero, tmp)
 *
 * O counter input_block[6] (offset 48..55) deve ter sido incrementado
 * pelo caller antes desta chamada.
 */
static void argon2_next_addresses(uint64_t address_block[128],
                                  const uint64_t input_block[128],
                                  const uint64_t zero_block[128]) {
  uint64_t tmp[128];
  argon2_G(zero_block, input_block, tmp);
  argon2_G(zero_block, tmp, address_block);
  wipe_bytes(tmp, sizeof(tmp));
}

/* ============================================================
 * Argon2id main: pre-hash, init, main loop, finalize
 * ============================================================ */

int argon2id_hash(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t t_cost, uint32_t m_cost,
                  uint8_t *memory, size_t memory_len,
                  uint8_t *out, size_t out_len) {
  /* ---- Validacao fail-closed ---- */
  if (!out || out_len < ARGON2_MIN_OUT_LEN) {
    return -1;
  }
  if (!salt || salt_len < ARGON2_MIN_SALT_LEN) {
    return -1;
  }
  if (password_len > 0u && !password) {
    return -1;
  }
  if (t_cost < ARGON2_MIN_T_COST) {
    return -1;
  }
  if (m_cost < ARGON2_MIN_M_COST) {
    return -1;
  }
  if (!memory) {
    return -1;
  }

  /* parallelism = 1 fixo; m_prime e ajustado para multiplo de 4*p = 4. */
  const uint32_t p = 1u;
  uint32_t m_prime = m_cost;
  if (m_prime < 8u * p) {
    m_prime = 8u * p;
  }
  m_prime = (m_prime / (4u * p)) * (4u * p);
  if (m_prime < 8u) {
    return -1;
  }

  const uint32_t segment_length = m_prime / (4u * p);
  const uint32_t lane_length    = segment_length * 4u;

  /* memory deve acomodar lane_length blocos de 1024 bytes. */
  if (memory_len < (size_t)lane_length * (size_t)ARGON2_BLOCK_SIZE) {
    return -1;
  }

  /* ---- Pre-hash H0 ---- */
  uint8_t H0[BLAKE2B_DIGEST_SIZE];
  {
    struct blake2b_ctx ctx;
    if (blake2b_init(&ctx, BLAKE2B_DIGEST_SIZE, NULL, 0) != 0) {
      blake2b_wipe(&ctx);
      wipe_bytes(H0, sizeof(H0));
      return -1;
    }
    uint8_t le[4];

    store32_le(le, p);                            blake2b_update(&ctx, le, 4);
    store32_le(le, (uint32_t)out_len);            blake2b_update(&ctx, le, 4);
    store32_le(le, m_cost);                       blake2b_update(&ctx, le, 4);
    store32_le(le, t_cost);                       blake2b_update(&ctx, le, 4);
    store32_le(le, (uint32_t)ARGON2_VERSION_13);  blake2b_update(&ctx, le, 4);
    store32_le(le, (uint32_t)ARGON2_TYPE_ID);     blake2b_update(&ctx, le, 4);

    store32_le(le, (uint32_t)password_len);       blake2b_update(&ctx, le, 4);
    if (password_len > 0u) {
      blake2b_update(&ctx, password, password_len);
    }
    store32_le(le, (uint32_t)salt_len);           blake2b_update(&ctx, le, 4);
    blake2b_update(&ctx, salt, salt_len);

    /* secret K (no support): length 0 */
    store32_le(le, 0u);                            blake2b_update(&ctx, le, 4);
    /* associated data X (no support): length 0 */
    store32_le(le, 0u);                            blake2b_update(&ctx, le, 4);

    blake2b_final(&ctx, H0);
    blake2b_wipe(&ctx);
    wipe_bytes(le, sizeof(le));
  }

  /* ---- Inicializa primeiros 2 blocos do lane ----
   *   B[0] = H'(1024, H0 || LE32(0) || LE32(0))
   *   B[1] = H'(1024, H0 || LE32(1) || LE32(0))
   */
  {
    uint8_t prefix[BLAKE2B_DIGEST_SIZE + 8u];
    for (uint32_t i = 0; i < BLAKE2B_DIGEST_SIZE; ++i) {
      prefix[i] = H0[i];
    }
    store32_le(prefix + BLAKE2B_DIGEST_SIZE,     0u);
    store32_le(prefix + BLAKE2B_DIGEST_SIZE + 4u, 0u);
    if (hash_prime(memory + 0u * ARGON2_BLOCK_SIZE,
                   ARGON2_BLOCK_SIZE, prefix, sizeof(prefix)) != 0) {
      wipe_bytes(prefix, sizeof(prefix));
      wipe_bytes(H0, sizeof(H0));
      return -1;
    }
    store32_le(prefix + BLAKE2B_DIGEST_SIZE,     1u);
    if (hash_prime(memory + 1u * ARGON2_BLOCK_SIZE,
                   ARGON2_BLOCK_SIZE, prefix, sizeof(prefix)) != 0) {
      wipe_bytes(prefix, sizeof(prefix));
      wipe_bytes(H0, sizeof(H0));
      return -1;
    }
    wipe_bytes(prefix, sizeof(prefix));
  }

  /* ---- Main loop: t_cost passes, 4 slices, segment_length blocos cada ---- */
  uint64_t zero_block[128];
  for (int i = 0; i < 128; ++i) {
    zero_block[i] = 0u;
  }

  for (uint32_t pass = 0u; pass < t_cost; ++pass) {
    for (uint32_t slice = 0u; slice < ARGON2_SYNC_POINTS; ++slice) {
      /* Argon2id: data-independent address gen para (pass=0, slice<2).
       *           Caso contrario, data-dependent (use prev block). */
      int data_independent = (pass == 0u && slice < 2u);

      uint64_t input_block[128];
      uint64_t address_block[128];

      if (data_independent) {
        for (int i = 0; i < 128; ++i) {
          input_block[i]   = 0u;
          address_block[i] = 0u;
        }
        input_block[0] = (uint64_t)pass;
        input_block[1] = (uint64_t)0u;          /* lane = 0 */
        input_block[2] = (uint64_t)slice;
        input_block[3] = (uint64_t)m_prime;
        input_block[4] = (uint64_t)t_cost;
        input_block[5] = (uint64_t)ARGON2_TYPE_ID;
        /* input_block[6] = counter; incremented before each new addr block */
      }

      uint32_t start_index = 0u;
      if (pass == 0u && slice == 0u) {
        /* Skip first two blocks (already initialized). */
        start_index = 2u;
        if (data_independent) {
          /* Pre-generate first address block since the natural
           * (index % 128 == 0) check at index = 2 would miss it. */
          input_block[6] = 1u;
          argon2_next_addresses(address_block, input_block, zero_block);
        }
      }

      for (uint32_t index = start_index; index < segment_length; ++index) {
        uint32_t abs_pos = slice * segment_length + index;
        uint32_t prev_pos = (abs_pos == 0u) ? (lane_length - 1u)
                                            : (abs_pos - 1u);

        /* Pseudo-random (J1, J2). For p=1, J2 is unused (always 0
         * lane). J1 is the low 32 bits of the 64-bit pseudo_rand. */
        uint64_t pseudo_rand;
        if (data_independent) {
          if ((index % ARGON2_ADDRESSES_IN_BLOCK) == 0u) {
            input_block[6] += 1u;
            argon2_next_addresses(address_block, input_block, zero_block);
          }
          pseudo_rand = address_block[index % ARGON2_ADDRESSES_IN_BLOCK];
        } else {
          /* Data-dependent: J1||J2 = first 8 bytes of B[prev_pos]. */
          pseudo_rand = load64_le(memory + (size_t)prev_pos * ARGON2_BLOCK_SIZE);
        }
        uint32_t J1 = (uint32_t)(pseudo_rand & 0xFFFFFFFFu);
        /* J2 = (uint32_t)(pseudo_rand >> 32); unused for p=1. */

        /* Reference area size per RFC 9106 §3.4.1.2 (same lane only
         * since p=1). */
        uint32_t ref_area_size;
        if (pass == 0u) {
          if (slice == 0u) {
            /* index >= 2 here (start_index = 2). */
            ref_area_size = index - 1u;
          } else {
            ref_area_size = slice * segment_length + index - 1u;
          }
        } else {
          /* Subsequent passes: exclude current segment. */
          ref_area_size = lane_length - segment_length + index - 1u;
        }

        /* Map J1 to relative position (RFC 9106 §3.4.1.2 step 1.2.4). */
        uint64_t rel_pos = (uint64_t)J1;
        rel_pos = (rel_pos * rel_pos) >> 32;
        rel_pos = (uint64_t)ref_area_size - 1u -
                  (((uint64_t)ref_area_size * rel_pos) >> 32);

        /* Start position (RFC 9106 §3.4.1.2 step 1.2.5). */
        uint32_t start_pos = 0u;
        if (pass != 0u) {
          start_pos = (slice == ARGON2_SYNC_POINTS - 1u)
                          ? 0u
                          : (slice + 1u) * segment_length;
        }

        uint32_t ref_pos = (uint32_t)(((uint64_t)start_pos + rel_pos) % lane_length);

        /* Compute new block: B[abs_pos] = G(B[prev], B[ref])  (pass 0)
         *                    B[abs_pos] ^= G(B[prev], B[ref]) (pass > 0)
         * (Argon2 v1.3 semantic: pass > 0 XOR-overwrites.) */
        uint64_t prev_block[128];
        uint64_t ref_block[128];
        uint64_t new_block[128];
        load_block(prev_block, memory + (size_t)prev_pos * ARGON2_BLOCK_SIZE);
        load_block(ref_block,  memory + (size_t)ref_pos  * ARGON2_BLOCK_SIZE);
        argon2_G(prev_block, ref_block, new_block);

        if (pass > 0u) {
          uint64_t existing[128];
          load_block(existing, memory + (size_t)abs_pos * ARGON2_BLOCK_SIZE);
          for (int i = 0; i < 128; ++i) {
            new_block[i] ^= existing[i];
          }
          wipe_bytes(existing, sizeof(existing));
        }
        store_block(memory + (size_t)abs_pos * ARGON2_BLOCK_SIZE, new_block);

        wipe_bytes(prev_block, sizeof(prev_block));
        wipe_bytes(ref_block,  sizeof(ref_block));
        wipe_bytes(new_block,  sizeof(new_block));
      } /* index */

      wipe_bytes(input_block,   sizeof(input_block));
      wipe_bytes(address_block, sizeof(address_block));
    } /* slice */
  } /* pass */

  wipe_bytes(zero_block, sizeof(zero_block));

  /* ---- Finalize: for p=1, final = B[lane_length - 1]; tag = H'(out_len, final) ---- */
  uint8_t final_block[ARGON2_BLOCK_SIZE];
  for (uint32_t i = 0u; i < ARGON2_BLOCK_SIZE; ++i) {
    final_block[i] = memory[(size_t)(lane_length - 1u) * ARGON2_BLOCK_SIZE + i];
  }

  int rc = hash_prime(out, (uint32_t)out_len, final_block, ARGON2_BLOCK_SIZE);

  wipe_bytes(final_block, sizeof(final_block));
  wipe_bytes(H0, sizeof(H0));
  return rc;
}
