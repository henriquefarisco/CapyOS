#ifndef FIRST_BOOT_INTERNAL_H
#define FIRST_BOOT_INTERNAL_H

#include "config_internal.h"
/* alpha.241: first-boot wizard calls acpi_reboot() after a successful
 * module-bootstrap sweep so the activation gate can pick up the
 * freshly installed desktop session on the next boot. The include
 * lives here (and not in config_internal.h) because the rest of the
 * config/ module does not need the ACPI surface. */
#include "drivers/acpi/acpi.h"

void config_debug_print_heap(const char *prefix, const char *path);
void config_first_boot_log_reset(void);
void config_build_home_path(const char *username, char *out, size_t out_len);
int config_validate_admin_username(const char *username);
const char *config_validate_theme(const char *input);
int config_verify_directory_exists(const char *path);
void config_log_user_record_state(const struct user_record *rec);

/* alpha.241: module selection step of the first-boot wizard.
 *
 * Runs interactively after the admin user is provisioned and before
 * the wizard writes its completion marker. Asks the operator which
 * module set to install (BASIC | FULL | CUSTOM), writes the answers
 * to /system/install/profile.ini, then drives the in-tree capypkg
 * adapter via capypkg_bootstrap_run_with_progress to download and
 * stage the selected modules with visible progress.
 *
 * Return contract:
 *   0  = continue normally (BASIC profile, or soft network failure;
 *        no immediate state change requires a reboot).
 *   1  = modules installed successfully — caller should reboot so
 *        the activation gate picks them up on next boot.
 *  <0  = hard error (could not write profile.ini, etc).
 *
 * Soft errors (network down, repo unreachable) do NOT abort the
 * wizard: the profile.ini stays written so the kernel auto-bootstrap
 * can complete the install in background after the next boot. */
int first_boot_module_selection_step(const char *setup_language);

#endif
