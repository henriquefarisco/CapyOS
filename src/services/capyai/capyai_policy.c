/* CapyAI governed execution policy v1.
 *
 * Pure validation and authorization logic: no allocation, global state, VFS
 * or shell dispatch.  A model may propose metadata, but only a caller-owned
 * capability grant can authorize a typed tool invocation.
 */
#include "services/capyai.h"

static void capyai_policy_zero(void *ptr, size_t size) {
    unsigned char *p = (unsigned char *)ptr;
    size_t i;
    if (!p) return;
    for (i = 0u; i < size; ++i) p[i] = 0u;
}

static void capyai_policy_copy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0u;
    if (!dst || dst_size == 0u) return;
    while (src && src[i] && i + 1u < dst_size) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int bounded_nonempty(const char *value, size_t capacity) {
    size_t i;
    if (!value || capacity == 0u || value[0] == '\0') return 0;
    for (i = 0u; i < capacity; ++i) {
        if (value[i] == '\0') return 1;
    }
    return 0;
}

static int bounded_empty(const char *value, size_t capacity) {
    (void)capacity;
    return value && value[0] == '\0';
}

static int digest_nonzero(const uint8_t digest[CAPYAI_PLAN_DIGEST_SIZE]) {
    uint8_t bits = 0u;
    size_t i;
    if (!digest) return 0;
    for (i = 0u; i < CAPYAI_PLAN_DIGEST_SIZE; ++i) bits |= digest[i];
    return bits != 0u;
}

static int digest_equal(const uint8_t a[CAPYAI_PLAN_DIGEST_SIZE],
                        const uint8_t b[CAPYAI_PLAN_DIGEST_SIZE]) {
    uint8_t difference = 0u;
    size_t i;
    for (i = 0u; i < CAPYAI_PLAN_DIGEST_SIZE; ++i) {
        difference |= (uint8_t)(a[i] ^ b[i]);
    }
    return difference == 0u;
}

static uint64_t valid_step_mask(uint32_t step_count) {
    if (step_count >= 64u) return UINT64_MAX;
    return (((uint64_t)1u << step_count) - (uint64_t)1u);
}

static int task_status_valid(uint32_t status) {
    return status <= (uint32_t)CAPYAI_TASK_NEEDS_CLARIFICATION;
}

static int task_status_executable(uint32_t status) {
    return status == (uint32_t)CAPYAI_TASK_PLANNED ||
           status == (uint32_t)CAPYAI_TASK_AWAITING_AUTHORIZATION ||
           status == (uint32_t)CAPYAI_TASK_RUNNING;
}

int capyai_capabilities_valid_v1(capyai_capability_mask_t capabilities) {
    return (capabilities & ~CAPYAI_CAP_ALL) == 0u;
}

int capyai_task_plan_metadata_valid_v1(
    const struct capyai_task_plan_metadata_v1 *task) {
    if (!task || task->abi_version != CAPYAI_POLICY_ABI_V1 ||
        task->struct_size < (uint32_t)sizeof(*task)) return 0;
    if (task->task_id == 0u || task->plan_id == 0u ||
        !digest_nonzero(task->plan_digest)) return 0;
    if (task->step_count == 0u ||
        task->step_count > CAPYAI_MAX_PLAN_STEPS ||
        task->step_index >= task->step_count) return 0;
    if (!task_status_valid(task->status) ||
        !capyai_capabilities_valid_v1(task->required_capabilities) ||
        task->required_capabilities == CAPYAI_CAP_NONE ||
        (task->flags & ~CAPYAI_TASK_FLAGS_ALL) != 0u) return 0;
    if (task->created_at_ms > task->updated_at_ms ||
        task->expires_at_ms <= task->created_at_ms ||
        task->expires_at_ms < task->updated_at_ms) return 0;
    return 1;
}

static capyai_capability_mask_t adapter_capability(uint32_t adapter_kind) {
    switch (adapter_kind) {
    case CAPYAI_TOOL_ADAPTER_VFS:
        return CAPYAI_CAP_READ | CAPYAI_CAP_WRITE | CAPYAI_CAP_EDIT |
               CAPYAI_CAP_DELETE;
    case CAPYAI_TOOL_ADAPTER_NETWORK:  return CAPYAI_CAP_NETWORK;
    case CAPYAI_TOOL_ADAPTER_PACKAGE:
        return CAPYAI_CAP_PACKAGE_INSTALL | CAPYAI_CAP_PACKAGE_REMOVE;
    case CAPYAI_TOOL_ADAPTER_SERVICE:  return CAPYAI_CAP_SERVICE;
    case CAPYAI_TOOL_ADAPTER_UPDATE:   return CAPYAI_CAP_UPDATE;
    case CAPYAI_TOOL_ADAPTER_SETTINGS: return CAPYAI_CAP_SYSTEM_SETTINGS;
    case CAPYAI_TOOL_ADAPTER_POWER:    return CAPYAI_CAP_POWER;
    case CAPYAI_TOOL_ADAPTER_USER:     return CAPYAI_CAP_USER;
    case CAPYAI_TOOL_ADAPTER_SECURITY: return CAPYAI_CAP_SECURITY;
    default:                            return CAPYAI_CAP_NONE;
    }
}

int capyai_tool_manifest_valid_v1(
    const struct capyai_tool_manifest_v1 *tool) {
    capyai_capability_mask_t adapter_caps;
    capyai_capability_mask_t mutation_caps =
        CAPYAI_CAP_WRITE | CAPYAI_CAP_EDIT | CAPYAI_CAP_DELETE |
        CAPYAI_CAP_PACKAGE_INSTALL | CAPYAI_CAP_PACKAGE_REMOVE |
        CAPYAI_CAP_SERVICE | CAPYAI_CAP_UPDATE |
        CAPYAI_CAP_SYSTEM_SETTINGS | CAPYAI_CAP_POWER |
        CAPYAI_CAP_USER | CAPYAI_CAP_SECURITY;
    if (!tool || tool->abi_version != CAPYAI_POLICY_ABI_V1 ||
        tool->struct_size < (uint32_t)sizeof(*tool)) return 0;
    if (tool->schema_version == 0u || tool->timeout_ms == 0u ||
        tool->reserved != 0u ||
        !capyai_capabilities_valid_v1(tool->required_capabilities) ||
        tool->required_capabilities == CAPYAI_CAP_NONE ||
        (tool->flags & ~CAPYAI_TOOL_FLAGS_ALL) != 0u ||
        !bounded_nonempty(tool->name, sizeof(tool->name)) ||
        !bounded_nonempty(tool->verifier, sizeof(tool->verifier))) return 0;

    adapter_caps = adapter_capability(tool->adapter_kind);
    /* A manifest is owned by exactly one typed adapter.  Requiring only an
     * intersection here would let a VFS manifest smuggle unrelated network,
     * service or administration capabilities alongside a valid VFS bit. */
    if (adapter_caps == CAPYAI_CAP_NONE ||
        (tool->required_capabilities & ~adapter_caps) != 0u) return 0;
    if ((tool->required_capabilities & mutation_caps) != 0u &&
        (tool->flags & CAPYAI_TOOL_MUTATING) == 0u) return 0;
    if ((tool->flags & CAPYAI_TOOL_MUTATING) != 0u &&
        (tool->flags & CAPYAI_TOOL_SUPPORTS_DRY_RUN) == 0u) return 0;
    if ((tool->required_capabilities & CAPYAI_CAP_CRITICAL_MASK) != 0u &&
        (tool->flags & CAPYAI_TOOL_REQUIRES_ELEVATION) == 0u) return 0;
    if ((tool->flags & CAPYAI_TOOL_SUPPORTS_ROLLBACK) != 0u) {
        if (!bounded_nonempty(tool->rollback, sizeof(tool->rollback))) return 0;
    } else if (!bounded_empty(tool->rollback, sizeof(tool->rollback))) {
        return 0;
    }
    return 1;
}

int capyai_capability_grant_valid_v1(
    const struct capyai_capability_grant_v1 *grant) {
    if (!grant || grant->abi_version != CAPYAI_POLICY_ABI_V1 ||
        grant->struct_size < (uint32_t)sizeof(*grant)) return 0;
    if (grant->grant_id == 0u || grant->task_id == 0u ||
        grant->plan_id == 0u || !digest_nonzero(grant->plan_digest) ||
        grant->step_mask == 0u || grant->reserved != 0u) return 0;
    if (!capyai_capabilities_valid_v1(grant->capabilities) ||
        grant->capabilities == CAPYAI_CAP_NONE ||
        (grant->flags & ~CAPYAI_GRANT_FLAGS_ALL) != 0u) return 0;
    if (grant->expires_at_ms <= grant->issued_at_ms) return 0;
    return 1;
}

capyai_capability_mask_t capyai_capabilities_from_legacy_perms(
    const struct capyai_perms *perms) {
    capyai_capability_mask_t capabilities = CAPYAI_CAP_READ;
    if (!perms) return capabilities;
    if (perms->allow_write) {
        capabilities |= CAPYAI_CAP_WRITE | CAPYAI_CAP_EDIT | CAPYAI_CAP_NETWORK;
    }
    if (perms->allow_delete) capabilities |= CAPYAI_CAP_DELETE;
    if (perms->allow_system_change) {
        capabilities |= CAPYAI_CAP_PACKAGE_INSTALL |
                        CAPYAI_CAP_PACKAGE_REMOVE | CAPYAI_CAP_SERVICE |
                        CAPYAI_CAP_UPDATE | CAPYAI_CAP_SYSTEM_SETTINGS |
                        CAPYAI_CAP_POWER | CAPYAI_CAP_USER |
                        CAPYAI_CAP_SECURITY;
    }
    return capabilities;
}

static void policy_result(struct capyai_policy_result_v1 *result,
                          uint32_t decision,
                          capyai_capability_mask_t required,
                          capyai_capability_mask_t missing,
                          const char *reason) {
    result->decision = decision;
    result->required_capabilities = required;
    result->missing_capabilities = missing;
    capyai_policy_copy(result->reason, sizeof(result->reason), reason);
}

int capyai_policy_evaluate_v1(
    const struct capyai_task_plan_metadata_v1 *task,
    const struct capyai_tool_manifest_v1 *tool,
    const struct capyai_capability_grant_v1 *grant,
    uint64_t now_ms,
    struct capyai_policy_result_v1 *result) {
    capyai_capability_mask_t missing;
    uint64_t approved_steps;

    if (!result) return -1;
    capyai_policy_zero(result, sizeof(*result));
    result->abi_version = CAPYAI_POLICY_ABI_V1;
    result->struct_size = (uint32_t)sizeof(*result);
    policy_result(result, CAPYAI_POLICY_INVALID_INPUT, CAPYAI_CAP_NONE,
                  CAPYAI_CAP_NONE, "Entrada de politica invalida.");

    if (!task || !tool || !grant) return 0;
    if (!capyai_task_plan_metadata_valid_v1(task)) {
        policy_result(result, CAPYAI_POLICY_INVALID_PLAN, CAPYAI_CAP_NONE,
                      CAPYAI_CAP_NONE, "Metadados de tarefa/plano invalidos.");
        return 0;
    }
    if (!capyai_tool_manifest_valid_v1(tool)) {
        policy_result(result, CAPYAI_POLICY_INVALID_MANIFEST,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Manifesto de ferramenta invalido ou nao tipado.");
        return 0;
    }
    if (!capyai_capability_grant_valid_v1(grant)) return 0;

    if (!task_status_executable(task->status)) {
        policy_result(result, CAPYAI_POLICY_DENIED_STATE,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Estado da tarefa nao permite execucao.");
        return 0;
    }
    if ((tool->required_capabilities & ~task->required_capabilities) != 0u ||
        (grant->capabilities & ~task->required_capabilities) != 0u) {
        policy_result(result, CAPYAI_POLICY_INVALID_PLAN,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Ferramenta ou concessao amplia o escopo do plano.");
        return 0;
    }
    approved_steps = valid_step_mask(task->step_count);
    if ((grant->step_mask & ~approved_steps) != 0u) {
        policy_result(result, CAPYAI_POLICY_INVALID_PLAN,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Concessao referencia etapa inexistente no plano.");
        return 0;
    }
    if ((grant->flags & CAPYAI_GRANT_REVOKED) != 0u) {
        policy_result(result, CAPYAI_POLICY_DENIED_REVOKED,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Autorizacao revogada pelo usuario.");
        return 0;
    }
    if (now_ms < grant->issued_at_ms) {
        policy_result(result, CAPYAI_POLICY_DENIED_NOT_YET_VALID,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Autorizacao ainda nao e valida.");
        return 0;
    }
    if (now_ms >= grant->expires_at_ms || now_ms >= task->expires_at_ms) {
        policy_result(result, CAPYAI_POLICY_DENIED_EXPIRED,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Autorizacao ou tarefa expirada.");
        return 0;
    }
    if (grant->user_id != task->user_id || grant->task_id != task->task_id) {
        policy_result(result, CAPYAI_POLICY_DENIED_SUBJECT,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Autorizacao pertence a outro usuario ou tarefa.");
        return 0;
    }
    if (grant->plan_id != task->plan_id ||
        !digest_equal(grant->plan_digest, task->plan_digest)) {
        policy_result(result, CAPYAI_POLICY_DENIED_PLAN,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Plano foi alterado ou nao corresponde a autorizacao.");
        return 0;
    }
    if ((grant->step_mask & ((uint64_t)1u << task->step_index)) == 0u) {
        policy_result(result, CAPYAI_POLICY_DENIED_STEP,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "Etapa nao autorizada pelo usuario.");
        return 0;
    }

    missing = tool->required_capabilities & ~grant->capabilities;
    if (missing != CAPYAI_CAP_NONE) {
        policy_result(result, CAPYAI_POLICY_DENIED_CAPABILITY,
                      tool->required_capabilities, missing,
                      "Falta capacidade explicita para a ferramenta.");
        return 0;
    }
    if (((tool->required_capabilities & CAPYAI_CAP_CRITICAL_MASK) != 0u ||
         (tool->flags & CAPYAI_TOOL_REQUIRES_ELEVATION) != 0u) &&
        (grant->flags & CAPYAI_GRANT_ELEVATED) == 0u) {
        policy_result(result, CAPYAI_POLICY_DENIED_ELEVATION,
                      tool->required_capabilities, CAPYAI_CAP_NONE,
                      "A etapa critica exige elevacao explicita.");
        return 0;
    }

    policy_result(result, CAPYAI_POLICY_ALLOWED,
                  tool->required_capabilities, CAPYAI_CAP_NONE,
                  "Autorizacao valida para esta etapa.");
    return 0;
}
