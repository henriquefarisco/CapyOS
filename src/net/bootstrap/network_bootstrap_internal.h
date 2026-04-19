#ifndef CORE_NETWORK_BOOTSTRAP_INTERNAL_H
#define CORE_NETWORK_BOOTSTRAP_INTERNAL_H

#include "net/network_bootstrap.h"

struct net_stack_status;
struct system_runtime_platform;
struct system_settings;

int network_bootstrap_io_ready(const struct network_bootstrap_io *io);
void network_bootstrap_apply_settings(const struct network_bootstrap_io *io,
                                      const struct system_settings *settings);
void network_bootstrap_print_status(
    const struct network_bootstrap_io *io,
    const struct system_runtime_platform *runtime_platform,
    const struct net_stack_status *net_status);
void network_bootstrap_print_selftest(const struct network_bootstrap_io *io,
                                      int ok);

#endif /* CORE_NETWORK_BOOTSTRAP_INTERNAL_H */
