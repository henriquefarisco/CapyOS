#include <stdio.h>
#include <string.h>

#include "core/service_manager.h"

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[service_manager] %s\n", msg);
        return 1;
    }
    return 0;
}

int run_service_manager_tests(void) {
    int fails = 0;
    struct system_service_status svc;

    service_manager_reset();
    service_manager_bootstrap_defaults();

    fails += expect_true(service_manager_count() == SYSTEM_SERVICE_COUNT,
                         "unexpected builtin service count");
    fails += expect_true(service_manager_get(SYSTEM_SERVICE_LOGGER, &svc) == 0,
                         "logger service should exist");
    fails += expect_true(strcmp(svc.name, "logger") == 0,
                         "logger service name mismatch");
    fails += expect_true(svc.state == SYSTEM_SERVICE_STATE_STARTING,
                         "logger should start in starting state");

    fails += expect_true(service_manager_set_state(
                             SYSTEM_SERVICE_LOGGER,
                             SYSTEM_SERVICE_STATE_READY, 0,
                             "persistent klog active") == 0,
                         "logger state update failed");
    fails += expect_true(service_manager_get(SYSTEM_SERVICE_LOGGER, &svc) == 0,
                         "logger should still be readable after update");
    fails += expect_true(svc.state == SYSTEM_SERVICE_STATE_READY,
                         "logger should be ready after update");
    fails += expect_true(strcmp(svc.summary, "persistent klog active") == 0,
                         "logger summary should be updated");
    fails += expect_true(svc.transitions >= 2,
                         "logger transitions should increase after update");

    fails += expect_true(service_manager_get_at(1, &svc) == 0,
                         "service lookup by index should work");
    fails += expect_true(strcmp(service_manager_state_label(
                                    SYSTEM_SERVICE_STATE_BLOCKED),
                                "blocked") == 0,
                         "blocked label mismatch");
    fails += expect_true(strcmp(service_manager_startup_label(
                                    SYSTEM_SERVICE_STARTUP_BOOT),
                                "boot") == 0,
                         "startup boot label mismatch");

    if (fails == 0) {
        printf("[tests] service_manager OK\n");
    }
    return fails;
}
