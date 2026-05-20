#ifndef SERVICES_CAPYPKG_BOOTSTRAP_H
#define SERVICES_CAPYPKG_BOOTSTRAP_H

/*
 * services/capypkg_bootstrap.h
 *
 * Bridge between the install-profile (`services/install_profile.h`)
 * and the package adapter (`services/capypkg.h`).
 *
 * `capypkg_bootstrap_run` reads `/system/install/profile.ini`,
 * registers the configured repository (idempotent), refreshes the
 * catalog and installs every available package (or the
 * `bootstrap_install` subset for profile=custom).
 *
 * Designed to be safe to call repeatedly:
 *   - first successful run writes
 *     `/system/install/bootstrap.done`; subsequent calls without
 *     `force=1` short-circuit;
 *   - network/HTTP errors do NOT write the marker, so retries on
 *     the next service poll can recover transient failures;
 *   - per-package failures are counted but do not abort the sweep.
 *
 * Both the shell command `pkg-bootstrap` and the kernel
 * auto-bootstrap hook from `kernel_service_poll_capypkg` are
 * thin wrappers around this entry point.
 *
 * Returns one of the `install_profile_result` values:
 *   - INSTALL_PROFILE_OK                  bootstrap complete (marker written)
 *   - INSTALL_PROFILE_ERR_NOT_READY       adapter not initialised
 *   - INSTALL_PROFILE_ERR_DENIED/PARSE    profile.ini rejected
 *   - INSTALL_PROFILE_ERR_MISSING_FIELD   profile=full/custom missing fields
 *   - INSTALL_PROFILE_ERR_STORAGE         repo add/fetch/install failed;
 *                                         retry on next poll
 */

#include "services/install_profile.h"

/* Progress event types emitted by capypkg_bootstrap_run_with_progress.
 * Stable enum: never renumber; UI consumers (first-boot wizard,
 * `capy module` command, future GUI installer) switch on these. */
enum capypkg_bootstrap_event {
    CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER  = 1, /* attempting repo add  */
    CAPYPKG_BOOTSTRAP_EVENT_INDEX_FETCH    = 2, /* downloading index    */
    CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_BEGIN  = 3, /* install starting     */
    CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_OK     = 4, /* install completed    */
    CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_FAIL   = 5, /* install failed       */
    CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_SKIP   = 6, /* skipped (custom)     */
    CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE     = 7  /* all packages handled */
};

/* Progress callback. `name` is NUL-terminated and points into
 * caller-owned memory; do not store the pointer past the callback
 * return. `index`/`total` are 1-based package counters; both 0 for
 * REPO_REGISTER and INDEX_FETCH events. `rc` carries the underlying
 * capypkg result code (CAPYPKG_OK, CAPYPKG_ERR_*) for FAIL events;
 * zero otherwise. `ctx` is opaque user data passed in unchanged. */
typedef void (*capypkg_bootstrap_progress_fn)(
    enum capypkg_bootstrap_event event,
    const char *name,
    int index,
    int total,
    int rc,
    void *ctx);

/* Legacy/silent entry point. Equivalent to passing NULL progress. */
int capypkg_bootstrap_run(int force, int *out_installed, int *out_failed);

/* Progress-aware entry point used by the first-boot wizard and the
 * `capy module` shell command. Behaviour is otherwise identical to
 * capypkg_bootstrap_run: idempotent, marker-driven, fail-soft on
 * per-package errors. Pass NULL progress to disable callbacks. */
int capypkg_bootstrap_run_with_progress(
    int force,
    int *out_installed,
    int *out_failed,
    capypkg_bootstrap_progress_fn progress,
    void *ctx);

#endif /* SERVICES_CAPYPKG_BOOTSTRAP_H */
