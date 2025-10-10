#ifndef CRYPT_H
#define CRYPT_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"

#define CRYPT_KEY_SIZE 32

struct block_device *crypt_init(struct block_device *lower,
                                const uint8_t key1[CRYPT_KEY_SIZE],
                                const uint8_t key2[CRYPT_KEY_SIZE]);

void crypt_pbkdf2_sha256(const uint8_t *password,
                           size_t password_len,
                           const uint8_t *salt,
                           size_t salt_len,
                           uint32_t iterations,
                           uint8_t *out,
                           size_t out_len);

void crypt_derive_xts_keys(const char *password,
                           const uint8_t *salt,
                           size_t salt_len,
                           uint32_t iterations,
                           uint8_t key1[CRYPT_KEY_SIZE],
                           uint8_t key2[CRYPT_KEY_SIZE]);

#endif
