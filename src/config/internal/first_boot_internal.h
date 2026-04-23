#ifndef FIRST_BOOT_INTERNAL_H
#define FIRST_BOOT_INTERNAL_H

#include "config_internal.h"

void config_debug_print_heap(const char *prefix, const char *path);
void config_first_boot_log_reset(void);
void config_build_home_path(const char *username, char *out, size_t out_len);
int config_validate_admin_username(const char *username);
const char *config_validate_theme(const char *input);
int config_verify_directory_exists(const char *path);
void config_log_user_record_state(const struct user_record *rec);

#endif
