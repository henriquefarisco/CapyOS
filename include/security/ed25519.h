#ifndef SECURITY_ED25519_H
#define SECURITY_ED25519_H

#include <stdint.h>
#include <stddef.h>

/*
 * Ed25519 signature scheme (RFC 8032) — implementacao real auditavel.
 *
 * Habilitada em alpha.217 substituindo o esqueleto fail-closed que
 * vinha desde alpha.210. Field arithmetic GF(2^255-19) e fe25519
 * compartilhado com X25519 (RFC 7748).
 *
 * Threat model:
 *  - Constant-time em scalar secreto (sign).
 *  - Verify usa cofator 8 per RFC 8032 §5.1.7 para rejeitar variantes
 *    com torsao.
 *  - Wipe volatile-safe em todos os intermediarios sensiveis.
 *  - Fail-closed em NULL/comprimento invalido/encoded point invalido.
 *  - Sem dependencias externas alem de fe25519 + sha512 + memoria
 *    automatica (sem heap).
 *
 * Composicao no CapyOS:
 *  - Update verifier (src/services/update_agent.c) usa ed25519_verify
 *    como gate criptografico oficial. Em alpha.217, o gate fail-closed
 *    placeholder foi substituido pela implementacao real.
 */

#define ED25519_PUBLIC_KEY_SIZE  32
#define ED25519_PRIVATE_KEY_SIZE 64
#define ED25519_SIGNATURE_SIZE   64
#define ED25519_SEED_SIZE        32

/*
 * Deriva (public_key, private_key) de seed de 32 bytes per RFC 8032
 * §5.1.5. Wipe hygiene em todos os intermediarios. NULL seed/output
 * resulta em outputs zerados.
 *
 * private_key layout: seed (32 bytes) || public_key (32 bytes) =
 * 64 bytes total.
 */
void ed25519_create_keypair(uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                            uint8_t private_key[ED25519_PRIVATE_KEY_SIZE],
                            const uint8_t seed[ED25519_SEED_SIZE]);

/*
 * Assina message com private_key per RFC 8032 §5.1.6 (PureEd25519).
 * signature = R (32 bytes, compressed) || S (32 bytes, scalar mod L).
 *
 * private_key layout esperado: seed (32 bytes) || public_key (32
 * bytes). NULL signature/private_key resulta em signature zerada.
 */
void ed25519_sign(uint8_t signature[ED25519_SIGNATURE_SIZE],
                  const uint8_t *message, size_t message_len,
                  const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                  const uint8_t private_key[ED25519_PRIVATE_KEY_SIZE]);

/*
 * Verifica signature de message contra public_key per RFC 8032 §5.1.7.
 * Retorna 0 se valida, -1 cc. Rejeita:
 *  - NULL signature/public_key.
 *  - S >= L (assinatura malleavel).
 *  - R ou public_key decodificam para ponto invalido (nao na curva).
 *  - [8]SB != [8]R + [8](kA) (multiplicacao por cofator obriga
 *    rejeicao de variantes com componente torsao).
 */
int ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                   const uint8_t *message, size_t message_len,
                   const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE]);

#endif /* SECURITY_ED25519_H */
