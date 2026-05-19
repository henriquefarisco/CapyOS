#ifndef SERVICES_INSTALL_PROFILE_H
#define SERVICES_INSTALL_PROFILE_H

/*
 * services/install_profile.h
 *
 * CapyOS install profile contract (Etapa 9 antecipatória).
 *
 * Reads and exposes /system/install/profile.ini. The first-boot
 * runtime and the capypkg adapter consume this to decide whether to
 * auto-bootstrap remote modules.
 *
 * Schema (line-oriented key=value, identical alphabet rules as
 * capypkg manifest):
 *
 *   profile=basic|full|custom
 *   bootstrap_repo_name=<name>             # 1..31 chars [a-zA-Z0-9._-]
 *   bootstrap_repo_url=<https-url>         # HTTPS only
 *   bootstrap_repo_signed=0|1              # default 0 in alpha
 *   bootstrap_install=<csv of names>       # optional, used when profile=custom
 *
 * Hard rules:
 *   - Missing/empty profile.ini => profile=basic (boot to clean shell,
 *     no bootstrap). Same as if profile=basic was explicitly set.
 *   - Any non-printable ASCII byte (outside 0x20-0x7E) in any value
 *     causes the file to be rejected as a whole (fail-closed).
 *   - bootstrap_repo_url must start with `https://` when present.
 *   - profile=full requires bootstrap_repo_name and bootstrap_repo_url.
 *   - profile=custom additionally requires bootstrap_install with at
 *     least one entry from the [a-zA-Z0-9._-] alphabet.
 *   - Unknown keys are tolerated forward-compat.
 *
 * The capypkg adapter is the only consumer that actually performs
 * fetch/install. This module never touches the network or the
 * package adapter directly.
 */

#include <stddef.h>
#include <stdint.h>

#define INSTALL_PROFILE_NAME_MAX        32u
#define INSTALL_PROFILE_URL_MAX         160u
#define INSTALL_PROFILE_INSTALL_LIST_MAX 384u

#define INSTALL_PROFILE_PATH            "/system/install/profile.ini"
#define INSTALL_PROFILE_BOOTSTRAP_DONE  "/system/install/bootstrap.done"
#define INSTALL_PROFILE_DIR             "/system/install"

enum install_profile_kind {
    INSTALL_PROFILE_BASIC = 0,
    INSTALL_PROFILE_FULL,
    INSTALL_PROFILE_CUSTOM,
};

enum install_profile_result {
    INSTALL_PROFILE_OK = 0,
    INSTALL_PROFILE_ERR_INVALID_ARG = -1,
    INSTALL_PROFILE_ERR_PARSE = -2,
    INSTALL_PROFILE_ERR_DENIED = -3,
    INSTALL_PROFILE_ERR_MISSING_FIELD = -4,
    INSTALL_PROFILE_ERR_NOT_READY = -5,
    INSTALL_PROFILE_ERR_STORAGE = -6,
};

struct install_profile {
    enum install_profile_kind kind;
    char repo_name[INSTALL_PROFILE_NAME_MAX];
    char repo_url[INSTALL_PROFILE_URL_MAX];
    uint8_t repo_signed;
    char install_list[INSTALL_PROFILE_INSTALL_LIST_MAX];
    uint8_t valid;
};

/* Reset the in-memory state to BASIC defaults. Idempotent. */
void install_profile_reset(struct install_profile *profile);

/* Parse a textual buffer. Returns INSTALL_PROFILE_OK and fills
 * `profile` even when the buffer is empty (defaults to BASIC). The
 * file is rejected as a whole (INSTALL_PROFILE_ERR_DENIED) if any
 * value contains non-printable ASCII. */
int install_profile_parse(const char *text, size_t len,
                          struct install_profile *out);

/* True when `profile.kind == INSTALL_PROFILE_FULL` or
 * `INSTALL_PROFILE_CUSTOM` and the required repo fields are set. */
int install_profile_should_bootstrap(const struct install_profile *profile);

/* Convenience: returns the readable label for the kind enum. */
const char *install_profile_kind_label(enum install_profile_kind kind);

/* Convenience: returns the readable label for the result enum. */
const char *install_profile_result_label(int rc);

/* Walk the comma-separated install_list and copy the next token into
 * `out` (NUL-terminated, truncated if necessary). Returns 0 on a
 * token found, -1 when no further tokens remain. `cursor` is updated
 * in place so callers can iterate. */
int install_profile_install_list_next(const char *list, size_t *cursor,
                                      char *out, size_t out_size);

#endif /* SERVICES_INSTALL_PROFILE_H */
