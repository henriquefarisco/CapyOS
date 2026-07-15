#include <stdio.h>
#include <string.h>

#include "services/update_agent.h"

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_CACHE_PATH "/system/update/latest.ini"
#define UPDATE_AGENT_STAGE_PATH "/system/update/staged.ini"
#define UPDATE_AGENT_STATE_PATH "/system/update/state.ini"
#define UPDATE_AGENT_IMPORT_PATH "/tmp/update-import.ini"
#define UPDATE_AGENT_FETCHED_PATH "/system/update/fetched.ini"
#define UPDATE_AGENT_PAYLOAD_CACHE_PATH "/system/update/payload.bin"
#define UPDATE_AGENT_GOOD_SHA256 \
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
#define UPDATE_AGENT_ABC_SHA256 \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define UPDATE_AGENT_ABC_SIZE_LINE "payload_size=3\n"
#define UPDATE_AGENT_GOOD_SIGNATURE \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define UPDATE_AGENT_BAD_SIGNATURE \
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" \
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define UPDATE_AGENT_PAYLOAD_URL_LINE \
    "payload_url=https://github.com/test/CapyOS/releases/download/v1.0.0/kernel.bin\n"
#define UPDATE_AGENT_SIGNATURE_LINE \
    "signature_ed25519=" UPDATE_AGENT_GOOD_SIGNATURE "\n"

struct fake_file {
    const char *path;
    char text[768];
    int present;
    size_t len;
};

static struct fake_file g_files[] = {
    {UPDATE_AGENT_REPOSITORY_PATH, "", 0, 0u},
    {UPDATE_AGENT_CACHE_PATH, "", 0, 0u},
    {UPDATE_AGENT_STAGE_PATH, "", 0, 0u},
    {UPDATE_AGENT_STATE_PATH, "", 0, 0u},
    {UPDATE_AGENT_IMPORT_PATH, "", 0, 0u},
    {UPDATE_AGENT_FETCHED_PATH, "", 0, 0u},
    {UPDATE_AGENT_PAYLOAD_CACHE_PATH, "", 0, 0u},
};

static const char *g_fetch_text;
static int g_fetch_rc;
static char g_last_fetch_url[192];
static const uint8_t *g_payload_bytes;
static size_t g_payload_len;
static size_t g_last_payload_buffer_size;
static int g_payload_rc;
static int g_corrupt_payload_write;
static char g_last_payload_url[192];

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
    file->len = 0u;
    if (text) {
        strncpy(file->text, text, sizeof(file->text) - 1u);
        file->text[sizeof(file->text) - 1u] = '\0';
        file->len = strlen(file->text);
    }
}

static void reset_files(void) {
    size_t i = 0;
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
        g_files[i].present = 0;
        g_files[i].text[0] = '\0';
        g_files[i].len = 0u;
    }
    g_fetch_text = NULL;
    g_fetch_rc = -1;
    g_last_fetch_url[0] = '\0';
    g_payload_bytes = NULL;
    g_payload_len = 0u;
    g_last_payload_buffer_size = 0u;
    g_payload_rc = -1;
    g_corrupt_payload_write = 0;
    g_last_payload_url[0] = '\0';
}

static int stub_read_file(const char *path, char *buffer, size_t buffer_size,
                          size_t *out_len) {
    struct fake_file *file = find_file(path);
    size_t len = 0u;
    size_t i = 0u;

    if (!file || !file->present || !buffer || buffer_size == 0u) {
        return -1;
    }
    len = file->len;
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

static int stub_read_bytes(const char *path, uint8_t *buffer,
                           size_t buffer_size, size_t *out_len) {
    struct fake_file *file = find_file(path);
    size_t i = 0u;
    if (!file || !file->present || !buffer || file->len > buffer_size) {
        return -1;
    }
    while (i < file->len) {
        buffer[i] = (uint8_t)file->text[i];
        ++i;
    }
    if (out_len) {
        *out_len = file->len;
    }
    return 0;
}

static int stub_write_bytes(const char *path, const uint8_t *data, size_t len) {
    struct fake_file *file = find_file(path);
    size_t i = 0u;
    if (!file || (!data && len > 0u) || len >= sizeof(file->text)) {
        return -1;
    }
    file->present = 1;
    while (i < len) {
        file->text[i] = (char)data[i];
        ++i;
    }
    if (g_corrupt_payload_write && len > 0u) {
        file->text[0] ^= 1;
    }
    file->text[len] = '\0';
    file->len = len;
    return 0;
}

static int stub_remove_file(const char *path) {
    struct fake_file *file = find_file(path);
    if (!file) {
        return -1;
    }
    file->present = 0;
    file->text[0] = '\0';
    file->len = 0u;
    return 0;
}

static int stub_manifest_verify(const char *signed_text, size_t signed_len,
                                const char *signature_hex) {
    return signed_text && signed_len > 0u && signature_hex &&
           strstr(signed_text, "signature_ed25519=") == NULL &&
           strcmp(signature_hex, UPDATE_AGENT_GOOD_SIGNATURE) == 0;
}

static int stub_fetch_manifest(const char *url, char *buffer, size_t buffer_size,
                               size_t *out_len) {
    size_t len = 0u;
    if (url) {
        strncpy(g_last_fetch_url, url, sizeof(g_last_fetch_url) - 1u);
        g_last_fetch_url[sizeof(g_last_fetch_url) - 1u] = '\0';
    }
    if (g_fetch_rc != 0 || !g_fetch_text || !buffer || buffer_size == 0u) {
        return g_fetch_rc ? g_fetch_rc : -1;
    }
    len = strlen(g_fetch_text);
    if (len + 1u > buffer_size) {
        return -2;
    }
    strncpy(buffer, g_fetch_text, buffer_size - 1u);
    buffer[buffer_size - 1u] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

static int stub_fetch_payload(const char *url, uint8_t *buffer,
                              size_t buffer_size, size_t *out_len) {
    size_t i = 0u;
    g_last_payload_buffer_size = buffer_size;
    if (url) {
        strncpy(g_last_payload_url, url, sizeof(g_last_payload_url) - 1u);
        g_last_payload_url[sizeof(g_last_payload_url) - 1u] = '\0';
    }
    if (g_payload_rc != 0 || !g_payload_bytes || !buffer ||
        g_payload_len > buffer_size) {
        return g_payload_rc ? g_payload_rc : -1;
    }
    while (i < g_payload_len) {
        buffer[i] = g_payload_bytes[i];
        ++i;
    }
    if (out_len) {
        *out_len = g_payload_len;
    }
    return 0;
}

int run_update_agent_tests(void) {
    int fails = 0;
    struct system_update_status status;
    struct update_prepare_explain explain;

    reset_files();
    update_agent_reset();
    update_agent_set_reader(stub_read_file);
    update_agent_set_bytes_reader(stub_read_bytes);
    update_agent_set_writer(stub_write_file);
    update_agent_set_bytes_writer(stub_write_bytes);
    update_agent_set_remover(stub_remove_file);
    update_agent_set_manifest_verifier(stub_manifest_verify);
    update_agent_set_manifest_fetcher(stub_fetch_manifest);
    update_agent_set_payload_fetcher(stub_fetch_payload);
    update_agent_init(NULL);
    set_file_text(
        UPDATE_AGENT_CACHE_PATH,
        "available_version=0.8.0-alpha.1\nchannel=stable\nbranch=main\n"
        "source=github:henriquefarisco/CapyOS\npublished_at=2026-04-08\n"
        "payload_sha256=" UPDATE_AGENT_GOOD_SHA256 "\n"
        UPDATE_AGENT_PAYLOAD_URL_LINE
        UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_poll() == -22,
                         "unknown current version must fail closed");
    update_agent_status_get(&status);
    fails += expect_true(status.update_available == 0u,
                         "unknown current version must not expose an update");
    set_file_text(UPDATE_AGENT_CACHE_PATH, NULL);
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
                                "https://github.com/henriquefarisco/CapyOS/releases/latest/download/latest.ini") == 0,
                         "default stable latest-release manifest mismatch");
    fails += expect_true(strcmp(status.summary, "catalog cache missing") == 0,
                         "missing catalog summary mismatch");

    set_file_text(
        UPDATE_AGENT_REPOSITORY_PATH,
        "channel=stable\nbranch=main\nsource=github:henriquefarisco/CapyOS\n"
        "manifest=/system/update/latest.ini\n"
        "remote_manifest=https://raw.githubusercontent.com/henriquefarisco/CapyOS/refs/tags/v0.8.0/system/update/latest.ini\n");
    fails += expect_true(update_agent_poll() == 0,
                         "official legacy stable URL should migrate safely");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.remote_manifest_url,
                                "https://github.com/henriquefarisco/CapyOS/releases/latest/download/latest.ini") == 0,
                         "legacy official URL should resolve to latest release");
    fails += expect_true(strstr(find_file(UPDATE_AGENT_REPOSITORY_PATH)->text,
                                "remote_manifest=https://github.com/henriquefarisco/CapyOS/releases/latest/download/latest.ini") != NULL,
                         "legacy official URL migration should persist");

    set_file_text(
        UPDATE_AGENT_REPOSITORY_PATH,
        "channel=stable\nbranch=main\nsource=github:henriquefarisco/CapyOS\n"
        "remote_manifest=https://updates.example.test/capyos/custom.ini\n");
    fails += expect_true(update_agent_poll() == 0,
                         "custom remote URL should remain usable");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.remote_manifest_url,
                                "https://updates.example.test/capyos/custom.ini") == 0,
                         "custom remote URL must not be migrated");
    fails += expect_true(strstr(find_file(UPDATE_AGENT_REPOSITORY_PATH)->text,
                                "remote_manifest=https://updates.example.test/capyos/custom.ini") != NULL,
                         "custom repository file must remain unchanged");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=develop\nbranch=develop\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=develop\npublished_at=2026-04-08\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
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
                                "https://raw.githubusercontent.com/test/CapyOS/refs/heads/develop/system/update/latest.ini") == 0,
                         "repository develop remote manifest mismatch");
    fails += expect_true(strcmp(status.available_version, "0.9.0-alpha.1") == 0,
                         "available version mismatch");
    fails += expect_true(strcmp(status.payload_url,
                                "https://github.com/test/CapyOS/releases/download/v1.0.0/kernel.bin") == 0,
                         "available payload url mismatch");
    fails += expect_true(strcmp(status.summary, "update available in local catalog") == 0,
                         "update available summary mismatch");

    set_file_text(UPDATE_AGENT_STATE_PATH,
                  "pending_activation=0\nstaged_manifest=/system/update/staged.ini\n"
                  "payload_cache=/system/update/payload.bin\npayload_cache_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n");
    fails += expect_true(update_agent_stage_latest() == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "staging must fail closed until persistent slots exist");
    update_agent_status_get(&status);
    fails += expect_true(status.stage_ready == 0u,
                         "unsupported staging must not create staged state");
    fails += expect_true(status.pending_activation == 0u,
                         "unsupported staging must not arm activation");
    fails += expect_true(strcmp(status.summary,
                                "persistent update apply unsupported; verified download only") == 0,
                         "unsupported staging summary mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_STAGE_PATH)->present == 0,
                         "unsupported staging must not write a staged manifest");
    fails += expect_true(find_file(UPDATE_AGENT_STATE_PATH)->present == 1,
                         "pre-existing verified cache state should remain intact");

    set_file_text(UPDATE_AGENT_STAGE_PATH,
                  find_file(UPDATE_AGENT_CACHE_PATH)->text);
    fails += expect_true(update_agent_poll() == 0,
                         "manually seeded legacy stage should remain inspectable");
    fails += expect_true(update_agent_set_pending_activation(1) ==
                             UPDATE_AGENT_ERR_UNSUPPORTED,
                         "arming must fail closed until persistent slots exist");
    update_agent_status_get(&status);
    fails += expect_true(status.pending_activation == 0u,
                         "unsupported arm must not set pending activation");
    fails += expect_true(strcmp(status.summary,
                                "persistent update apply unsupported; verified download only") == 0,
                         "unsupported arm summary mismatch");

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
    fails += expect_true(update_agent_stage_latest() == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "staging remains unsupported without a payload cache");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary,
                                "persistent update apply unsupported; verified download only") == 0,
                         "unsupported stage summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.8.0-alpha.0+20260305\npublished_at=2026-04-08\n");
    fails += expect_true(update_agent_poll() == 0,
                         "matching catalog version should still be healthy");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "catalog should remain present");
    fails += expect_true(status.update_available == 0u,
                         "matching version should not signal update");
    fails += expect_true(update_agent_stage_latest() == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "staging remains unsupported without a newer catalog");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary,
                                "persistent update apply unsupported; verified download only") == 0,
                         "unsupported stage refusal summary mismatch");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH, NULL);
    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.0.0-alpha.1\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == 0,
                         "importing a matching external manifest should succeed");
    update_agent_status_get(&status);
    fails += expect_true(status.catalog_present == 1u,
                         "imported manifest should populate the catalog");
    fails += expect_true(status.update_available == 1u,
                         "imported manifest should expose an update");
    fails += expect_true(strcmp(status.available_version, "1.0.0-alpha.1") == 0,
                         "imported available version mismatch");
    fails += expect_true(strcmp(status.payload_url,
                                "https://github.com/test/CapyOS/releases/download/v1.0.0/kernel.bin") == 0,
                         "imported payload url mismatch");
    fails += expect_true(strcmp(status.summary, "manifest imported into local catalog") == 0,
                         "import summary mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_CACHE_PATH)->present == 1,
                         "import should persist the catalog cache");

    g_fetch_rc = 0;
    g_fetch_text =
        "available_version=1.0.0-alpha.2\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\n"
        UPDATE_AGENT_ABC_SIZE_LINE
        "payload_sha256=" UPDATE_AGENT_ABC_SHA256 "\n"
        UPDATE_AGENT_PAYLOAD_URL_LINE
        UPDATE_AGENT_SIGNATURE_LINE;
    fails += expect_true(update_agent_fetch_remote_manifest() == 0,
                         "fetching a valid remote manifest should import it");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.available_version, "1.0.0-alpha.2") == 0,
                         "fetched available version mismatch");
    fails += expect_true(strcmp(status.payload_url,
                                "https://github.com/test/CapyOS/releases/download/v1.0.0/kernel.bin") == 0,
                         "fetched payload url mismatch");
    fails += expect_true(strcmp(status.summary, "remote manifest fetched into local catalog") == 0,
                         "fetch success summary mismatch");
    fails += expect_true(strcmp(g_last_fetch_url,
                                "https://github.com/test/CapyOS/releases/latest/download/latest.ini") == 0,
                         "fetch should use latest-release manifest URL");
    fails += expect_true(find_file(UPDATE_AGENT_FETCHED_PATH)->present == 0,
                         "fetched temporary manifest should be removed after import");

    set_file_text(UPDATE_AGENT_PAYLOAD_CACHE_PATH, "abc");
    set_file_text(UPDATE_AGENT_STATE_PATH,
                  "pending_activation=0\nstaged_manifest=/system/update/staged.ini\n"
                  "payload_cache=/system/update/payload.bin\npayload_cache_sha256="
                  UPDATE_AGENT_ABC_SHA256 "\n");
    g_last_fetch_url[0] = '\0';
    g_last_payload_url[0] = '\0';
    fails += expect_true(update_agent_prepare_dry_run() == 0,
                         "prepare dry-run should validate cached catalog without side effects");
    update_agent_status_get(&status);
    fails += expect_true(status.stage_ready == 0u,
                         "prepare dry-run should not create staged update");
    fails += expect_true(status.pending_activation == 0u,
                         "prepare dry-run should not arm activation");
    fails += expect_true(strcmp(status.available_version, "1.0.0-alpha.2") == 0,
                         "prepare dry-run available version mismatch");
    fails += expect_true(strcmp(status.summary,
                                "dry-run passed; verified download ready, apply unsupported") == 0,
                         "prepare dry-run summary mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_STAGE_PATH)->present == 0,
                         "prepare dry-run should not persist staged manifest");
    fails += expect_true(g_last_fetch_url[0] == '\0',
                         "prepare dry-run should not fetch remote manifest");
    fails += expect_true(g_last_payload_url[0] == '\0',
                         "prepare dry-run should not download payload");
    fails += expect_true(update_agent_prepare_explain(&explain) ==
                             UPDATE_AGENT_ERR_UNSUPPORTED,
                         "prepare explain should expose missing persistent apply");
    fails += expect_true(explain.poll_ready == 1u && explain.catalog_ready == 1u &&
                             explain.repository_ready == 1u && explain.version_ready == 1u,
                         "prepare explain primary gates should pass");
    fails += expect_true(explain.payload_sha256_ready == 1u &&
                             explain.payload_url_ready == 1u &&
                             explain.signature_ready == 1u && explain.cache_ready == 1u,
                         "prepare explain payload gates should pass");
    fails += expect_true(explain.stage_safe == 0u,
                         "prepare explain must not advertise persistent staging");
    fails += expect_true(strcmp(explain.failing_gate, "persistence") == 0,
                         "prepare explain persistence gate mismatch");
    set_file_text(UPDATE_AGENT_PAYLOAD_CACHE_PATH, NULL);
    fails += expect_true(update_agent_prepare_dry_run() == -53,
                         "prepare dry-run should reject state without cached payload bytes");
    fails += expect_true(update_agent_prepare_explain(&explain) == -53,
                         "prepare explain should reject state without cached payload bytes");
    fails += expect_true(explain.cache_ready == 0u &&
                             strcmp(explain.failing_gate, "cache") == 0,
                         "missing cached bytes should fail the cache gate");
    set_file_text(UPDATE_AGENT_PAYLOAD_CACHE_PATH, "abc");
    set_file_text(UPDATE_AGENT_STATE_PATH, NULL);
    fails += expect_true(update_agent_prepare_dry_run() == -53,
                         "prepare dry-run should require verified payload cache");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "prepare dry-run requires verified payload cache") == 0,
                         "prepare dry-run missing cache summary mismatch");
    fails += expect_true(update_agent_prepare_explain(&explain) == -53,
                         "prepare explain should identify missing verified cache");
    fails += expect_true(explain.cache_ready == 0u && explain.stage_safe == 0u,
                         "prepare explain missing cache gates mismatch");
    fails += expect_true(strcmp(explain.failing_gate, "cache") == 0,
                         "prepare explain missing cache gate mismatch");

    g_fetch_rc = 0;
    g_fetch_text =
        "available_version=1.0.0-alpha.3\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\npayload_sha256="
        UPDATE_AGENT_ABC_SHA256 "\n"
        UPDATE_AGENT_PAYLOAD_URL_LINE
        UPDATE_AGENT_SIGNATURE_LINE;
    g_payload_rc = 0;
    g_payload_bytes = (const uint8_t *)"abc";
    g_payload_len = 3u;
    fails += expect_true(update_agent_prepare_staged_update() ==
                             UPDATE_AGENT_ERR_UNSUPPORTED,
                         "mutating prepare must fail closed without persistent slots");
    update_agent_status_get(&status);
    fails += expect_true(status.stage_ready == 0u,
                         "unsupported prepare must not leave staged state");
    fails += expect_true(status.pending_activation == 0u,
                         "unsupported prepare must not arm activation");
    fails += expect_true(strcmp(status.summary,
                                "persistent update apply unsupported; verified download only") == 0,
                         "unsupported prepare summary mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_STAGE_PATH)->present == 0,
                         "unsupported prepare must not persist staged manifest");
    fails += expect_true(find_file(UPDATE_AGENT_STATE_PATH)->present == 0,
                         "unsupported prepare must not persist activation state");
    fails += expect_true(g_last_fetch_url[0] == '\0' &&
                             g_last_payload_url[0] == '\0',
                         "unsupported prepare must not perform network side effects");
    fails += expect_true(find_file(UPDATE_AGENT_FETCHED_PATH)->present == 0,
                         "prepare should remove fetched temporary manifest");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.0.0-alpha.4\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\n"
                  UPDATE_AGENT_ABC_SIZE_LINE
                  "payload_sha256=" UPDATE_AGENT_ABC_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    g_payload_rc = 0;
    g_payload_bytes = (const uint8_t *)"abc";
    g_payload_len = 3u;
    fails += expect_true(update_agent_download_payload() == 0,
                         "valid payload download should persist verified cache");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(g_last_payload_url,
                                "https://github.com/test/CapyOS/releases/download/v1.0.0/kernel.bin") == 0,
                         "payload fetch should use manifest payload URL");
    fails += expect_true(g_last_payload_buffer_size == 3u,
                         "signed payload size should bound the download buffer");
    fails += expect_true(strcmp(status.payload_cache_path, UPDATE_AGENT_PAYLOAD_CACHE_PATH) == 0,
                         "payload cache path mismatch");
    fails += expect_true(strcmp(status.payload_cache_sha256, UPDATE_AGENT_ABC_SHA256) == 0,
                         "payload cache sha256 mismatch");
    fails += expect_true(find_file(UPDATE_AGENT_PAYLOAD_CACHE_PATH)->present == 1,
                         "payload cache file should be present");
    fails += expect_true(strcmp(find_file(UPDATE_AGENT_PAYLOAD_CACHE_PATH)->text, "abc") == 0,
                         "payload cache content mismatch");
    fails += expect_true(strcmp(status.summary, "payload downloaded and verified") == 0,
                         "payload download summary mismatch");
    fails += expect_true(strstr(find_file(UPDATE_AGENT_STATE_PATH)->text,
                                "payload_cache_sha256=" UPDATE_AGENT_ABC_SHA256) != NULL,
                         "payload cache sha256 should persist in state");
    fails += expect_true(update_agent_poll() == 0,
                         "poll should preserve verified payload cache state");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.payload_cache_sha256, UPDATE_AGENT_ABC_SHA256) == 0,
                         "payload cache sha256 should survive poll");

    g_corrupt_payload_write = 1;
    fails += expect_true(update_agent_download_payload() == -49,
                         "corrupted persisted payload should fail readback verification");
    update_agent_status_get(&status);
    fails += expect_true(status.payload_cache_sha256[0] == '\0',
                         "readback failure should clear cached payload digest");
    fails += expect_true(find_file(UPDATE_AGENT_PAYLOAD_CACHE_PATH)->present == 0,
                         "readback failure should remove corrupted payload cache");
    fails += expect_true(find_file(UPDATE_AGENT_STATE_PATH)->present == 0,
                         "readback failure should remove stale cache state");
    g_corrupt_payload_write = 0;

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.0.0-alpha.5\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\n"
                  "payload_size=4\n"
                  "payload_sha256=" UPDATE_AGENT_ABC_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_download_payload() == -58,
                         "declared payload size mismatch should refuse cache");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary,
                                "payload size mismatch; cache refused") == 0,
                         "payload size mismatch summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.0.0-alpha.6\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    g_payload_rc = 0;
    g_payload_bytes = (const uint8_t *)"abc";
    g_payload_len = 3u;
    fails += expect_true(update_agent_download_payload() == -44,
                         "payload digest mismatch should refuse cache");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "payload sha256 mismatch; cache refused") == 0,
                         "payload mismatch summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.0.0-alpha.6\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\npayload_sha256="
                  UPDATE_AGENT_ABC_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    g_payload_rc = -1;
    g_payload_bytes = NULL;
    g_payload_len = 0u;
    fails += expect_true(update_agent_download_payload() == -42,
                         "payload transport failure should be surfaced");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "payload download failed") == 0,
                         "payload transport failure summary mismatch");

    g_fetch_text =
        "available_version=1.0.0-alpha.3\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-09\npayload_sha256="
        UPDATE_AGENT_GOOD_SHA256 "\n"
        UPDATE_AGENT_PAYLOAD_URL_LINE
        "signature_ed25519="
        UPDATE_AGENT_BAD_SIGNATURE "\n";
    fails += expect_true(update_agent_fetch_remote_manifest() == -23,
                         "fetching an invalidly signed remote manifest should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest missing or invalid ed25519 signature") == 0,
                         "fetch invalid signature summary mismatch");

    g_fetch_rc = -1;
    g_fetch_text = NULL;
    fails += expect_true(update_agent_fetch_remote_manifest() == -36,
                         "remote fetch transport failure should be surfaced");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "remote manifest fetch failed") == 0,
                         "fetch transport failure summary mismatch");

    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.0.0-alpha.2\nchannel=develop\nbranch=develop\nsource=github:test/CapyOS\npublished_at=2026-04-09\n");
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -19,
                         "importing a manifest from another track should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest does not match selected update repository") == 0,
                         "import mismatch summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.1.0-alpha.1\npublished_at=2026-04-10\n");
    fails += expect_true(update_agent_poll() == -26,
                         "newer cached catalog without payload sha256 should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "catalog cache missing or malformed payload sha256") == 0,
                         "missing payload sha256 summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.1.0-alpha.1\npublished_at=2026-04-10\n"
                  "payload_size=0\npayload_sha256=" UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_poll() == -55,
                         "zero payload size should fail closed");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary,
                                "catalog cache payload size invalid") == 0,
                         "invalid payload size summary mismatch");
    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.1.0-alpha.1\npublished_at=2026-04-10\n"
                  "payload_size=42949672960\npayload_sha256=" UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_poll() == -55,
                         "overflowing payload size should fail closed");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.1.0-alpha.1\npublished_at=2026-04-10\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  "payload_url=https://github.com/test/../kernel.bin\n"
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_poll() == -37,
                         "newer cached catalog with malformed payload url should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "catalog cache missing or malformed payload url") == 0,
                         "catalog payload url summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.7.0-alpha.1\npublished_at=2026-04-10\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_poll() == -24,
                         "older cached catalog should be rejected as downgrade");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "catalog cache version is older than current system") == 0,
                         "catalog downgrade summary mismatch");

    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=0.7.0-alpha.1\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-10\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -20,
                         "importing a non-newer manifest should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest not newer than current system") == 0,
                         "import downgrade summary mismatch");

    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.1.0-alpha.1\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-10\n");
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -22,
                         "importing a manifest without payload sha256 should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest missing or malformed payload sha256") == 0,
                         "import payload sha256 summary mismatch");

    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.1.0-alpha.1\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-10\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\npayload_url=http://example.invalid/kernel.bin\n"
                  UPDATE_AGENT_SIGNATURE_LINE);
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -39,
                         "importing a manifest with invalid payload url should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest missing or malformed payload url") == 0,
                         "import payload url summary mismatch");

    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=1.1.0-alpha.1\npublished_at=2026-04-10\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE);
    fails += expect_true(update_agent_poll() == -28,
                         "newer cached catalog without ed25519 signature should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "catalog cache missing or invalid ed25519 signature") == 0,
                         "missing ed25519 signature summary mismatch");

    set_file_text(UPDATE_AGENT_IMPORT_PATH,
                  "available_version=1.1.0-alpha.1\nchannel=stable\nbranch=main\nsource=github:test/CapyOS\npublished_at=2026-04-10\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  "signature_ed25519="
                  UPDATE_AGENT_BAD_SIGNATURE "\n");
    fails += expect_true(update_agent_import_manifest_path(UPDATE_AGENT_IMPORT_PATH) == -23,
                         "importing a manifest with invalid ed25519 signature should fail");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "imported manifest missing or invalid ed25519 signature") == 0,
                         "import ed25519 signature summary mismatch");

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
                  "available_version=0.9.0-alpha.1\npublished_at=2026-04-08\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
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
                  "available_version=0.9.0-alpha.1\nchannel=stable\npublished_at=2026-04-08\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
    set_file_text(UPDATE_AGENT_STAGE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=stable\npublished_at=2026-04-08\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_SIGNATURE_LINE);
    set_file_text(UPDATE_AGENT_STATE_PATH,
                  "pending_activation=0\nstaged_manifest=/system/update/staged.ini\n");
    fails += expect_true(update_agent_poll() == -38,
                         "staged update without payload url should be rejected");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary, "staged update missing or malformed payload url") == 0,
                         "staged payload url summary mismatch");

    set_file_text(UPDATE_AGENT_REPOSITORY_PATH,
                  "channel=stable\nbranch=main\nsource=github:test/CapyOS\n");
    set_file_text(UPDATE_AGENT_CACHE_PATH,
                  "available_version=0.9.0-alpha.1\nchannel=stable\npublished_at=2026-04-08\npayload_sha256="
                  UPDATE_AGENT_GOOD_SHA256 "\n"
                  UPDATE_AGENT_PAYLOAD_URL_LINE
                  UPDATE_AGENT_SIGNATURE_LINE);
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
