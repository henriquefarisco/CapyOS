#include <stdio.h>
#include <string.h>

#include "services/update_agent.h"
#include "boot/boot_slot.h"

#define UA_REPO_PATH    "/system/update/repository.ini"
#define UA_CACHE_PATH   "/system/update/latest.ini"
#define UA_STAGE_PATH   "/system/update/staged.ini"
#define UA_STATE_PATH   "/system/update/state.ini"

struct fake_file {
    const char *path;
    char text[512];
    int present;
};

static struct fake_file g_files[] = {
    {UA_REPO_PATH,  "", 0},
    {UA_CACHE_PATH, "", 0},
    {UA_STAGE_PATH, "", 0},
    {UA_STATE_PATH, "", 0},
};

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
    if (text) { strncpy(f->text, text, sizeof(f->text) - 1u); }
}

static void reset_files(void) {
    size_t i;
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); i++) {
        g_files[i].present = 0;
        g_files[i].text[0] = '\0';
    }
}

static int stub_read(const char *path, char *buf, size_t sz, size_t *out_len) {
    struct fake_file *f = find_file(path);
    if (!f || !f->present) return -1;
    size_t len = strlen(f->text);
    if (len >= sz) return -1;
    memcpy(buf, f->text, len + 1u);
    if (out_len) *out_len = len;
    return 0;
}

static int stub_write(const char *path, const char *text) {
    struct fake_file *f = find_file(path);
    if (!f) return -1;
    f->present = 1;
    strncpy(f->text, text, sizeof(f->text) - 1u);
    f->text[sizeof(f->text) - 1u] = '\0';
    return 0;
}

static int stub_remove(const char *path) {
    struct fake_file *f = find_file(path);
    if (!f) return -1;
    f->present = 0;
    f->text[0] = '\0';
    return 0;
}

static void setup(void) {
    reset_files();
    update_agent_reset();
    update_agent_set_reader(stub_read);
    update_agent_set_writer(stub_write);
    update_agent_set_remover(stub_remove);
    update_agent_init("1.0.0");
    boot_slot_init();
}

/* Arm a staged update using the real update_agent API (write via stub_write).
 * Manifest key for version is "available_version" (not "version"). */
static void arm_staged_update(const char *version) {
    char manifest[256];
    snprintf(manifest, sizeof(manifest),
             "available_version=%s\nchannel=stable\nbranch=main\n"
             "source=github:henriquefarisco/CapyOS\n", version);
    set_file(UA_CACHE_PATH, manifest);
    set_file(UA_STAGE_PATH, manifest);
    /* State file key is "staged_manifest" (not "staged_manifest_path") */
    set_file(UA_STATE_PATH,
             "pending_activation=1\n"
             "staged_manifest=/system/update/staged.ini\n");
}

static void arm_staged_update_with_sha256(const char *version,
                                          const char *sha256_hex) {
    char manifest[320];
    snprintf(manifest, sizeof(manifest),
             "available_version=%s\nchannel=stable\nbranch=main\n"
             "source=github:henriquefarisco/CapyOS\n"
             "payload_sha256=%s\n", version, sha256_hex);
    set_file(UA_CACHE_PATH, manifest);
    set_file(UA_STAGE_PATH, manifest);
    set_file(UA_STATE_PATH,
             "pending_activation=1\n"
             "staged_manifest=/system/update/staged.ini\n");
}

static int test_apply_boot_slot_requires_stage(void) {
    int fails = 0;
    setup();
    /* No staged update — should fail */
    fails += expect_true(update_agent_apply_boot_slot() < 0,
                         "apply_boot_slot should fail when nothing staged");
    return fails;
}

static int test_apply_boot_slot_success(void) {
    int fails = 0;
    struct boot_slot active;
    setup();
    arm_staged_update("2.0.0");

    fails += expect_true(update_agent_poll() >= 0, "poll should succeed");
    fails += expect_true(update_agent_apply_boot_slot() == 0,
                         "apply_boot_slot should succeed with armed staged update");

    fails += expect_true(boot_slot_needs_rollback() != 0,
                         "rollback should be pending after activation");
    fails += expect_true(boot_slot_get_active(&active) == 0,
                         "get_active should succeed");
    fails += expect_true(strcmp(active.version, "2.0.0") == 0,
                         "active slot should have new version 2.0.0");

    return fails;
}

static int test_confirm_health_clears_rollback(void) {
    int fails = 0;
    setup();
    arm_staged_update("2.0.0");

    fails += expect_true(update_agent_poll() >= 0, "poll");
    fails += expect_true(update_agent_apply_boot_slot() == 0, "apply_boot_slot");
    fails += expect_true(boot_slot_needs_rollback() != 0, "rollback pending before confirm");

    fails += expect_true(update_agent_confirm_health() == 0,
                         "confirm_health should succeed");
    fails += expect_true(boot_slot_needs_rollback() == 0,
                         "rollback should not be pending after confirm_health");

    return fails;
}

static int test_check_rollback_triggers_rollback(void) {
    int fails = 0;
    setup();

    /*
     * Simulate two-slot update lifecycle:
     * 1. Stage+activate slot 1 (simulates "old known-good boot")
     * 2. Confirm health to clear rollback_pending
     * 3. Stage+activate slot 0 (the new update) → slot 1 becomes ROLLBACK
     * 4. Do NOT confirm health → rollback_pending = 1
     * 5. check_rollback() should roll back to slot 1
     */
    fails += expect_true(boot_slot_stage(1u, "1.0.0", 0u) == 0, "stage slot 1");
    fails += expect_true(boot_slot_activate(1u) == 0, "activate slot 1");
    fails += expect_true(boot_slot_confirm_health() == 0, "confirm slot 1 health");

    fails += expect_true(boot_slot_stage(0u, "2.0.0", 0u) == 0, "stage slot 0");
    fails += expect_true(boot_slot_activate(0u) == 0, "activate slot 0");
    fails += expect_true(boot_slot_needs_rollback() != 0, "rollback pending for slot 0");

    /* check_rollback should detect unconfirmed boot and roll back */
    fails += expect_true(update_agent_check_rollback() == 1,
                         "check_rollback should return 1 (rollback happened)");
    fails += expect_true(boot_slot_needs_rollback() == 0,
                         "rollback_pending cleared after rollback");

    return fails;
}

static int test_check_rollback_no_op_when_healthy(void) {
    int fails = 0;
    setup();
    /* No activation, so needs_rollback == 0 */
    fails += expect_true(update_agent_check_rollback() == 0,
                         "check_rollback should return 0 when no rollback needed");
    return fails;
}

/* M6.4 payload sha256 verification ------------------------------------ */

#define UA_GOOD_SHA256 \
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
#define UA_OTHER_SHA256 \
    "0011223344556677889900112233445566778899001122334455667788990011"

static int test_apply_verified_legacy_path_no_digest(void) {
    /* When the manifest does NOT declare payload_sha256, the verified
     * variant must behave exactly like the legacy apply call. */
    int fails = 0;
    setup();
    arm_staged_update("2.0.0");

    fails += expect_true(
        update_agent_staged_requires_payload_verification() == 0,
        "manifest without payload_sha256 reports no verification required");
    fails += expect_true(
        update_agent_apply_boot_slot_verified(NULL) == 0,
        "verified apply with NULL digest succeeds when manifest is silent");
    return fails;
}

static int test_apply_verified_matching_digest(void) {
    int fails = 0;
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);

    fails += expect_true(
        update_agent_staged_requires_payload_verification() == 1,
        "manifest with payload_sha256 reports verification required");

    fails += expect_true(
        update_agent_apply_boot_slot_verified(UA_GOOD_SHA256) == 0,
        "verified apply with matching digest succeeds");

    /* Case-insensitive comparison: same digest with upper-case hex must also
     * match so manifests in either case are accepted. */
    setup();
    arm_staged_update_with_sha256("2.0.0", UA_GOOD_SHA256);
    fails += expect_true(
        update_agent_apply_boot_slot_verified(
            "ABCDEF0123456789ABCDEF0123456789"
            "ABCDEF0123456789ABCDEF0123456789") == 0,
        "verified apply is case-insensitive on hex digest");
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

int run_update_transact_tests(void) {
    int fails = 0;
    fails += test_apply_boot_slot_requires_stage();
    fails += test_apply_boot_slot_success();
    fails += test_confirm_health_clears_rollback();
    fails += test_check_rollback_triggers_rollback();
    fails += test_check_rollback_no_op_when_healthy();
    fails += test_apply_verified_legacy_path_no_digest();
    fails += test_apply_verified_matching_digest();
    fails += test_apply_verified_mismatched_digest_refuses();
    fails += test_apply_verified_missing_digest_refuses();
    fails += test_apply_verified_malformed_digest_refuses();
    if (fails == 0) {
        printf("[tests] update_transact OK\n");
    }
    return fails;
}
