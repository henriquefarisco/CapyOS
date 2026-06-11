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

/* ---- dependency-ordered install planner ----------------------------- *
 *
 * The first-boot sweep no longer installs packages in raw catalog
 * order. It builds a plan that:
 *
 *   - selects the target set (FULL = every available package; CUSTOM =
 *     the bootstrap_install list expanded to include the transitive
 *     dependencies that exist in the catalog);
 *   - groups the targets into dependency "waves": a package lands in the
 *     earliest wave in which all of its in-target dependencies are
 *     already placed (or were already installed). Packages in the same
 *     wave have no dependency relationship, so the order inside a wave is
 *     irrelevant — they are independent (the parallel-ready seam below);
 *   - installs each package with an individual in-place retry for
 *     transient errors, so one flaky download does not restart the sweep.
 *
 * capypkg_install() still performs its own recursive dependency
 * resolution as a backstop; ordering the sweep here means each
 * dependency is attempted (and retried) on its own before any dependent,
 * and an already-installed dependency is a cheap no-op for the
 * dependent. */

#define CAPYPKG_BOOTSTRAP_PLAN_MAX CAPYPKG_MAX_AVAILABLE
/* Total install attempts per package (1 initial + up to 2 retries). The
 * wizard/kernel poll still retry the whole sweep with backoff on top of
 * this for longer outages; this bound only absorbs transient blips. */
#define CAPYPKG_BOOTSTRAP_PKG_ATTEMPTS 3u

struct bootstrap_planner {
    uint8_t  is_target[CAPYPKG_BOOTSTRAP_PLAN_MAX];
    uint8_t  placed[CAPYPKG_BOOTSTRAP_PLAN_MAX];
    uint8_t  dep_n[CAPYPKG_BOOTSTRAP_PLAN_MAX];
    uint16_t dep_idx[CAPYPKG_BOOTSTRAP_PLAN_MAX][CAPYPKG_MAX_DEPS];
    uint16_t order[CAPYPKG_BOOTSTRAP_PLAN_MAX];
    uint16_t wave[CAPYPKG_BOOTSTRAP_PLAN_MAX];
    uint16_t order_count;
    uint16_t target_count;
};

/* Single-shot scratch for the planner. The capypkg adapter (and this
 * bootstrap) are single-threaded, so a file-scope instance keeps the
 * ~3 KiB plan off the kernel stack without a reentrancy hazard. */
static struct bootstrap_planner g_bootstrap_plan;

static void bootstrap_zero(void *ptr, size_t len) {
    uint8_t *dst = (uint8_t *)ptr;
    while (len--) {
        *dst++ = 0u;
    }
}

/* Catalog index of `name`, or -1 when it is not in the available set. */
static int bootstrap_catalog_index(const char *name) {
    size_t count = capypkg_available_count();
    struct capypkg_entry e;
    if (!name || !name[0]) {
        return -1;
    }
    if (count > CAPYPKG_BOOTSTRAP_PLAN_MAX) {
        count = CAPYPKG_BOOTSTRAP_PLAN_MAX;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (capypkg_available_get_at(i, &e) == CAPYPKG_OK &&
            profile_strings_equal(e.name, name)) {
            return (int)i;
        }
    }
    return -1;
}

/* Only retry errors that an immediate second attempt can plausibly
 * clear: a transient network fetch, a transient storage write, or a
 * corrupted/truncated download (digest mismatch). Deterministic errors
 * (missing signature/verifier, unresolved dependency, quota, policy) are
 * returned as-is so retries are not wasted on them. */
static int bootstrap_pkg_error_is_retryable(int rc) {
    return rc == CAPYPKG_ERR_FETCH ||
           rc == CAPYPKG_ERR_STORAGE ||
           rc == CAPYPKG_ERR_DIGEST;
}

static int bootstrap_install_one_with_retry(
        const char *name, int index, int total,
        capypkg_bootstrap_progress_fn progress, void *ctx) {
    int irc = capypkg_install(name);
    for (uint32_t attempt = 1u;
         attempt < CAPYPKG_BOOTSTRAP_PKG_ATTEMPTS &&
         irc != CAPYPKG_OK && irc != CAPYPKG_ERR_ALREADY;
         ++attempt) {
        if (!bootstrap_pkg_error_is_retryable(irc)) {
            break;
        }
        emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_RETRY,
                      name, index, total, irc);
        irc = capypkg_install(name);
    }
    return irc;
}

/* Build g_bootstrap_plan for `profile`: select the target set, resolve
 * the in-target dependency edges and order the targets into waves. */
static void bootstrap_plan_build(const struct install_profile *profile) {
    size_t count = capypkg_available_count();
    struct capypkg_entry e;
    uint16_t wave_num = 0u;

    bootstrap_zero(&g_bootstrap_plan, sizeof(g_bootstrap_plan));
    if (count > CAPYPKG_BOOTSTRAP_PLAN_MAX) {
        count = CAPYPKG_BOOTSTRAP_PLAN_MAX;
    }

    /* Select initial targets. */
    for (size_t i = 0u; i < count; ++i) {
        if (capypkg_available_get_at(i, &e) != CAPYPKG_OK) continue;
        if (profile->kind == INSTALL_PROFILE_CUSTOM) {
            if (profile_install_list_contains(profile->install_list, e.name)) {
                g_bootstrap_plan.is_target[i] = 1u;
            }
        } else {
            g_bootstrap_plan.is_target[i] = 1u; /* FULL: everything */
        }
    }

    /* CUSTOM: pull in transitive in-catalog dependencies so a selected
     * package is never installed without what it needs. */
    if (profile->kind == INSTALL_PROFILE_CUSTOM) {
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t i = 0u; i < count; ++i) {
                if (!g_bootstrap_plan.is_target[i]) continue;
                if (capypkg_available_get_at(i, &e) != CAPYPKG_OK) continue;
                for (uint32_t d = 0u; d < e.dep_count; ++d) {
                    int di = bootstrap_catalog_index(e.deps[d]);
                    if (di >= 0 && !g_bootstrap_plan.is_target[di]) {
                        g_bootstrap_plan.is_target[di] = 1u;
                        changed = 1;
                    }
                }
            }
        }
    }

    /* Resolve the dependency edges that constrain ordering. A dep that is
     * already installed (or not in the target set) does not constrain the
     * wave assignment. */
    for (size_t i = 0u; i < count; ++i) {
        struct capypkg_entry dep_probe;
        uint8_t n = 0u;
        if (!g_bootstrap_plan.is_target[i]) continue;
        if (capypkg_available_get_at(i, &e) != CAPYPKG_OK) continue;
        for (uint32_t d = 0u; d < e.dep_count && n < CAPYPKG_MAX_DEPS; ++d) {
            if (capypkg_installed_get(e.deps[d], &dep_probe) == CAPYPKG_OK) {
                continue; /* already satisfied */
            }
            int di = bootstrap_catalog_index(e.deps[d]);
            if (di >= 0 && g_bootstrap_plan.is_target[di]) {
                g_bootstrap_plan.dep_idx[i][n++] = (uint16_t)di;
            }
            /* A dep neither installed nor in the catalog is non-blocking
             * here; capypkg_install will surface CAPYPKG_ERR_DEPENDENCY. */
        }
        g_bootstrap_plan.dep_n[i] = n;
        g_bootstrap_plan.target_count++;
    }

    /* Wave assignment. A snapshot per pass (placed[] updated only after
     * the pass) keeps each wave's members mutually independent. */
    while (g_bootstrap_plan.order_count < g_bootstrap_plan.target_count) {
        uint16_t start = g_bootstrap_plan.order_count;
        for (size_t i = 0u; i < count; ++i) {
            int ready = 1;
            if (!g_bootstrap_plan.is_target[i] || g_bootstrap_plan.placed[i]) {
                continue;
            }
            for (uint8_t k = 0u; k < g_bootstrap_plan.dep_n[i]; ++k) {
                if (!g_bootstrap_plan.placed[g_bootstrap_plan.dep_idx[i][k]]) {
                    ready = 0;
                    break;
                }
            }
            if (ready) {
                g_bootstrap_plan.order[g_bootstrap_plan.order_count] =
                    (uint16_t)i;
                g_bootstrap_plan.wave[g_bootstrap_plan.order_count] = wave_num;
                g_bootstrap_plan.order_count++;
            }
        }
        if (g_bootstrap_plan.order_count == start) {
            /* Dependency cycle or unsatisfiable edge: emit the remaining
             * targets in a final wave so they are still attempted (and
             * fail cleanly inside capypkg_install) rather than dropped. */
            for (size_t i = 0u; i < count; ++i) {
                if (!g_bootstrap_plan.is_target[i] ||
                    g_bootstrap_plan.placed[i]) {
                    continue;
                }
                g_bootstrap_plan.order[g_bootstrap_plan.order_count] =
                    (uint16_t)i;
                g_bootstrap_plan.wave[g_bootstrap_plan.order_count] = wave_num;
                g_bootstrap_plan.placed[i] = 1u;
                g_bootstrap_plan.order_count++;
            }
            break;
        }
        for (uint16_t j = start; j < g_bootstrap_plan.order_count; ++j) {
            g_bootstrap_plan.placed[g_bootstrap_plan.order[j]] = 1u;
        }
        wave_num++;
    }
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
    uint16_t slot = 0u;
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

    /* Build the dependency-ordered install plan from the freshly fetched
     * catalog. `planned` becomes the number of packages we will actually
     * attempt (deps included for CUSTOM), so the UI's "[i/N]" is exact. */
    bootstrap_plan_build(&profile);
    planned = (int)g_bootstrap_plan.order_count;

    /* Install in wave order. Each per-package failure is counted but does
     * not abort the sweep: a single bad package must not block the rest.
     *
     * PARALLEL-READY SEAM: packages that share g_bootstrap_plan.wave[slot]
     * are mutually independent. They are installed sequentially today
     * because the network/TLS/VFS/capypkg layers are single-threaded and
     * the first-boot wizard runs before the preemptive scheduler is
     * enabled (CAPYOS_PREEMPTIVE_SCHEDULER is off by default). Once those
     * are reentrant, a whole wave can be submitted to a kernel worker
     * pool and drained before advancing to the next wave, with no change
     * to the plan above. */
    for (slot = 0u; slot < g_bootstrap_plan.order_count; ++slot) {
        struct capypkg_entry plan_entry;
        uint16_t cidx = g_bootstrap_plan.order[slot];
        int irc;
        if (capypkg_available_get_at(cidx, &plan_entry) != CAPYPKG_OK) {
            continue;
        }
        ++progressed;
        emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_BEGIN,
                      plan_entry.name, progressed, planned, 0);
        irc = bootstrap_install_one_with_retry(plan_entry.name, progressed,
                                               planned, progress, ctx);
        if (irc == CAPYPKG_OK || irc == CAPYPKG_ERR_ALREADY) {
            ++installed;
            emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_OK,
                          plan_entry.name, progressed, planned, irc);
        } else {
            ++failed;
            klog(KLOG_WARN,
                 "[audit] [capypkg] bootstrap: package install failed");
            emit_progress(progress, ctx, CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_FAIL,
                          plan_entry.name, progressed, planned, irc);
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
