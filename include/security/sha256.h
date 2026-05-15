#ifndef SECURITY_SHA256_H
#define SECURITY_SHA256_H

#include <stddef.h>
#include <stdint.h>

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
void sha256_hash(const uint8_t *data, size_t len,
                 uint8_t hash[SHA256_DIGEST_SIZE]);
void sha256_hex(const uint8_t hash[SHA256_DIGEST_SIZE], char out_hex[65]);

/* Defensive wipe of a SHA-256 context. After `sha256_final` the context
 * still holds the finalized state (which IS the produced digest) and the
 * last padded block, both of which may contain residue derived from
 * secret inputs. Callers that handle secrets (CSPRNG pool snapshots,
 * PBKDF2 inner contexts, HMAC inner state) should call `sha256_clear`
 * before letting the context go out of scope. The implementation uses a
 * volatile-pointer loop and is safe against dead-store elimination by
 * the compiler. */
void sha256_clear(struct sha256_ctx *ctx);

#endif /* SECURITY_SHA256_H */
