#ifndef NET_HYPERV_PLATFORM_DIAG_H
#define NET_HYPERV_PLATFORM_DIAG_H

#include "core/system_init.h"
#include "net/hyperv_runtime_gate.h"
#include "net/stack.h"

int net_hyperv_platform_is_blocked(
    const struct system_runtime_platform *platform);
int net_hyperv_platform_blocked(struct system_runtime_platform *platform);
const char *net_hyperv_stage_label(const struct net_stack_status *st);
const char *net_hyperv_bus_label(const struct net_stack_status *st);
const char *net_hyperv_runtime_phase_label(uint8_t phase);
const char *net_hyperv_refresh_action_label(uint8_t action);
const char *net_hyperv_effective_next_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform);
const char *net_hyperv_offer_cache_label(const struct net_stack_status *st);
const char *net_hyperv_block_label(
    const struct net_stack_status *st,
    const struct system_runtime_platform *platform);
const char *net_hyperv_storage_bus_label(void);
const char *net_hyperv_storage_cache_label(void);
const char *net_hyperv_storage_gate_label(
    const struct system_runtime_platform *runtime_platform);
const char *net_hyperv_storage_next_label(
    const struct system_runtime_platform *runtime_platform);
const char *net_hyperv_storage_block_label(
    const struct system_runtime_platform *runtime_platform);

#endif /* NET_HYPERV_PLATFORM_DIAG_H */
