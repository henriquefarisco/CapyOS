/*
 * src/config/first_boot/modules.c — alpha.241
 *
 * Module selection step of the first-boot wizard.
 *
 * Runs after the admin user has been created (see program.c) and
 * before the wizard's completion marker is written. Asks the
 * operator to pick a module set (BASIC | FULL | CUSTOM), serialises
 * the answers into `/system/install/profile.ini` via the
 * `install_profile` contract, then triggers the in-tree capypkg
 * adapter through `capypkg_bootstrap_run_with_progress` so the
 * download + stage flow shows live progress in the wizard.
 *
 * The wizard never fails on transient network errors: when the
 * bootstrap hits CAPYPKG_ERR_NOT_READY or any storage failure, the
 * profile.ini remains on disk and the kernel auto-bootstrap hook
 * (see kernel_capypkg_maybe_bootstrap) finishes the work after the
 * network comes online. The user is told.
 *
 * Design notes:
 *   - All UI lives in TUI helpers from system_setup_wizard.c. No GUI
 *     dependency: the wizard runs on the fbcon during early first
 *     boot, well before the desktop session module is available.
 *   - install_profile_format + the VFS writer reuse the same
 *     printable-ASCII gate enforced by the parser, so adversarial
 *     answers cannot escape into profile.ini.
 *   - Default modules-index URL is overrideable at compile time via
 *     CAPYOS_DEFAULT_MODULES_INDEX_URL; operators can also paste a
 *     full HTTPS URL when prompted.
 *   - CUSTOM mode asks for a comma-separated list of module names.
 *     The string is fed verbatim into install_profile_format which
 *     in turn round-trips through the parser to enforce the
 *     [a-zA-Z0-9._-] alphabet before persisting.
 */

#include "../internal/first_boot_internal.h"

#include "services/capypkg.h"
#include "services/capypkg_bootstrap.h"
#include "services/install_profile.h"
#include "fs/vfs.h"

#ifndef CAPYOS_DEFAULT_MODULES_INDEX_URL
#define CAPYOS_DEFAULT_MODULES_INDEX_URL \
    "https://capyos.org/releases/modules-index.txt"
#endif

#ifndef CAPYOS_DEFAULT_REPO_NAME
#define CAPYOS_DEFAULT_REPO_NAME "modules"
#endif

#define MODULES_PROFILE_BUF 1024u

/* ---- localized strings (PT default, EN, ES) -------------------------- */

static const char *L(const char *language, const char *pt, const char *en,
                     const char *es) {
    if (strings_equal(language, "en")) return en;
    if (strings_equal(language, "es")) return es;
    return pt;
}

/* ---- profile-driven VFS write --------------------------------------- */

static int modules_write_profile_ini(const struct install_profile *profile) {
    char buf[MODULES_PROFILE_BUF];
    size_t len = 0;
    int rc = install_profile_format(profile, buf, sizeof(buf), &len);
    if (rc != INSTALL_PROFILE_OK || len == 0u) {
        return -1;
    }
    if (config_ensure_directory(INSTALL_PROFILE_DIR) != 0) {
        return -1;
    }
    /* Re-use the wizard's standard text-file writer; it already
     * normalises ownership and respects the persistent root. */
    return config_write_text_file(INSTALL_PROFILE_PATH, buf);
}

/* ---- progress callback ---------------------------------------------- */

struct modules_progress_state {
    int started_sweep;
};

static void modules_render_progress(enum capypkg_bootstrap_event event,
                                    const char *name,
                                    int index, int total,
                                    int rc, void *ctx) {
    struct modules_progress_state *st = (struct modules_progress_state *)ctx;
    char line[160];
    line[0] = '\0';

    switch (event) {
    case CAPYPKG_BOOTSTRAP_EVENT_REPO_REGISTER:
        config_buffer_append(line, sizeof(line), "[modules] registrando repositorio ");
        config_buffer_append(line, sizeof(line), name);
        config_print_line(line);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_INDEX_FETCH:
        config_print_line("[modules] baixando indice de modulos...");
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_BEGIN: {
        char idx_buf[12];
        char tot_buf[12];
        config_u32_to_string((uint32_t)index, idx_buf, sizeof(idx_buf));
        config_u32_to_string((uint32_t)total, tot_buf, sizeof(tot_buf));
        config_buffer_append(line, sizeof(line), "[modules] [");
        config_buffer_append(line, sizeof(line), idx_buf);
        config_buffer_append(line, sizeof(line), "/");
        config_buffer_append(line, sizeof(line), tot_buf);
        config_buffer_append(line, sizeof(line), "] instalando ");
        config_buffer_append(line, sizeof(line), name);
        config_buffer_append(line, sizeof(line), "...");
        config_print_line(line);
        if (st) st->started_sweep = 1;
        break;
    }
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_OK:
        config_buffer_append(line, sizeof(line), "         ok: ");
        config_buffer_append(line, sizeof(line), name);
        config_print_line(line);
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_FAIL: {
        char rc_buf[12];
        config_u32_to_string((uint32_t)(rc < 0 ? -rc : rc), rc_buf, sizeof(rc_buf));
        config_buffer_append(line, sizeof(line), "         falha (rc=");
        config_buffer_append(line, sizeof(line), rc < 0 ? "-" : "");
        config_buffer_append(line, sizeof(line), rc_buf);
        config_buffer_append(line, sizeof(line), "): ");
        config_buffer_append(line, sizeof(line), name);
        config_print_line(line);
        break;
    }
    case CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_SKIP:
        /* Quiet: SKIP fires for every off-list package in CUSTOM
         * mode and would otherwise dominate the screen. */
        (void)name;
        break;
    case CAPYPKG_BOOTSTRAP_EVENT_SWEEP_DONE: {
        char ok_buf[12];
        char fail_buf[12];
        config_u32_to_string((uint32_t)index, ok_buf, sizeof(ok_buf));
        config_u32_to_string((uint32_t)rc, fail_buf, sizeof(fail_buf));
        config_buffer_append(line, sizeof(line), "[modules] sweep completo: ");
        config_buffer_append(line, sizeof(line), ok_buf);
        config_buffer_append(line, sizeof(line), " instalados, ");
        config_buffer_append(line, sizeof(line), fail_buf);
        config_buffer_append(line, sizeof(line), " falhas.");
        config_print_line(line);
        break;
    }
    default:
        break;
    }
}

/* ---- public step ---------------------------------------------------- */

int first_boot_module_selection_step(const char *setup_language) {
    struct install_profile profile;
    install_profile_reset(&profile);

    wizard_draw_header(96u,
                       L(setup_language,
                         "Selecao de modulos",
                         "Module selection",
                         "Seleccion de modulos"));
    config_print_line(L(setup_language,
                        "Escolha quais modulos remotos instalar via repositorios oficiais.",
                        "Choose which remote modules to install from the official repositories.",
                        "Elija que modulos remotos instalar desde los repositorios oficiales."));
    vga_newline();

    const char *profile_items[3];
    profile_items[0] = L(setup_language,
                         "Basico - so o nucleo (capysh, sem desktop)",
                         "Basic - core only (capysh, no desktop)",
                         "Basico - solo el nucleo (capysh, sin escritorio)");
    profile_items[1] = L(setup_language,
                         "Completo - desktop, apps e modulos recomendados",
                         "Full - desktop, apps and recommended modules",
                         "Completo - escritorio, apps y modulos recomendados");
    profile_items[2] = L(setup_language,
                         "Personalizado - eu escolho a lista",
                         "Custom - I pick the list",
                         "Personalizado - elijo la lista");

    int pick = wizard_menu_select_setup(
        96u,
        L(setup_language, "Perfil de instalacao",
          "Installation profile",
          "Perfil de instalacion"),
        setup_language, profile_items, 3u, 1u);

    if (pick == 0) {
        profile.kind = INSTALL_PROFILE_BASIC;
        profile.valid = 1u;
        if (modules_write_profile_ini(&profile) != 0) {
            config_print_line(L(setup_language,
                                "[modules] aviso: nao consegui gravar profile.ini (sera tratado como basico).",
                                "[modules] warning: could not write profile.ini (treated as basic).",
                                "[modules] aviso: no fue posible escribir profile.ini (sera basico)."));
        } else {
            config_print_line(L(setup_language,
                                "[modules] perfil = basico; nenhum modulo remoto sera baixado.",
                                "[modules] profile = basic; no remote module will be fetched.",
                                "[modules] perfil = basico; ningun modulo remoto sera descargado."));
        }
        return 0;
    }

    /* Common path: FULL or CUSTOM both need a repo URL. */
    profile.kind = (pick == 2) ? INSTALL_PROFILE_CUSTOM : INSTALL_PROFILE_FULL;

    cstring_copy(profile.repo_name, sizeof(profile.repo_name),
                 CAPYOS_DEFAULT_REPO_NAME);

    char repo_url_in[INSTALL_PROFILE_URL_MAX];
    memory_zero(repo_url_in, sizeof(repo_url_in));
    {
        char url_prompt[160];
        url_prompt[0] = '\0';
        config_buffer_append(url_prompt, sizeof(url_prompt),
                             L(setup_language,
                               "URL do indice de modulos [Enter = padrao oficial]: ",
                               "Modules-index URL [Enter = official default]: ",
                               "URL del indice de modulos [Enter = oficial]: "));
        size_t len = wizard_prompt_setup(
            97u,
            L(setup_language,
              "Repositorio de modulos",
              "Modules repository",
              "Repositorio de modulos"),
            url_prompt, repo_url_in, sizeof(repo_url_in), 0);
        if (len == 0u) {
            cstring_copy(profile.repo_url, sizeof(profile.repo_url),
                         CAPYOS_DEFAULT_MODULES_INDEX_URL);
        } else {
            cstring_copy(profile.repo_url, sizeof(profile.repo_url),
                         repo_url_in);
        }
    }
    profile.repo_signed = 0u; /* alpha: unsigned modules tolerated */

    if (profile.kind == INSTALL_PROFILE_CUSTOM) {
        char list_in[INSTALL_PROFILE_INSTALL_LIST_MAX];
        memory_zero(list_in, sizeof(list_in));
        size_t len = wizard_prompt_setup(
            98u,
            L(setup_language,
              "Modulos personalizados",
              "Custom module list",
              "Lista de modulos personalizados"),
            L(setup_language,
              "Lista CSV (ex: org.capyos.ui.desktop-session,org.capyos.browser.core): ",
              "CSV list (e.g. org.capyos.ui.desktop-session,org.capyos.browser.core): ",
              "Lista CSV (ej: org.capyos.ui.desktop-session,org.capyos.browser.core): "),
            list_in, sizeof(list_in), 0);
        if (len == 0u) {
            config_print_line(L(setup_language,
                                "[modules] lista vazia; tratando como perfil completo.",
                                "[modules] empty list; treating as full profile.",
                                "[modules] lista vacia; tratando como completo."));
            profile.kind = INSTALL_PROFILE_FULL;
        } else {
            cstring_copy(profile.install_list, sizeof(profile.install_list),
                         list_in);
        }
    }

    profile.valid = 1u;
    if (!install_profile_should_bootstrap(&profile)) {
        config_print_line(L(setup_language,
                            "[modules] aviso: perfil incompleto; voltando para basico.",
                            "[modules] warning: incomplete profile; falling back to basic.",
                            "[modules] aviso: perfil incompleto; volviendo a basico."));
        install_profile_reset(&profile);
        profile.valid = 1u;
        (void)modules_write_profile_ini(&profile);
        return 0;
    }

    if (modules_write_profile_ini(&profile) != 0) {
        config_print_line(L(setup_language,
                            "[modules] erro: nao consegui gravar profile.ini; abortando bootstrap.",
                            "[modules] error: could not write profile.ini; aborting bootstrap.",
                            "[modules] error: no fue posible escribir profile.ini; abortando bootstrap."));
        return -1;
    }
    config_sync_root_device();
    config_print_line(L(setup_language,
                        "[modules] profile.ini gravado. Iniciando bootstrap...",
                        "[modules] profile.ini saved. Starting bootstrap...",
                        "[modules] profile.ini guardado. Iniciando bootstrap..."));

    /* Run the bootstrap synchronously with progress so the user sees
     * what is happening. Any failure here is soft: the profile.ini
     * remains on disk and the kernel auto-bootstrap retries when the
     * network warms up later. */
    struct modules_progress_state st = {0};
    int installed = 0;
    int failed = 0;
    int rc = capypkg_bootstrap_run_with_progress(
        1, &installed, &failed, modules_render_progress, &st);

    if (rc == INSTALL_PROFILE_ERR_NOT_READY) {
        config_print_line(L(setup_language,
                            "[modules] adaptador capypkg ainda nao pronto; bootstrap ocorrera em segundo plano.",
                            "[modules] capypkg adapter not yet ready; bootstrap will run in the background.",
                            "[modules] adaptador capypkg aun no listo; bootstrap se ejecutara en segundo plano."));
        return 0;
    }
    if (rc == INSTALL_PROFILE_ERR_STORAGE) {
        config_print_line(L(setup_language,
                            "[modules] rede ou repositorio indisponivel; sera tentado novamente apos boot.",
                            "[modules] network or repo unavailable; will retry after next boot.",
                            "[modules] red o repositorio no disponible; reintentara despues del proximo arranque."));
        return 0;
    }
    if (rc != INSTALL_PROFILE_OK) {
        config_print_line(L(setup_language,
                            "[modules] aviso: bootstrap retornou erro permanente.",
                            "[modules] warning: bootstrap returned a permanent error.",
                            "[modules] aviso: bootstrap devolvio error permanente."));
        return 0;
    }

    /* Modules successfully staged. Caller should reboot so the
     * activation gate (kernel_main first-boot check on
     * /var/capypkg/org.capyos.ui.desktop-session) initialises the
     * newly installed desktop session in the next boot. */
    if (installed > 0) {
        config_print_line(L(setup_language,
                            "[modules] instalacao concluida; reinicio recomendado para ativar.",
                            "[modules] install complete; reboot recommended to activate.",
                            "[modules] instalacion completa; se recomienda reiniciar para activar."));
        return 1;
    }
    return 0;
}
