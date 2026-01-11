#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "core/session.h"

int system_detect_first_boot(void);
int system_run_first_boot_setup(void);
/* Versao que recebe senha pre-definida para o admin (usada pelo instalador). */
int system_run_first_boot_setup_with_password(const char *admin_password);
int system_mark_first_boot_complete(void);

struct system_settings {
    char hostname[32];
    char theme[16];
    char keyboard_layout[16];
    int splash_enabled;
};

int system_load_settings(struct system_settings *out);
int system_save_settings(const struct system_settings *settings);
int system_save_keyboard_layout(const char *layout);
int system_login(struct session_context *session, const struct system_settings *settings);
void system_apply_theme(const struct system_settings *settings);
void system_apply_keyboard_layout(const struct system_settings *settings);
void system_show_splash(const struct system_settings *settings);

#endif
