/*
 * src/kernel/module_gate.c — alpha.241
 *
 * Implementation of the boot-time module activation gate. See
 * include/kernel/module_gate.h for the contract.
 *
 * The gate is intentionally tiny and dependency-free: only the VFS
 * is touched (to stat the installed marker file under
 * /var/capypkg/<name>/installed). No allocation, no logging, no
 * network. Safe to call from any kernel context after the VFS has
 * been initialised.
 */

#include "kernel/module_gate.h"

#include "fs/vfs.h"

#include <stddef.h>
#include <stdint.h>

#define MODULE_GATE_PATH_MAX 192u

/* Stable canonical names — mirror the constants documented in the
 * header. Kept here as static char arrays so a stray rename of one
 * site causes a compile error in the other. */
static const char *const k_desktop_session_name =
    "org.capyos.ui.desktop-session";
static const char *const k_widget_core_name =
    "org.capyos.ui.widget-core";

static int module_gate_compose_marker(const char *name,
                                      char *out, size_t out_size) {
    static const char prefix[] = "/var/capypkg/";
    static const char suffix[] = "/installed";
    size_t cursor = 0u;

    if (!name || !out || out_size == 0u) {
        return -1;
    }

    /* prefix */
    for (size_t i = 0u; prefix[i]; ++i) {
        if (cursor + 1u >= out_size) return -1;
        out[cursor++] = prefix[i];
    }
    /* name (already validated by capypkg before the marker existed;
     * we only need to guard our own buffer here) */
    for (size_t i = 0u; name[i]; ++i) {
        if (cursor + 1u >= out_size) return -1;
        out[cursor++] = name[i];
    }
    /* suffix */
    for (size_t i = 0u; suffix[i]; ++i) {
        if (cursor + 1u >= out_size) return -1;
        out[cursor++] = suffix[i];
    }
    out[cursor] = '\0';
    return 0;
}

int kernel_module_is_installed(const char *canonical_name) {
    char path[MODULE_GATE_PATH_MAX];
    struct vfs_stat st;

    if (module_gate_compose_marker(canonical_name, path, sizeof(path)) != 0) {
        return 0;
    }
    if (vfs_stat_path(path, &st) != 0) {
        return 0;
    }
    return 1;
}

int kernel_module_desktop_session_available(void) {
#ifdef CAPYOS_PROFILE_CORE_ONLY
    /* The kernel was built without the desktop-side code, so even a
     * staged module marker cannot be honored: there is no symbol to
     * activate. Fail closed. */
    return 0;
#else
    return kernel_module_is_installed(k_desktop_session_name);
#endif
}

int kernel_module_widget_core_available(void) {
#ifdef CAPYOS_PROFILE_CORE_ONLY
    return 0;
#else
    return kernel_module_is_installed(k_widget_core_name);
#endif
}
