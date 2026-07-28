/*
 * Update agent — transactional integration with the boot slot system.
 *
 * Responsibilities of this translation unit:
 *  - update_agent_apply_boot_slot           (explicit unsupported capability)
 *  - update_agent_apply_boot_slot_verified  (sha256 gate, then unsupported)
 *  - update_agent_staged_requires_payload_verification
 *  - update_agent_confirm_health
 *  - update_agent_check_rollback
 *
 * The catalog/manifest parsing, repository configuration, IO and the
 * stage/import/poll machinery live in the update_agent sibling TUs. They
 * share the runtime status struct and the small string helper
 * through src/services/internal/update_agent_internal.h.
 *
 * Splitting the agent in two keeps each TU well below the monolith
 * threshold without forcing the rest of the codebase to know about the
 * split. No function in this file reports a RAM-only boot_slot mutation as a
 * successful persistent system update.
 */
#include "internal/update_agent_internal.h"

#include "boot/boot_slot.h"
#include "kernel/log/klog.h"

#include <stddef.h>

/* Forward declarations of the arch bridge that owns the persistent boot-slot
 * capability. Declaring them here keeps this TU free of the x86_64 storage
 * runtime header while still binding the update lifecycle to the durable
 * provider: staging writes the inactive slot, arming publishes a single boot
 * attempt, and the post-reboot commands observe the same metadata. */
int x64_storage_runtime_confirm_current_boot_health(void);
int x64_storage_runtime_current_boot_rollback_check(void);
int x64_storage_runtime_stage_boot_payload_sha256(
    const char *version, const uint8_t *payload, size_t payload_len,
    const uint8_t expected_sha256[BOOT_SLOT_SHA256_SIZE], uint32_t *out_slot,
    uint64_t *out_generation);
int x64_storage_runtime_arm_boot_slot(uint32_t slot,
                                     uint64_t expected_generation,
                                     uint64_t *out_generation);
int update_agent_poll(void);
int update_agent_clear_stage(void);
int update_agent_set_pending_activation(int enabled);
void update_agent_init(const char *current_version);
static int update_agent_apply_unsupported(void);
static int update_agent_health_durably_confirmed;

/* ---- M6.4 payload sha256 verification helpers ----------------------- */

/* Compare two 64-char hex digests for equality. The byte loop runs the
 * full fixed length so the per-byte work is constant; we lower-case both
 * sides as we go so manifests written with upper-case hex still match. */
static int update_agent_sha256_hex_equal(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) return 0;
    while (i < UPDATE_AGENT_SHA256_HEX_LEN) {
        char ca = a[i];
        char cb = b[i];
        if (!ca || !cb) return 0;
        if (ca >= 'A' && ca <= 'F') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'F') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
        ++i;
    }
    /* Both must terminate exactly at UPDATE_AGENT_SHA256_HEX_LEN. */
    return a[i] == '\0' && b[i] == '\0';
}

static int update_agent_sha256_hex_is_well_formed(const char *hex) {
    size_t i = 0;
    if (!hex) return 0;
    while (i < UPDATE_AGENT_SHA256_HEX_LEN) {
        char c = hex[i];
        int ok = (c >= '0' && c <= '9') ||
                 (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F');
        if (!ok) return 0;
        ++i;
    }
    return hex[i] == '\0';
}

int update_agent_staged_requires_payload_verification(void) {
    update_agent_init(NULL);
    /* The staged digest only lands in g_status after a successful poll
     * which parses the staged manifest. Without this call, callers that
     * arm a staged update and then immediately ask whether verification
     * is required would always see an empty digest. */
    (void)update_agent_poll();
    return update_agent_g_status.staged_payload_sha256[0] ? 1 : 0;
}

/* Persistent A/B commit. The bytes handed in here are the ones this call
 * already verified against the signed manifest, so the store re-derives the
 * same digest after readback and the manager binds it to the slot header. The
 * two steps stay separate on purpose: staging publishes a VALID inactive slot
 * without changing what boots, and arming spends the generation the stage
 * produced to grant exactly one boot attempt. */
static int update_agent_commit_boot_slot(
    const struct update_manifest_view *manifest, const uint8_t *payload,
    size_t payload_len) {
    uint8_t expected[BOOT_SLOT_SHA256_SIZE];
    uint32_t slot = BOOT_SLOT_NONE;
    uint64_t staged_generation = 0u;
    uint64_t armed_generation = 0u;
    if (!manifest || !payload || payload_len == 0u ||
        update_agent_local_hex_to_bytes(manifest->payload_sha256, expected,
                                        sizeof(expected)) != 0 ||
        !manifest->version[0])
        return update_agent_apply_unsupported();
    if (x64_storage_runtime_stage_boot_payload_sha256(
            manifest->version, payload, payload_len, expected, &slot,
            &staged_generation) != 0) {
        update_agent_g_status.last_result = UPDATE_AGENT_ERR_UNSUPPORTED;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "persistent slot staging refused; inactive slot not written");
        klog(KLOG_WARN,
             "[audit] [update] stage refused: inactive slot write unavailable");
        return UPDATE_AGENT_ERR_UNSUPPORTED;
    }
    if (x64_storage_runtime_arm_boot_slot(slot, staged_generation,
                                          &armed_generation) != 0) {
        update_agent_g_status.last_result = UPDATE_AGENT_ERR_UNSUPPORTED;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "inactive slot staged but boot attempt not armed");
        klog(KLOG_ERROR,
             "[audit] [update] arm refused: staged slot left unbootable");
        return UPDATE_AGENT_ERR_UNSUPPORTED;
    }
    update_agent_g_status.last_result = 0;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "inactive slot written and armed for one boot attempt");
    klog(KLOG_INFO,
         "[audit] [update] inactive slot staged and armed for one attempt");
    return 0;
}

int update_agent_apply_boot_slot_verified(const char *actual_sha256_hex) {
    struct update_manifest_view manifest;
    uint8_t *payload = NULL;
    size_t payload_len = 0u;
    int rc = 0;
    int poll_rc = update_agent_poll();
    if (poll_rc < 0) return poll_rc;
    if (!update_agent_g_status.staged_payload_sha256[0]) {
        update_agent_g_status.last_result = -30;
        update_agent_local_copy(update_agent_g_status.summary,
                                sizeof(update_agent_g_status.summary),
                                "staged payload sha256 missing; refusing verified apply");
        klog(KLOG_WARN,
             "[audit] [update] staged payload-sha256 missing -> refused");
        return -30;
    }

    if (!actual_sha256_hex || !actual_sha256_hex[0]) {
        update_agent_g_status.last_result = -30;
        update_agent_local_copy(update_agent_g_status.summary,
                                sizeof(update_agent_g_status.summary),
                                "payload sha256 declared but verifier supplied no digest");
        klog(KLOG_WARN,
             "[audit] [update] payload-sha256 declared, no actual digest -> refused");
        return -30;
    }

    if (!update_agent_sha256_hex_is_well_formed(actual_sha256_hex)) {
        update_agent_g_status.last_result = -32;
        update_agent_local_copy(update_agent_g_status.summary,
                                sizeof(update_agent_g_status.summary),
                                "payload sha256 supplied is not a 64-char hex digest");
        klog(KLOG_WARN,
             "[audit] [update] payload-sha256 supplied malformed -> refused");
        return -32;
    }

    if (!update_agent_sha256_hex_equal(actual_sha256_hex,
                                       update_agent_g_status.staged_payload_sha256)) {
        update_agent_g_status.last_result = -31;
        update_agent_local_copy(update_agent_g_status.summary,
                                sizeof(update_agent_g_status.summary),
                                "payload sha256 mismatch; refusing to apply update");
        klog(KLOG_ERROR,
             "[audit] [update] payload-sha256 mismatch -> refused");
        return -31;
    }
    update_agent_manifest_view_reset(&manifest);
    if (update_agent_read_manifest_view(
            update_agent_g_status.staged_manifest_path, &manifest) != 0 ||
        update_agent_payload_load_verified(&manifest, &payload,
                                           &payload_len) != 0) {
        update_agent_g_status.last_result = -31;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "payload sha256 mismatch; payload cache unreadable or altered");
        klog(KLOG_ERROR,
             "[audit] [update] payload cache readback mismatch -> refused");
        return -31;
    }

    rc = update_agent_commit_boot_slot(&manifest, payload, payload_len);
    update_agent_payload_release(payload);
    return rc;
}

int update_agent_apply_cached_payload(void) {
    struct update_manifest_view manifest;
    int poll_rc = update_agent_poll();
    if (poll_rc < 0) return poll_rc;
    if (!update_agent_g_status.payload_cache_sha256[0]) {
        update_agent_g_status.last_result = -50;
        update_agent_local_copy(update_agent_g_status.summary,
                                sizeof(update_agent_g_status.summary),
                                "payload cache sha256 missing; refusing cached apply");
        klog(KLOG_WARN,
             "[audit] [update] payload cache sha256 missing -> refused");
        return -50;
    }
    update_agent_manifest_view_reset(&manifest);
    if (update_agent_read_manifest_view(
            update_agent_g_status.staged_manifest_path, &manifest) != 0 ||
        update_agent_verify_cached_payload(&manifest) != 0) {
        update_agent_g_status.last_result = -31;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "payload sha256 mismatch; payload cache unreadable or altered");
        klog(KLOG_ERROR,
             "[audit] [update] payload cache readback mismatch -> refused");
        return -31;
    }
    return update_agent_apply_boot_slot_verified(
        update_agent_g_status.payload_cache_sha256);
}

/* ---- Boot-slot lifecycle ------------------------------------------- */

static int update_agent_apply_unsupported(void) {
    update_agent_g_status.last_result = UPDATE_AGENT_ERR_UNSUPPORTED;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        "persistent update apply unsupported; verified download only");
    klog(KLOG_WARN,
         "[audit] [update] apply refused: persistent boot-slot writer unavailable");
    return UPDATE_AGENT_ERR_UNSUPPORTED;
}

int update_agent_apply_boot_slot(void) {
    int poll_rc = update_agent_poll();
    if (poll_rc < 0) return poll_rc;
    return update_agent_apply_unsupported();
}

int update_agent_confirm_health(void) {
    int rc;
    if (update_agent_health_durably_confirmed) {
        rc = update_agent_clear_stage();
        return rc < 0 ? rc : 0;
    }
    rc = x64_storage_runtime_confirm_current_boot_health();
    if (rc != 0) {
        update_agent_g_status.last_result = UPDATE_AGENT_ERR_UNSUPPORTED;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "persistent health confirmation unavailable or token mismatch");
        klog(KLOG_WARN,
             "[audit] [update] health confirm refused: provider/token unavailable");
        return UPDATE_AGENT_ERR_UNSUPPORTED;
    }
    update_agent_health_durably_confirmed = 1;
    rc = update_agent_clear_stage();
    if (rc < 0)
        return rc;
    update_agent_g_status.last_result = 0;
    update_agent_local_copy(update_agent_g_status.summary,
                            sizeof(update_agent_g_status.summary),
                            "persistent boot health confirmed");
    klog(KLOG_INFO, "[audit] [update] persistent boot health confirmed");
    return 0;
}

/* Post-reboot rollback observation. The loader owns the destructive part of
 * rollback: when the single attempt is spent without a durable health
 * confirmation it restores the confirmed slot before handing control over, and
 * marks the handoff attempt accordingly. This command therefore reports the
 * durable outcome instead of mutating metadata a second time. */
int update_agent_check_rollback(void) {
    int state = x64_storage_runtime_current_boot_rollback_check();
    update_agent_g_status.rollback_applied = 0u;
    if (state < 0) {
        update_agent_g_status.last_result = UPDATE_AGENT_ERR_UNSUPPORTED;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "persistent rollback unsupported; no boot control committed");
        klog(KLOG_WARN,
             "[audit] [update] rollback refused: loader boot control unavailable");
        return UPDATE_AGENT_ERR_UNSUPPORTED;
    }
    if (state == 2) {
        int rc = update_agent_set_pending_activation(0);
        update_agent_g_status.rollback_applied = 1u;
        update_agent_g_status.last_result = 0;
        update_agent_local_copy(
            update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
            "boot rolled back to the confirmed slot; staged update disarmed");
        klog(KLOG_WARN,
             "[audit] [update] rollback applied by loader; running confirmed slot");
        return rc < 0 ? rc : 0;
    }
    update_agent_g_status.last_result = 0;
    update_agent_local_copy(
        update_agent_g_status.summary, sizeof(update_agent_g_status.summary),
        state == 1 ? "boot attempt pending confirmation; rollback still armed"
                   : "no boot rollback pending");
    klog(KLOG_INFO, "[audit] [update] rollback check completed");
    return 0;
}
