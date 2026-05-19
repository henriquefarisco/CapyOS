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

int capypkg_bootstrap_run(int force, int *out_installed, int *out_failed);

#endif /* SERVICES_CAPYPKG_BOOTSTRAP_H */
