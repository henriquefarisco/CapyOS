#ifndef SECURITY_X25519_H
#define SECURITY_X25519_H

#include <stddef.h>
#include <stdint.h>

/*
 * X25519 (RFC 7748): Elliptic Curve Diffie-Hellman sobre Curve25519.
 *
 * Implementacao canonica do CapyOS, audit-friendly, alinhada com a
 * higiene criptografica do resto do projeto (constant-time, wipe
 * volatile-safe, fail-closed em todos os bounds). Independente do
 * TLS stack BearSSL — esta primitiva serve usos fora de TLS (key
 * agreement local, futura derivacao de chave compartilhada
 * WireGuard-like, secure boot key exchange).
 *
 * Composicao canonica com slices anteriores:
 *
 *   1. Alice e Bob geram cada um um scalar aleatorio via CSPRNG
 *      (alpha.214):
 *        csprng_fill(alice_sk, 32);
 *        csprng_fill(bob_sk, 32);
 *
 *   2. Cada um computa a chave publica via base point:
 *        x25519_base(alice_sk, alice_pk);
 *        x25519_base(bob_sk, bob_pk);
 *
 *   3. Trocam alice_pk <-> bob_pk em canal autenticado.
 *
 *   4. Cada um deriva o segredo compartilhado:
 *        x25519(alice_sk, bob_pk, shared);
 *        x25519(bob_sk, alice_pk, shared);
 *      (Ambos chegam ao mesmo `shared`.)
 *
 *   5. Derivam material de chave via HKDF (alpha.213):
 *        crypt_hkdf_sha256(shared, 32, "binding=channel", 15,
 *                          NULL, 0, session_key, 32);
 *
 *   6. Estabelecem canal autenticado via ChaCha20-Poly1305
 *      (alpha.215):
 *        chacha20_poly1305_encrypt(session_key, nonce, aad, ...);
 *
 * Modelo de ameacas (per RFC 7748 §6 e Curve25519 design notes):
 *
 *  - Atacante NAO consegue computar `shared` observando apenas
 *    `alice_pk` e `bob_pk` no canal (computational Diffie-Hellman
 *    assumption sobre Curve25519).
 *  - Atacante NAO consegue forjar `shared` substituindo `bob_pk` por
 *    um small-order point e fazendo Alice computar shared = 0; esta
 *    implementacao detecta shared==0 e retorna -1 fail-closed (RFC
 *    7748 §6.1: "Implementations of X25519 SHOULD check for the
 *    all-zero values").
 *  - Atacante NAO consegue distinguir public keys de bytes aleatorios
 *    uniformes (Elligator2 assumption, modulo o top bit que e
 *    sempre mascarado em frombytes).
 *
 * Limites:
 *
 *  - Esta primitiva NAO autentica as chaves publicas. Caller e
 *    responsavel por garantir que `bob_pk` veio de Bob (e nao do
 *    atacante) via canal autenticado (e.g. assinatura Ed25519 quando
 *    disponivel, certificate pinning, out-of-band).
 *  - O scalar e clampeado per RFC 7748 §5 (zera bits 0..2, define
 *    bit 254, zera bit 255). Cofator 8 da curva e absorvido pelo
 *    clamping.
 *  - Apenas o eixo u (coordenada x da curva Montgomery) e processado;
 *    coordenada v nunca aparece (X25519 e funcao da x-coord).
 *
 * Composicao com BearSSL:
 *
 *  - BearSSL ja tem X25519 internamente em sua TLS stack
 *    (`third_party/bearssl/src/ec/ec_c25519_*.c`), mas ele e acessivel
 *    apenas via o engine SSL. Esta primitiva expoe X25519 como API
 *    publica auditavel e independente, alinhada com a higiene do
 *    resto de `src/security/`.
 */

#define X25519_SCALAR_SIZE 32u
#define X25519_POINT_SIZE  32u

/*
 * Computa shared = X25519(scalar, u_coord). Retorna 0 em sucesso.
 *
 * Retorna -1 se:
 *  - Algum input obrigatorio for NULL.
 *  - O resultado for o all-zero point (small-subgroup detection per
 *    RFC 7748 §6.1: o ponto computado nao tem ordem suficiente para
 *    fornecer key agreement seguro).
 *
 * O `scalar` e clampeado internamente per RFC 7748 §5 — caller pode
 * passar bytes brutos do CSPRNG; o clamping nao escreve no buffer do
 * caller.
 *
 * O `u_coord` tem o bit 255 mascarado internamente per RFC 7748 §5
 * antes de ser usado.
 *
 * Wipe hygiene: todos os intermediarios (x_2, z_2, x_3, z_3, scalar
 * clamped, u parsed) sao zerados antes do retorno (sucesso e erro).
 */
int x25519(const uint8_t scalar[X25519_SCALAR_SIZE],
           const uint8_t u_coord[X25519_POINT_SIZE],
           uint8_t shared[X25519_POINT_SIZE]);

/*
 * Computa public_key = X25519(scalar, base_point). Equivalente a
 * `x25519(scalar, BASE_POINT, public_key)` onde BASE_POINT e o
 * u-coord 9 (RFC 7748 §4.1).
 *
 * Convenientemente, nunca retorna -1 por small-subgroup detection
 * (o base point tem ordem prima por design), mas pode retornar -1
 * em NULL inputs.
 *
 * Mesma wipe hygiene de `x25519`.
 */
int x25519_base(const uint8_t scalar[X25519_SCALAR_SIZE],
                uint8_t public_key[X25519_POINT_SIZE]);

#endif /* SECURITY_X25519_H */
