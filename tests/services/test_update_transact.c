#include <stdio.h>
#include <string.h>

#include "services/update_agent.h"
#include "boot/boot_slot.h"
#include "boot/internal/boot_slot_internal.h"

/* Controllable stand-in for the x86_64 boot-slot capability. The zeroed
 * default models a system with no registered persistent provider (every rc is
 * a refusal), which is what keeps the legacy refusal assertions below honest.
 * Individual tests opt into a working provider by clearing an rc. */
struct runtime_stub {
    int health_rc, rollback_state, stage_rc, arm_rc, stage_calls, arm_calls;
    uint32_t stage_slot, arm_seen_slot;
    uint64_t stage_generation, arm_generation, arm_seen_generation;
    size_t stage_seen_len;
    char stage_seen_version[BOOT_SLOT_VERSION_MAX];
    uint8_t stage_seen_sha256[BOOT_SLOT_SHA256_SIZE];
    uint8_t stage_seen_payload[64];
};

static struct runtime_stub g_stub;

static void reset_runtime_stubs(void) {
    memset(&g_stub, 0, sizeof(g_stub));
    g_stub.health_rc = -1;
    g_stub.rollback_state = -1;
    g_stub.stage_rc = -1;
    g_stub.arm_rc = -1;
    g_stub.stage_slot = 1u;
    g_stub.stage_generation = 7u;
    g_stub.arm_generation = 8u;
    g_stub.arm_seen_slot = BOOT_SLOT_NONE;
}

int x64_storage_runtime_confirm_current_boot_health(void) {
    return g_stub.health_rc;
}

int x64_storage_runtime_current_boot_rollback_check(void) {
    return g_stub.rollback_state;
}

int x64_storage_runtime_stage_boot_payload_sha256(
    const char *version, const uint8_t *payload, size_t payload_len,
    const uint8_t expected_sha256[BOOT_SLOT_SHA256_SIZE], uint32_t *out_slot,
    uint64_t *out_generation) {
    g_stub.stage_calls++;
    if (out_slot) *out_slot = BOOT_SLOT_NONE;
    if (out_generation) *out_generation = 0u;
    if (version)
        strncpy(g_stub.stage_seen_version, version,
                sizeof(g_stub.stage_seen_version) - 1u);
    if (expected_sha256)
        memcpy(g_stub.stage_seen_sha256, expected_sha256,
               sizeof(g_stub.stage_seen_sha256));
    if (payload && payload_len <= sizeof(g_stub.stage_seen_payload)) {
        memcpy(g_stub.stage_seen_payload, payload, payload_len);
        g_stub.stage_seen_len = payload_len;
    }
    if (g_stub.stage_rc != 0) return g_stub.stage_rc;
    if (out_slot) *out_slot = g_stub.stage_slot;
    if (out_generation) *out_generation = g_stub.stage_generation;
    return 0;
}

int x64_storage_runtime_arm_boot_slot(uint32_t slot,
                                      uint64_t expected_generation,
                                      uint64_t *out_generation) {
    g_stub.arm_calls++;
    g_stub.arm_seen_slot = slot;
    g_stub.arm_seen_generation = expected_generation;
    if (out_generation) *out_generation = 0u;
    if (g_stub.arm_rc != 0) return g_stub.arm_rc;
    if (out_generation) *out_generation = g_stub.arm_generation;
    return 0;
}

#define UA_GOOD_SHA256 \
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
#define UA_OTHER_SHA256 \
    "0011223344556677889900112233445566778899001122334455667788990011"
#define UA_ABC_SHA256 \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define UA_GOOD_SIGNATURE \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define UA_PAYLOAD_URL_LINE \
    "payload_url=https://github.com/henriquefarisco/CapyOS/releases/download/v2.0.0/kernel.bin\n"
#define UA_SIGNATURE_LINE "signature_ed25519=" UA_GOOD_SIGNATURE "\n"

#define UA_REPO_PATH    "/system/update/repository.ini"
#define UA_CACHE_PATH   "/system/update/latest.ini"
#define UA_STAGE_PATH   "/system/update/staged.ini"
#define UA_STATE_PATH   "/system/update/state.ini"
#define UA_PAYLOAD_PATH "/system/update/payload.bin"

struct fake_file {
    const char *path;
    char text[512];
    int present;
    size_t len;
};

static struct fake_file g_files[] = {
    {UA_REPO_PATH,  "", 0, 0u},
    {UA_CACHE_PATH, "", 0, 0u},
    {UA_STAGE_PATH, "", 0, 0u},
    {UA_STATE_PATH, "", 0, 0u},
    {UA_PAYLOAD_PATH, "", 0, 0u},
};

static uint8_t g_boot_records[BOOT_SLOT_PERSIST_COPY_COUNT]
                             [BOOT_SLOT_PERSIST_RECORD_SIZE];
static int g_boot_record_present[BOOT_SLOT_PERSIST_COPY_COUNT];

static int expect_true(int cond, const char *msg) {
    if (!cond) { fprintf(stderr, "[update-transact] %s\n", msg); return 1; }
    return 0;
}

static struct fake_file *find_file(const char *path) {
    size_t i;
    if (!path) return NULL;
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); i++) {
        if (strcmp(g_files[i].path, path) == 0) return &g_files[i];
    }
    return NULL;
}

static void set_file(const char *path, const char *text) {
    struct fake_file *f = find_file(path);
    if (!f) return;
    f->present = text ? 1 : 0;
    f->text[0] = '\0';
    f->len = 0u;
    if (text) {
        strncpy(f->text, text, sizeof(f->text) - 1u);
        f->text[sizeof(f->text) - 1u] = '\0';
        f->len = strlen(f->text);
    }
}

static void reset_files(void) {
    size_t i;
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); i++) {
        g_files[i].present = 0;
        g_files[i].text[0] = '\0';
        g_files[i].len = 0u;
    }
}

static int stub_read(const char *path, char *buf, size_t sz, size_t *out_len) {
    struct fake_file *f = find_file(path);
    if (!f || !f->present) return -1;
    size_t len = f->len;
    if (len >= sz) return -1;
    memcpy(buf, f->text, len + 1u);
    if (out_len) *out_len = len;
    return 0;
}

static int stub_read_bytes(const char *path, uint8_t *buf, size_t size,
                           size_t *out_len) {
    struct fake_file *f = find_file(path);
    if (!f || !f->present || !buf || f->len > size) return -1;
    memcpy(buf, f->text, f->len);
    if (out_len) *out_len = f->len;
    return 0;
}

static int stub_write(const char *path, const char *text) {
    struct fake_file *f = find_file(path);
    if (!f) return -1;
    f->present = 1;
    strncpy(f->text, text, sizeof(f->text) - 1u);
    f->text[sizeof(f->text) - 1u] = '\0';
    f->len = strlen(f->text);
    return 0;
}

static int stub_remove(const char *path) {
    struct fake_file *f = find_file(path);
    if (!f) return -1;
    f->present = 0;
    f->text[0] = '\0';
    f->len = 0u;
    return 0;
}

static int stub_manifest_verify(const char *signed_text, size_t signed_len,
                                const char *signature_hex) {
    return signed_text && signed_len > 0u && signature_hex &&
           strstr(signed_text, "signature_ed25519=") == NULL &&
           strcmp(signature_hex, UA_GOOD_SIGNATURE) == 0;
}

static int stub_boot_read(void *ctx, uint32_t copy_index, uint8_t *record,
                          size_t record_size) {
    (void)ctx;
    if (!record || copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT ||
        record_size != BOOT_SLOT_PERSIST_RECORD_SIZE ||
        !g_boot_record_present[copy_index]) return BOOT_SLOT_PERSIST_EMPTY;
    memcpy(record, g_boot_records[copy_index], record_size);
    return 0;
}

static int stub_boot_write(void *ctx, uint32_t copy_index,
                           const uint8_t *record, size_t record_size) {
    (void)ctx;
    if (!record || copy_index >= BOOT_SLOT_PERSIST_COPY_COUNT ||
        record_size != BOOT_SLOT_PERSIST_RECORD_SIZE) return -1;
    memcpy(g_boot_records[copy_index], record, record_size);
    g_boot_record_present[copy_index] = 1;
    return 0;
}

static int stub_boot_flush(void *ctx) {
    (void)ctx;
    return 0;
}

static void stub_boot_image(struct boot_slot_image *image, const char *version,
                            uint8_t seed) {
    memset(image, 0, sizeof(*image));
    strncpy(image->version, version, sizeof(image->version) - 1u);
    image->payload_size = 4096u;
    for (size_t i = 0u; i < sizeof(image->payload_sha256); ++i)
        image->payload_sha256[i] = (uint8_t)(seed + (uint8_t)i);
}

static int stub_boot_stage_image(uint32_t slot, const char *version,
                                 uint8_t seed) {
    struct boot_slot_image image;
    stub_boot_image(&image, version, seed);
    return boot_slot_test_publish_metadata(slot, &image);
}

static int setup_persistent_boot_control(void) {
    struct boot_slot_layout layout;
    struct boot_slot_image image;
    memset(g_boot_records, 0, sizeof(g_boot_records));
    memset(g_boot_record_present, 0, sizeof(g_boot_record_present));
    boot_slot_init();
    if (boot_slot_layout_plan(524288u, &layout) != 0 ||
        boot_slot_set_persistence(stub_boot_read, stub_boot_write,
                                  stub_boot_flush, g_boot_records, &layout) !=
            BOOT_SLOT_PERSIST_EMPTY) return -1;
    stub_boot_image(&image, "1.0.0", 0x10u);
    return boot_slot_initialize_persistent(&layout, &image);
}

static void setup(void) {
    reset_files();
    reset_runtime_stubs();
    update_agent_reset();
    update_agent_set_reader(stub_read);
    update_agent_set_bytes_reader(stub_read_bytes);
    update_agent_set_writer(stub_write);
    update_agent_set_remover(stub_remove);
    update_agent_set_manifest_verifier(stub_manifest_verify);
    update_agent_init("1.0.0");
    boot_slot_init();
}

/* Arm a staged update fixture directly. Manifest key for version is
 * "available_version" (not "version"). */
static void arm_staged_update(const char *version) {
    char manifest[256];
    snprintf(manifest, sizeof(manifest),
             "available_version=%s\nchannel=stable\nbranch=main\n"
             "source=github:henriquefarisco/CapyOS\n", version);
    set_file(UA_CACHE_PATH, NULL);
    set_file(UA_STAGE_PATH, manifest);
    /* State file key is "staged_manifest" (not "staged_manifest_path") */
    set_file(UA_STATE_PATH,
             "pending_activation=1\n"
             "staged_manifest=/system/update/staged.ini\n");
}

static void arm_staged_update_with_sha256(const char *version,
                                          const char *sha256_hex) {
    char manifest[512];
    snprintf(manifest, sizeof(manifest),
             "available_version=%s\nchannel=stable\nbranch=main\n"
             "source=github:henriquefarisco/CapyOS\n"
             "payload_sha256=%s\n"
             UA_PAYLOAD_URL_LINE
             UA_SIGNATURE_LINE, version, sha256_hex);
    set_file(UA_CACHE_PATH, manifest);
    set_file(UA_STAGE_PATH, manifest);
    set_file(UA_STATE_PATH,
             "pending_activation=1\n"
             "staged_manifest=/system/update/staged.ini\n");
    set_file(UA_PAYLOAD_PATH, "abc");
}

static void arm_staged_update_with_sha256_cache(const char *version,
                                                const char *manifest_sha256,
                                                const char *cache_sha256) {
    char manifest[512];
    char state[256];
    snprintf(manifest, sizeof(manifest),
             "available_version=%s\nchannel=stable\nbranch=main\n"
             "source=github:henriquefarisco/CapyOS\n"
             "payload_sha256=%s\n"
             UA_PAYLOAD_URL_LINE
             UA_SIGNATURE_LINE, version, manifest_sha256);
    snprintf(state, sizeof(state),
             "pending_activation=1\n"
             "staged_manifest=/system/update/staged.ini\n"
             "payload_cache=/system/update/payload.bin\n"
             "payload_cache_sha256=%s\n", cache_sha256);
    set_file(UA_CACHE_PATH, manifest);
    set_file(UA_STAGE_PATH, manifest);
    set_file(UA_STATE_PATH, state);
    set_file(UA_PAYLOAD_PATH, "abc");
}

static void set_catalog_update_with_sha256(const char *version,
                                           const char *sha256_hex) {
    char manifest[512];
    snprintf(manifest, sizeof(manifest),
             "available_version=%s\nchannel=stable\nbranch=main\n"
             "source=github:henriquefarisco/CapyOS\n"
             "payload_sha256=%s\n"
             UA_PAYLOAD_URL_LINE
             UA_SIGNATURE_LINE, version, sha256_hex);
    set_file(UA_CACHE_PATH, manifest);
}

static int test_apply_boot_slot_requires_stage(void) {
    int fails = 0;
    setup();
    /* No staged update — should fail */
    fails += expect_true(
        update_agent_apply_boot_slot() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "apply_boot_slot must report unavailable persistent application");
    return fails;
}

static int test_direct_apply_refuses_hashed_stage(void) {
    int fails = 0;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);

    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(
        update_agent_apply_boot_slot() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "direct apply should report unavailable persistent application");
    return fails;
}

static int test_apply_verified_success(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);

    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_ABC_SHA256) ==
            UPDATE_AGENT_ERR_UNSUPPORTED,
        "matching digest must not turn RAM metadata into a fake update");

    fails += expect_true(boot_slot_needs_rollback() == 0,
                         "unsupported apply must not mutate boot slots");
    fails += expect_true(g_stub.arm_calls == 0,
                         "a refused slot write must never reach the arm step");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "unsupported apply result mismatch");
    fails += expect_true(strcmp(status.summary,
                                "persistent slot staging refused; inactive slot not written") == 0,
                         "unsupported apply summary mismatch");

    return fails;
}

/* Full persistent apply: the verified bytes reach the staging bridge, and only
 * the generation that staging produced is spent by the arm step. */
static int test_apply_verified_stages_and_arms(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    g_stub.stage_rc = 0;
    g_stub.arm_rc = 0;
    g_stub.stage_slot = 1u;
    g_stub.stage_generation = 11u;

    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(update_agent_apply_boot_slot_verified(UA_ABC_SHA256) == 0,
                         "verified apply must commit to the inactive slot");
    fails += expect_true(g_stub.stage_calls == 1 && g_stub.arm_calls == 1,
                         "apply must stage once and arm once");
    fails += expect_true(g_stub.stage_seen_len == 3u &&
                         memcmp(g_stub.stage_seen_payload, "abc", 3u) == 0,
                         "apply must stage the verified cached bytes");
    fails += expect_true(strcmp(g_stub.stage_seen_version, "2.0.0") == 0,
                         "apply must stage the staged manifest version");
    fails += expect_true(g_stub.stage_seen_sha256[0] == 0xbau &&
                         g_stub.stage_seen_sha256[31] == 0xadu,
                         "apply must pass the signed digest as raw bytes");
    fails += expect_true(g_stub.arm_seen_slot == 1u &&
                         g_stub.arm_seen_generation == 11u,
                         "arm must consume the staged slot and generation");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == 0,
                         "successful apply must clear last_result");
    fails += expect_true(strcmp(status.summary,
                                "inactive slot written and armed for one boot attempt") == 0,
                         "successful apply summary mismatch");
    return fails;
}

/* A staged-but-unarmed slot is a refusal, not a success: the loader would keep
 * booting the confirmed slot and the user must not be told otherwise. */
static int test_apply_verified_unarmed_slot_refuses(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    g_stub.stage_rc = 0;
    g_stub.arm_rc = -1;

    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_ABC_SHA256) ==
            UPDATE_AGENT_ERR_UNSUPPORTED,
        "an unarmed staged slot must not report success");
    fails += expect_true(g_stub.stage_calls == 1 && g_stub.arm_calls == 1,
                         "arm failure must be observed after a single attempt");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary,
                                "inactive slot staged but boot attempt not armed") == 0,
                         "unarmed apply summary mismatch");
    return fails;
}

static int test_apply_verified_requires_payload_bytes(void) {
    int fails = 0;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    set_file(UA_PAYLOAD_PATH, NULL);

    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_ABC_SHA256) == -31,
        "manual verified apply refuses missing payload bytes");
    fails += expect_true(boot_slot_needs_rollback() == 0,
                         "missing payload bytes must not mutate boot slots");
    return fails;
}

static int test_apply_rejects_metadata_only_control(void) {
    int fails = 0;
    struct boot_slot inactive;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    fails += expect_true(setup_persistent_boot_control() == 0,
                         "initialize metadata-only control for apply");
    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_ABC_SHA256) ==
            UPDATE_AGENT_ERR_UNSUPPORTED,
        "metadata persistence must not unlock verified apply");
    fails += expect_true(boot_slot_get(1u, &inactive) == 0 &&
                         inactive.state == BOOT_SLOT_EMPTY &&
                         boot_slot_persistence_generation() == 2u,
                         "refused verified apply mutated metadata");
    return fails;
}

static int test_confirm_health_requires_persistent_boot_control(void) {
    int fails = 0;
    struct system_update_status status;
    struct boot_slot before;
    struct boot_slot after;
    struct boot_slot_manager manager_before;
    struct boot_slot_manager manager_after;
    setup();
    fails += expect_true(boot_slot_stage(0u, "1.0.0", 0u) == 0,
                         "stage synthetic known-good slot");
    fails += expect_true(boot_slot_activate(0u) == 0,
                         "activate synthetic known-good slot");
    fails += expect_true(boot_slot_stage(1u, "2.0.0", 0u) == 0,
                         "stage synthetic slot for health test");
    fails += expect_true(boot_slot_activate(1u) == 0,
                         "activate synthetic slot for health test");
    fails += expect_true(boot_slot_needs_rollback() != 0,
                         "rollback pending before refused confirm");
    boot_slot_get_active(&before);
    boot_slot_manager_get(&manager_before);

    fails += expect_true(
        update_agent_confirm_health() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "confirm_health must reject RAM-only boot metadata");
    boot_slot_get_active(&after);
    boot_slot_manager_get(&manager_after);
    fails += expect_true(boot_slot_needs_rollback() != 0 &&
                         memcmp(&before, &after, sizeof(before)) == 0 &&
                         memcmp(&manager_before, &manager_after,
                                sizeof(manager_before)) == 0,
                         "refused health confirm mutated RAM slot state");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "confirm_health refusal should expose unsupported");
    fails += expect_true(
        strcmp(status.summary,
               "persistent health confirmation unavailable or token mismatch") == 0,
        "confirm_health refusal summary mismatch");

    return fails;
}

static int test_check_rollback_requires_persistent_boot_control(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    fails += expect_true(
        update_agent_check_rollback() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "rollback check must reject RAM-only boot metadata");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "rollback refusal should expose unsupported");
    fails += expect_true(
        strcmp(status.summary,
               "persistent rollback unsupported; no boot control committed") == 0,
        "rollback refusal summary mismatch");
    return fails;
}

static int test_check_rollback_rejects_metadata_only_control(void) {
    int fails = 0;
    struct system_update_status status;
    struct boot_slot active;
    uint32_t selected = BOOT_SLOT_NONE;
    uint32_t before_boot_count = 0u;
    uint32_t before_tries = 0u;
    uint64_t before_generation = 0u;
    uint64_t token = 0u;
    setup();
    fails += expect_true(setup_persistent_boot_control() == 0,
                         "initialize metadata-only boot control");

    /*
     * Simulate two-slot update lifecycle:
     * 1. Stage+activate slot 1 (simulates "old known-good boot")
     * 2. Confirm health to clear rollback_pending
     * 3. Stage+activate slot 0 (the new update) → slot 1 becomes ROLLBACK
     * 4. Do NOT confirm health → rollback_pending = 1
     * 5. check_rollback() should roll back to slot 1
     */
    fails += expect_true(stub_boot_stage_image(1u, "1.0.0", 0x30u) == 0,
                         "stage metadata-only slot 1");
    fails += expect_true(boot_slot_activate(1u) == 0,
                         "activate metadata-only slot 1");
    fails += expect_true(boot_slot_select_for_boot(&selected, &token) == 1,
                         "consume metadata-only slot 1 attempt");
    fails += expect_true(boot_slot_confirm_health_verified(1u, token, 0u, 0u) == 0,
                         "confirm metadata-only slot 1");
    fails += expect_true(stub_boot_stage_image(0u, "2.0.0", 0x50u) == 0,
                         "stage metadata-only slot 0");
    fails += expect_true(boot_slot_activate(0u) == 0,
                         "activate metadata-only slot 0");
    fails += expect_true(boot_slot_needs_rollback() != 0,
                         "metadata-only rollback pending");
    boot_slot_get_active(&active);
    before_boot_count = active.boot_count;
    {
        struct boot_slot_manager manager;
        boot_slot_manager_get(&manager);
        before_tries = manager.tries_remaining;
    }
    before_generation = boot_slot_persistence_generation();
    fails += expect_true(
        update_agent_check_rollback() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "metadata persistence must not unlock updater rollback");
    fails += expect_true(boot_slot_needs_rollback() != 0,
                         "refused updater rollback must not mutate metadata");
    fails += expect_true(boot_slot_get_active(&active) == 0 &&
                         strcmp(active.version, "2.0.0") == 0 &&
                         !active.health_confirmed &&
                         active.boot_count == before_boot_count &&
                         boot_slot_persistence_generation() == before_generation,
                         "refused updater rollback changed active metadata");
    {
        struct boot_slot_manager manager;
        fails += expect_true(boot_slot_manager_get(&manager) == 0 &&
                             manager.tries_remaining == before_tries,
                             "refused updater rollback consumed attempt");
    }
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "metadata-only refusal should expose unsupported");
    fails += expect_true(
        strcmp(status.summary,
               "persistent rollback unsupported; no boot control committed") == 0,
        "metadata-only rollback refusal summary mismatch");
    return fails;
}

static int test_check_rollback_rejects_healthy_metadata(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    fails += expect_true(setup_persistent_boot_control() == 0,
                         "initialize healthy metadata-only control");
    fails += expect_true(
        update_agent_check_rollback() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "healthy metadata must not unlock updater rollback");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "healthy metadata refusal should expose unsupported");
    return fails;
}

/* A consumed-but-unconfirmed attempt is reported without touching metadata:
 * the loader owns the transition, the updater only observes it. */
static int test_check_rollback_reports_pending_attempt(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    g_stub.rollback_state = 1;
    fails += expect_true(update_agent_check_rollback() == 0,
                         "a pending attempt is a valid, reportable state");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == 0,
                         "pending attempt must not be reported as an error");
    fails += expect_true(
        strcmp(status.summary,
               "boot attempt pending confirmation; rollback still armed") == 0,
        "pending attempt summary mismatch");
    return fails;
}

/* When the loader already rolled back, the staged update must be disarmed so
 * the next boot does not retry the payload that failed to confirm. */
static int test_check_rollback_reports_loader_rollback(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    g_stub.rollback_state = 2;
    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(update_agent_check_rollback() == 0,
                         "an applied rollback is reported, not refused");
    update_agent_status_get(&status);
    fails += expect_true(status.pending_activation == 0u,
                         "an applied rollback must disarm the staged update");
    fails += expect_true(
        strcmp(status.summary,
               "boot rolled back to the confirmed slot; staged update disarmed") == 0,
        "applied rollback summary mismatch");
    return fails;
}

/* Health confirmation is the commit point of the transaction: it flips the
 * durable metadata and only then drops the staged catalog. Keep this last —
 * a successful confirm is memoised for the rest of the process. */
static int test_confirm_health_commits_and_clears_stage(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    g_stub.health_rc = 0;
    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(update_agent_confirm_health() == 0,
                         "a matching attempt token must confirm health");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == 0,
                         "confirmed health must clear last_result");
    fails += expect_true(strcmp(status.summary,
                                "persistent boot health confirmed") == 0,
                         "confirmed health summary mismatch");
    fails += expect_true(status.stage_ready == 0u &&
                         status.pending_activation == 0u,
                         "confirmed health must clear the staged catalog");
    return fails;
}

/* M6.4 payload sha256 verification ------------------------------------ */

static int test_apply_verified_refuses_missing_manifest_digest(void) {
    int fails = 0;
    setup();
    arm_staged_update("2.0.0");

    fails += expect_true(
        update_agent_staged_requires_payload_verification() == 0,
        "manifest without payload_sha256 reports no verification required");
    fails += expect_true(
        update_agent_apply_boot_slot_verified(NULL) == -27,
        "verified apply refuses a staged manifest without payload sha256");
    return fails;
}

static int test_apply_verified_matching_digest(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);

    fails += expect_true(
        update_agent_staged_requires_payload_verification() == 1,
        "manifest with payload_sha256 reports verification required");

    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_ABC_SHA256) ==
            UPDATE_AGENT_ERR_UNSUPPORTED,
        "verified apply stops at the missing persistent capability");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "verified apply should expose unsupported result");
    fails += expect_true(strcmp(status.summary,
                                "persistent slot staging refused; inactive slot not written") == 0,
                         "verified apply unsupported summary mismatch");

    /* Case-insensitive comparison: same digest with upper-case hex must also
     * match so manifests in either case are accepted. */
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_ABC_SHA256);
    fails += expect_true(
        update_agent_apply_boot_slot_verified(
            "BA7816BF8F01CFEA414140DE5DAE2223"
            "B00361A396177A9CB410FF61F20015AD") ==
            UPDATE_AGENT_ERR_UNSUPPORTED,
        "uppercase matching digest still reaches unsupported capability gate");
    return fails;
}

static int test_apply_verified_mismatched_digest_refuses(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);

    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_OTHER_SHA256) == -31,
        "verified apply refuses mismatched digest with -31");

    update_agent_status_get(&status);
    fails += expect_true(
        strstr(status.summary, "payload sha256 mismatch") != NULL,
        "mismatch must surface a stable summary");
    fails += expect_true(status.last_result == -31,
                         "last_result reflects the mismatch refusal");
    return fails;
}

static int test_apply_verified_missing_digest_refuses(void) {
    int fails = 0;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);

    fails += expect_true(
        update_agent_apply_boot_slot_verified(NULL) == -30,
        "verified apply with NULL digest refuses with -30");
    fails += expect_true(
        update_agent_apply_boot_slot_verified("") == -30,
        "verified apply with empty digest refuses with -30");
    return fails;
}

static int test_apply_verified_malformed_digest_refuses(void) {
    int fails = 0;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);

    /* Too short. */
    fails += expect_true(
        update_agent_apply_boot_slot_verified("abc") == -32,
        "verified apply refuses short digest with -32");
    /* Right length but not hex. */
    fails += expect_true(
        update_agent_apply_boot_slot_verified(
            "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+"
            "0123456789ABCDEF") == -32,
        "verified apply refuses non-hex chars with -32");
    /* Right length, valid hex, but extra trailing data. */
    fails += expect_true(
        update_agent_apply_boot_slot_verified(
            UA_GOOD_SHA256 "extra") == -32,
        "verified apply refuses overlong digest with -32");
    return fails;
}

static int test_apply_cached_payload_matching_digest(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256_cache("2.0.0", UA_ABC_SHA256,
                                        UA_ABC_SHA256);

    fails += expect_true(
        update_agent_apply_cached_payload() == UPDATE_AGENT_ERR_UNSUPPORTED,
        "cached payload apply stops at the missing persistent capability");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == UPDATE_AGENT_ERR_UNSUPPORTED,
                         "cached apply should expose unsupported result");
    fails += expect_true(strcmp(status.summary,
                                "persistent slot staging refused; inactive slot not written") == 0,
                         "cached apply unsupported summary mismatch");
    return fails;
}

static int test_apply_cached_payload_missing_cache_refuses(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);

    fails += expect_true(update_agent_apply_cached_payload() == -50,
                         "cached payload apply refuses missing cache digest");
    update_agent_status_get(&status);
    fails += expect_true(strcmp(status.summary,
                                "payload cache sha256 missing; refusing cached apply") == 0,
                         "missing cache apply summary mismatch");
    return fails;
}

static int test_apply_cached_payload_missing_bytes_refuses(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256_cache("2.0.0", UA_ABC_SHA256,
                                        UA_ABC_SHA256);
    set_file(UA_PAYLOAD_PATH, NULL);

    fails += expect_true(update_agent_apply_cached_payload() == -31,
                         "cached payload apply refuses missing payload bytes");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == -31,
                         "missing payload bytes should expose last_result -31");
    fails += expect_true(strstr(status.summary, "payload cache") != NULL,
                         "missing payload bytes summary mismatch");
    return fails;
}

static int test_apply_cached_payload_catalog_changed_mismatch_refuses(void) {
    int fails = 0;
    struct system_update_status status;
    setup();
    arm_staged_update_with_sha256_cache("2.0.0", UA_GOOD_SHA256,
                                        UA_ABC_SHA256);
    set_catalog_update_with_sha256("2.1.0", UA_ABC_SHA256);

    fails += expect_true(update_agent_apply_cached_payload() == -31,
                         "cached payload apply refuses cache/staged mismatch");
    update_agent_status_get(&status);
    fails += expect_true(status.last_result == -31,
                         "cached mismatch should expose last_result -31");
    fails += expect_true(strstr(status.summary, "payload sha256 mismatch") != NULL,
                         "cached mismatch summary mismatch");
    return fails;
}

int run_update_transact_tests(void) {
    int fails = 0;
    fails += test_apply_boot_slot_requires_stage();
    fails += test_direct_apply_refuses_hashed_stage();
    fails += test_apply_verified_success();
    fails += test_apply_verified_stages_and_arms();
    fails += test_apply_verified_unarmed_slot_refuses();
    fails += test_apply_verified_requires_payload_bytes();
    fails += test_apply_rejects_metadata_only_control();
    fails += test_confirm_health_requires_persistent_boot_control();
    fails += test_check_rollback_requires_persistent_boot_control();
    fails += test_check_rollback_rejects_metadata_only_control();
    fails += test_check_rollback_rejects_healthy_metadata();
    fails += test_check_rollback_reports_pending_attempt();
    fails += test_check_rollback_reports_loader_rollback();
    fails += test_apply_verified_refuses_missing_manifest_digest();
    fails += test_apply_verified_matching_digest();
    fails += test_apply_verified_mismatched_digest_refuses();
    fails += test_apply_verified_missing_digest_refuses();
    fails += test_apply_verified_malformed_digest_refuses();
    fails += test_apply_cached_payload_catalog_changed_mismatch_refuses();
    fails += test_apply_cached_payload_missing_bytes_refuses();
    fails += test_apply_cached_payload_missing_cache_refuses();
    fails += test_apply_cached_payload_matching_digest();
    /* Keep last: a successful confirm is memoised for the whole process. */
    fails += test_confirm_health_commits_and_clears_stage();
    if (fails == 0) {
        printf("[tests] update_transact OK\n");
    }
    return fails;
}
