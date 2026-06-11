#ifndef CONFIG_FIRST_BOOT_MODULES_PROGRESS_H
#define CONFIG_FIRST_BOOT_MODULES_PROGRESS_H

/*
 * src/config/internal/modules_progress.h
 *
 * Shared contract between the first-boot module-install wizard
 * (modules.c: selection menu + profile.ini writer + install/retry
 * orchestration) and its live status-bar renderer (modules_progress.c).
 * Split out so neither translation unit exceeds the 900-line
 * source-layout ceiling.
 */

#include "first_boot_internal.h"

#include "services/capypkg.h"
#include "services/capypkg_bootstrap.h"

#include <stdint.h>

/* PT-default localized string picker shared by the module-selection
 * wizard and its status-bar renderer. */
static inline const char *L(const char *language, const char *pt,
                            const char *en, const char *es) {
    if (strings_equal(language, "en")) return en;
    if (strings_equal(language, "es")) return es;
    return pt;
}

/* Shared status-bar state.
 *
 * The install screen is a redrawn "frame" (an overall progress bar plus
 * a per-package download/verify/install bar) instead of a scrolling log.
 * Two callbacks feed the same state and trigger redraws:
 *
 *   - modules_render_progress() handles the coarse bootstrap events
 *     (repo/index/package begin/ok/fail/retry) and forces a redraw;
 *   - modules_install_observer() handles the fine per-package phase +
 *     byte-level download progress from capypkg and redraws (throttled)
 *     as bytes stream in.
 *
 * The durable audit trail still lives in the kernel klog (capypkg and
 * the bootstrap both emit [audit] lines), so the live frame does not
 * need to scroll the log to the framebuffer. */
struct modules_ui_state {
    const char *language;
    int pkg_index;   /* current package, 1-based (from PACKAGE_BEGIN) */
    int pkg_total;   /* planned package count                         */
    int ok_count;
    int fail_count;
    int retry_count;
    char cur_name[CAPYPKG_NAME_MAX];
    int phase;       /* enum capypkg_install_phase, -1 = none yet      */
    uint64_t dl_cur; /* bytes downloaded for the current package        */
    uint64_t dl_total;
    int last_rc;
    char status[128]; /* short status / last-error line                 */
    /* redraw throttle */
    int last_pct;
    int last_phase;
    uint64_t last_render_tick;
};

/* Force a full redraw of the status-bar frame now. */
void modules_force_render(struct modules_ui_state *st);

/* capypkg per-package install observer (capypkg_set_install_observer):
 * fine phase + byte-level download progress for the current package. */
void modules_install_observer(const char *name,
                              enum capypkg_install_phase phase,
                              uint64_t cur, uint64_t total, void *ctx);

/* capypkg bootstrap coarse-event callback
 * (capypkg_bootstrap_run_with_progress). */
void modules_render_progress(enum capypkg_bootstrap_event event,
                             const char *name, int index, int total,
                             int rc, void *ctx);

#endif /* CONFIG_FIRST_BOOT_MODULES_PROGRESS_H */
