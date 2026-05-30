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

static int profile_path_exists(const char *path, uint16_t mode) {
    struct dentry *d = NULL;
    struct vfs_stat st;
    if (vfs_lookup(path, &d) != 0) {
        return 0;
    }
    if (d && d->refcount) {
        d->refcount--;
    }
    if (vfs_stat_path(path, &st) != 0) {
        return 0;
    }
    return (st.mode & mode) != 0 ? 1 : 0;
}

static int profile_ensure_marker_dir(void) {
    if (!profile_path_exists("/system", VFS_MODE_DIR)) {
        if (vfs_create("/system", VFS_MODE_DIR, NULL) != 0 &&
            !profile_path_exists("/system", VFS_MODE_DIR)) {
            return -1;
        }
    }
    if (!profile_path_exists(INSTALL_PROFILE_DIR, VFS_MODE_DIR)) {
        if (vfs_create(INSTALL_PROFILE_DIR, VFS_MODE_DIR, NULL) != 0 &&
            !profile_path_exists(INSTALL_PROFILE_DIR, VFS_MODE_DIR)) {
            return -1;
        }
    }
    return 0;
}

static int profile_remove_existing_path(const char *path) {
    struct vfs_stat st;
    if (vfs_stat_path(path, &st) != 0) {
        return 0;
    }
    if (st.mode & VFS_MODE_DIR) {
        return vfs_rmdir(path);
    }
    return vfs_unlink(path);
}

static int profile_read_from_vfs(struct install_profile *out) {
    static char buffer[CAPYPKG_BOOTSTRAP_PROFILE_BUF];
    struct file *file = NULL;
    struct session_context *previous_session = NULL;
    long read = 0;
    int rc = INSTALL_PROFILE_OK;
    int open_error = VFS_OK;
    if (!out) {
        return INSTALL_PROFILE_ERR_INVALID_ARG;
    }
    install_profile_reset(out);
    previous_session = session_active();
    session_set_active(NULL);
    file = vfs_open(INSTALL_PROFILE_PATH, VFS_OPEN_READ);
    if (!file) {
        open_error = vfs_last_error();
        if (open_error != VFS_ERR_NOT_FOUND) {
            rc = INSTALL_PROFILE_ERR_STORAGE;
        }
        goto done; /* Missing profile.ini keeps the BASIC default. */
    }
    read = vfs_read(file, buffer, sizeof(buffer) - 1u);
    vfs_close(file);
    if (read < 0) {
        rc = INSTALL_PROFILE_ERR_STORAGE;
        goto done;
    }
    if (read == 0) {
        goto done;
    }
    buffer[(size_t)read] = '\0';
    rc = install_profile_parse(buffer, (size_t)read, out);
done:
    session_set_active(previous_session);
    return rc;
}

static int profile_write_marker(const char *path, const char *text) {
    struct file *file = NULL;
    struct dentry *d = NULL;
    struct session_context *previous_session = NULL;
    int rc = 0;
    int create_error = VFS_OK;
    size_t len = 0u;
    if (!path || !text) {
        return -1;
    }
    previous_session = session_active();
    session_set_active(NULL);
    if (profile_strings_equal(path, INSTALL_PROFILE_BOOTSTRAP_DONE)) {
        if (profile_ensure_marker_dir() != 0) {
            rc = -1;
            goto done;
        }
    }
    if (profile_remove_existing_path(path) != 0) {
        rc = -1;
        goto done;
    }
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
        create_error = vfs_last_error();
        if (create_error != VFS_ERR_ALREADY_EXISTS ||
            vfs_lookup(path, &d) != 0) {
            rc = -1;
            goto done;
        }
    }
    if (d && d->refcount) {
        d->refcount--;
    }
    file = vfs_open(path, VFS_OPEN_WRITE);
    if (!file) {
        (void)vfs_unlink(path);
        rc = -1;
        goto done;
    }
    while (text[len] != '\0') {
        ++len;
    }
    if (len > 0u && vfs_write(file, text, len) != (long)len) {
        vfs_close(file);
        (void)vfs_unlink(path);
        rc = -1;
        goto done;
    }
    vfs_close(file);
done:
    session_set_active(previous_session);
    return rc;
}

static int profile_marker_exists(void) {
    struct session_context *previous_session = session_active();
    int exists = 0;
    session_set_active(NULL);
    exists = profile_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE,
                                 VFS_MODE_FILE);
    session_set_active(previous_session);
    return exists;
}

static int profile_install_list_contains(const char *list, const char *name) {
    size_t cursor = 0u;
    char token[INSTALL_PROFILE_NAME_MAX];
    if (!list || !list[0] || !name || !name[0]) {
        return 0;
    }
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
    int prc = INSTALL_PROFILE_OK;
    int rrc = CAPYPKG_OK;
    int frc = CAPYPKG_OK;
    int planned = 0;
    int progressed = 0;
    int irc = CAPYPKG_OK;
    size_t count = 0u;
    size_t i = 0u;
    struct install_profile profile;
    struct capypkg_stats stats;
    struct capypkg_entry entry;

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

    prc = profile_read_from_vfs(&profile);
    if (prc != INSTALL_PROFILE_OK) {
        klog(KLOG_WARN,
             prc == INSTALL_PROFILE_ERR_STORAGE
                 ? "[audit] [capypkg] bootstrap: profile.ini read failed (will retry)"
                 : "[audit] [capypkg] bootstrap: profile.ini rejected");
        return prc;
    }
    if (!install_profile_should_bootstrap(&profile)) {
        /* BASIC profile or missing repo fields: nothing to do.
         * Drop a marker anyway so the kernel hook does not retry
         * forever on every poll. */
        if (profile_write_marker(INSTALL_PROFILE_BOOTSTRAP_DONE,
                                 "kind=basic\n") != 0) {
            klog(KLOG_WARN,
                 "[audit] [capypkg] bootstrap: marker update failed");
            return INSTALL_PROFILE_ERR_STORAGE;
        }
        klog(KLOG_INFO,
             "[audit] [capypkg] bootstrap: profile=basic, no modules");
        emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE,
                      NULL, 0, 0, 0);
        return INSTALL_PROFILE_OK;
    }

    /* Register repository (idempotent). */
    emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER,
                  profile.repo_name, 0, 0, 0);
    rrc = capypkg_repo_add(profile.repo_name, profile.repo_url,
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
    frc = capypkg_fetch_index();
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
    count = capypkg_available_count();
    for (i = 0u; i < count; ++i) {
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
    for (i = 0u; i < count; ++i) {
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
        irc = capypkg_install(entry.name);
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

    if (failed != 0) {
        klog(KLOG_WARN,
             "[audit] [capypkg] bootstrap: incomplete; per-package failures will retry");
        emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE,
                      NULL, installed, planned, failed);
        return INSTALL_PROFILE_ERR_STORAGE;
    }

    if (profile_write_marker(INSTALL_PROFILE_BOOTSTRAP_DONE,
                             profile.kind == INSTALL_PROFILE_CUSTOM
                                 ? "kind=custom\n"
                                 : "kind=full\n") != 0) {
        klog(KLOG_WARN,
             "[audit] [capypkg] bootstrap: marker update failed");
        return INSTALL_PROFILE_ERR_STORAGE;
    }

    klog(KLOG_INFO,
         "[audit] [capypkg] bootstrap: completed (all packages ok)");
    emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE,
                  NULL, installed, planned, failed);
    return INSTALL_PROFILE_OK;
}

/* Backwards-compatible silent entry point: just forwards. */
int capypkg_bootstrap_run(int force, int *out_installed, int *out_failed) {
    return capypkg_bootstrap_run_with_progress(force, out_installed,
                                               out_failed, NULL, NULL);
}
