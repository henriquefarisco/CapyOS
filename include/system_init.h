#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "session.h"

int system_detect_first_boot(void);
int system_run_first_boot_setup(void);
int system_mark_first_boot_complete(void);

struct system_settings {
    char hostname[32];
    char theme[16];
    int splash_enabled;
};

int system_load_settings(struct system_settings *out);
int system_login(struct session_context *session, const struct system_settings *settings);
void system_apply_theme(const struct system_settings *settings);
void system_show_splash(const struct system_settings *settings);

#endif
