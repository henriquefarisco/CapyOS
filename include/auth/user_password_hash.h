#ifndef AUTH_USER_PASSWORD_HASH_H
#define AUTH_USER_PASSWORD_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "auth/user.h"

/*
 * Password hash dispatch for `userdb`.
 *
 * This module isolates the algorithm-aware code so that:
 *
 *  1. `src/auth/user.c` does not depend on either crypt primitive
 *     directly; it just hands a password and metadata to the dispatch
 *     layer.
 *  2. The dispatch logic itself (algorithm selection, parameter
 *     validation, memory allocation for Argon2id, wipe hygiene) can be
 *     unit-tested in the host binary without dragging the VFS/userdb
 *     stack into the test harness.
 *  3. The legacy PBKDF2-SHA256 path keeps working for any record that
 *     was serialized before alpha.219, so an existing
 *     `/etc/users.db` does not require a migration step at boot.
 *
 * Two algorithms are supported (selected via `algo_id` from
 * `include/auth/user.h`):
 *
 *  - `USER_PASSWORD_ALGO_PBKDF2_SHA256` — `t_cost == 0` is interpreted
 *    as "use the canonical `USER_ITERATIONS` iteration count" so legacy
 *    records that never serialized a t_cost field deserialize to a
 *    safe default. `m_cost` is ignored.
 *  - `USER_PASSWORD_ALGO_ARGON2ID` — requires `t_cost >= 1` and
 *    `m_cost >= 8` (RFC 9106 §3.1 lower bound for parallelism=1). The
 *    dispatch layer allocates `m_cost * 1024` bytes of work memory via
 *    `kalloc`, runs `argon2id_hash`, wipes the work memory, frees it,
 *    and returns the derived hash to the caller.
 *
 * Wipe hygiene:
 *
 *  - Argon2id work memory is wiped via `volatile`-typed pointer before
 *    `kfree`. The output buffer is wiped only on failure paths so that
 *    callers can rely on `hash_out` being zero-valued when the function
 *    fails.
 *  - The caller is responsible for wiping the password buffer (the
 *    dispatch layer does not own it and does not know its lifetime).
 *
 * Threat model considerations:
 *
 *  - Calling `user_password_hash_derive` always pays the full algorithm
 *    cost regardless of whether the salt/password match a real record.
 *    The caller (`userdb_authenticate`) routes unknown usernames into
 *    `user_password_hash_derive` with the same algorithm/parameters as
 *    a new record so that existent-vs-non-existent timing remains
 *    equalised. A residual leak persists for records still hashed
 *    under the legacy PBKDF2 algorithm (~50 ms PBKDF2 vs ~200 ms
 *    Argon2id). That leak only reveals "this account predates the
 *    last password change after alpha.219 deployment" and never the
 *    password itself; it will be eliminated by a future implicit
 *    re-hash slice that upgrades legacy records on the next successful
 *    login.
 */

#define USER_PBKDF2_DEFAULT_ITERATIONS USER_ITERATIONS

/*
 * Returns the canonical lowercase string for `algo_id`, or NULL when
 * the identifier is unknown. The string lives in `.rodata` and must
 * not be freed.
 */
const char *user_password_hash_algo_to_string(uint8_t algo_id);

/*
 * Parses `text` (`len` bytes) into `*out_algo_id`. Returns 0 on
 * success and -1 when the token is unknown or any pointer is NULL.
 * The accepted tokens are exactly the strings returned by
 * `user_password_hash_algo_to_string` ("pbkdf2", "argon2id"). The
 * comparison is case-sensitive to keep `/etc/users.db` canonical.
 */
int user_password_hash_algo_from_string(const char *text, size_t len,
                                        uint8_t *out_algo_id);

/*
 * Derives a 32-byte hash from `password`/`salt` according to
 * `algo_id`/`t_cost`/`m_cost`. Returns 0 on success and -1 on any
 * invalid parameter, unsupported algorithm or allocation failure.
 *
 * The output buffer is zeroed on every failure path (fail-closed) so
 * a caller comparing it against a stored hash cannot accidentally
 * accept a partially-derived value.
 *
 * `password` may be NULL only when `password_len == 0`. `salt` must
 * never be NULL.
 */
int user_password_hash_derive(const uint8_t *password, size_t password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint8_t algo_id,
                              uint32_t t_cost, uint32_t m_cost,
                              uint8_t *hash_out, size_t hash_out_len);

/*
 * Verifies `candidate_password`/`salt` against `stored_hash` using
 * `algo_id`/`t_cost`/`m_cost`. Returns 0 on match, -1 on mismatch or
 * any failure (NULL pointer, invalid parameter, allocation failure).
 *
 * The comparison itself runs in constant time via
 * `crypt_constant_time_compare` so a successful tag and a mismatching
 * tag take the same number of operations. The derived hash is wiped
 * before returning, regardless of outcome.
 */
int user_password_hash_verify(const uint8_t *candidate_password,
                              size_t candidate_password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint8_t algo_id,
                              uint32_t t_cost, uint32_t m_cost,
                              const uint8_t *stored_hash,
                              size_t stored_hash_len);

#endif /* AUTH_USER_PASSWORD_HASH_H */
