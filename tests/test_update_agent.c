#include <stdio.h>
#include <string.h>

#include "core/update_agent.h"

static const char *g_repo_text = NULL;
static const char *g_manifest_text = NULL;

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[update_agent] %s\n", msg);
        return 1;
    }
    return 0;
}

static int stub_read_file(const char *path, char *buffer, size_t buffer_size,
                          size_t *out_len) {
    const char *text = NULL;
    size_t len = 0;
    size_t i = 0;

    if (!path || !buffer || buffer_size == 0u) {
        return -1;
    }
    if (strcmp(path, "/system/update/repository.ini") == 0) {
        text = g_repo_text;
    } else if (strcmp(path, "/system/update/cache/latest.ini") == 0) {
        text = g_manifest_text;
    } else {
        return -1;
    }
    if (!text) {
        return -1;
    }
    len = strlen(text);
    if (len + 1u > buffer_size) {
        len = buffer_size - 1u;
    }
    for (i = 0; i < len; ++i) {
        buffer[i] = text[i];
    }
    buffer[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

int run_update_agent_tests(void) {
    int fails = 0;
    struct system_update_status status;

    update_agent_reset();
    update_agent_set_reader(stub_read_file);
    update_agent_init("0.8.0-alpha.0+20260305");

    g_repo_text = NULL;
    g_manifest_text = NULL;
    fails += expect_true(update_agent_poll() == 0,
                         "missing catalog cache should be tolerated");
    update_agent_status_get(&status);
    fails += expect_true(status.configured == 1u,
                         "update agent should keep defaults when repo file is absent");
    fails += expect_true(status.catalog_present == 0u,
                         "catalog should be absent without cached manifest");
    fails += expect_true(status.update_available == 0u,
                         "no cached catalog should not signal update");
    fails += expect_true(strcmp(status.channel, "stable") == 0,
                         "default channel mismatch");
    fails += expect_true(strcmp(status.source, "github:henriquefarisco/CapyOS") == 0,
                         "default source mismatch");
    fails += expect_true(strcmp(status.summary, "catalog cache missing") == 0,
                         "missing catalog summary mismatch");

    g_repo_text = "channel=beta\nsource=github:test/CapyOS\n";
    g_manifest_text =
        "available_version=0.9.0-alpha.1\nchannel=beta\npublished_at=2026-04-08\n";
    fails += expect_true(update_agent_poll() == 0,
                         "valid catalog should poll successfully");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "valid manifest should mark catalog present");
    fails += expect_true(status.update_available == 1u,
                         "newer cached version should signal update available");
    fails += expect_true(strcmp(status.channel, "beta") == 0,
                         "repository channel should override default");
    fails += expect_true(strcmp(status.available_version, "0.9.0-alpha.1") == 0,
                         "available version mismatch");
    fails += expect_true(strcmp(status.summary, "update available in local catalog") == 0,
                         "update available summary mismatch");

    g_manifest_text =
        "available_version=0.8.0-alpha.0+20260305\npublished_at=2026-04-08\n";
    fails += expect_true(update_agent_poll() == 0,
                         "matching catalog version should still be healthy");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "catalog should remain present");
    fails += expect_true(status.update_available == 0u,
                         "matching version should not signal update");
    fails += expect_true(strcmp(status.summary, "system already matches cached catalog") == 0,
                         "up-to-date summary mismatch");

    g_manifest_text = "published_at=2026-04-08\n";
    fails += expect_true(update_agent_poll() == -2,
                         "invalid manifest should degrade update agent");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == -2,
                         "invalid manifest result mismatch");
    fails += expect_true(strcmp(status.summary, "catalog cache invalid") == 0,
                         "invalid manifest summary mismatch");

    update_agent_reset();
    if (fails == 0) {
        printf("[tests] update_agent OK\n");
    }
    return fails;
}
