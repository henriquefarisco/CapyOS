#ifndef CRYPT_H
#define CRYPT_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"

#define CRYPT_KEY_SIZE 32
#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

struct sha256_ctx {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t data[SHA256_BLOCK_SIZE];
  uint32_t datalen;
};

void sha256_init(struct sha256_ctx *ctx);
void sha256_update(struct sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(struct sha256_ctx *ctx, uint8_t hash[SHA256_DIGEST_SIZE]);

struct block_device *crypt_init(struct block_device *lower,
                                const uint8_t key1[CRYPT_KEY_SIZE],
                                const uint8_t key2[CRYPT_KEY_SIZE]);

void crypt_free(struct block_device *dev);

void crypt_pbkdf2_sha256(const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         uint32_t iterations, uint8_t *out, size_t out_len);

void crypt_derive_xts_keys(const char *password, const uint8_t *salt,
                           size_t salt_len, uint32_t iterations,
                           uint8_t key1[CRYPT_KEY_SIZE],
                           uint8_t key2[CRYPT_KEY_SIZE]);

#endif
