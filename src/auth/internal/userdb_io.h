/*
 * src/auth/internal/userdb_io.h
 *
 * Internal storage primitives for /etc/users.db. Shared between the
 * `userdb_io` translation unit (which owns these primitives) and the
 * `userdb_auth` translation unit (which composes them for the
 * authenticate and password-change flows).
 *
 * Not part of the public API. External callers must keep using the
 * stable surface declared in `include/auth/user.h`.
 */
#ifndef AUTH_INTERNAL_USERDB_IO_H
#define AUTH_INTERNAL_USERDB_IO_H

#include <stddef.h>

#include "auth/user.h"

/* Read the entire `/etc/users.db` file into a freshly allocated
 * heap buffer (caller frees with `kfree`). The buffer is
 * NUL-terminated for safety; `*out_len` (when non-NULL) carries the
 * actual byte count without the terminator. Returns NULL when the DB
 * cannot be opened or the allocation fails. */
char *userdb_read_all(size_t *out_len);

/* Parse a single colon-delimited user line from `/etc/users.db` into
 * `*out`. Accepts both the legacy 7-field PBKDF2 format and the
 * 10-field Argon2id format. Returns 0 on success, -1 on malformed
 * input. */
int userdb_parse_line(const char *line, size_t len, struct user_record *out);

/* Serialize `user` into a colon-delimited line terminated by '\n' and
 * a NUL byte. Writes the byte count (excluding the NUL) into
 * `*out_len` when non-NULL. Returns 0 on success, -1 on overflow or
 * invalid input. */
int userdb_serialize_line(const struct user_record *user, char *line,
                          size_t line_cap, size_t *out_len);

/* Atomically replace `/etc/users.db` with `data` of length `len`. The
 * caller owns `data`; this helper performs an unlink + create +
 * write sequence. Returns 0 on success, -1 on failure. */
int userdb_write_blob(const char *data, size_t len);

/* Iterate the parsed user records in `/etc/users.db`. `callback`
 * receives each successfully parsed record; it may return non-zero to
 * stop the iteration early. Lines that fail to parse are silently
 * skipped to keep partially corrupt databases recoverable. Returns 0
 * when iteration completed normally, 1 when the callback requested an
 * early stop, and -1 on read errors. */
int userdb_iterate(int (*callback)(const struct user_record *, void *),
                   void *userdata);

#endif /* AUTH_INTERNAL_USERDB_IO_H */
