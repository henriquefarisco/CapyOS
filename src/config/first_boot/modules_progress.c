/*
 * src/config/first_boot/modules_progress.c — alpha.256
 *
 * Live status-bar renderer for the first-boot module-install wizard.
 * Split out of modules.c (which keeps the selection menu, profile.ini
 * writer and install/retry orchestration) to stay under the 900-line
 * source-layout ceiling. The shared struct modules_ui_state and the
 * localized-string helper live in ../internal/modules_progress.h.
 *
 * Two entry points are driven by the install loop in modules.c:
 *   - modules_render_progress(): coarse capypkg bootstrap events
 *     (repo/index/package begin/ok/fail/retry/sweep);
 *   - modules_install_observer(): fine per-package phase + byte-level
 *     download progress from capypkg_set_install_observer().
 * Both update the shared state and (re)draw the frame.
 */

#include "../internal/first_boot_internal.h"
#include "../internal/modules_progress.h"

#include "drivers/timer/pit.h"
#include "net/http.h"
#include "security/tls.h"
#include "services/capypkg.h"
#include "services/capypkg_bootstrap.h"

/* ~30ms at the 100 Hz PIT: caps observer-driven redraws so a fast
 * download does not flood the (slow) framebuffer with full repaints. */
#define MODULES_RENDER_MIN_TICKS 3u
#define MODULES_BAR_WIDTH 20

static const char *modules_phase_label(const char *lang, int phase) {
    switch (phase) {
    case CAPYPKG_INSTALL_PHASE_RESOLVE:
        return L(lang, "Dependencias", "Dependencies", "Dependencias");
    case CAPYPKG_INSTALL_PHASE_DOWNLOAD:
        return L(lang, "Baixando    ", "Downloading ", "Descargando ");
    case CAPYPKG_INSTALL_PHASE_VERIFY:
        return L(lang, "Verificando ", "Verifying   ", "Verificando ");
    case CAPYPKG_INSTALL_PHASE_STAGE:
        return L(lang, "Instalando  ", "Installing  ", "Instalando  ");
    case CAPYPKG_INSTALL_PHASE_DONE:
        return L(lang, "Concluido   ", "Done        ", "Completado  ");
    default:
        return L(lang, "Preparando  ", "Preparing   ", "Preparando  ");
    }
}

/* Map the current package's phase to an overall 0..100 completion, with
 * the download phase (the bulk of the work) spanning 2..82%. */
static int modules_pkg_phase_pct(const struct modules_ui_state *st) {
    switch (st->phase) {
    case CAPYPKG_INSTALL_PHASE_RESOLVE:
        return 2;
    case CAPYPKG_INSTALL_PHASE_DOWNLOAD:
        if (st->dl_total > 0u) {
            return 2 + (int)((st->dl_cur * 80u) / st->dl_total);
        }
        return 42; /* unknown length: indeterminate midpoint */
    case CAPYPKG_INSTALL_PHASE_VERIFY:
        return 88;
    case CAPYPKG_INSTALL_PHASE_STAGE:
        return 95;
    case CAPYPKG_INSTALL_PHASE_DONE:
        return 100;
    default:
        return 0;
    }
}

static void modules_append_bar(char *line, size_t size, int pct) {
    char num[12];
    int filled;
    int i;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    filled = (pct * MODULES_BAR_WIDTH) / 100;
    config_buffer_append(line, size, "[");
    for (i = 0; i < MODULES_BAR_WIDTH; ++i) {
        config_buffer_append(line, size, i < filled ? "#" : "-");
    }
    config_buffer_append(line, size, "] ");
    config_u32_to_string((uint32_t)pct, num, sizeof(num));
    config_buffer_append(line, size, num);
    config_buffer_append(line, size, "%");
}

static void modules_render_frame(const struct modules_ui_state *st) {
    const char *lang = st->language;
    char line[192];
    char num[12];
    int completed = st->ok_count + st->fail_count;
    int total = st->pkg_total;
    int overall = 0;

    if (total > 0) {
        overall = (completed * 100 + modules_pkg_phase_pct(st)) / total;
        if (overall > 100) overall = 100;
    }

    vga_clear();
    vga_write("CAPYOS");
    vga_newline();
    vga_newline();
    vga_write(L(lang, "Instalando modulos", "Installing modules",
                "Instalando modulos"));
    vga_newline();
    vga_newline();

    line[0] = '\0';
    config_buffer_append(line, sizeof(line),
                         L(lang, "Total   ", "Overall ", "Total   "));
    modules_append_bar(line, sizeof(line), overall);
    config_buffer_append(line, sizeof(line), "  (");
    config_u32_to_string((uint32_t)completed, num, sizeof(num));
    config_buffer_append(line, sizeof(line), num);
    config_buffer_append(line, sizeof(line), "/");
    config_u32_to_string((uint32_t)(total > 0 ? total : 0), num, sizeof(num));
    config_buffer_append(line, sizeof(line), num);
    config_buffer_append(line, sizeof(line),
                         L(lang, " pacotes)", " packages)", " paquetes)"));
    vga_write(line);
    vga_newline();
    vga_newline();

    if (st->cur_name[0]) {
        line[0] = '\0';
        config_buffer_append(line, sizeof(line),
                             L(lang, "Pacote: ", "Package: ", "Paquete: "));
        config_buffer_append(line, sizeof(line), st->cur_name);
        vga_write(line);
        vga_newline();

        line[0] = '\0';
        config_buffer_append(line, sizeof(line), "  ");
        config_buffer_append(line, sizeof(line),
                             modules_phase_label(lang, st->phase));
        config_buffer_append(line, sizeof(line), " ");
        if (st->phase == CAPYPKG_INSTALL_PHASE_DOWNLOAD && st->dl_total > 0u) {
            modules_append_bar(line, sizeof(line),
                               (int)((st->dl_cur * 100u) / st->dl_total));
        } else {
            modules_append_bar(line, sizeof(line), modules_pkg_phase_pct(st));
        }
        vga_write(line);
        vga_newline();

        if (st->phase == CAPYPKG_INSTALL_PHASE_DOWNLOAD && st->dl_total > 0u) {
            line[0] = '\0';
            config_buffer_append(line, sizeof(line), "  ");
            config_u32_to_string((uint32_t)(st->dl_cur / 1024u), num,
                                 sizeof(num));
            config_buffer_append(line, sizeof(line), num);
            config_buffer_append(line, sizeof(line), "/");
            config_u32_to_string((uint32_t)(st->dl_total / 1024u), num,
                                 sizeof(num));
            config_buffer_append(line, sizeof(line), num);
            config_buffer_append(line, sizeof(line), " KiB");
            vga_write(line);
            vga_newline();
        }
    }
    vga_newline();

    line[0] = '\0';
    config_buffer_append(line, sizeof(line),
                         L(lang, "Concluidos: ", "Completed: ",
                           "Completados: "));
    config_u32_to_string((uint32_t)st->ok_count, num, sizeof(num));
    config_buffer_append(line, sizeof(line), num);
    config_buffer_append(line, sizeof(line),
                         L(lang, "   Falhas: ", "   Failed: ", "   Fallos: "));
    config_u32_to_string((uint32_t)st->fail_count, num, sizeof(num));
    config_buffer_append(line, sizeof(line), num);
    vga_write(line);
    vga_newline();

    if (st->status[0]) {
        vga_newline();
        vga_write(st->status);
        vga_newline();
    }
}

void modules_force_render(struct modules_ui_state *st) {
    st->last_render_tick = pit_ticks();
    st->last_phase = st->phase;
    st->last_pct = modules_pkg_phase_pct(st);
    modules_render_frame(st);
}

static void modules_maybe_render(struct modules_ui_state *st) {
    int pct = modules_pkg_phase_pct(st);
    uint64_t now = pit_ticks();
    if (st->phase != st->last_phase ||
        (pct != st->last_pct &&
         (now - st->last_render_tick) >= MODULES_RENDER_MIN_TICKS)) {
        st->last_render_tick = now;
        st->last_phase = st->phase;
        st->last_pct = pct;
        modules_render_frame(st);
    }
}

/* capypkg per-package install observer: fine phase + byte-level download
 * progress for the package currently being installed. */
void modules_install_observer(const char *name,
                                     enum capypkg_install_phase phase,
                                     uint64_t cur, uint64_t total,
                                     void *ctx) {
    struct modules_ui_state *st = (struct modules_ui_state *)ctx;
    if (!st) return;
    if (name && name[0]) {
        cstring_copy(st->cur_name, sizeof(st->cur_name), name);
    }
    st->phase = (int)phase;
    if (phase == CAPYPKG_INSTALL_PHASE_DOWNLOAD) {
        st->dl_cur = cur;
        st->dl_total = total;
    }
    modules_maybe_render(st);
}

/* Build the localized one-line diagnostic shown at the bottom of the
 * frame for a failed stage. */
static void modules_set_status_fail(struct modules_ui_state *st,
                                    const char *prefix, int rc,
                                    const char *detail) {
    char num[12];
    st->status[0] = '\0';
    config_buffer_append(st->status, sizeof(st->status), prefix);
    config_buffer_append(st->status, sizeof(st->status), " (rc=");
    config_buffer_append(st->status, sizeof(st->status), rc < 0 ? "-" : "");
    config_u32_to_string((uint32_t)(rc < 0 ? -rc : rc), num, sizeof(num));
    config_buffer_append(st->status, sizeof(st->status), num);
    config_buffer_append(st->status, sizeof(st->status), ") ");
    if (detail) {
        config_buffer_append(st->status, sizeof(st->status), detail);
    }
}

/* Coarse bootstrap-event callback: updates counters/stage and forces a
 * redraw. The fine-grained per-package progress arrives separately
 * through modules_install_observer. */
void modules_render_progress(enum capypkg_bootstrap_event event,
                                    const char *name,
                                    int index, int total,
                                    int rc, void *ctx) {
    struct modules_ui_state *st = (struct modules_ui_state *)ctx;
    if (!st) return;

    switch (event) {
    case CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER:
        cstring_copy(st->status, sizeof(st->status),
                     L(st->language, "Registrando repositorio...",
                       "Registering repository...",
                       "Registrando repositorio..."));
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER_FAIL:
        st->last_rc = rc;
        modules_set_status_fail(
            st, L(st->language, "Falha ao registrar repositorio",
                  "Repository registration failed",
                  "Fallo al registrar repositorio"),
            rc, name);
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_INDEX_FETCH:
        cstring_copy(st->status, sizeof(st->status),
                     L(st->language, "Baixando indice de modulos...",
                       "Downloading module index...",
                       "Descargando indice de modulos..."));
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_INDEX_FETCH_FAIL: {
        char detail[80];
        detail[0] = '\0';
        config_buffer_append(detail, sizeof(detail),
                             http_error_string(http_last_error()));
        config_buffer_append(detail, sizeof(detail), " tls=");
        config_buffer_append(detail, sizeof(detail),
                             tls_state_name(tls_last_state()));
        st->last_rc = rc;
        modules_set_status_fail(
            st, L(st->language, "Falha ao baixar indice",
                  "Index download failed", "Fallo al descargar indice"),
            rc, detail);
        modules_force_render(st);
        break;
    }
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_BEGIN:
        st->pkg_index = index;
        st->pkg_total = total;
        cstring_copy(st->cur_name, sizeof(st->cur_name), name ? name : "");
        st->phase = -1;
        st->dl_cur = 0u;
        st->dl_total = 0u;
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_OK:
        st->ok_count++;
        st->phase = CAPYPKG_INSTALL_PHASE_DONE;
        st->status[0] = '\0';
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_FAIL:
        st->fail_count++;
        st->last_rc = rc;
        modules_set_status_fail(
            st, name ? name : "?", rc, capypkg_result_label(rc));
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_RETRY: {
        char detail[64];
        detail[0] = '\0';
        st->retry_count++;
        config_buffer_append(detail, sizeof(detail), name ? name : "?");
        modules_set_status_fail(
            st, L(st->language, "Repetindo pacote", "Retrying package",
                  "Reintentando paquete"),
            rc, detail);
        modules_force_render(st);
        break;
    }
    case CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE:
        if (total > 0) {
            st->pkg_total = total;
        }
        modules_force_render(st);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_SKIP:
    default:
        break;
    }
}
