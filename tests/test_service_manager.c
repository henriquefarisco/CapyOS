#include <stdio.h>
#include <string.h>

#include "core/service_manager.h"

static int g_poll_hits = 0;

static int test_poll_cb(void *ctx) {
    int *value = (int *)ctx;
    g_poll_hits++;
    if (value) {
        (*value)++;
    }
    return 0;
}

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[service_manager] %s\n", msg);
        return 1;
    }
    return 0;
}

int run_service_manager_tests(void) {
    int fails = 0;
    int poll_ctx = 0;
    int blocked_ctx = 0;
    struct system_service_status svc;

    g_poll_hits = 0;
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

    fails += expect_true(service_manager_set_poll(
                             SYSTEM_SERVICE_NETWORKD,
                             test_poll_cb, &poll_ctx) == 0,
                         "networkd poll callback registration failed");
    fails += expect_true(service_manager_set_poll_interval(
                             SYSTEM_SERVICE_NETWORKD, 5u) == 0,
                         "networkd poll interval should be configurable");
    fails += expect_true(service_manager_set_poll(
                             SYSTEM_SERVICE_UPDATE_AGENT,
                             test_poll_cb, &blocked_ctx) == 0,
                         "blocked service poll callback registration failed");
    fails += expect_true(service_manager_poll_due(0u) == 1,
                         "service manager should poll one due service");
    fails += expect_true(g_poll_hits == 1 && poll_ctx == 1 && blocked_ctx == 0,
                         "only active due service should be polled");
    fails += expect_true(service_manager_poll_due(3u) == 0,
                         "service should not repoll before interval");
    fails += expect_true(service_manager_poll_due(5u) == 1,
                         "service should repoll when interval expires");
    fails += expect_true(g_poll_hits == 2 && poll_ctx == 2 && blocked_ctx == 0,
                         "service poll cadence mismatch");
    fails += expect_true(service_manager_poll_once() == 1,
                         "manual poll should still reach active registered service");
    fails += expect_true(g_poll_hits == 3 && poll_ctx == 3 && blocked_ctx == 0,
                         "manual poll should ignore blocked services");
    fails += expect_true(service_manager_get(SYSTEM_SERVICE_NETWORKD, &svc) == 0,
                         "networkd service should remain readable after poll");
    fails += expect_true(svc.polls == 3,
                         "networkd poll counter should increase");
    fails += expect_true(svc.poll_interval_ticks == 5u,
                         "networkd poll interval should be reported");

    if (fails == 0) {
        printf("[tests] service_manager OK\n");
    }
    return fails;
}
