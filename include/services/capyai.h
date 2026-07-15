/*
 * services/capyai.h — CapyOS in-tree adapter for the capy-ai-core ABI.
 *
 * This is the CapyOS-owned boundary that consumes the decoupled CapyAI
 * inference core (../CapyAI/src/core, ABI "capy-ai-core" v0). It mirrors the
 * capybrowse/capy-browser-core pattern: the sibling owns the model + portable
 * inference; CapyOS owns the executor that turns a prediction into a gated,
 * auditable action against the real system (VFS/shell/capypkg).
 *
 * Split of concerns:
 *   - capy_ai_predict() (sibling core)  -> intent => {action,command,risk,...}
 *   - capyai_plan()      (this adapter) -> apply risk gates + denylist +
 *                                          clarification + session memory
 *   - caller (shell/gfx)                -> dispatch an ALLOWED command to the
 *                                          existing capysh command runner, then
 *                                          report the outcome back
 *   - capyai_summary()   (this adapter) -> PT-BR natural-language summary
 *
 * The planning + summary layer is pure logic (no VFS, no allocation, no
 * globals) so it is fully host-testable. Real side effects run through the
 * caller's shell dispatch (mkdir/ls/cat/rm/capy ... already exist as gated
 * commands); the executor never touches the filesystem itself.
 */
#ifndef SERVICES_CAPYAI_H
#define SERVICES_CAPYAI_H

#include <stddef.h>
#include <stdint.h>

#include "capy_ai_core.h" /* ABI: -I ../CapyAI/src/core (Makefile sibling) */

#define CAPYAI_PATH_MAX 192u
#define CAPYAI_REASON_MAX 160u
#define CAPYAI_SUMMARY_MAX 320u
#define CAPYAI_COMMAND_MAX 256u

/* Canonical activation module name (mirrors module_gate.c). */
#define CAPYAI_MODULE_NAME "org.capyos.ai.assistant"

/*
 * Legacy permission toggles. read_only always runs. These correspond to the
 * three task-scoped graphical buttons ("Escrita", "Deletar", "Sistema") and
 * the shell flags (--yes, --allow-delete, --allow-system-change). Writes and
 * edits share one legacy write gate; delete and system_change are separate,
 * more dangerous gates. New orchestrators use the granular v1 capabilities
 * declared below.
 */
struct capyai_perms {
    int allow_write;          /* write_file + network (create/edit/append) */
    int allow_delete;         /* file/dir deletion */
    int allow_system_change;  /* system_change (pkg install/remove, etc.) */
};

/* One-session memory: last file created/edited (for "adicione neste arquivo"
 * style follow-ups) + a turn counter. Caller owns and persists it. */
struct capyai_session {
    char last_file[CAPYAI_PATH_MAX];
    uint32_t turns;
};

/*
 * Governed execution policy v1.
 *
 * This API is deliberately additive: the legacy structs and entry points
 * above/below keep their original layout and semantics so existing CapyAI
 * shell and GUI clients remain ABI-compatible.  New orchestrators should use
 * these versioned contracts to bind a typed tool invocation to an immutable
 * plan, one user, an explicit set of steps and an expiring capability grant.
 * No field in these structs is model-controlled after user authorization.
 */
#define CAPYAI_POLICY_ABI_V1 1u
#define CAPYAI_PLAN_DIGEST_SIZE 32u
#define CAPYAI_TOOL_NAME_MAX 48u
#define CAPYAI_TOOL_HOOK_MAX 48u
#define CAPYAI_MAX_PLAN_STEPS 5u

typedef uint32_t capyai_capability_mask_t;

#define CAPYAI_CAP_NONE      ((capyai_capability_mask_t)0u)
#define CAPYAI_CAP_READ      ((capyai_capability_mask_t)(1u << 0))
#define CAPYAI_CAP_WRITE     ((capyai_capability_mask_t)(1u << 1))
#define CAPYAI_CAP_EDIT      ((capyai_capability_mask_t)(1u << 2))
#define CAPYAI_CAP_DELETE    ((capyai_capability_mask_t)(1u << 3))
#define CAPYAI_CAP_NETWORK   ((capyai_capability_mask_t)(1u << 4))
#define CAPYAI_CAP_PACKAGE_INSTALL ((capyai_capability_mask_t)(1u << 5))
#define CAPYAI_CAP_PACKAGE_REMOVE  ((capyai_capability_mask_t)(1u << 6))
#define CAPYAI_CAP_SERVICE         ((capyai_capability_mask_t)(1u << 7))
#define CAPYAI_CAP_UPDATE          ((capyai_capability_mask_t)(1u << 8))
#define CAPYAI_CAP_SYSTEM_SETTINGS ((capyai_capability_mask_t)(1u << 9))
#define CAPYAI_CAP_POWER           ((capyai_capability_mask_t)(1u << 10))
#define CAPYAI_CAP_USER            ((capyai_capability_mask_t)(1u << 11))
#define CAPYAI_CAP_SECURITY        ((capyai_capability_mask_t)(1u << 12))
#define CAPYAI_CAP_ALL             ((capyai_capability_mask_t)((1u << 13) - 1u))

/* These capabilities always require an explicitly elevated grant. */
#define CAPYAI_CAP_CRITICAL_MASK \
    ((capyai_capability_mask_t)(CAPYAI_CAP_DELETE | \
                                CAPYAI_CAP_PACKAGE_INSTALL | \
                                CAPYAI_CAP_PACKAGE_REMOVE | \
                                CAPYAI_CAP_UPDATE | CAPYAI_CAP_POWER | \
                                CAPYAI_CAP_USER | CAPYAI_CAP_SECURITY))

enum capyai_task_status_v1 {
    CAPYAI_TASK_PLANNED = 0,
    CAPYAI_TASK_AWAITING_AUTHORIZATION,
    CAPYAI_TASK_RUNNING,
    CAPYAI_TASK_VERIFIED,
    CAPYAI_TASK_FAILED,
    CAPYAI_TASK_ROLLED_BACK,
    CAPYAI_TASK_CANCELLED,
    CAPYAI_TASK_NEEDS_CLARIFICATION,
};

enum capyai_tool_adapter_v1 {
    CAPYAI_TOOL_ADAPTER_VFS = 1,
    CAPYAI_TOOL_ADAPTER_NETWORK,
    CAPYAI_TOOL_ADAPTER_PACKAGE,
    CAPYAI_TOOL_ADAPTER_SERVICE,
    CAPYAI_TOOL_ADAPTER_UPDATE,
    CAPYAI_TOOL_ADAPTER_SETTINGS,
    CAPYAI_TOOL_ADAPTER_POWER,
    CAPYAI_TOOL_ADAPTER_USER,
    CAPYAI_TOOL_ADAPTER_SECURITY,
};

#define CAPYAI_TOOL_MUTATING            (1u << 0)
#define CAPYAI_TOOL_IDEMPOTENT          (1u << 1)
#define CAPYAI_TOOL_SUPPORTS_DRY_RUN    (1u << 2)
#define CAPYAI_TOOL_SUPPORTS_ROLLBACK   (1u << 3)
#define CAPYAI_TOOL_REQUIRES_ELEVATION  (1u << 4)
#define CAPYAI_TOOL_FLAGS_ALL           ((1u << 5) - 1u)

#define CAPYAI_TASK_DRY_RUN              (1u << 0)
#define CAPYAI_TASK_GENERATIVE_PROPOSAL  (1u << 1)
#define CAPYAI_TASK_RESUMABLE            (1u << 2)
#define CAPYAI_TASK_FLAGS_ALL            ((1u << 3) - 1u)

#define CAPYAI_GRANT_ELEVATED  (1u << 0)
#define CAPYAI_GRANT_REVOKED   (1u << 1)
#define CAPYAI_GRANT_FLAGS_ALL ((1u << 2) - 1u)

/* Persisted task/plan identity and current checkpoint.  plan_digest is the
 * SHA-256 digest of the canonical approved TaskPlan v1 representation. */
struct capyai_task_plan_metadata_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t task_id;
    uint64_t plan_id;
    uint32_t user_id;
    uint32_t step_index;
    uint32_t step_count;
    uint32_t status; /* enum capyai_task_status_v1 */
    capyai_capability_mask_t required_capabilities;
    uint32_t flags;
    uint64_t created_at_ms;
    uint64_t updated_at_ms;
    uint64_t expires_at_ms;
    uint8_t plan_digest[CAPYAI_PLAN_DIGEST_SIZE];
};

/* Typed tool declaration.  adapter_kind intentionally has no arbitrary-shell
 * value.  Mutating tools must advertise dry-run support and every tool must
 * name a verifier; rollback is explicit when supported. */
struct capyai_tool_manifest_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t adapter_kind; /* enum capyai_tool_adapter_v1 */
    capyai_capability_mask_t required_capabilities;
    uint32_t flags;
    uint32_t timeout_ms;
    uint32_t reserved;
    char name[CAPYAI_TOOL_NAME_MAX];
    char verifier[CAPYAI_TOOL_HOOK_MAX];
    char rollback[CAPYAI_TOOL_HOOK_MAX];
};

/* User-issued authorization.  step_mask scopes approval to the plan's
 * bounded step_count (currently at most CAPYAI_MAX_PLAN_STEPS).  The grant
 * cannot authorize capabilities absent from the approved plan and cannot be
 * renewed or elevated by the model/tool result. */
struct capyai_capability_grant_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t grant_id;
    uint64_t task_id;
    uint64_t plan_id;
    uint32_t user_id;
    capyai_capability_mask_t capabilities;
    uint32_t flags;
    uint32_t reserved;
    uint64_t step_mask;
    uint64_t issued_at_ms;
    uint64_t expires_at_ms;
    uint8_t plan_digest[CAPYAI_PLAN_DIGEST_SIZE];
};

enum capyai_policy_decision_v1 {
    CAPYAI_POLICY_ALLOWED = 0,
    CAPYAI_POLICY_INVALID_INPUT,
    CAPYAI_POLICY_INVALID_MANIFEST,
    CAPYAI_POLICY_INVALID_PLAN,
    CAPYAI_POLICY_DENIED_STATE,
    CAPYAI_POLICY_DENIED_REVOKED,
    CAPYAI_POLICY_DENIED_NOT_YET_VALID,
    CAPYAI_POLICY_DENIED_EXPIRED,
    CAPYAI_POLICY_DENIED_SUBJECT,
    CAPYAI_POLICY_DENIED_PLAN,
    CAPYAI_POLICY_DENIED_STEP,
    CAPYAI_POLICY_DENIED_CAPABILITY,
    CAPYAI_POLICY_DENIED_ELEVATION,
};

struct capyai_policy_result_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t decision; /* enum capyai_policy_decision_v1 */
    capyai_capability_mask_t required_capabilities;
    capyai_capability_mask_t missing_capabilities;
    uint32_t reserved;
    char reason[CAPYAI_REASON_MAX];
};

/* Validation helpers are pure, allocation-free and safe for host/kernel use. */
int capyai_capabilities_valid_v1(capyai_capability_mask_t capabilities);
int capyai_task_plan_metadata_valid_v1(
    const struct capyai_task_plan_metadata_v1 *task);
int capyai_tool_manifest_valid_v1(
    const struct capyai_tool_manifest_v1 *tool);
int capyai_capability_grant_valid_v1(
    const struct capyai_capability_grant_v1 *grant);

/* Preserve old permission semantics for migration only.  The returned mask is
 * not an authorization token and does not imply elevation. */
capyai_capability_mask_t capyai_capabilities_from_legacy_perms(
    const struct capyai_perms *perms);

/* Evaluate one typed tool step against its immutable plan and user grant.
 * Returns 0 with the outcome in result; negative only when result is NULL. */
int capyai_policy_evaluate_v1(
    const struct capyai_task_plan_metadata_v1 *task,
    const struct capyai_tool_manifest_v1 *tool,
    const struct capyai_capability_grant_v1 *grant,
    uint64_t now_ms,
    struct capyai_policy_result_v1 *result);

enum capyai_decision {
    CAPYAI_DECISION_ALLOWED = 0,
    CAPYAI_DECISION_NEEDS_CLARIFICATION,
    CAPYAI_DECISION_BLOCKED_RISK,      /* needs a permission not granted */
    CAPYAI_DECISION_BLOCKED_DENYLIST,  /* dangerous command fragment */
    CAPYAI_DECISION_INVALID,           /* no label / malformed model */
};

struct capyai_plan {
    struct capy_ai_output out;      /* raw prediction from the core */
    enum capyai_decision decision;
    char reason[CAPYAI_REASON_MAX]; /* short PT-BR explanation of the decision */
};

/*
 * Plan a request: run inference over `model_text`, apply the denylist + risk
 * gates against `perms`, honor a low-confidence-write clarification, and (when
 * the action targets an implicit "last file") resolve it from `session`.
 * Pure: no side effects. `session` may be NULL. Returns 0 always (the outcome
 * is in plan->decision); negative only on NULL required args.
 */
int capyai_plan(const char *model_text, size_t model_len,
                const char *intent, const char *platform, const char *shell,
                const struct capyai_perms *perms,
                const struct capyai_session *session,
                struct capyai_plan *plan);

/* Return 1 if `command` contains a hard-blocked destructive fragment. */
int capyai_command_is_blocked(const char *command);

/* Validate the legacy model output against the kernel-compiled exact
 * action+command+risk allowlist and its bounded single-token target policy.
 * This is the final fail-closed boundary before any capysh callback. */
int capyai_legacy_dispatch_valid(const struct capy_ai_output *out);

/* Return the built-in default capy-ai-core model artifact (shared by the
 * terminal command and the graphical chat app). `*out_len` gets its byte
 * length. Defined in capyai_command.c; only when CAPYOS_HAVE_CAPYAI. */
const char *capyai_builtin_model(size_t *out_len);

/* Outcome the caller reports back after dispatching an allowed command. */
struct capyai_exec_result {
    int executed;      /* 1 if the command actually ran */
    int rc;            /* 0 ok, non-zero on command failure */
    char detail[CAPYAI_SUMMARY_MAX]; /* optional extra (path acted on, etc.) */
};

/* Existing shell/VFS execution remains owned by the caller.  The shared
 * executor invokes this callback only after inference, clarification,
 * denylist, permission gates, exact tool allowlist and target validation all
 * allow the request. */
typedef int (*capyai_dispatch_fn)(void *ctx, const char *command_line);

/* Typed dispatch boundary used by the governed runtime. The model output is
 * treated only as bounded data: capyai_execute_intent_v2 validates the exact
 * action/tool tuple, risk and argument schema before invoking this callback.
 * Human-provided content is never serialized into a shell command line. */
typedef int (*capyai_typed_dispatch_fn)(
    void *ctx, const struct capy_ai_output *tool_call,
    char *detail, size_t detail_size);

/* Validate one model output against the compiled typed-tool schemas. Returns
 * 1 only for a complete, exact typed invocation; unknown actions remain the
 * responsibility of the legacy exact allowlist. */
int capyai_typed_dispatch_valid(const struct capy_ai_output *out);

/* Shared plan + execute path used by both the terminal command and graphical
 * chat.  It builds the bounded command line, calls `dispatch` for ALLOWED
 * plans, updates `session`, and increments the turn counter exactly once.
 * A NULL dispatch performs a dry run. */
int capyai_execute_intent(const char *model_text, size_t model_len,
                          const char *intent, const char *platform,
                          const char *shell,
                          const struct capyai_perms *perms,
                          struct capyai_session *session,
                          capyai_dispatch_fn dispatch, void *dispatch_ctx,
                          struct capyai_plan *plan,
                          struct capyai_exec_result *result);

/* Additive v2 execution boundary. Registered typed actions are required to go
 * through `typed_dispatch`; all other actions may use the legacy exact-command
 * dispatcher. A missing typed callback fails closed. The v1 entry point above
 * is retained unchanged for older modules and ISO compatibility. */
int capyai_execute_intent_v2(
    const char *model_text, size_t model_len,
    const char *intent, const char *platform, const char *shell,
    const struct capyai_perms *perms, struct capyai_session *session,
    capyai_dispatch_fn legacy_dispatch, void *legacy_dispatch_ctx,
    capyai_typed_dispatch_fn typed_dispatch, void *typed_dispatch_ctx,
    struct capyai_plan *plan, struct capyai_exec_result *result);

/* CapyOS-owned VFS adapter for the file.create, file.edit and file.move typed
 * calls. `ctx` is a shell_context whose authenticated session has already been
 * made active by the caller. */
int capyai_native_file_dispatch(
    void *ctx, const struct capy_ai_output *tool_call,
    char *detail, size_t detail_size);

/*
 * Record the acted-on file into the session so follow-up edits ("adicione
 * neste arquivo novo ...") can target it without repeating the path.
 */
void capyai_session_note_file(struct capyai_session *session, const char *path);

/*
 * Compose a PT-BR natural-language summary of the outcome into `buf`.
 * `result` may be NULL when the plan was blocked / needs clarification.
 */
void capyai_summary(const struct capyai_plan *plan,
                    const struct capyai_exec_result *result,
                    char *buf, size_t buf_size);

#endif /* SERVICES_CAPYAI_H */
