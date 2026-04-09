#ifndef SECURITY_ED25519_H
#define SECURITY_ED25519_H

#include <stdint.h>
#include <stddef.h>

#define ED25519_PUBLIC_KEY_SIZE  32
#define ED25519_PRIVATE_KEY_SIZE 64
#define ED25519_SIGNATURE_SIZE   64
#define ED25519_SEED_SIZE        32

void ed25519_create_keypair(uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                            uint8_t private_key[ED25519_PRIVATE_KEY_SIZE],
                            const uint8_t seed[ED25519_SEED_SIZE]);

void ed25519_sign(uint8_t signature[ED25519_SIGNATURE_SIZE],
                  const uint8_t *message, size_t message_len,
                  const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                  const uint8_t private_key[ED25519_PRIVATE_KEY_SIZE]);

int ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                   const uint8_t *message, size_t message_len,
                   const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE]);

#endif /* SECURITY_ED25519_H */
