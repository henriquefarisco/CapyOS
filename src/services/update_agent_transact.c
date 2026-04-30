/*
 * Update agent — transactional integration with the boot slot system.
 *
 * Responsibilities of this translation unit:
 *  - update_agent_apply_boot_slot           (legacy applier)
 *  - update_agent_apply_boot_slot_verified  (M6.4 payload sha256 gate)
 *  - update_agent_staged_requires_payload_verification
 *  - update_agent_confirm_health
 *  - update_agent_check_rollback
 *
 * The catalog/manifest parsing, repository configuration, IO and the
 * stage/import/poll machinery live in src/services/update_agent.c. The
 * two TUs share the runtime status struct and the small string helper
 * through src/services/internal/update_agent_internal.h.
 *
 * Splitting the agent in two keeps each TU well below the monolith
 * threshold without forcing the rest of the codebase to know about the
 * split: the public API in include/services/update_agent.h is unchanged.
 */
#include "internal/update_agent_internal.h"

#include "boot/boot_slot.h"
#include "kernel/log/klog.h"

#include <stddef.h>

/* Forward declaration: defined in update_agent.c. We do not pull in the
 * whole include just for this one symbol because update_agent_transact.c
 * has no other reason to know about manifest IO. */
int update_agent_poll(void);
int update_agent_clear_stage(void);
int update_agent_set_pending_activation(int enabled);
void update_agent_init(const char *current_version);

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

int update_agent_apply_boot_slot_verified(const char *actual_sha256_hex) {
    if (update_agent_poll() < 0) return -1;
    if (!update_agent_g_status.staged_payload_sha256[0]) {
        /* Manifest did not declare a digest: legacy fallback path. We log
         * it as INFO so the audit grep-er sees the non-verified
         * application. */
        klog(KLOG_INFO,
             "[update] payload sha256 not declared; applying without verification");
        return update_agent_apply_boot_slot();
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

    klog(KLOG_INFO,
         "[audit] [update] payload-sha256 match -> applying staged update");
    return update_agent_apply_boot_slot();
}

/* ---- Boot-slot lifecycle ------------------------------------------- */

int update_agent_apply_boot_slot(void) {
    struct boot_slot s0;
    uint32_t next_slot;

    if (update_agent_poll() < 0) return -1;
    if (!update_agent_g_status.stage_ready ||
        !update_agent_g_status.pending_activation) {
        return -2;
    }
    if (!update_agent_g_status.staged_version[0]) {
        return -3;
    }

    /* Pick the slot that is NOT currently ACTIVE. */
    if (boot_slot_get(0, &s0) == 0 && s0.state == BOOT_SLOT_ACTIVE) {
        next_slot = 1u;
    } else {
        next_slot = 0u;
    }

    if (boot_slot_stage(next_slot, update_agent_g_status.staged_version, 0u) != 0) {
        klog(KLOG_ERROR, "[update] Failed to stage boot slot for activation.");
        return -4;
    }
    if (boot_slot_activate(next_slot) != 0) {
        klog(KLOG_ERROR, "[update] Failed to activate boot slot.");
        return -5;
    }

    klog(KLOG_INFO, "[update] Boot slot armed for transactional update.");
    return 0;
}

int update_agent_confirm_health(void) {
    if (boot_slot_confirm_health() != 0) {
        klog(KLOG_WARN, "[update] Boot slot health confirm failed.");
        return -1;
    }
    if (update_agent_g_status.pending_activation) {
        update_agent_set_pending_activation(0);
    }
    klog(KLOG_INFO, "[update] Boot health confirmed; update committed.");
    return 0;
}

int update_agent_check_rollback(void) {
    if (!boot_slot_needs_rollback()) {
        return 0;
    }
    klog(KLOG_WARN, "[update] Unhealthy boot detected; initiating rollback.");
    if (boot_slot_rollback() != 0) {
        klog(KLOG_ERROR, "[update] Boot slot rollback failed.");
        return -1;
    }
    update_agent_clear_stage();
    klog(KLOG_INFO, "[update] Rollback complete; staged update cleared.");
    return 1;
}
