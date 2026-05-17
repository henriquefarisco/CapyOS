/*
 * src/security/ed25519_scalar.c
 *
 * Scalar arithmetic modulo L for Ed25519, extracted from the
 * pre-split `src/security/ed25519.c` monolith (PR A.3 of the
 * dedicated monolith residual plan).
 *
 * Hosts:
 *   - `ED_L_BYTES` : the little-endian encoded subgroup order.
 *   - `load_3` / `load_4` : byte-stream loaders that feed the
 *     21-bit-limb representation.
 *   - `sc_reduce64`     : 64-byte SHA-512 digest -> 32-byte canonical
 *     scalar modulo L.
 *   - `sc_muladd`       : (a*b + c) mod L on 32-byte little-endian
 *     scalars.
 *   - `sc_is_canonical` : constant-time `s < L` predicate on a
 *     32-byte little-endian buffer.
 *
 * The implementations are byte-for-byte identical to the
 * pre-split originals; only the storage class changed for the
 * symbols that are now consumed by `src/security/ed25519.c` (public
 * sign/verify entry points). See
 * `src/security/internal/ed25519_internal.h` for the exposed surface
 * and `docs/plans/active/monolith-residual-dedicated-plan.md` (§2,
 * Estagio A.3) for the rationale.
 *
 * Threat model preserved from the monolith:
 *   - Constant-time arithmetic over the secret scalar `s`: every
 *     branch in `sc_reduce64` and `sc_muladd` depends only on fixed
 *     constants or on the carry chain (data-independent), so secret
 *     bits never steer control flow.
 *   - `sc_is_canonical` uses an explicit accept/reject mask without
 *     short-circuit branches, so even when called on attacker-chosen
 *     S halves of a forged signature it does not leak through timing.
 *   - No dynamic allocation; every limb lives on the stack so the
 *     wipe done by `ed25519_sign` after the muladd is final.
 *   - `ED_L_BYTES` is read-only data and is never wiped.
 */
#include "security/internal/ed25519_internal.h"

#include <stddef.h>
#include <stdint.h>

/* L = 2^252 + 27742317777372353535851937790883648493 (ordem do subgrupo
 * prime). Encoded little-endian 32 bytes. Consumida apenas por
 * `sc_is_canonical`; mantida `static` para impedir consumo por outros
 * arquivos sem auditoria. */
static const uint8_t ED_L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

/* ============================================================
 * Scalar arithmetic mod L.
 *
 * L = 2^252 + 27742317777372353535851937790883648493.
 *
 * Representacao em 24 limbs signed de 21 bits (252-bit / 21 = 12,
 * mais 12 extras para acomodar produtos ate 2*512 bits). Porte de
 * ref10/sc25519.c (Bernstein et al.), audit-verified.
 *
 * Fato chave: 2^252 ≡ -L_lo (mod L), onde L_lo = 27742...8493 expandido
 * em 21-bit signed limbs: [666643, 470296, 654183, -997805, 136657,
 * -683901]. Reducao folds high limbs s_i (i >= 12) em low limbs
 * subtraindo s_i * L_lo * 2^(21*(i-12)).
 * ============================================================ */

static int64_t load_3(const uint8_t *in) {
  return (int64_t)((uint64_t)in[0] | ((uint64_t)in[1] << 8) |
                   ((uint64_t)in[2] << 16));
}

static int64_t load_4(const uint8_t *in) {
  return (int64_t)((uint64_t)in[0] | ((uint64_t)in[1] << 8) |
                   ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 24));
}

/*
 * sc_reduce: reduz 64 bytes (LE) mod L. Output em 32 bytes LE
 * canonico em [0, L).
 *
 * Algoritmo: parse 24 21-bit limbs, fold high 12 em low 12 via
 * cascading multiply-and-add, recarregar, propagate carries,
 * pack em 32 bytes.
 */
void sc_reduce64(uint8_t out[32], const uint8_t in[64]) {
  int64_t s0 = 2097151 & load_3(in);
  int64_t s1 = 2097151 & (load_4(in + 2) >> 5);
  int64_t s2 = 2097151 & (load_3(in + 5) >> 2);
  int64_t s3 = 2097151 & (load_4(in + 7) >> 7);
  int64_t s4 = 2097151 & (load_4(in + 10) >> 4);
  int64_t s5 = 2097151 & (load_3(in + 13) >> 1);
  int64_t s6 = 2097151 & (load_4(in + 15) >> 6);
  int64_t s7 = 2097151 & (load_3(in + 18) >> 3);
  int64_t s8 = 2097151 & load_3(in + 21);
  int64_t s9 = 2097151 & (load_4(in + 23) >> 5);
  int64_t s10 = 2097151 & (load_3(in + 26) >> 2);
  int64_t s11 = 2097151 & (load_4(in + 28) >> 7);
  int64_t s12 = 2097151 & (load_4(in + 31) >> 4);
  int64_t s13 = 2097151 & (load_3(in + 34) >> 1);
  int64_t s14 = 2097151 & (load_4(in + 36) >> 6);
  int64_t s15 = 2097151 & (load_3(in + 39) >> 3);
  int64_t s16 = 2097151 & load_3(in + 42);
  int64_t s17 = 2097151 & (load_4(in + 44) >> 5);
  int64_t s18 = 2097151 & (load_3(in + 47) >> 2);
  int64_t s19 = 2097151 & (load_4(in + 49) >> 7);
  int64_t s20 = 2097151 & (load_4(in + 52) >> 4);
  int64_t s21 = 2097151 & (load_3(in + 55) >> 1);
  int64_t s22 = 2097151 & (load_4(in + 57) >> 6);
  int64_t s23 = (load_4(in + 60) >> 3);
  int64_t carry0, carry1, carry2, carry3, carry4, carry5, carry6;
  int64_t carry7, carry8, carry9, carry10, carry11, carry12, carry13;
  int64_t carry14, carry15, carry16;

  s11 += s23 * 666643;
  s12 += s23 * 470296;
  s13 += s23 * 654183;
  s14 -= s23 * 997805;
  s15 += s23 * 136657;
  s16 -= s23 * 683901;
  s23 = 0;
  s10 += s22 * 666643;
  s11 += s22 * 470296;
  s12 += s22 * 654183;
  s13 -= s22 * 997805;
  s14 += s22 * 136657;
  s15 -= s22 * 683901;
  s22 = 0;
  s9 += s21 * 666643;
  s10 += s21 * 470296;
  s11 += s21 * 654183;
  s12 -= s21 * 997805;
  s13 += s21 * 136657;
  s14 -= s21 * 683901;
  s21 = 0;
  s8 += s20 * 666643;
  s9 += s20 * 470296;
  s10 += s20 * 654183;
  s11 -= s20 * 997805;
  s12 += s20 * 136657;
  s13 -= s20 * 683901;
  s20 = 0;
  s7 += s19 * 666643;
  s8 += s19 * 470296;
  s9 += s19 * 654183;
  s10 -= s19 * 997805;
  s11 += s19 * 136657;
  s12 -= s19 * 683901;
  s19 = 0;
  s6 += s18 * 666643;
  s7 += s18 * 470296;
  s8 += s18 * 654183;
  s9 -= s18 * 997805;
  s10 += s18 * 136657;
  s11 -= s18 * 683901;
  s18 = 0;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry12 = (s12 + (1LL << 20)) >> 21;
  s13 += carry12;
  s12 -= carry12 << 21;
  carry14 = (s14 + (1LL << 20)) >> 21;
  s15 += carry14;
  s14 -= carry14 << 21;
  carry16 = (s16 + (1LL << 20)) >> 21;
  s17 += carry16;
  s16 -= carry16 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  carry13 = (s13 + (1LL << 20)) >> 21;
  s14 += carry13;
  s13 -= carry13 << 21;
  carry15 = (s15 + (1LL << 20)) >> 21;
  s16 += carry15;
  s15 -= carry15 << 21;
  s5 += s17 * 666643;
  s6 += s17 * 470296;
  s7 += s17 * 654183;
  s8 -= s17 * 997805;
  s9 += s17 * 136657;
  s10 -= s17 * 683901;
  s17 = 0;
  s4 += s16 * 666643;
  s5 += s16 * 470296;
  s6 += s16 * 654183;
  s7 -= s16 * 997805;
  s8 += s16 * 136657;
  s9 -= s16 * 683901;
  s16 = 0;
  s3 += s15 * 666643;
  s4 += s15 * 470296;
  s5 += s15 * 654183;
  s6 -= s15 * 997805;
  s7 += s15 * 136657;
  s8 -= s15 * 683901;
  s15 = 0;
  s2 += s14 * 666643;
  s3 += s14 * 470296;
  s4 += s14 * 654183;
  s5 -= s14 * 997805;
  s6 += s14 * 136657;
  s7 -= s14 * 683901;
  s14 = 0;
  s1 += s13 * 666643;
  s2 += s13 * 470296;
  s3 += s13 * 654183;
  s4 -= s13 * 997805;
  s5 += s13 * 136657;
  s6 -= s13 * 683901;
  s13 = 0;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = (s0 + (1LL << 20)) >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry2 = (s2 + (1LL << 20)) >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry4 = (s4 + (1LL << 20)) >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry1 = (s1 + (1LL << 20)) >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry3 = (s3 + (1LL << 20)) >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry5 = (s5 + (1LL << 20)) >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry11 = s11 >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;

  out[0] = (uint8_t)(s0 >> 0);
  out[1] = (uint8_t)(s0 >> 8);
  out[2] = (uint8_t)((s0 >> 16) | (s1 << 5));
  out[3] = (uint8_t)(s1 >> 3);
  out[4] = (uint8_t)(s1 >> 11);
  out[5] = (uint8_t)((s1 >> 19) | (s2 << 2));
  out[6] = (uint8_t)(s2 >> 6);
  out[7] = (uint8_t)((s2 >> 14) | (s3 << 7));
  out[8] = (uint8_t)(s3 >> 1);
  out[9] = (uint8_t)(s3 >> 9);
  out[10] = (uint8_t)((s3 >> 17) | (s4 << 4));
  out[11] = (uint8_t)(s4 >> 4);
  out[12] = (uint8_t)(s4 >> 12);
  out[13] = (uint8_t)((s4 >> 20) | (s5 << 1));
  out[14] = (uint8_t)(s5 >> 7);
  out[15] = (uint8_t)((s5 >> 15) | (s6 << 6));
  out[16] = (uint8_t)(s6 >> 2);
  out[17] = (uint8_t)(s6 >> 10);
  out[18] = (uint8_t)((s6 >> 18) | (s7 << 3));
  out[19] = (uint8_t)(s7 >> 5);
  out[20] = (uint8_t)(s7 >> 13);
  out[21] = (uint8_t)(s8 >> 0);
  out[22] = (uint8_t)(s8 >> 8);
  out[23] = (uint8_t)((s8 >> 16) | (s9 << 5));
  out[24] = (uint8_t)(s9 >> 3);
  out[25] = (uint8_t)(s9 >> 11);
  out[26] = (uint8_t)((s9 >> 19) | (s10 << 2));
  out[27] = (uint8_t)(s10 >> 6);
  out[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
  out[29] = (uint8_t)(s11 >> 1);
  out[30] = (uint8_t)(s11 >> 9);
  out[31] = (uint8_t)(s11 >> 17);
}

/*
 * sc_muladd: out = (a*b + c) mod L. Porte de ref10/sc25519.c.
 * Todos os scalars sao 32 bytes LE em [0, L).
 */
void sc_muladd(uint8_t out[32], const uint8_t a[32],
               const uint8_t b[32], const uint8_t c[32]) {
  int64_t a0 = 2097151 & load_3(a);
  int64_t a1 = 2097151 & (load_4(a + 2) >> 5);
  int64_t a2 = 2097151 & (load_3(a + 5) >> 2);
  int64_t a3 = 2097151 & (load_4(a + 7) >> 7);
  int64_t a4 = 2097151 & (load_4(a + 10) >> 4);
  int64_t a5 = 2097151 & (load_3(a + 13) >> 1);
  int64_t a6 = 2097151 & (load_4(a + 15) >> 6);
  int64_t a7 = 2097151 & (load_3(a + 18) >> 3);
  int64_t a8 = 2097151 & load_3(a + 21);
  int64_t a9 = 2097151 & (load_4(a + 23) >> 5);
  int64_t a10 = 2097151 & (load_3(a + 26) >> 2);
  int64_t a11 = (load_4(a + 28) >> 7);
  int64_t b0 = 2097151 & load_3(b);
  int64_t b1 = 2097151 & (load_4(b + 2) >> 5);
  int64_t b2 = 2097151 & (load_3(b + 5) >> 2);
  int64_t b3 = 2097151 & (load_4(b + 7) >> 7);
  int64_t b4 = 2097151 & (load_4(b + 10) >> 4);
  int64_t b5 = 2097151 & (load_3(b + 13) >> 1);
  int64_t b6 = 2097151 & (load_4(b + 15) >> 6);
  int64_t b7 = 2097151 & (load_3(b + 18) >> 3);
  int64_t b8 = 2097151 & load_3(b + 21);
  int64_t b9 = 2097151 & (load_4(b + 23) >> 5);
  int64_t b10 = 2097151 & (load_3(b + 26) >> 2);
  int64_t b11 = (load_4(b + 28) >> 7);
  int64_t c0 = 2097151 & load_3(c);
  int64_t c1 = 2097151 & (load_4(c + 2) >> 5);
  int64_t c2 = 2097151 & (load_3(c + 5) >> 2);
  int64_t c3 = 2097151 & (load_4(c + 7) >> 7);
  int64_t c4 = 2097151 & (load_4(c + 10) >> 4);
  int64_t c5 = 2097151 & (load_3(c + 13) >> 1);
  int64_t c6 = 2097151 & (load_4(c + 15) >> 6);
  int64_t c7 = 2097151 & (load_3(c + 18) >> 3);
  int64_t c8 = 2097151 & load_3(c + 21);
  int64_t c9 = 2097151 & (load_4(c + 23) >> 5);
  int64_t c10 = 2097151 & (load_3(c + 26) >> 2);
  int64_t c11 = (load_4(c + 28) >> 7);
  int64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  int64_t s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23;
  int64_t carry0, carry1, carry2, carry3, carry4, carry5, carry6;
  int64_t carry7, carry8, carry9, carry10, carry11, carry12, carry13;
  int64_t carry14, carry15, carry16, carry17, carry18, carry19, carry20;
  int64_t carry21, carry22;

  s0 = c0 + a0 * b0;
  s1 = c1 + a0 * b1 + a1 * b0;
  s2 = c2 + a0 * b2 + a1 * b1 + a2 * b0;
  s3 = c3 + a0 * b3 + a1 * b2 + a2 * b1 + a3 * b0;
  s4 = c4 + a0 * b4 + a1 * b3 + a2 * b2 + a3 * b1 + a4 * b0;
  s5 = c5 + a0 * b5 + a1 * b4 + a2 * b3 + a3 * b2 + a4 * b1 + a5 * b0;
  s6 = c6 + a0 * b6 + a1 * b5 + a2 * b4 + a3 * b3 + a4 * b2 + a5 * b1 +
       a6 * b0;
  s7 = c7 + a0 * b7 + a1 * b6 + a2 * b5 + a3 * b4 + a4 * b3 + a5 * b2 +
       a6 * b1 + a7 * b0;
  s8 = c8 + a0 * b8 + a1 * b7 + a2 * b6 + a3 * b5 + a4 * b4 + a5 * b3 +
       a6 * b2 + a7 * b1 + a8 * b0;
  s9 = c9 + a0 * b9 + a1 * b8 + a2 * b7 + a3 * b6 + a4 * b5 + a5 * b4 +
       a6 * b3 + a7 * b2 + a8 * b1 + a9 * b0;
  s10 = c10 + a0 * b10 + a1 * b9 + a2 * b8 + a3 * b7 + a4 * b6 +
        a5 * b5 + a6 * b4 + a7 * b3 + a8 * b2 + a9 * b1 + a10 * b0;
  s11 = c11 + a0 * b11 + a1 * b10 + a2 * b9 + a3 * b8 + a4 * b7 +
        a5 * b6 + a6 * b5 + a7 * b4 + a8 * b3 + a9 * b2 + a10 * b1 +
        a11 * b0;
  s12 = a1 * b11 + a2 * b10 + a3 * b9 + a4 * b8 + a5 * b7 + a6 * b6 +
        a7 * b5 + a8 * b4 + a9 * b3 + a10 * b2 + a11 * b1;
  s13 = a2 * b11 + a3 * b10 + a4 * b9 + a5 * b8 + a6 * b7 + a7 * b6 +
        a8 * b5 + a9 * b4 + a10 * b3 + a11 * b2;
  s14 = a3 * b11 + a4 * b10 + a5 * b9 + a6 * b8 + a7 * b7 + a8 * b6 +
        a9 * b5 + a10 * b4 + a11 * b3;
  s15 = a4 * b11 + a5 * b10 + a6 * b9 + a7 * b8 + a8 * b7 + a9 * b6 +
        a10 * b5 + a11 * b4;
  s16 = a5 * b11 + a6 * b10 + a7 * b9 + a8 * b8 + a9 * b7 + a10 * b6 +
        a11 * b5;
  s17 = a6 * b11 + a7 * b10 + a8 * b9 + a9 * b8 + a10 * b7 + a11 * b6;
  s18 = a7 * b11 + a8 * b10 + a9 * b9 + a10 * b8 + a11 * b7;
  s19 = a8 * b11 + a9 * b10 + a10 * b9 + a11 * b8;
  s20 = a9 * b11 + a10 * b10 + a11 * b9;
  s21 = a10 * b11 + a11 * b10;
  s22 = a11 * b11;
  s23 = 0;

  carry0 = (s0 + (1LL << 20)) >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry2 = (s2 + (1LL << 20)) >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry4 = (s4 + (1LL << 20)) >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry12 = (s12 + (1LL << 20)) >> 21;
  s13 += carry12;
  s12 -= carry12 << 21;
  carry14 = (s14 + (1LL << 20)) >> 21;
  s15 += carry14;
  s14 -= carry14 << 21;
  carry16 = (s16 + (1LL << 20)) >> 21;
  s17 += carry16;
  s16 -= carry16 << 21;
  carry18 = (s18 + (1LL << 20)) >> 21;
  s19 += carry18;
  s18 -= carry18 << 21;
  carry20 = (s20 + (1LL << 20)) >> 21;
  s21 += carry20;
  s20 -= carry20 << 21;
  carry22 = (s22 + (1LL << 20)) >> 21;
  s23 += carry22;
  s22 -= carry22 << 21;
  carry1 = (s1 + (1LL << 20)) >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry3 = (s3 + (1LL << 20)) >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry5 = (s5 + (1LL << 20)) >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  carry13 = (s13 + (1LL << 20)) >> 21;
  s14 += carry13;
  s13 -= carry13 << 21;
  carry15 = (s15 + (1LL << 20)) >> 21;
  s16 += carry15;
  s15 -= carry15 << 21;
  carry17 = (s17 + (1LL << 20)) >> 21;
  s18 += carry17;
  s17 -= carry17 << 21;
  carry19 = (s19 + (1LL << 20)) >> 21;
  s20 += carry19;
  s19 -= carry19 << 21;
  carry21 = (s21 + (1LL << 20)) >> 21;
  s22 += carry21;
  s21 -= carry21 << 21;

  s11 += s23 * 666643;
  s12 += s23 * 470296;
  s13 += s23 * 654183;
  s14 -= s23 * 997805;
  s15 += s23 * 136657;
  s16 -= s23 * 683901;
  s23 = 0;
  s10 += s22 * 666643;
  s11 += s22 * 470296;
  s12 += s22 * 654183;
  s13 -= s22 * 997805;
  s14 += s22 * 136657;
  s15 -= s22 * 683901;
  s22 = 0;
  s9 += s21 * 666643;
  s10 += s21 * 470296;
  s11 += s21 * 654183;
  s12 -= s21 * 997805;
  s13 += s21 * 136657;
  s14 -= s21 * 683901;
  s21 = 0;
  s8 += s20 * 666643;
  s9 += s20 * 470296;
  s10 += s20 * 654183;
  s11 -= s20 * 997805;
  s12 += s20 * 136657;
  s13 -= s20 * 683901;
  s20 = 0;
  s7 += s19 * 666643;
  s8 += s19 * 470296;
  s9 += s19 * 654183;
  s10 -= s19 * 997805;
  s11 += s19 * 136657;
  s12 -= s19 * 683901;
  s19 = 0;
  s6 += s18 * 666643;
  s7 += s18 * 470296;
  s8 += s18 * 654183;
  s9 -= s18 * 997805;
  s10 += s18 * 136657;
  s11 -= s18 * 683901;
  s18 = 0;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry12 = (s12 + (1LL << 20)) >> 21;
  s13 += carry12;
  s12 -= carry12 << 21;
  carry14 = (s14 + (1LL << 20)) >> 21;
  s15 += carry14;
  s14 -= carry14 << 21;
  carry16 = (s16 + (1LL << 20)) >> 21;
  s17 += carry16;
  s16 -= carry16 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  carry13 = (s13 + (1LL << 20)) >> 21;
  s14 += carry13;
  s13 -= carry13 << 21;
  carry15 = (s15 + (1LL << 20)) >> 21;
  s16 += carry15;
  s15 -= carry15 << 21;

  s5 += s17 * 666643;
  s6 += s17 * 470296;
  s7 += s17 * 654183;
  s8 -= s17 * 997805;
  s9 += s17 * 136657;
  s10 -= s17 * 683901;
  s17 = 0;
  s4 += s16 * 666643;
  s5 += s16 * 470296;
  s6 += s16 * 654183;
  s7 -= s16 * 997805;
  s8 += s16 * 136657;
  s9 -= s16 * 683901;
  s16 = 0;
  s3 += s15 * 666643;
  s4 += s15 * 470296;
  s5 += s15 * 654183;
  s6 -= s15 * 997805;
  s7 += s15 * 136657;
  s8 -= s15 * 683901;
  s15 = 0;
  s2 += s14 * 666643;
  s3 += s14 * 470296;
  s4 += s14 * 654183;
  s5 -= s14 * 997805;
  s6 += s14 * 136657;
  s7 -= s14 * 683901;
  s14 = 0;
  s1 += s13 * 666643;
  s2 += s13 * 470296;
  s3 += s13 * 654183;
  s4 -= s13 * 997805;
  s5 += s13 * 136657;
  s6 -= s13 * 683901;
  s13 = 0;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;

  carry0 = (s0 + (1LL << 20)) >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry2 = (s2 + (1LL << 20)) >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry4 = (s4 + (1LL << 20)) >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry6 = (s6 + (1LL << 20)) >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry8 = (s8 + (1LL << 20)) >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry10 = (s10 + (1LL << 20)) >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry1 = (s1 + (1LL << 20)) >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry3 = (s3 + (1LL << 20)) >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry5 = (s5 + (1LL << 20)) >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry7 = (s7 + (1LL << 20)) >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry9 = (s9 + (1LL << 20)) >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry11 = (s11 + (1LL << 20)) >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;
  carry11 = s11 >> 21;
  s12 += carry11;
  s11 -= carry11 << 21;
  s0 += s12 * 666643;
  s1 += s12 * 470296;
  s2 += s12 * 654183;
  s3 -= s12 * 997805;
  s4 += s12 * 136657;
  s5 -= s12 * 683901;
  s12 = 0;
  carry0 = s0 >> 21;
  s1 += carry0;
  s0 -= carry0 << 21;
  carry1 = s1 >> 21;
  s2 += carry1;
  s1 -= carry1 << 21;
  carry2 = s2 >> 21;
  s3 += carry2;
  s2 -= carry2 << 21;
  carry3 = s3 >> 21;
  s4 += carry3;
  s3 -= carry3 << 21;
  carry4 = s4 >> 21;
  s5 += carry4;
  s4 -= carry4 << 21;
  carry5 = s5 >> 21;
  s6 += carry5;
  s5 -= carry5 << 21;
  carry6 = s6 >> 21;
  s7 += carry6;
  s6 -= carry6 << 21;
  carry7 = s7 >> 21;
  s8 += carry7;
  s7 -= carry7 << 21;
  carry8 = s8 >> 21;
  s9 += carry8;
  s8 -= carry8 << 21;
  carry9 = s9 >> 21;
  s10 += carry9;
  s9 -= carry9 << 21;
  carry10 = s10 >> 21;
  s11 += carry10;
  s10 -= carry10 << 21;

  out[0] = (uint8_t)(s0 >> 0);
  out[1] = (uint8_t)(s0 >> 8);
  out[2] = (uint8_t)((s0 >> 16) | (s1 << 5));
  out[3] = (uint8_t)(s1 >> 3);
  out[4] = (uint8_t)(s1 >> 11);
  out[5] = (uint8_t)((s1 >> 19) | (s2 << 2));
  out[6] = (uint8_t)(s2 >> 6);
  out[7] = (uint8_t)((s2 >> 14) | (s3 << 7));
  out[8] = (uint8_t)(s3 >> 1);
  out[9] = (uint8_t)(s3 >> 9);
  out[10] = (uint8_t)((s3 >> 17) | (s4 << 4));
  out[11] = (uint8_t)(s4 >> 4);
  out[12] = (uint8_t)(s4 >> 12);
  out[13] = (uint8_t)((s4 >> 20) | (s5 << 1));
  out[14] = (uint8_t)(s5 >> 7);
  out[15] = (uint8_t)((s5 >> 15) | (s6 << 6));
  out[16] = (uint8_t)(s6 >> 2);
  out[17] = (uint8_t)(s6 >> 10);
  out[18] = (uint8_t)((s6 >> 18) | (s7 << 3));
  out[19] = (uint8_t)(s7 >> 5);
  out[20] = (uint8_t)(s7 >> 13);
  out[21] = (uint8_t)(s8 >> 0);
  out[22] = (uint8_t)(s8 >> 8);
  out[23] = (uint8_t)((s8 >> 16) | (s9 << 5));
  out[24] = (uint8_t)(s9 >> 3);
  out[25] = (uint8_t)(s9 >> 11);
  out[26] = (uint8_t)((s9 >> 19) | (s10 << 2));
  out[27] = (uint8_t)(s10 >> 6);
  out[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
  out[29] = (uint8_t)(s11 >> 1);
  out[30] = (uint8_t)(s11 >> 9);
  out[31] = (uint8_t)(s11 >> 17);
}

/*
 * sc_is_canonical: retorna 1 se s em [0, L), 0 cc. Constant-time.
 */
int sc_is_canonical(const uint8_t s[32]) {
  /* Compare s vs L (LE byte arrays). Walk from MSB to LSB. */
  uint8_t accept = 0u; /* 0 = undecided, 1 = s < L confirmed */
  uint8_t reject = 0u; /* 1 = s >= L confirmed */
  for (int i = 31; i >= 0; --i) {
    uint8_t si = s[i];
    uint8_t li = ED_L_BYTES[i];
    /* if undecided: if si < li, accept; if si > li, reject; equal -> continue */
    uint8_t lt = (uint8_t)((si - li) >> 7); /* 1 if si<li, else 0 (works for unsigned subtract bit) */
    uint8_t gt = (uint8_t)((li - si) >> 7);
    uint8_t undecided = (uint8_t)(1u - (accept | reject));
    accept |= undecided & lt;
    reject |= undecided & gt;
  }
  /* If neither accept nor reject (s == L), reject. */
  return accept ? 1 : 0;
}
