#ifndef SECURITY_SHA512_H
#define SECURITY_SHA512_H

#include <stdint.h>
#include <stddef.h>

#define SHA512_BLOCK_SIZE  128
#define SHA512_DIGEST_SIZE 64

struct sha512_ctx {
  uint64_t state[8];
  uint64_t bitlen[2];
  uint8_t data[SHA512_BLOCK_SIZE];
  uint32_t datalen;
};

void sha512_init(struct sha512_ctx *ctx);
void sha512_update(struct sha512_ctx *ctx, const uint8_t *data, size_t len);
void sha512_final(struct sha512_ctx *ctx, uint8_t hash[SHA512_DIGEST_SIZE]);
void sha512_hash(const uint8_t *data, size_t len, uint8_t hash[SHA512_DIGEST_SIZE]);

#endif /* SECURITY_SHA512_H */
