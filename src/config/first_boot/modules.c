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
 *   - CUSTOM mode renders a checkbox list of official modules and
 *     serialises the selected names as bootstrap_install CSV.
 */

#include "../internal/first_boot_internal.h"

#include "drivers/console/tty.h"
#include "drivers/timer/pit.h"
#include "arch/x86_64/framebuffer_console.h"
#include "net/http.h"
#include "net/stack.h"
#include "security/tls.h"
#include "services/capypkg.h"
#include "services/capypkg_bootstrap.h"
#include "services/capypkg_runtime.h"
#include "services/install_profile.h"
#include "fs/vfs.h"

/* struct modules_ui_state, the L() string picker and the status-bar
 * renderer entry points live here; the renderer itself is in the
 * sibling modules_progress.c translation unit. */
#include "../internal/modules_progress.h"

/*
 * Default modules-index URL.
 *
 * Pin the modules index to the frozen CapyUI release consumed by this
 * CapyOS alpha. Avoiding `/releases/latest/download/` keeps the kernel
 * downloader on a direct release asset URL with no redirect dependency.
 *
 * The compile-time CAPYOS_DEFAULT_MODULES_INDEX_URL knob lets vendor
 * builds redirect to a private/mirrored channel without patching this
 * source file.
 */
#ifndef CAPYOS_DEFAULT_MODULES_INDEX_URL
#define CAPYOS_DEFAULT_MODULES_INDEX_URL \
    "https://github.com/henriquefarisco/CapyUI/releases/download/v2.13.0/modules-index.txt"
#endif

#ifndef CAPYOS_DEFAULT_REPO_NAME
#define CAPYOS_DEFAULT_REPO_NAME "modules"
#endif

#define MODULES_PROFILE_BUF 1024u
#define MODULES_OFFICIAL_COUNT 7u

/* ---- localized strings (PT default, EN, ES) -------------------------- */
/* The L(language, pt, en, es) picker is shared with the status-bar
 * renderer and is defined in ../internal/modules_progress.h. */

struct modules_official_package {
    const char *name;
    const char *pt;
    const char *en;
    const char *es;
};

static const struct modules_official_package g_modules_official[MODULES_OFFICIAL_COUNT] = {
    {"org.capyos.ui.widget-core",
     "CapyUI widgets oficiais",
     "Official CapyUI widgets",
     "Widgets oficiales de CapyUI"},
    {"org.capyos.ui.desktop-session",
     "Desktop oficial CapyUI",
     "Official CapyUI desktop",
     "Escritorio oficial CapyUI"},
    {"org.capyos.browser.core",
     "Navegador oficial",
     "Official browser",
     "Navegador oficial"},
    {"org.capyos.codecs.image-basic",
     "Codecs oficiais de imagem",
     "Official image codecs",
     "Codecs oficiales de imagen"},
    {"org.capyos.agent.core",
     "Agente oficial",
     "Official agent",
     "Agente oficial"},
    {"org.capyos.lang.runtime",
     "Runtime oficial CapyLang",
     "Official CapyLang runtime",
     "Runtime oficial CapyLang"},
    {"org.capyos.benchmark.harness",
     "Benchmark oficial",
     "Official benchmark",
     "Benchmark oficial"},
};

static const char *modules_official_label(const char *language, size_t idx) {
    const struct modules_official_package *p = &g_modules_official[idx];
    return L(language, p->pt, p->en, p->es);
}

static size_t modules_build_selected_csv(const uint8_t selected[MODULES_OFFICIAL_COUNT],
                                         char *out, size_t out_size) {
    size_t count = 0u;
    if (!out || out_size == 0u) return 0u;
    out[0] = '\0';
    for (size_t i = 0u; i < MODULES_OFFICIAL_COUNT; ++i) {
        if (!selected[i]) continue;
        if (count > 0u) {
            config_buffer_append(out, out_size, ",");
        }
        config_buffer_append(out, out_size, g_modules_official[i].name);
        ++count;
    }
    return count;
}

static void modules_draw_official_checklist(const char *setup_language,
                                            const uint8_t selected[MODULES_OFFICIAL_COUNT],
                                            size_t cursor) {
    wizard_draw_header(98u,
                       L(setup_language,
                         "Modulos oficiais",
                         "Official modules",
                         "Modulos oficiales"));
    config_print_line(L(setup_language,
                        "Espaco alterna, numeros alternam, Enter confirma.",
                        "Space toggles, numbers toggle, Enter confirms.",
                        "Espacio alterna, numeros alternan, Enter confirma."));
    config_print_line(L(setup_language,
                        "[a] marcar todos, [n] desmarcar todos.",
                        "[a] select all, [n] select none.",
                        "[a] marcar todos, [n] desmarcar todos."));
    vga_newline();
    for (size_t i = 0u; i < MODULES_OFFICIAL_COUNT; ++i) {
        char line[192];
        char idx[12];
        line[0] = '\0';
        config_u32_to_string((uint32_t)(i + 1u), idx, sizeof(idx));
        config_buffer_append(line, sizeof(line), (i == cursor) ? "> " : "  ");
        config_buffer_append(line, sizeof(line), selected[i] ? "[x] " : "[ ] ");
        config_buffer_append(line, sizeof(line), idx);
        config_buffer_append(line, sizeof(line), ". ");
        config_buffer_append(line, sizeof(line), modules_official_label(setup_language, i));
        config_buffer_append(line, sizeof(line), " - ");
        config_buffer_append(line, sizeof(line), g_modules_official[i].name);
        config_print_line(line);
    }
}

static size_t modules_select_official_packages(const char *setup_language,
                                               char *out_csv, size_t out_csv_size) {
    uint8_t selected[MODULES_OFFICIAL_COUNT];
    size_t cursor = 0u;
    for (size_t i = 0u; i < MODULES_OFFICIAL_COUNT; ++i) {
        selected[i] = 1u;
    }
    tty_set_echo(1);
    tty_set_echo_mask('\0');
    for (;;) {
        char ch;
        modules_draw_official_checklist(setup_language, selected, cursor);
        ch = tty_getc();
        if (ch == '\r' || ch == '\n') {
            return modules_build_selected_csv(selected, out_csv, out_csv_size);
        }
        if (ch == ' ' || ch == '\t') {
            selected[cursor] = selected[cursor] ? 0u : 1u;
            continue;
        }
        if (ch == 'a' || ch == 'A') {
            for (size_t i = 0u; i < MODULES_OFFICIAL_COUNT; ++i) selected[i] = 1u;
            continue;
        }
        if (ch == 'n' || ch == 'N') {
            for (size_t i = 0u; i < MODULES_OFFICIAL_COUNT; ++i) selected[i] = 0u;
            continue;
        }
        if (ch >= '1' && ch <= '9') {
            size_t idx = (size_t)(ch - '1');
            if (idx < MODULES_OFFICIAL_COUNT) {
                selected[idx] = selected[idx] ? 0u : 1u;
                cursor = idx;
            }
            continue;
        }
        if (ch != 27) {
            continue;
        }
        ch = tty_getc();
        if (ch != '[') {
            continue;
        }
        ch = tty_getc();
        if (ch == 'A') {
            cursor = (cursor == 0u) ? (MODULES_OFFICIAL_COUNT - 1u) : (cursor - 1u);
        } else if (ch == 'B') {
            cursor = (cursor + 1u) % MODULES_OFFICIAL_COUNT;
        }
    }
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


/* ---- timing + network + retry helpers ------------------------------- */

/* PIT runs at 100 Hz (platform_timer.c), so 1 tick = 10 ms. */
#define MODULES_TICKS_PER_SECOND 100u
#define MODULES_NETWORK_WAIT_TICKS (20u * MODULES_TICKS_PER_SECOND)
#define MODULES_RETRY_MAX 3u

/* Busy-wait helper that does NOT touch the keyboard. Used between
 * retries and while polling the network: the wizard is interactive
 * only at well-defined moments (the retry/cancel dialogue), and a
 * non-polled sleep keeps the framebuffer/log layout deterministic. */
static void modules_sleep_ticks(uint32_t ticks) {
    uint64_t target = pit_ticks() + (uint64_t)ticks;
    while (pit_ticks() < target) {
        /* spin */
        __asm__ volatile("pause");
    }
}

/* Wait for the kernel network stack to flag ready. If DHCP was
 * attempted (`dhcp_attempts > 0`) we also require a lease, because
 * otherwise http_get is guaranteed to fail at DNS resolution. This
 * heuristic avoids leaking `g_shell_settings` into config/ code.
 * Returns 1 on success, 0 on timeout. */
static int modules_wait_for_network(uint32_t timeout_ticks,
                                    const char *setup_language) {
    (void)setup_language;
    uint64_t deadline = pit_ticks() + (uint64_t)timeout_ticks;
    int announced = 0;
    while (pit_ticks() < deadline) {
        struct net_stack_status status;
        if (net_stack_status(&status) == 0 &&
            status.initialized && status.runtime_supported &&
            status.nic.found && status.ready) {
            int dhcp_tried = (status.dhcp_attempts > 0u);
            if (!dhcp_tried || status.dhcp_lease_acquired) {
                return 1;
            }
        }
        if (!announced) {
            config_print_line("[modules] aguardando rede ficar pronta...");
            announced = 1;
        }
        modules_sleep_ticks(MODULES_TICKS_PER_SECOND); /* 1 s */
    }
    return 0;
}

/* Run capypkg_bootstrap_run_with_progress up to MODULES_RETRY_MAX times
 * with exponential backoff (2/4/8 s) at the *sweep* level. Per-package
 * retries already happen inside the bootstrap; this outer loop only
 * covers longer outages (DHCP/DNS/TLS warm-up). The shared `ui` state
 * drives the live status bar across attempts. Returns the
 * install_profile_result of the last attempt. */
static int modules_run_bootstrap_with_retry(struct modules_ui_state *ui,
                                            int *out_installed,
                                            int *out_failed) {
    static const uint32_t backoff_seconds[MODULES_RETRY_MAX] = {2u, 4u, 8u};
    int rc = INSTALL_PROFILE_ERR_STORAGE;
    if (out_installed) *out_installed = 0;
    if (out_failed) *out_failed = 0;
    for (uint32_t attempt = 0u; attempt < MODULES_RETRY_MAX; ++attempt) {
        int installed = 0;
        int failed = 0;
        rc = capypkg_bootstrap_run_with_progress(
            1, &installed, &failed, modules_render_progress, ui);
        if (out_installed) *out_installed = installed;
        if (out_failed) *out_failed = failed;
        if (rc == INSTALL_PROFILE_OK && failed == 0) {
            return rc;
        }
        if (rc == INSTALL_PROFILE_OK && failed > 0) {
            rc = INSTALL_PROFILE_ERR_STORAGE;
        }
        if (rc == INSTALL_PROFILE_ERR_NOT_READY) {
            return rc;
        }
        if (attempt + 1u < MODULES_RETRY_MAX) {
            uint32_t wait_s = backoff_seconds[attempt];
            char num[12];
            ui->status[0] = '\0';
            config_buffer_append(ui->status, sizeof(ui->status),
                                 L(ui->language, "Nova tentativa em ",
                                   "Retrying in ", "Reintento en "));
            config_u32_to_string(wait_s, num, sizeof(num));
            config_buffer_append(ui->status, sizeof(ui->status), num);
            config_buffer_append(ui->status, sizeof(ui->status), "s...");
            modules_force_render(ui);
            modules_sleep_ticks(wait_s * MODULES_TICKS_PER_SECOND);
        }
    }
    return rc;
}

/* Ask the operator how to proceed after MODULES_RETRY_MAX failed
 * attempts. Returns 'r' (try again), 'b' (background) or 'c'
 * (cancel). Any other key defaults to 'b' so an accidental keystroke
 * never silently downgrades the install profile. */
static char modules_ask_retry_choice(const char *setup_language) {
    uint64_t deadline = pit_ticks() + (30u * MODULES_TICKS_PER_SECOND);
    int announced_timeout = 0;

    config_print_line(L(setup_language,
                        "[modules] falha persistente. Escolha:",
                        "[modules] persistent failure. Choose:",
                        "[modules] fallo persistente. Elija:"));
    config_print_line(L(setup_language,
                        "  [r] Tentar novamente agora",
                        "  [r] Try again now",
                        "  [r] Reintentar ahora"));
    config_print_line(L(setup_language,
                        "  [b] Continuar e tentar em segundo plano",
                        "  [b] Continue; system retries in background",
                        "  [b] Continuar; el sistema reintenta en segundo plano"));
    config_print_line(L(setup_language,
                        "  [c] Cancelar e ficar so com o nucleo (basico)",
                        "  [c] Cancel and keep core-only (basic)",
                        "  [c] Cancelar y quedarse solo con el nucleo (basico)"));
    config_print_line(L(setup_language,
                        "  Sem escolha em 30s: continuar em segundo plano.",
                        "  No choice in 30s: continue in background.",
                        "  Sin eleccion en 30s: continuar en segundo plano."));
    tty_set_echo(1);
    tty_set_echo_mask('\0');
    while (pit_ticks() < deadline) {
        char ch = 0;
        if (!announced_timeout &&
            pit_ticks() + (10u * MODULES_TICKS_PER_SECOND) >= deadline) {
            config_print_line(L(setup_language,
                                "[modules] aguardando escolha; fallback em 10s...",
                                "[modules] waiting for choice; fallback in 10s...",
                                "[modules] esperando eleccion; fallback en 10s..."));
            announced_timeout = 1;
        }
        if (kernel_input_trygetc(&ch) == 0) {
            modules_sleep_ticks(10u);
            continue;
        }
        if (ch == 'r' || ch == 'R') return 'r';
        if (ch == 'b' || ch == 'B' || ch == '\r' || ch == '\n') return 'b';
        if (ch == 'c' || ch == 'C') return 'c';
        /* loop: ignore other keys */
    }
    config_print_line(L(setup_language,
                        "[modules] sem resposta; continuando em segundo plano.",
                        "[modules] no response; continuing in background.",
                        "[modules] sin respuesta; continuando en segundo plano."));
    return 'b';
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
        size_t selected_count = modules_select_official_packages(
            setup_language, list_in, sizeof(list_in));
        if (selected_count == 0u) {
            config_print_line(L(setup_language,
                                "[modules] nenhum modulo marcado; usando perfil basico.",
                                "[modules] no module selected; using basic profile.",
                                "[modules] ningun modulo marcado; usando perfil basico."));
            profile.kind = INSTALL_PROFILE_BASIC;
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

    /* Run the bootstrap with a robust retry policy so the user is not
     * told "will retry after next boot" the first time DHCP, DNS or
     * TLS is slow. The wizard insists, in order:
     *   1. wait up to 20 s for the network stack to become ready;
     *   2. try the bootstrap up to MODULES_RETRY_MAX times with
     *      exponential backoff (2/4/8 s);
     *   3. if all attempts fail, ask the user whether to keep trying,
     *      defer to the kernel background poll, or cancel.
     * The "absolute install" mode (the user explicitly chose FULL or
     * CUSTOM) keeps looping until success or explicit cancel: that is
     * the difference between "I want the desktop" and "I am happy
     * with just the core". */
    kernel_capypkg_bind_runtime_adapters();

    /* Shared status-bar state for the whole install loop. Registered as
     * the capypkg install observer so byte-level download/verify/install
     * progress feeds the live bar; ALWAYS cleared before returning so the
     * stack-allocated `ui` can never dangle for the kernel's background
     * bootstrap poll (which would otherwise call the observer with a
     * freed stack pointer). */
    struct modules_ui_state ui;
    int step_result = 0;
    memory_zero(&ui, sizeof(ui));
    ui.language = setup_language;
    ui.phase = -1;
    capypkg_set_install_observer(modules_install_observer, &ui);

    if (modules_wait_for_network(MODULES_NETWORK_WAIT_TICKS,
                                 setup_language) == 0) {
        cstring_copy(ui.status, sizeof(ui.status),
                     L(setup_language,
                       "Rede ainda nao pronta; tentando mesmo assim...",
                       "Network not ready yet; trying anyway...",
                       "Red aun no lista; intentando igualmente..."));
        modules_force_render(&ui);
    }

    for (;;) {
        int installed = 0;
        int failed = 0;
        int rc = modules_run_bootstrap_with_retry(&ui, &installed, &failed);
        if (rc == INSTALL_PROFILE_OK && failed > 0) {
            /* Treat a completed sweep with leftover failures as
             * retryable so the outer dialogue offers another attempt. */
            rc = INSTALL_PROFILE_ERR_STORAGE;
        }
        if (rc == INSTALL_PROFILE_OK) {
            cstring_copy(ui.status, sizeof(ui.status),
                         L(setup_language,
                           "Instalacao concluida. Reinicie para ativar.",
                           "Install complete. Reboot to activate.",
                           "Instalacion completa. Reinicie para activar."));
            modules_force_render(&ui);
            step_result = (installed > 0) ? 1 : 0;
            break;
        }
        if (rc == INSTALL_PROFILE_ERR_NOT_READY) {
            cstring_copy(ui.status, sizeof(ui.status),
                         L(setup_language,
                           "capypkg nao iniciou; seguira em segundo plano.",
                           "capypkg not started; will continue in background.",
                           "capypkg no inicio; seguira en segundo plano."));
            modules_force_render(&ui);
            step_result = 0;
            break;
        }
        /* Soft failure (network / repo / index / per-package / marker).
         * profile.ini stays on disk and the kernel poll keeps retrying in
         * the background regardless of the choice here; the dialogue is
         * only about whether to keep the wizard on screen. It uses the
         * scrolling prompt, which intentionally replaces the frame. */
        char choice = modules_ask_retry_choice(setup_language);
        if (choice == 'r') {
            continue;
        }
        if (choice == 'b') {
            config_print_line(L(setup_language,
                                "[modules] ok: o sistema tentara terminar a instalacao em segundo plano.",
                                "[modules] ok: the system will keep retrying the install in the background.",
                                "[modules] ok: el sistema seguira reintentando la instalacion en segundo plano."));
            step_result = 0;
            break;
        }
        /* 'c' (cancel) or unknown: fall back to BASIC. Rewrite the
         * profile so the kernel poll does not keep hammering the
         * network for modules the operator no longer wants. */
        {
            struct install_profile basic;
            install_profile_reset(&basic);
            basic.valid = 1u;
            (void)modules_write_profile_ini(&basic);
            config_sync_root_device();
        }
        config_print_line(L(setup_language,
                            "[modules] instalacao remota cancelada; sistema seguira em modo basico.",
                            "[modules] remote install cancelled; system will stay in basic mode.",
                            "[modules] instalacion remota cancelada; el sistema seguira en modo basico."));
        step_result = 0;
        break;
    }

    capypkg_set_install_observer(NULL, NULL);
    return step_result;
}
