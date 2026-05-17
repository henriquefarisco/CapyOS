/*
 * src/auth/internal/user_helpers.h
 *
 * Internal helpers shared inside the `auth` module across the
 * user-record, userdb-io and userdb-auth translation units.
 *
 * Not part of the public API: any consumer outside `src/auth/` must
 * keep using the public surface in `include/auth/user.h` and
 * `include/auth/user_password_hash.h`.
 *
 * The helpers are intentionally minimal and credential-safe:
 *  - `memory_zero` is the volatile-safe wipe primitive used to clear
 *    salts, derived hashes, parsed user records and intermediate
 *    /etc/users.db buffers. The volatility marker prevents the
 *    optimiser from eliding the stores as dead-code when the buffer
 *    leaves scope or is freed immediately afterwards.
 *  - `bytes_to_hex` / `hex_to_bytes` deal exclusively with hex
 *    encoding of fixed-length buffers (salt, hash). The helpers do
 *    not branch on the byte values, which matters for the salt/hash
 *    serialization path that is reached during authentication.
 *  - `cstring_copy_n`, `parse_u32` and `u32_to_string` underpin the
 *    on-disk userdb format and must stay deterministic and
 *    overflow-safe even for malformed input.
 */
#ifndef AUTH_INTERNAL_USER_HELPERS_H
#define AUTH_INTERNAL_USER_HELPERS_H

#include <stddef.h>
#include <stdint.h>

/* Length of a NUL-terminated string, or 0 when `s` is NULL. */
size_t auth_cstring_length(const char *s);

/* Volatile-safe zero of `len` bytes starting at `dst`. */
void auth_memory_zero(void *dst, size_t len);

/* Copy a NUL-terminated source into a fixed-size destination,
 * NUL-terminating the destination even on truncation. */
void auth_cstring_copy(char *dst, size_t dst_size, const char *src);

/* Copy at most `src_len` bytes from `src` into `dst`, NUL-terminating
 * the result. Safe when `src_len` is larger than `dst_size`. */
void auth_cstring_copy_n(char *dst, size_t dst_size, const char *src,
                         size_t src_len);

/* Byte-by-byte equality test for two NUL-terminated strings. Returns
 * 1 on equality, 0 on mismatch or NULL input. */
int auth_strings_equal(const char *a, const char *b);

/* Hex encode `len` bytes from `src` into `dst`. `dst` must hold at
 * least `len * 2 + 1` bytes; the result is NUL-terminated. */
void auth_bytes_to_hex(const uint8_t *src, size_t len, char *dst);

/* Hex decode `hex_len` characters from `hex` into `dst`. Returns 0 on
 * success, -1 on malformed input or length mismatch. */
int auth_hex_to_bytes(const char *hex, size_t hex_len, uint8_t *dst,
                      size_t dst_len);

/* Parse a decimal unsigned 32-bit integer from a non-NUL-terminated
 * field of length `len`. Stops at the first non-digit. */
uint32_t auth_parse_u32(const char *str, size_t len);

/* Render `value` as a decimal string into `buf` (`buf_len` includes
 * room for the trailing NUL). */
void auth_u32_to_string(uint32_t value, char *buf, size_t buf_len);

/* Fill `salt` with `len` cryptographically random bytes from the
 * project CSPRNG. */
void auth_generate_salt(uint8_t *salt, size_t len);

/* Append a NUL-terminated `src` into `dst` at position `*idx`,
 * advancing `*idx`. Returns 0 on success, -1 on overflow or invalid
 * input. The destination is NOT NUL-terminated by this helper. */
int auth_append_piece(char *dst, size_t cap, size_t *idx, const char *src);

#endif /* AUTH_INTERNAL_USER_HELPERS_H */
