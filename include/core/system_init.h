#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "core/session.h"

#include <stdint.h>

int system_detect_first_boot(void);
int system_run_first_boot_setup(void);
/* Versao que recebe senha pre-definida para o admin (usada pelo instalador). */
int system_run_first_boot_setup_with_password(const char *admin_password);
int system_mark_first_boot_complete(void);

struct system_settings {
    char hostname[32];
    char theme[16];
    char keyboard_layout[16];
    char language[16];
    char update_channel[16];
    char network_mode[8];
    char service_target[16];
    uint32_t ipv4_addr;
    uint32_t ipv4_mask;
    uint32_t ipv4_gateway;
    uint32_t ipv4_dns;
    int splash_enabled;
    int diagnostics_enabled; /* 0 = skip CLI self-tests/diagnostics (default) */
};

struct system_runtime_platform {
    int boot_services_active;
    int firmware_input_active;
    int firmware_block_io_active;
    int hybrid_boot;
    int native_input_ready;
    int native_storage_ready;
    int synthetic_storage_ready;
    uint8_t hyperv_vmbus_stage;
    uint8_t hyperv_input_gate;
    uint8_t exit_boot_services_gate;
    uint64_t exit_boot_services_status;
};

enum system_exit_boot_services_gate {
    SYSTEM_EXIT_BOOT_SERVICES_GATE_UNKNOWN = 0,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_NATIVE,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_READY,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_CONTRACT,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_DEVICE,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE,
    SYSTEM_EXIT_BOOT_SERVICES_GATE_FAILED,
};

enum system_hyperv_input_gate {
    SYSTEM_HYPERV_INPUT_GATE_UNKNOWN = 0,
    SYSTEM_HYPERV_INPUT_GATE_OFF,
    SYSTEM_HYPERV_INPUT_GATE_ACTIVE,
    SYSTEM_HYPERV_INPUT_GATE_WAIT_BOOT_SERVICES,
    SYSTEM_HYPERV_INPUT_GATE_PREPARED,
    SYSTEM_HYPERV_INPUT_GATE_READY,
    SYSTEM_HYPERV_INPUT_GATE_RETRY,
    SYSTEM_HYPERV_INPUT_GATE_FAILED,
};

void system_set_boot_defaults(const char *keyboard_layout, const char *language);
int system_load_settings(struct system_settings *out);
int system_save_settings(const struct system_settings *settings);
int system_save_keyboard_layout(const char *layout);
int system_save_theme(const char *theme);
int system_save_splash_enabled(int enabled);
int system_save_network_ipv4(uint32_t addr, uint32_t mask, uint32_t gateway,
                             uint32_t dns);
int system_save_network_mode(const char *mode);
int system_save_service_target(const char *target);
int system_save_update_channel(const char *channel);
int system_prepare_update_catalog(void);
int system_login(struct session_context *session, const struct system_settings *settings);
void system_apply_theme(const struct system_settings *settings);
void system_apply_keyboard_layout(const struct system_settings *settings);
void system_show_splash(const struct system_settings *settings);
void system_runtime_platform_set(const struct system_runtime_platform *status);
void system_runtime_platform_get(struct system_runtime_platform *out);
const char *system_exit_boot_services_gate_label(uint8_t gate);
const char *system_hyperv_input_gate_label(uint8_t gate);

#endif
