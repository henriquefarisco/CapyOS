/* Host regression for CapyAI governed execution policy v1. */
#include "services/capyai.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, message) do {                                      \
    if (condition) printf("  ok   %s\n", message);                          \
    else { printf("  FAIL %s\n", message); ++failures; }                   \
} while (0)

static void set_digest(uint8_t digest[CAPYAI_PLAN_DIGEST_SIZE], uint8_t seed) {
    size_t i;
    for (i = 0u; i < CAPYAI_PLAN_DIGEST_SIZE; ++i) {
        digest[i] = (uint8_t)(seed + (uint8_t)i);
    }
}

static void copy_name(char *dst, size_t size, const char *src) {
    size_t i = 0u;
    while (src[i] && i + 1u < size) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void make_task(struct capyai_task_plan_metadata_v1 *task) {
    memset(task, 0, sizeof(*task));
    task->abi_version = CAPYAI_POLICY_ABI_V1;
    task->struct_size = (uint32_t)sizeof(*task);
    task->task_id = 101u;
    task->plan_id = 202u;
    task->user_id = 7u;
    task->step_index = 1u;
    task->step_count = 3u;
    task->status = CAPYAI_TASK_AWAITING_AUTHORIZATION;
    task->required_capabilities = CAPYAI_CAP_READ | CAPYAI_CAP_WRITE |
                                  CAPYAI_CAP_DELETE | CAPYAI_CAP_SERVICE;
    task->flags = CAPYAI_TASK_RESUMABLE;
    task->created_at_ms = 1000u;
    task->updated_at_ms = 1100u;
    task->expires_at_ms = 9000u;
    set_digest(task->plan_digest, 0x31u);
}

static void make_write_tool(struct capyai_tool_manifest_v1 *tool) {
    memset(tool, 0, sizeof(*tool));
    tool->abi_version = CAPYAI_POLICY_ABI_V1;
    tool->struct_size = (uint32_t)sizeof(*tool);
    tool->schema_version = 1u;
    tool->adapter_kind = CAPYAI_TOOL_ADAPTER_VFS;
    tool->required_capabilities = CAPYAI_CAP_WRITE;
    tool->flags = CAPYAI_TOOL_MUTATING | CAPYAI_TOOL_IDEMPOTENT |
                  CAPYAI_TOOL_SUPPORTS_DRY_RUN | CAPYAI_TOOL_SUPPORTS_ROLLBACK;
    tool->timeout_ms = 5000u;
    copy_name(tool->name, sizeof(tool->name), "vfs.file.write");
    copy_name(tool->verifier, sizeof(tool->verifier), "vfs.file.hash");
    copy_name(tool->rollback, sizeof(tool->rollback), "vfs.file.restore");
}

static void make_grant(struct capyai_capability_grant_v1 *grant,
                       const struct capyai_task_plan_metadata_v1 *task) {
    memset(grant, 0, sizeof(*grant));
    grant->abi_version = CAPYAI_POLICY_ABI_V1;
    grant->struct_size = (uint32_t)sizeof(*grant);
    grant->grant_id = 303u;
    grant->task_id = task->task_id;
    grant->plan_id = task->plan_id;
    grant->user_id = task->user_id;
    grant->capabilities = task->required_capabilities;
    grant->step_mask = ((uint64_t)1u << 0) | ((uint64_t)1u << 1) |
                       ((uint64_t)1u << 2);
    grant->issued_at_ms = 1200u;
    grant->expires_at_ms = 8000u;
    memcpy(grant->plan_digest, task->plan_digest, CAPYAI_PLAN_DIGEST_SIZE);
}

static uint32_t evaluate(const struct capyai_task_plan_metadata_v1 *task,
                         const struct capyai_tool_manifest_v1 *tool,
                         const struct capyai_capability_grant_v1 *grant,
                         uint64_t now_ms,
                         struct capyai_policy_result_v1 *result) {
    CHECK(capyai_policy_evaluate_v1(task, tool, grant, now_ms, result) == 0,
          "policy evaluation returns a structured outcome");
    return result->decision;
}

int main(void) {
    struct capyai_task_plan_metadata_v1 task;
    struct capyai_tool_manifest_v1 tool;
    struct capyai_capability_grant_v1 grant;
    struct capyai_policy_result_v1 result;
    struct capyai_perms legacy_none = {0, 0, 0};
    struct capyai_perms legacy_all = {1, 1, 1};
    capyai_capability_mask_t mask;

    printf("[test_capyai_policy]\n");

    /* Existing ABI/GUI contract remains unchanged. */
    CHECK(sizeof(struct capyai_perms) == 3u * sizeof(int),
          "legacy capyai_perms layout is unchanged");
    CHECK(offsetof(struct capyai_perms, allow_write) == 0u &&
              offsetof(struct capyai_perms, allow_delete) == sizeof(int) &&
              offsetof(struct capyai_perms, allow_system_change) ==
                  2u * sizeof(int),
          "legacy permission field offsets are unchanged");
    CHECK(offsetof(struct capyai_session, last_file) == 0u &&
              offsetof(struct capyai_session, turns) == CAPYAI_PATH_MAX,
          "legacy session prefix is unchanged");

    mask = capyai_capabilities_from_legacy_perms(&legacy_none);
    CHECK(mask == CAPYAI_CAP_READ, "legacy no-permission mode maps to read only");
    mask = capyai_capabilities_from_legacy_perms(&legacy_all);
    CHECK(mask == CAPYAI_CAP_ALL,
          "legacy broad toggles map deterministically without implicit grant");
    CHECK(capyai_capabilities_valid_v1(CAPYAI_CAP_ALL) &&
              !capyai_capabilities_valid_v1(CAPYAI_CAP_ALL | (1u << 20)),
          "unknown capabilities fail closed");

    make_task(&task);
    make_write_tool(&tool);
    make_grant(&grant, &task);
    CHECK(capyai_task_plan_metadata_valid_v1(&task),
          "versioned task/plan metadata validates");
    task.step_count = CAPYAI_MAX_PLAN_STEPS + 1u;
    CHECK(!capyai_task_plan_metadata_valid_v1(&task),
          "plans above the five-step bound fail closed");
    make_task(&task);
    CHECK(capyai_tool_manifest_valid_v1(&tool),
          "typed VFS tool manifest validates");
    CHECK(capyai_capability_grant_valid_v1(&grant),
          "scoped capability grant validates");
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_ALLOWED && result.missing_capabilities == 0u,
          "matching user, plan, step and capability are allowed");

    tool.adapter_kind = 0u;
    CHECK(!capyai_tool_manifest_valid_v1(&tool),
          "manifest has no arbitrary-shell adapter");
    make_write_tool(&tool);
    tool.flags &= ~CAPYAI_TOOL_SUPPORTS_DRY_RUN;
    CHECK(!capyai_tool_manifest_valid_v1(&tool),
          "mutating tool without dry-run support is invalid");
    make_write_tool(&tool);
    tool.verifier[0] = '\0';
    CHECK(!capyai_tool_manifest_valid_v1(&tool),
          "tool without verifier is invalid");
    make_write_tool(&tool);
    tool.required_capabilities |= CAPYAI_CAP_NETWORK;
    CHECK(!capyai_tool_manifest_valid_v1(&tool),
          "typed adapter rejects capabilities from another adapter");

    make_write_tool(&tool);
    grant.capabilities &= ~CAPYAI_CAP_WRITE;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_CAPABILITY &&
              result.missing_capabilities == CAPYAI_CAP_WRITE,
          "missing granular capability is reported and blocked");

    make_grant(&grant, &task);
    grant.user_id++;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_SUBJECT,
          "grant cannot cross users");
    make_grant(&grant, &task);
    grant.task_id++;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_SUBJECT,
          "grant cannot cross tasks");
    make_grant(&grant, &task);
    grant.plan_id++;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_PLAN,
          "grant cannot cross plans");
    make_grant(&grant, &task);
    grant.plan_digest[3] ^= 0x55u;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_PLAN,
          "modified plan digest invalidates authorization");
    make_grant(&grant, &task);
    grant.step_mask &= ~((uint64_t)1u << task.step_index);
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_STEP,
          "unapproved plan step is blocked");

    make_grant(&grant, &task);
    CHECK(evaluate(&task, &tool, &grant, 1100u, &result) ==
              CAPYAI_POLICY_DENIED_NOT_YET_VALID,
          "grant cannot be used before issuance");
    CHECK(evaluate(&task, &tool, &grant, 8000u, &result) ==
              CAPYAI_POLICY_DENIED_EXPIRED,
          "expired grant is blocked");
    grant.flags = CAPYAI_GRANT_REVOKED;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_REVOKED,
          "revoked grant is blocked immediately");

    /* Critical delete is declared as typed + elevated, then requires a
     * separately elevated user grant at evaluation time. */
    make_write_tool(&tool);
    copy_name(tool.name, sizeof(tool.name), "vfs.path.delete");
    copy_name(tool.verifier, sizeof(tool.verifier), "vfs.path.absent");
    copy_name(tool.rollback, sizeof(tool.rollback), "vfs.trash.restore");
    tool.required_capabilities = CAPYAI_CAP_DELETE;
    tool.flags |= CAPYAI_TOOL_REQUIRES_ELEVATION;
    CHECK(capyai_tool_manifest_valid_v1(&tool),
          "critical delete manifest explicitly requires elevation");
    make_grant(&grant, &task);
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_ELEVATION,
          "critical action is blocked without elevated grant");
    grant.flags = CAPYAI_GRANT_ELEVATED;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_ALLOWED,
          "critical action runs only with explicit elevation");
    tool.flags &= ~CAPYAI_TOOL_REQUIRES_ELEVATION;
    CHECK(!capyai_tool_manifest_valid_v1(&tool),
          "critical manifest cannot omit elevation declaration");

    make_write_tool(&tool);
    make_grant(&grant, &task);
    grant.capabilities |= CAPYAI_CAP_NETWORK;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_INVALID_PLAN,
          "grant cannot add capabilities absent from approved plan");
    make_grant(&grant, &task);
    grant.step_mask |= ((uint64_t)1u << 12);
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_INVALID_PLAN,
          "grant cannot authorize nonexistent steps");
    make_grant(&grant, &task);
    task.status = CAPYAI_TASK_VERIFIED;
    CHECK(evaluate(&task, &tool, &grant, 2000u, &result) ==
              CAPYAI_POLICY_DENIED_STATE,
          "terminal task cannot execute another tool");

    CHECK(capyai_policy_evaluate_v1(&task, &tool, &grant, 2000u, NULL) < 0,
          "NULL policy result is rejected");

    if (failures == 0) {
        printf("[test_capyai_policy] all passed\n");
        return 0;
    }
    printf("[test_capyai_policy] %d failure(s)\n", failures);
    return 1;
}
