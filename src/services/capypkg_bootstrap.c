/*
 * src/services/capypkg_bootstrap.c
 *
 * Profile-driven module bootstrap on top of the in-tree capypkg
 * adapter. See `include/services/capypkg_bootstrap.h` for the
 * contract; see `include/services/install_profile.h` for the
 * profile.ini schema.
 *
 * This TU is the bridge so the same logic can be triggered both by
 * the `pkg-bootstrap` shell command and by the kernel
 * auto-bootstrap hook in `kernel_service_poll_capypkg`. It does no
 * direct I/O of its own beyond reading the profile file and the
 * marker through the VFS; all package operations go through the
 * capypkg public API.
 */

#include "services/capypkg_bootstrap.h"

#include "services/capypkg.h"
#include "services/install_profile.h"
#include "fs/vfs.h"
#include "kernel/log/klog.h"
#include "auth/session.h"

#include <stddef.h>
#include <stdint.h>

#define CAPYPKG_BOOTSTRAP_PROFILE_BUF 1024u

/* ---- local helpers ---- */

static int profile_strings_equal(const char *a, const char *b) {
    size_t i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static int profile_read_from_vfs(struct install_profile *out) {
    static char buffer[CAPYPKG_BOOTSTRAP_PROFILE_BUF];
    struct file *file = NULL;
    long read = 0;
    if (!out) {
        return INSTALL_PROFILE_ERR_INVALID_ARG;
    }
    install_profile_reset(out);
    file = vfs_open(INSTALL_PROFILE_PATH, VFS_OPEN_READ);
    if (!file) {
        /* Missing profile.ini is not an error: BASIC default. */
        return INSTALL_PROFILE_OK;
    }
    read = vfs_read(file, buffer, sizeof(buffer) - 1u);
    vfs_close(file);
    if (read < 0) {
        return INSTALL_PROFILE_ERR_STORAGE;
    }
    if (read == 0) {
        return INSTALL_PROFILE_OK;
    }
    buffer[(size_t)read] = '\0';
    return install_profile_parse(buffer, (size_t)read, out);
}

static int profile_write_marker(const char *path, const char *text) {
    struct file *file = NULL;
    struct dentry *d = NULL;
    struct session_context *previous_session = NULL;
    int rc = 0;
    if (!path || !text) {
        return -1;
    }
    previous_session = session_active();
    session_set_active(NULL);
    if (vfs_lookup(path, &d) == 0) {
        (void)vfs_unlink(path);
    }
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0 &&
        vfs_lookup(path, &d) != 0) {
        rc = -1;
        goto done;
    }
    file = vfs_open(path, VFS_OPEN_WRITE);
    if (!file) {
        rc = -1;
        goto done;
    }
    size_t len = 0u;
    while (text[len] != '\0') {
        ++len;
    }
    if (len > 0u && vfs_write(file, text, len) != (long)len) {
        vfs_close(file);
        rc = -1;
        goto done;
    }
    vfs_close(file);
done:
    session_set_active(previous_session);
    return rc;
}

static int profile_marker_exists(void) {
    struct vfs_stat st;
    return vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 ? 1 : 0;
}

static int profile_install_list_contains(const char *list, const char *name) {
    if (!list || !list[0] || !name || !name[0]) {
        return 0;
    }
    size_t cursor = 0u;
    char token[INSTALL_PROFILE_NAME_MAX];
    while (install_profile_install_list_next(list, &cursor, token,
                                             sizeof(token)) == 0) {
        if (profile_strings_equal(token, name)) {
            return 1;
        }
    }
    return 0;
}

/* Internal: emit a progress event when a callback is set. */
static void emit_progress(capypkg_bootstrap_progress_fn fn,
                          void *ctx,
                          enum capypkg_bootstrap_event event,
                          const char *name,
                          int index, int total, int rc) {
    if (!fn) return;
    fn(event, name ? name : "", index, total, rc, ctx);
}

int capypkg_bootstrap_run_with_progress(
        int force,
        int *out_installed,
        int *out_failed,
        capypkg_bootstrap_progress_fn progress,
        void *ctx) {
    int installed = 0;
    int failed = 0;
    struct install_profile profile;
    struct capypkg_stats stats;

    if (out_installed) *out_installed = 0;
    if (out_failed) *out_failed = 0;

    capypkg_stats_get(&stats);
    if (!stats.initialized) {
        return INSTALL_PROFILE_ERR_NOT_READY;
    }

    if (!force && profile_marker_exists()) {
        /* Bootstrap already ran successfully once; no-op. */
        emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE,
                      NULL, 0, 0, 0);
        return INSTALL_PROFILE_OK;
    }

    int prc = profile_read_from_vfs(&profile);
    if (prc != INSTALL_PROFILE_OK) {
        klog(KLOG_WARN,
             "[audit] [capypkg] bootstrap: profile.ini rejected");
        return prc;
    }
    if (!install_profile_should_bootstrap(&profile)) {
        /* BASIC profile or missing repo fields: nothing to do.
         * Drop a marker anyway so the kernel hook does not retry
         * forever on every poll. */
        (void)profile_write_marker(INSTALL_PROFILE_BOOTSTRAP_DONE,
                                   "kind=basic\n");
        klog(KLOG_INFO,
             "[audit] [capypkg] bootstrap: profile=basic, no modules");
        emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE,
                      NULL, 0, 0, 0);
        return INSTALL_PROFILE_OK;
    }

    /* Register repository (idempotent). */
    emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER,
                  profile.repo_name, 0, 0, 0);
    int rrc = capypkg_repo_add(profile.repo_name, profile.repo_url,
                               profile.repo_signed ? 1 : 0);
    if (rrc != CAPYPKG_OK && rrc != CAPYPKG_ERR_ALREADY) {
        klog(KLOG_WARN,
             "[audit] [capypkg] bootstrap: repo registration failed");
        emit_progress(progress, ctx,
                      CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER_FAIL,
                      profile.repo_name, 0, 0, rrc);
        return INSTALL_PROFILE_ERR_STORAGE;
    }

    /* Fetch the index. If this fails the bootstrap is incomplete and
     * the marker is NOT written so the next poll can retry. */
    emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_INDEX_FETCH,
                  profile.repo_url, 0, 0, 0);
    int frc = capypkg_fetch_index();
    if (frc != CAPYPKG_OK) {
        klog(KLOG_WARN,
             "[audit] [capypkg] bootstrap: index fetch failed (will retry)");
        emit_progress(progress, ctx,
                      CAPYPKG_BOOTSTRAP_EVENT_INDEX_FETCH_FAIL,
                      profile.repo_url, 0, 0, frc);
        return INSTALL_PROFILE_ERR_STORAGE;
    }

    /* First pass: count packages that will actually be installed so
     * the progress UI can show "[i/N] name" deterministically. */
    size_t count = capypkg_available_count();
    int planned = 0;
    for (size_t i = 0u; i < count; ++i) {
        struct capypkg_entry entry;
        if (capypkg_available_get_at(i, &entry) != CAPYPKG_OK) continue;
        if (profile.kind == INSTALL_PROFILE_CUSTOM) {
            if (!profile_install_list_contains(profile.install_list,
                                               entry.name)) {
                continue;
            }
        }
        ++planned;
    }

    /* Install every package in the catalog (or the custom subset).
     * Each per-package failure is counted but does not abort the
     * sweep: a single bad package should not block the rest. */
    int progressed = 0;
    for (size_t i = 0u; i < count; ++i) {
        struct capypkg_entry entry;
        if (capypkg_available_get_at(i, &entry) != CAPYPKG_OK) {
            continue;
        }
        if (profile.kind == INSTALL_PROFILE_CUSTOM) {
            if (!profile_install_list_contains(profile.install_list,
                                               entry.name)) {
                emit_progress(progress, ctx,
                              CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_SKIP,
                              entry.name, 0, planned, 0);
                continue;
            }
        }
        ++progressed;
        emit_progress(progress, ctx,
                      CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_BEGIN,
                      entry.name, progressed, planned, 0);
        int irc = capypkg_install(entry.name);
        if (irc == CAPYPKG_OK || irc == CAPYPKG_ERR_ALREADY) {
            ++installed;
            emit_progress(progress, ctx,
                          CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_OK,
                          entry.name, progressed, planned, irc);
        } else {
            ++failed;
            klog(KLOG_WARN,
                 "[audit] [capypkg] bootstrap: package install failed");
            emit_progress(progress, ctx,
                          CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_FAIL,
                          entry.name, progressed, planned, irc);
        }
    }

    if (out_installed) *out_installed = installed;
    if (out_failed) *out_failed = failed;

    /* Write the marker even when some packages failed: the bootstrap
     * was attempted with a valid profile and we don't want the kernel
     * hook to retry forever. The operator can `pkg-bootstrap --force`
     * after fixing the upstream catalog. */
    (void)profile_write_marker(INSTALL_PROFILE_BOOTSTRAP_DONE,
                               profile.kind == INSTALL_PROFILE_CUSTOM
                                   ? "kind=custom\n"
                                   : "kind=full\n");

    if (failed == 0) {
        klog(KLOG_INFO,
             "[audit] [capypkg] bootstrap: completed (all packages ok)");
    } else {
        klog(KLOG_WARN,
             "[audit] [capypkg] bootstrap: completed with per-package failures");
    }
    emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE,
                  NULL, installed, planned, failed);
    /* Repository/index failures remain retryable errors. Once the sweep
     * starts, however, package failures are isolated: installed packages
     * stay usable and the marker prevents a boot-time retry loop. */
    return INSTALL_PROFILE_OK;
}

/* Backwards-compatible silent entry point: just forwards. */
int capypkg_bootstrap_run(int force, int *out_installed, int *out_failed) {
    return capypkg_bootstrap_run_with_progress(force, out_installed,
                                               out_failed, NULL, NULL);
}
