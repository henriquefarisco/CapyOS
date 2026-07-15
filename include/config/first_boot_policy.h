#ifndef CONFIG_FIRST_BOOT_POLICY_H
#define CONFIG_FIRST_BOOT_POLICY_H

int first_boot_setup_required(int marker_exists, int has_users,
                              int config_exists);

#endif
