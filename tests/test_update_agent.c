#include <stdio.h>
#include <string.h>

#include "core/update_agent.h"

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_CACHE_PATH "/system/update/latest.ini"
#define UPDATE_AGENT_STAGE_PATH "/system/update/staged.ini"
#define UPDATE_AGENT_STATE_PATH "/system/update/state.ini"
#define UPDATE_AGENT_IMPORT_PATH "/tmp/update-import.ini"

struct fake_file {
    const char *path;
    char text[512];
    int present;
};

static struct fake_file g_files[] = {
    {UPDATE_AGENT_REPOSITORY_PATH, "", 0},
    {UPDATE_AGENT_CACHE_PATH, "", 0},
    {UPDATE_AGENT_STAGE_PATH, "", 0},
    {UPDATE_AGENT_STATE_PATH, "", 0},
    {UPDATE_AGENT_IMPORT_PATH, "", 0},
};

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[update_agent] %s\n", msg);
        return 1;
    }
    return 0;
}

static struct fake_file *find_file(const char *path) {
    size_t i = 0;
    if (!path) {
        return NULL;
    }
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
        if (strcmp(g_files[i].path, path) == 0) {
            return &g_files[i];
        }
    }
    return NULL;
}

static void set_file_text(const char *path, const char *text) {
    struct fake_file *file = find_file(path);
    if (!file) {
        return;
    }
    file->present = text ? 1 : 0;
    file->text[0] = '\0';
    if (text) {
        strncpy(file->text, text, sizeof(file->text) - 1u);
        file->text[sizeof(file->text) - 1u] = '\0';
    }
}

static void reset_files(void) {
    size_t i = 0;
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
        g_files[i].present = 0;
        g_files[i].text[0] = '\0';
    }
}

static int stub_read_file(const char *path, char *buffer, size_t buffer_size,
                          size_t *out_len) {
    struct fake_file *file = find_file(path);
    size_t len = 0u;
    size_t i = 0u;

    if (!file || !file->present || !buffer || buffer_size == 0u) {
        return -1;
    }
    len = strlen(file->text);
    if (len + 1u > buffer_size) {
        len = buffer_size - 1u;
    }
    for (i = 0u; i < len; ++i) {
        buffer[i] = file->text[i];
    }
    buffer[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

static int stub_write_file(const char *path, const char *text) {
    if (!path || !text) {
        return -1;
    }
    set_file_text(path, text);
    return 0;
}

static int stub_remove_file(const char *path) {
    struct fake_file *file = find_file(path);
    if (!file) {
        return -1;
    }
    file->present = 0;
    file->text[0] = '\0';
    return 0;
}

int run_update_agent_tests(void) {
    int fails = 0;
    struct system_update_status status;

    reset_files();
    update_agent_reset();
    update_agent_set_reader(stub_read_file);
    update_agent_set_writer(stub_write_file);
    update_agent_set_remover(stub_remove_file);
    update_agent_init("0.8.0-alpha.0+20260305");

    fails += expect_true(update_agent_poll() == 0,
                         "missing catalog cache should be tolerated");
    update_agent_status_get(&status);
    fails += expect_true(status.configured == 1u,
                         "update agent should keep defaults when repo file is absent");
    fails += expect_true(status.catalog_present == 0u,
                         "catalog should be absent without cached manifest");
    fails += expect_true(status.update_available == 0u,
                         "no cached catalog should not signal update");
    fails += expect_true(status.stage_ready == 0u,
                         "missing catalog should not expose a staged update");
    fails += expect_true(status.pending_activation == 0u,
                         "missing catalog should not arm activation");
    fails += expect_true(strcmp(status.channel, "stable") == 0,
                         "default channel mismatch");
    fails += expect_true(strcmp(status.branch, "main") == 0,
                         "default branch mismatch");
    fails += expect_true(strcmp(status.source, "github:henriquefarisco/CapyOS") == 0,
                         "default source mismatch");
    fails += expect_true(strcmp(status.remote_manifest_url,
                                "https://raw.githubusercontent.com/henriquefarisco/CapyOS/main/system/update/latest.ini") == 0,
                         "default remote manifest mismatch");
    fails += expect_true(strcmp(status.summary, "catalog cache missing") == 0,
                         "missing catalog summary mismatch");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=develop\nbranch=develop\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=develop\npublished_at=2026-04-08\n");
    fails += expect_true(update_agent_poll() == 0,
                         "valid catalog should poll successfully");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "valid manifest should mark catalog present");
    fails += expect_true(status.update_available == 1u,
                         "newer cached version should signal update available");
    fails += expect_true(status.stage_ready == 0u,
                         "catalog poll should not auto-stage updates");
    fails += expect_true(strcmp(status.channel, "develop") == 0,
                         "repository channel should override default");
    fails += expect_true(strcmp(status.branch, "develop") == 0,
                         "repository branch should override default");
    fails += expect_true(strcmp(status.remote_manifest_url,
                                "https://raw.githubusercontent.com/test/CapyOS/develop/system/update/latest.ini") == 0,
                         "repository remote manifest mismatch");
    fails += expect_true(strcmp(status.available_version, "0.9.0-alpha.1") == 0,
                         "available version mismatch");
    fails += expect_true(strcmp(status.summary, "update available in local catalog") == 0,
                         "update available summary mismatch");

    fails += expect_true(update_agent_stage_latest() == 0,
                         "staging should succeed when a cached update exists");
    update_agent_status_get(&status);
    fails += expect_true(status.stage_ready == 1u,
                         "staged update should be visible after staging");
    fails += expect_true(status.pending_activation == 0u,
                         "staging should not arm activation automatically");
    fails += expect_true(strcmp(status.staged_version, "0.9.0-alpha.1") == 0,
                         "staged version mismatch");
    fails += expect_true(strcmp(status.summary, "staged update ready") == 0,
                         "staged summary mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_STAGE_PATH)->present == 1,
                         "staged manifest file should exist");
    fails += expect_true(find_file(UPDATE_AGENT_STATE_PATH)->present == 1,
                         "state file should exist after staging");

    fails += expect_true(update_agent_set_pending_activation(1) == 0,
                         "arming a staged update should succeed");
    update_agent_status_get(&status);
    fails += expect_true(status.pending_activation == 1u,
                         "pending activation flag mismatch");
    fails += expect_true(strcmp(status.summary, "staged update armed for activation") == 0,
                         "armed summary mismatch");

    fails += expect_true(update_agent_set_pending_activation(0) == 0,
                         "disarming a staged update should succeed");
    update_agent_status_get(&status);
    fails += expect_true(status.stage_ready == 1u,
                         "disarming should keep staged payload");
    fails += expect_true(status.pending_activation == 0u,
                         "pending activation should clear when disarmed");
    fails += expect_true(strcmp(status.summary, "staged update ready") == 0,
                         "disarmed summary mismatch");

    fails += expect_true(update_agent_clear_stage() == 0,
                         "clearing staged update should succeed");
    update_agent_status_get(&status);
    fails += expect_true(status.stage_ready == 0u,
                         "clear should remove staged status");
    fails += expect_true(status.pending_activation == 0u,
                         "clear should remove pending activation");
    fails += expect_true(find_file(UPDATE_AGENT_STAGE_PATH)->present == 0,
                         "staged manifest should be removed");
    fails += expect_true(find_file(UPDATE_AGENT_STATE_PATH)->present == 0,
                         "state file should be removed");
    fails += expect_true(strcmp(status.summary, "update available in local catalog") == 0,
                         "clear should fall back to catalog summary");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.8.0-alpha.0+20260305\npublished_at=2026-04-08\n");
    fails += expect_true(update_agent_poll() == 0,
                         "matching catalog version should still be healthy");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "catalog should remain present");
    fails += expect_true(status.update_available == 0u,
                         "matching version should not signal update");
    fails += expect_true(update_agent_stage_latest() == -5,
                         "staging should refuse when there is no new cached update");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "no cached update available to stage") == 0,
                         "stage refusal summary mismatch");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH, NULL);
    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.0.0-alpha.1\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\n");
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == 0,
                         "importing a matching external manifest should succeed");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "imported manifest should populate the catalog");
    fails += expect_true(status.update_available == 1u,
                         "imported manifest should expose an update");
    fails += expect_true(strcmp(status.available_version, "1.0.0-alpha.1") == 0,
                         "imported available version mismatch");
    fails += expect_true(strcmp(status.summary, "manifest imported into local catalog") == 0,
                         "import summary mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_CACHE_PATH)->present == 1,
                         "import should persist the catalog cache");

    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.0.0-alpha.2\nchannel=develop\nbranch=develop\nsource=github:test/CapyOS\npublished_at=2026-04-09\n");
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -19,
                         "importing a manifest from another track should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest does not match selected update repository") == 0,
                         "import mismatch summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH, "published_at=2026-04-08\n");
    fails += expect_true(update_agent_poll() == -2,
                         "invalid manifest should degrade update agent");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == -2,
                         "invalid manifest result mismatch");
    fails += expect_true(strcmp(status.summary, "catalog cache invalid") == 0,
                         "invalid manifest summary mismatch");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=develop\npublished_at=2026-04-08\n");
    fails += expect_true(update_agent_poll() == -13,
                         "catalog from a different channel should be rejected");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.channel, "stable") == 0,
                         "selected channel should remain stable on mismatch");
    fails += expect_true(strcmp(status.branch, "main") == 0,
                         "selected branch should remain main on mismatch");
    fails += expect_true(strcmp(status.summary, "catalog cache does not match selected update repository") == 0,
                         "catalog mismatch summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.9.0-alpha.1\npublished_at=2026-04-08\n");
    set_file_text(UPDATE_AGENT_STAGE_PATH, "published_at=2026-04-08\n");
    set_file_text(UPDATE_AGENT_STATE_PATH,
                  "pending_activation=1\nstaged_manifest=/system/update/staged.ini\n");
    fails += expect_true(update_agent_poll() == -3,
                         "invalid staged manifest should degrade update agent");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == -3,
                         "invalid staged manifest result mismatch");
    fails += expect_true(strcmp(status.summary, "staged update invalid") == 0,
                         "invalid staged summary mismatch");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=stable\npublished_at=2026-04-08\n");
    set_file_text(UPDATE_AGENT_STAGE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=develop\npublished_at=2026-04-08\n");
    set_file_text(UPDATE_AGENT_STATE_PATH,
                  "pending_activation=0\nstaged_manifest=/system/update/staged.ini\n");
    fails += expect_true(update_agent_poll() == -14,
                         "staged update from a different channel should be rejected");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "staged update does not match selected update repository") == 0,
                         "staged mismatch summary mismatch");

    update_agent_reset();
    if (fails == 0) {
        printf("[tests] update_agent OK\n");
    }
    return fails;
}
